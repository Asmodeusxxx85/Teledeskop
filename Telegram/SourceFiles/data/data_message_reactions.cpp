/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_message_reactions.h"

#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/session/send_as_peers.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer_values.h"
#include "data/stickers/data_custom_emoji.h"
#include "storage/localimageloader.h"
#include "ui/image/image_location_factory.h"
#include "ui/animated_icon.h"
#include "mtproto/mtproto_config.h"
#include "base/timer_rpl.h"
#include "base/call_delayed.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

#include "tdb/tdb_tl_scheme.h"
#include "tdb/tdb_account.h"

namespace Data {
namespace {

using namespace Tdb;

constexpr auto kRefreshFullListEach = 60 * 60 * crl::time(1000);
constexpr auto kPollEach = 20 * crl::time(1000);
constexpr auto kSizeForDownscale = 64;
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);
constexpr auto kRecentReactionsLimit = 40;
constexpr auto kTopRequestDelay = 60 * crl::time(1000);
constexpr auto kTopReactionsLimit = 14;

[[nodiscard]] QString ReactionIdToLog(const ReactionId &id) {
	if (const auto custom = id.custom()) {
		return "custom:" + QString::number(custom);
	}
	return id.emoji();
}

#if 0 // mtp
[[nodiscard]] std::vector<ReactionId> ListFromMTP(
		const MTPDmessages_reactions &data) {
	const auto &list = data.vreactions().v;
	auto result = std::vector<ReactionId>();
	result.reserve(list.size());
	for (const auto &reaction : list) {
		const auto id = ReactionFromMTP(reaction);
		if (id.empty()) {
			LOG(("API Error: reactionEmpty in messages.reactions."));
		} else {
			result.push_back(id);
		}
	}
	return result;
}
#endif

[[nodiscard]] Reaction CustomReaction(not_null<DocumentData*> document) {
	return Reaction{
		.id = { { document->id } },
		.title = "Custom reaction",
		.appearAnimation = document,
		.selectAnimation = document,
		.centerIcon = document,
		.active = true,
	};
}

[[nodiscard]] int SentReactionsLimit(not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto config = &session->account().appConfig();
	return session->premium()
		? config->get<int>("reactions_user_max_premium", 3)
		: config->get<int>("reactions_user_max_default", 1);
}

bool IsMyRecent(
		const MTPDmessagePeerReaction &data,
		const ReactionId &id,
		not_null<PeerData*> peer,
		const base::flat_map<
			ReactionId,
			std::vector<RecentReaction>> &recent,
		bool ignoreChosen) {
	if (peer->id == peer->session().userPeerId()) {
		return true;
	} else if (!ignoreChosen) {
		return data.is_my();
	}
	const auto j = recent.find(id);
	if (j == end(recent)) {
		return false;
	}
	const auto k = ranges::find(
		j->second,
		peer,
		&RecentReaction::peer);
	return (k != end(j->second)) && k->my;
}

} // namespace

PossibleItemReactionsRef LookupPossibleReactions(
		not_null<HistoryItem*> item) {
	if (!item->canReact()) {
		return {};
	}
	auto result = PossibleItemReactionsRef();
	const auto peer = item->history()->peer;
	const auto session = &peer->session();
	const auto reactions = &session->data().reactions();
	const auto &full = reactions->list(Reactions::Type::Active);
	const auto &top = reactions->list(Reactions::Type::Top);
	const auto &recent = reactions->list(Reactions::Type::Recent);
	const auto &all = item->reactions();
	const auto limit = UniqueReactionsLimit(peer);
	const auto premiumPossible = session->premiumPossible();
	const auto limited = (all.size() >= limit) && [&] {
		const auto my = item->chosenReactions();
		if (my.empty()) {
			return true;
		}
		return true; // #TODO reactions
	}();
	auto added = base::flat_set<ReactionId>();
	const auto add = [&](auto predicate) {
		auto &&all = ranges::views::concat(top, recent, full);
		for (const auto &reaction : all) {
			if (predicate(reaction)) {
				if (added.emplace(reaction.id).second) {
					result.recent.push_back(&reaction);
				}
			}
		}
	};
	reactions->clearTemporary();
	if (limited) {
		result.recent.reserve(all.size());
		add([&](const Reaction &reaction) {
			return ranges::contains(all, reaction.id, &MessageReaction::id);
		});
		for (const auto &reaction : all) {
			const auto id = reaction.id;
			if (!added.contains(id)) {
				if (const auto temp = reactions->lookupTemporary(id)) {
					result.recent.push_back(temp);
				}
			}
		}
	} else {
		const auto &allowed = PeerAllowedReactions(peer);
		result.recent.reserve((allowed.type == AllowedReactionsType::Some)
			? allowed.some.size()
			: full.size());
		add([&](const Reaction &reaction) {
			const auto id = reaction.id;
			if (id.custom() && !premiumPossible) {
				return false;
			} else if ((allowed.type == AllowedReactionsType::Some)
				&& !ranges::contains(allowed.some, id)) {
				return false;
			} else if (id.custom()
				&& allowed.type == AllowedReactionsType::Default) {
				return false;
			} else if (reaction.premium
				&& !session->premium()
				&& !ranges::contains(all, id, &MessageReaction::id)) {
				if (premiumPossible) {
					result.morePremiumAvailable = true;
				}
				return false;
			}
			return true;
		});
		result.customAllowed = (allowed.type == AllowedReactionsType::All)
			&& premiumPossible;
	}
	const auto i = ranges::find(
		result.recent,
		reactions->favoriteId(),
		&Reaction::id);
	if (i != end(result.recent) && i != begin(result.recent)) {
		std::rotate(begin(result.recent), i, i + 1);
	}
	return result;
}

PossibleItemReactionsRef ParsePossibleReactions(
		not_null<Main::Session*> session,
		const TLavailableReactions &available) {
	const auto &data = available.data();
	const auto reactions = &session->data().reactions();
	auto result = PossibleItemReactionsRef();
	result.customAllowed = data.vallow_custom_emoji().v;
	const auto &top = data.vtop_reactions().v;
	const auto &recent = data.vrecent_reactions().v;
	const auto &popular = data.vpopular_reactions().v;
	auto added = base::flat_set<ReactionId>();
	reactions->clearTemporary();
	result.recent.reserve(top.size() + recent.size() + popular.size());

	auto &&all = ranges::views::concat(top, recent, popular);
	for (const auto &reaction : all) {
		const auto id = ReactionFromTL(reaction.data().vtype());
		if (added.emplace(id).second) {
			if (const auto temp = reactions->lookupTemporary(id)) {
				result.recent.push_back(temp);
			}
		}
	}
	const auto i = ranges::find(
		result.recent,
		reactions->favoriteId(),
		&Reaction::id);
	if (i != end(result.recent) && i != begin(result.recent)) {
		std::rotate(begin(result.recent), i, i + 1);
	}
	return result;

}

PossibleItemReactions::PossibleItemReactions(
	const PossibleItemReactionsRef &other)
	: recent(other.recent | ranges::views::transform([](const auto &value) {
	return *value;
}) | ranges::to_vector)
, morePremiumAvailable(other.morePremiumAvailable)
, customAllowed(other.customAllowed) {
}

Reactions::Reactions(not_null<Session*> owner)
: _owner(owner)
#if 0 // mtp
, _topRefreshTimer([=] { refreshTop(); })
, _repaintTimer([=] { repaintCollected(); }) {
	refreshDefault();

	base::timer_each(
		kRefreshFullListEach
	) | rpl::start_with_next([=] {
		refreshDefault();
	}, _lifetime);

	_owner->session().changes().messageUpdates(
		MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const MessageUpdate &update) {
		const auto item = update.item;
		_pollingItems.remove(item);
		_pollItems.remove(item);
		_repaintItems.remove(item);
	}, _lifetime);

	crl::on_main(&owner->session(), [=] {
		// applyFavorite accesses not yet constructed parts of session.
		rpl::single(rpl::empty) | rpl::then(
			_owner->session().mtp().config().updates()
		) | rpl::map([=] {
			const auto &config = _owner->session().mtp().configValues();
			return config.reactionDefaultCustom
				? ReactionId{ DocumentId(config.reactionDefaultCustom) }
				: ReactionId{ config.reactionDefaultEmoji };
		}) | rpl::filter([=](const ReactionId &id) {
			return !_saveFaveRequestId;
		}) | rpl::start_with_next([=](ReactionId &&id) {
			applyFavorite(id);
		}, _lifetime);
	});
#endif
{
}

Reactions::~Reactions() = default;

Main::Session &Reactions::session() const {
	return _owner->session();
}

#if 0 // mtp
void Reactions::refreshTop() {
	requestTop();
}

void Reactions::refreshRecent() {
	requestRecent();
}

void Reactions::refreshRecentDelayed() {
	if (_recentRequestId || _recentRequestScheduled) {
		return;
	}
	_recentRequestScheduled = true;
	base::call_delayed(kRecentRequestTimeout, &_owner->session(), [=] {
		if (_recentRequestScheduled) {
			requestRecent();
		}
	});
}

void Reactions::refreshDefault() {
	requestDefault();
}
#endif

void Reactions::refreshActive(const TLDupdateActiveEmojiReactions &data) {
	updateFromData(data);
}

void Reactions::refreshFavorite(const TLDupdateDefaultReactionType &data) {
	applyFavorite(ReactionFromTL(data.vreaction_type()));
}

const std::vector<Reaction> &Reactions::list(Type type) const {
	switch (type) {
	case Type::Active: return _active;
	case Type::Recent: return _recent;
	case Type::Top: return _top;
	case Type::All: return _available;
	}
	Unexpected("Type in Reactions::list.");
}

ReactionId Reactions::favoriteId() const {
	return _favoriteId;
}

const Reaction *Reactions::favorite() const {
	return _favorite ? &*_favorite : nullptr;
}

void Reactions::setFavorite(const ReactionId &id) {
#if 0 // mtp
	const auto api = &_owner->session().api();
	if (_saveFaveRequestId) {
		api->request(_saveFaveRequestId).cancel();
	}
	_saveFaveRequestId = api->request(MTPmessages_SetDefaultReaction(
		ReactionToMTP(id)
	)).done([=] {
		_saveFaveRequestId = 0;
	}).fail([=] {
		_saveFaveRequestId = 0;
	}).send();
#endif
	_owner->session().sender().request(TLsetDefaultReactionType(
		ReactionToTL(id)
	)).send();

	applyFavorite(id);
}

DocumentData *Reactions::chooseGenericAnimation(
		not_null<DocumentData*> custom) const {
	const auto sticker = custom->sticker();
	const auto i = sticker
		? ranges::find(
			_available,
			::Data::ReactionId{ { sticker->alt } },
			&::Data::Reaction::id)
		: end(_available);
	if (i != end(_available) && i->aroundAnimation) {
		const auto view = i->aroundAnimation->createMediaView();
		view->checkStickerLarge();
		if (view->loaded()) {
			return i->aroundAnimation;
		}
	}
	if (_genericAnimations.empty()) {
		return nullptr;
	}
	auto copy = _genericAnimations;
	ranges::shuffle(copy);
	const auto first = copy.front();
	const auto view = first->createMediaView();
	view->checkStickerLarge();
	if (view->loaded()) {
		return first;
	}
	const auto k = ranges::find_if(copy, [&](not_null<DocumentData*> value) {
		return value->createMediaView()->loaded();
	});
	return (k != end(copy)) ? (*k) : first;
}

void Reactions::applyFavorite(const ReactionId &id) {
	if (_favoriteId != id) {
		_favoriteId = id;
		_favorite = resolveById(_favoriteId);
		if (!_favorite && _unresolvedFavoriteId != _favoriteId) {
			_unresolvedFavoriteId = _favoriteId;
			resolve(_favoriteId);
		}
		_favoriteUpdated.fire({});
	}
}

rpl::producer<> Reactions::topUpdates() const {
	return _topUpdated.events();
}

rpl::producer<> Reactions::recentUpdates() const {
	return _recentUpdated.events();
}

rpl::producer<> Reactions::defaultUpdates() const {
	return _defaultUpdated.events();
}

rpl::producer<> Reactions::favoriteUpdates() const {
	return _favoriteUpdated.events();
}

void Reactions::preloadImageFor(const ReactionId &id) {
	if (_images.contains(id) || id.emoji().isEmpty()) {
		return;
	}
	auto &set = _images.emplace(id).first->second;
	const auto i = ranges::find(_available, id, &Reaction::id);
	const auto document = (i == end(_available))
		? nullptr
		: i->centerIcon
		? i->centerIcon
		: i->selectAnimation.get();
	if (document) {
		loadImage(set, document, !i->centerIcon);
	} else {
		resolveEmoji(id.emoji());
#if 0 // mtp
	} else if (!_waitingForList) {
		_waitingForList = true;
		refreshDefault();
#endif
	}
}

void Reactions::preloadAnimationsFor(const ReactionId &id) {
	const auto custom = id.custom();
	const auto document = custom ? _owner->document(custom).get() : nullptr;
	const auto customSticker = document ? document->sticker() : nullptr;
	const auto findId = custom
		? ReactionId{ { customSticker ? customSticker->alt : QString() } }
		: id;
	const auto i = ranges::find(_available, findId, &Reaction::id);
	if (i == end(_available)) {
		return;
	}
	const auto preload = [&](DocumentData *document) {
		const auto view = document
			? document->activeMediaView()
			: nullptr;
		if (view) {
			view->checkStickerLarge();
		}
	};

	if (!custom) {
		preload(i->centerIcon);
	}
	preload(i->aroundAnimation);
}

QImage Reactions::resolveImageFor(
		const ReactionId &emoji,
		ImageSize size) {
	const auto i = _images.find(emoji);
	if (i == end(_images)) {
		preloadImageFor(emoji);
	}
	auto &set = (i != end(_images)) ? i->second : _images[emoji];
	const auto resolve = [&](QImage &image, int size) {
		const auto factor = style::DevicePixelRatio();
		const auto frameSize = set.fromSelectAnimation
			? (size / 2)
			: size;
		// Must not be colored to text.
		image = set.icon->frame(QColor()).scaled(
			frameSize * factor,
			frameSize * factor,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		if (set.fromSelectAnimation) {
			auto result = QImage(
				size * factor,
				size * factor,
				QImage::Format_ARGB32_Premultiplied);
			result.fill(Qt::transparent);

			auto p = QPainter(&result);
			p.drawImage(
				(size - frameSize) * factor / 2,
				(size - frameSize) * factor / 2,
				image);
			p.end();

			std::swap(result, image);
		}
		image.setDevicePixelRatio(factor);
	};
	if (set.bottomInfo.isNull() && set.icon) {
		resolve(set.bottomInfo, st::reactionInfoImage);
		resolve(set.inlineList, st::reactionInlineImage);
		crl::async([icon = std::move(set.icon)]{});
	}
	switch (size) {
	case ImageSize::BottomInfo: return set.bottomInfo;
	case ImageSize::InlineList: return set.inlineList;
	}
	Unexpected("ImageSize in Reactions::resolveImageFor.");
}

void Reactions::resolveImages() {
	for (auto &[id, set] : _images) {
		if (!set.bottomInfo.isNull() || set.icon || set.media) {
			continue;
		}
		const auto i = ranges::find(_available, id, &Reaction::id);
		const auto document = (i == end(_available))
			? nullptr
			: i->centerIcon
			? i->centerIcon
			: i->selectAnimation.get();
		if (document) {
			loadImage(set, document, !i->centerIcon);
		}
	}
}

void Reactions::loadImage(
		ImageSet &set,
		not_null<DocumentData*> document,
		bool fromSelectAnimation) {
	if (!set.bottomInfo.isNull() || set.icon) {
		return;
	} else if (!set.media) {
		set.fromSelectAnimation = fromSelectAnimation;
		set.media = document->createMediaView();
		set.media->checkStickerLarge();
	}
	if (set.media->loaded()) {
		setAnimatedIcon(set);
	} else if (!_imagesLoadLifetime) {
		document->session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			downloadTaskFinished();
		}, _imagesLoadLifetime);
	}
}

void Reactions::setAnimatedIcon(ImageSet &set) {
	const auto size = style::ConvertScale(kSizeForDownscale);
	set.icon = Ui::MakeAnimatedIcon({
		.generator = DocumentIconFrameGenerator(set.media),
		.sizeOverride = QSize(size, size),
		.colorized = set.media->owner()->emojiUsesTextColor(),
	});
	set.media = nullptr;
}

void Reactions::downloadTaskFinished() {
	auto hasOne = false;
	for (auto &[emoji, set] : _images) {
		if (!set.media) {
			continue;
		} else if (set.media->loaded()) {
			setAnimatedIcon(set);
		} else {
			hasOne = true;
		}
	}
	if (!hasOne) {
		_imagesLoadLifetime.destroy();
	}
}

#if 0 // mtp
void Reactions::requestTop() {
	if (_topRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_topRefreshTimer.cancel();
	_topRequestId = api.request(MTPmessages_GetTopReactions(
		MTP_int(kTopReactionsLimit),
		MTP_long(_topHash)
	)).done([=](const MTPmessages_Reactions &result) {
		_topRequestId = 0;
		result.match([&](const MTPDmessages_reactions &data) {
			updateTop(data);
		}, [](const MTPDmessages_reactionsNotModified&) {
		});
	}).fail([=] {
		_topRequestId = 0;
		_topHash = 0;
	}).send();
}

void Reactions::requestRecent() {
	if (_recentRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_recentRequestScheduled = false;
	_recentRequestId = api.request(MTPmessages_GetRecentReactions(
		MTP_int(kRecentReactionsLimit),
		MTP_long(_recentHash)
	)).done([=](const MTPmessages_Reactions &result) {
		_recentRequestId = 0;
		result.match([&](const MTPDmessages_reactions &data) {
			updateRecent(data);
		}, [](const MTPDmessages_reactionsNotModified&) {
		});
	}).fail([=] {
		_recentRequestId = 0;
		_recentHash = 0;
	}).send();
}

void Reactions::requestDefault() {
	if (_defaultRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_defaultRequestId = api.request(MTPmessages_GetAvailableReactions(
		MTP_int(_defaultHash)
	)).done([=](const MTPmessages_AvailableReactions &result) {
		_defaultRequestId = 0;
		result.match([&](const MTPDmessages_availableReactions &data) {
			updateDefault(data);
		}, [&](const MTPDmessages_availableReactionsNotModified &) {
		});
	}).fail([=] {
		_defaultRequestId = 0;
		_defaultHash = 0;
	}).send();
}

void Reactions::requestGeneric() {
	if (_genericRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_genericRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiGenericAnimations(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_genericRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateGeneric(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_genericRequestId = 0;
	}).send();
}

void Reactions::updateTop(const MTPDmessages_reactions &data) {
	_topHash = data.vhash().v;
	_topIds = ListFromMTP(data);
	_top = resolveByIds(_topIds, _unresolvedTop);
	_topUpdated.fire({});
}

void Reactions::updateRecent(const MTPDmessages_reactions &data) {
	_recentHash = data.vhash().v;
	_recentIds = ListFromMTP(data);
	_recent = resolveByIds(_recentIds, _unresolvedRecent);
	recentUpdated();
}

void Reactions::updateDefault(const MTPDmessages_availableReactions &data) {
	_defaultHash = data.vhash().v;

#endif

void Reactions::updateFromData(
		const Tdb::TLDupdateActiveEmojiReactions &data) {
	auto list = std::vector<EmojiResolved>();
	list.reserve(data.vemojis().v.size() + _emojiReactions.size());
	for (const auto &entry : data.vemojis().v) {
		const auto emoji = entry.v;
		const auto i = ranges::find(
			_emojiReactions,
			emoji,
			&EmojiResolved::emoji);
		if (i == end(_emojiReactions)) {
			list.push_back({ .emoji = emoji, .active = true });
		} else {
			list.push_back(*i);
			list.back().active = true;
		}
	}
	for (auto &entry : _emojiReactions) {
		if (!ranges::contains(list, entry.emoji, &EmojiResolved::emoji)) {
			list.push_back(entry);
			list.back().active = false;
		}
	}
	_emojiReactions = std::move(list);
	resolveEmojiNext();
}

void Reactions::resolveEmojiNext() {
	const auto sent = ranges::find_if(
		_emojiReactions,
		[](mtpRequestId id) { return id != 0; },
		&EmojiResolved::requestId);
	const auto resolving = (sent != end(_emojiReactions));
	if (resolving) {
		return;
	}
	const auto unknown = ranges::find(
		_emojiReactions,
		false,
		&EmojiResolved::resolved);
	if (unknown == end(_emojiReactions)) {
		return;
	}
	resolveEmoji(&*unknown);
}

void Reactions::resolveEmoji(const QString &emoji) {
	const auto i = ranges::find(
		_emojiReactions,
		emoji,
		&EmojiResolved::emoji);
	if (i != end(_emojiReactions)) {
		return;
	}
	_emojiReactions.push_back({ .emoji = emoji });
	resolveEmoji(&_emojiReactions.back());
}

void Reactions::resolveEmoji(not_null<EmojiResolved*> entry) {
	const auto emoji = entry->emoji;
	const auto finish = [=](std::optional<Reaction> parsed) {
		const auto i = ranges::find(
			_emojiReactions,
			emoji,
			&EmojiResolved::emoji);
		Assert(i != end(_emojiReactions));
		i->resolved = true;
		i->requestId = 0;
		const auto active = i->active;
		if (parsed) {
			const auto id = ReactionId{ emoji };
			const auto i = ranges::find(_available, id, &Reaction::id);
			if (i != end(_available)) {
				*i = std::move(*parsed);
			} else {
				_available.reserve(_emojiReactions.size());
				_available.push_back(std::move(*parsed));
			}
			const auto toCache = [&](DocumentData *document) {
				if (document) {
					_iconsCache.emplace(document, document->createMediaView());
				}
			};
			if (active) {
				toCache(parsed->appearAnimation);
				toCache(parsed->selectAnimation);
				toCache(parsed->centerIcon);
				toCache(parsed->aroundAnimation);
			}
			resolveImages();
		}
		if (active) {
			checkAllActiveResolved();
		}
		resolveEmojiNext();
	};
	auto &api = _owner->session().sender();
	entry->requestId = api.request(TLgetEmojiReaction(
		tl_string(emoji)
	)).done([=](const TLemojiReaction &result) {
		finish(parse(result));
	}).fail([=](const Error &error) {
		LOG(("API Error: Reaction '%1' not found!").arg(emoji));
		finish(std::nullopt);
	}).send();
}

void Reactions::checkAllActiveResolved() {
	if (!allActiveResolved()) {
		return;
	}
	_active.clear();
	_active.reserve(_available.size());
	for (const auto &entry : _emojiReactions) {
		if (!entry.active) {
			break;
		}
		const auto i = ranges::find(
			_available,
			ReactionId{ entry.emoji },
			&Reaction::id);
		if (i != end(_available)) {
			_active.push_back(*i);
		}
	}
	resolveImages();
	defaultUpdated();
}

bool Reactions::allActiveResolved() const {
	for (const auto &entry : _emojiReactions) {
		if (!entry.active) {
			return true;
		} else if (!entry.resolved) {
			return false;
		}
	}
	return true;
}

void Reactions::requestGeneric() {
	if (_genericRequestId) {
		return;
	}
	auto &api = _owner->session().sender();
	_genericRequestId = api.request(TLgetCustomEmojiReactionAnimations(
	)).done([=](const TLDstickers &result) {
		_genericRequestId = 0;
		updateGeneric(result);
	}).fail([=] {
		_genericRequestId = 0;
	}).send();
}

void Reactions::updateGeneric(const TLDstickers &data) {
	const auto oldCache = base::take(_genericCache);
	const auto toCache = [&](not_null<DocumentData*> document) {
		_genericAnimations.push_back(document);
		_genericCache.emplace(document, document->createMediaView());
	};
	const auto &list = data.vstickers().v;
	_genericAnimations.clear();
	_genericAnimations.reserve(list.size());
	_genericCache.reserve(list.size());
	for (const auto &sticker : list) {
		toCache(_owner->processDocument(sticker));
	}
	if (!_genericCache.empty()) {
		_genericCache.front().second->checkStickerLarge();
	}
}

#if 0 // mtp
void Reactions::updateGeneric(const MTPDmessages_stickerSet &data) {
	const auto oldCache = base::take(_genericCache);
	const auto toCache = [&](not_null<DocumentData*> document) {
		if (document->sticker()) {
			_genericAnimations.push_back(document);
			_genericCache.emplace(document, document->createMediaView());
		}
	};
	const auto &list = data.vdocuments().v;
	_genericAnimations.clear();
	_genericAnimations.reserve(list.size());
	_genericCache.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		toCache(_owner->processDocument(sticker));
	}
	if (!_genericCache.empty()) {
		_genericCache.front().second->checkStickerLarge();
	}
}

void Reactions::recentUpdated() {
	_topRefreshTimer.callOnce(kTopRequestDelay);
	_recentUpdated.fire({});
}
#endif

void Reactions::defaultUpdated() {
#if 0 // mtp
	refreshTop();
	refreshRecent();
#endif
	if (_genericAnimations.empty()) {
		requestGeneric();
	}
	_defaultUpdated.fire({});
}

not_null<CustomEmojiManager::Listener*> Reactions::resolveListener() {
	return static_cast<CustomEmojiManager::Listener*>(this);
}

void Reactions::customEmojiResolveDone(not_null<DocumentData*> document) {
	const auto id = ReactionId{ { document->id } };
	const auto favorite = (_unresolvedFavoriteId == id);
	const auto i = _unresolvedTop.find(id);
	const auto top = (i != end(_unresolvedTop));
	const auto j = _unresolvedRecent.find(id);
	const auto recent = (j != end(_unresolvedRecent));
	if (favorite) {
		_unresolvedFavoriteId = ReactionId();
		_favorite = resolveById(_favoriteId);
	}
	if (top) {
		_unresolvedTop.erase(i);
		_top = resolveByIds(_topIds, _unresolvedTop);
	}
	if (recent) {
		_unresolvedRecent.erase(j);
		_recent = resolveByIds(_recentIds, _unresolvedRecent);
	}
	if (favorite) {
		_favoriteUpdated.fire({});
	}
	if (top) {
		_topUpdated.fire({});
	}
	if (recent) {
		_recentUpdated.fire({});
	}
}

std::optional<Reaction> Reactions::resolveById(const ReactionId &id) {
	if (const auto emoji = id.emoji(); !emoji.isEmpty()) {
		const auto i = ranges::find(_available, id, &Reaction::id);
		if (i != end(_available)) {
			return *i;
		}
	} else if (const auto customId = id.custom()) {
		const auto document = _owner->document(customId);
		if (document->sticker()) {
			return CustomReaction(document);
		}
	}
	return {};
}

std::vector<Reaction> Reactions::resolveByIds(
		const std::vector<ReactionId> &ids,
		base::flat_set<ReactionId> &unresolved) {
	auto result = std::vector<Reaction>();
	result.reserve(ids.size());
	for (const auto &id : ids) {
		if (const auto resolved = resolveById(id)) {
			result.push_back(*resolved);
		} else if (unresolved.emplace(id).second) {
			resolve(id);
		}
	}
	return result;
}

void Reactions::resolve(const ReactionId &id) {
	if (const auto emoji = id.emoji(); !emoji.isEmpty()) {
		resolveEmoji(emoji);
	} else if (const auto customId = id.custom()) {
		_owner->customEmojiManager().resolve(
			customId,
			resolveListener());
	}
}

#if 0 // mtp
std::optional<Reaction> Reactions::parse(const MTPAvailableReaction &entry) {
	return entry.match([&](const MTPDavailableReaction &data) {
		const auto emoji = qs(data.vreaction());
		const auto known = (Ui::Emoji::Find(emoji) != nullptr);
		if (!known) {
			LOG(("API Error: Unknown emoji in reactions: %1").arg(emoji));
		}
		return known
			? std::make_optional(Reaction{
				.id = ReactionId{ emoji },
				.title = qs(data.vtitle()),
				//.staticIcon = _owner->processDocument(data.vstatic_icon()),
				.appearAnimation = _owner->processDocument(
					data.vappear_animation()),
				.selectAnimation = _owner->processDocument(
					data.vselect_animation()),
				//.activateAnimation = _owner->processDocument(
				//	data.vactivate_animation()),
				//.activateEffects = _owner->processDocument(
				//	data.veffect_animation()),
				.centerIcon = (data.vcenter_icon()
					? _owner->processDocument(*data.vcenter_icon()).get()
					: nullptr),
				.aroundAnimation = (data.varound_animation()
					? _owner->processDocument(
						*data.varound_animation()).get()
					: nullptr),
				.active = !data.is_inactive(),
				.premium = data.is_premium(),
			})
			: std::nullopt;
	});
}
#endif

std::optional<Reaction> Reactions::parse(const TLemojiReaction &entry) {
	const auto &data = entry.data();
	const auto emoji = data.vemoji().v;
	const auto known = (Ui::Emoji::Find(emoji) != nullptr);
	if (!known) {
		LOG(("API Error: Unknown emoji in reactions: %1").arg(emoji));
	}
	const auto selectAnimation = _owner->processDocument(
		data.vselect_animation());
	return known
		? std::make_optional(Reaction{
			.id = ReactionId{ emoji },
			.title = data.vtitle().v,
			//.staticIcon = _owner->processDocument(data.vstatic_icon()),
			.appearAnimation = _owner->processDocument(
				data.vappear_animation()),
			.selectAnimation = selectAnimation,
			//.activateAnimation = _owner->processDocument(
			//	data.vactivate_animation()),
			//.activateEffects = _owner->processDocument(
			//	data.veffect_animation()),
			.centerIcon = (data.vcenter_animation()
				? _owner->processDocument(*data.vcenter_animation()).get()
				: nullptr),
			.aroundAnimation = (data.varound_animation()
				? _owner->processDocument(
					*data.varound_animation()).get()
				: nullptr),
			.active = data.vis_active().v,
		})
		: std::nullopt;
}

#if 0 // mtp
void Reactions::send(not_null<HistoryItem*> item, bool addToRecent) {
	auto &api = _owner->session().api();
	auto i = _sentRequests.find(id);
	if (i != end(_sentRequests)) {
		api.request(i->second).cancel();
	} else {
		i = _sentRequests.emplace(id).first;
	}
	const auto chosen = item->chosenReactions();
	using Flag = MTPmessages_SendReaction::Flag;
	const auto flags = (chosen.empty() ? Flag(0) : Flag::f_reaction)
		| (addToRecent ? Flag::f_add_to_recent : Flag(0));
	i->second = api.request(MTPmessages_SendReaction(
		MTP_flags(flags),
		item->history()->peer->input,
		MTP_int(id.msg),
		MTP_vector<MTPReaction>(chosen | ranges::views::transform(
			ReactionToMTP
		) | ranges::to<QVector<MTPReaction>>())
	)).done([=](const MTPUpdates &result) {
		_sentRequests.remove(id);
		_owner->session().api().applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		_sentRequests.remove(id);
	}).send();
}

void Reactions::poll(not_null<HistoryItem*> item, crl::time now) {
	// Group them by one second.
	const auto last = item->lastReactionsRefreshTime();
	const auto grouped = ((last + 999) / 1000) * 1000;
	if (!grouped || item->history()->peer->isUser()) {
		// First reaction always edits message.
		return;
	} else if (const auto left = grouped + kPollEach - now; left > 0) {
		if (!_repaintItems.contains(item)) {
			_repaintItems.emplace(item, grouped + kPollEach);
			if (!_repaintTimer.isActive()
				|| _repaintTimer.remainingTime() > left) {
				_repaintTimer.callOnce(left);
			}
		}
	} else if (!_pollingItems.contains(item)) {
		if (_pollItems.empty() && !_pollRequestId) {
			crl::on_main(&_owner->session(), [=] {
				pollCollected();
			});
		}
		_pollItems.emplace(item);
	}
}

void Reactions::updateAllInHistory(not_null<PeerData*> peer, bool enabled) {
	if (const auto history = _owner->historyLoaded(peer)) {
		history->reactionsEnabledChanged(enabled);
	}
}
#endif

void Reactions::clearTemporary() {
	_temporary.clear();
}

Reaction *Reactions::lookupTemporary(const ReactionId &id) {
	if (const auto emoji = id.emoji(); !emoji.isEmpty()) {
		const auto i = ranges::find(_available, id, &Reaction::id);
		return (i != end(_available)) ? &*i : nullptr;
	} else if (const auto customId = id.custom()) {
		if (const auto i = _temporary.find(customId); i != end(_temporary)) {
			return &i->second;
		}
		const auto document = _owner->document(customId);
		if (document->sticker()) {
			return &_temporary.emplace(
				customId,
				CustomReaction(document)).first->second;
		}
		_owner->customEmojiManager().resolve(
			customId,
			resolveListener());
		return nullptr;
	}
	return nullptr;
}

#if 0 // mtp
void Reactions::repaintCollected() {
	const auto now = crl::now();
	auto closest = crl::time();
	for (auto i = begin(_repaintItems); i != end(_repaintItems);) {
		if (i->second <= now) {
			_owner->requestItemRepaint(i->first);
			i = _repaintItems.erase(i);
		} else {
			if (!closest || i->second < closest) {
				closest = i->second;
			}
			++i;
		}
	}
	if (closest) {
		_repaintTimer.callOnce(closest - now);
	}
}

void Reactions::pollCollected() {
	auto toRequest = base::flat_map<not_null<PeerData*>, QVector<MTPint>>();
	_pollingItems = std::move(_pollItems);
	for (const auto &item : _pollingItems) {
		toRequest[item->history()->peer].push_back(MTP_int(item->id));
	}
	auto &api = _owner->session().api();
	for (const auto &[peer, ids] : toRequest) {
		const auto finalize = [=] {
			const auto now = crl::now();
			for (const auto &item : base::take(_pollingItems)) {
				const auto last = item->lastReactionsRefreshTime();
				if (last && last + kPollEach <= now) {
					item->updateReactions(nullptr);
				}
			}
			_pollRequestId = 0;
			if (!_pollItems.empty()) {
				crl::on_main(&_owner->session(), [=] {
					pollCollected();
				});
			}
		};
		_pollRequestId = api.request(MTPmessages_GetMessagesReactions(
			peer->input,
			MTP_vector<MTPint>(ids)
		)).done([=](const MTPUpdates &result) {
			_owner->session().api().applyUpdates(result);
			finalize();
		}).fail([=] {
			finalize();
		}).send();
	}
}

bool Reactions::sending(not_null<HistoryItem*> item) const {
	return _sentRequests.contains(item->fullId());
}

bool Reactions::HasUnread(const MTPMessageReactions &data) {
	return data.match([&](const MTPDmessageReactions &data) {
		if (const auto &recent = data.vrecent_reactions()) {
			for (const auto &one : recent->v) {
				if (one.match([&](const MTPDmessagePeerReaction &data) {
					return data.is_unread();
				})) {
					return true;
				}
			}
		}
		return false;
	});
}

void Reactions::CheckUnknownForUnread(
		not_null<Session*> owner,
		const MTPMessage &message) {
	message.match([&](const MTPDmessage &data) {
		if (data.vreactions() && HasUnread(*data.vreactions())) {
			const auto peerId = peerFromMTP(data.vpeer_id());
			if (const auto history = owner->historyLoaded(peerId)) {
				owner->histories().requestDialogEntry(history);
			}
		}
	}, [](const auto &) {
	});
}
#endif

MessageReactions::MessageReactions(not_null<HistoryItem*> item)
: _item(item) {
}

void MessageReactions::add(const ReactionId &id, bool addToRecent) {
	Expects(!id.empty());

	const auto history = _item->history();
	const auto myLimit = SentReactionsLimit(_item);
	if (ranges::contains(chosen(), id)) {
		return;
	}
	auto my = 0;
	_list.erase(ranges::remove_if(_list, [&](MessageReaction &one) {
		const auto removing = one.my && (my == myLimit || ++my == myLimit);
		if (!removing) {
			return false;
		}
		one.my = false;
		const auto removed = !--one.count;
		const auto j = _recent.find(one.id);
		if (j != end(_recent)) {
			if (removed) {
				j->second.clear();
				_recent.erase(j);
			} else {
				j->second.erase(
					ranges::remove(j->second, true, &RecentReaction::my),
					end(j->second));
				if (j->second.empty()) {
					_recent.erase(j);
				}
			}
		}
		return removed;
	}), end(_list));
	const auto peer = history->peer;
	if (_item->canViewReactions() || peer->isUser()) {
		auto &list = _recent[id];
		const auto from = peer->session().sendAsPeers().resolveChosen(peer);
		list.insert(begin(list), RecentReaction{
			.peer = from,
			.my = true,
		});
	}
	const auto i = ranges::find(_list, id, &MessageReaction::id);
	if (i != end(_list)) {
		i->my = true;
		++i->count;
		std::rotate(i, i + 1, end(_list));
	} else {
		_list.push_back({ .id = id, .count = 1, .my = true });
	}
	auto &owner = history->owner();
#if 0 // mtp
	owner.reactions().send(_item, addToRecent);
#endif
	_item->history()->session().sender().request(TLaddMessageReaction(
		peerToTdbChat(_item->history()->peer->id),
		tl_int53(_item->id.bare),
		ReactionToTL(id),
		tl_bool(false), // is_big
		tl_bool(addToRecent)
	)).send();

	owner.notifyItemDataChange(_item);
}

void MessageReactions::remove(const ReactionId &id) {
	const auto history = _item->history();
	const auto self = history->session().user();
	const auto i = ranges::find(_list, id, &MessageReaction::id);
	const auto j = _recent.find(id);
	if (i == end(_list)) {
		Assert(j == end(_recent));
		return;
	} else if (!i->my) {
		Assert(j == end(_recent)
			|| !ranges::contains(j->second, self, &RecentReaction::peer));
		return;
	}
	i->my = false;
	const auto removed = !--i->count;
	if (removed) {
		_list.erase(i);
	}
	if (j != end(_recent)) {
		if (removed) {
			j->second.clear();
			_recent.erase(j);
		} else {
			j->second.erase(
				ranges::remove(j->second, true, &RecentReaction::my),
				end(j->second));
			if (j->second.empty()) {
				_recent.erase(j);
			}
		}
	}
	auto &owner = history->owner();
#if 0 // mtp
	owner.reactions().send(_item, false);
#endif
	_item->history()->session().sender().request(TLremoveMessageReaction(
		peerToTdbChat(_item->history()->peer->id),
		tl_int53(_item->id.bare),
		ReactionToTL(id)
	)).send();

	owner.notifyItemDataChange(_item);
}

#if 0 // mtp
bool MessageReactions::checkIfChanged(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent,
		bool min) const {
	auto &owner = _item->history()->owner();
	if (owner.reactions().sending(_item)) {
		// We'll apply non-stale data from the request response.
		return false;
	}
	auto existing = base::flat_set<ReactionId>();
	for (const auto &count : list) {
		const auto changed = count.match([&](const MTPDreactionCount &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			const auto nowCount = data.vcount().v;
			const auto i = ranges::find(_list, id, &MessageReaction::id);
			const auto wasCount = (i != end(_list)) ? i->count : 0;
			if (wasCount != nowCount) {
				return true;
			}
			existing.emplace(id);
			return false;
		});
		if (changed) {
			return true;
		}
	}
	for (const auto &reaction : _list) {
		if (!existing.contains(reaction.id)) {
			return true;
		}
	}
	auto parsed = base::flat_map<ReactionId, std::vector<RecentReaction>>();
	for (const auto &reaction : recent) {
		reaction.match([&](const MTPDmessagePeerReaction &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			if (!ranges::contains(_list, id, &MessageReaction::id)) {
				return;
			}
			const auto peerId = peerFromMTP(data.vpeer_id());
			const auto peer = owner.peer(peerId);
			const auto my = IsMyRecent(data, id, peer, _recent, min);
			parsed[id].push_back({
				.peer = peer,
				.unread = data.is_unread(),
				.big = data.is_big(),
				.my = my,
			});
		});
	}
	return !ranges::equal(_recent, parsed, [](
			const auto &a,
			const auto &b) {
		return ranges::equal(a.second, b.second, [](
				const RecentReaction &a,
				const RecentReaction &b) {
			return (a.peer == b.peer) && (a.big == b.big) && (a.my == b.my);
		});
	});
}

bool MessageReactions::change(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent,
		bool min) {
	auto &owner = _item->history()->owner();
	if (owner.reactions().sending(_item)) {
		// We'll apply non-stale data from the request response.
		return false;
	}
	auto changed = false;
	auto existing = base::flat_set<ReactionId>();
	auto order = base::flat_map<ReactionId, int>();
	for (const auto &count : list) {
		count.match([&](const MTPDreactionCount &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			const auto &chosen = data.vchosen_order();
			if (!min && chosen) {
				order[id] = chosen->v;
			}
			const auto i = ranges::find(_list, id, &MessageReaction::id);
			const auto nowCount = data.vcount().v;
			if (i == end(_list)) {
				changed = true;
				_list.push_back({
					.id = id,
					.count = nowCount,
					.my = (!min && chosen)
				});
			} else {
				const auto nowMy = min ? i->my : chosen.has_value();
				if (i->count != nowCount || i->my != nowMy) {
					i->count = nowCount;
					i->my = nowMy;
					changed = true;
				}
			}
			existing.emplace(id);
		});
	}
	if (!min && !order.empty()) {
		const auto minimal = std::numeric_limits<int>::min();
		const auto proj = [&](const MessageReaction &reaction) {
			return reaction.my ? order[reaction.id] : minimal;
		};
		const auto correctOrder = [&] {
			auto previousOrder = minimal;
			for (const auto &reaction : _list) {
				const auto nowOrder = proj(reaction);
				if (nowOrder < previousOrder) {
					return false;
				}
				previousOrder = nowOrder;
			}
			return true;
		}();
		if (!correctOrder) {
			changed = true;
			ranges::sort(_list, std::less(), proj);
		}
	}
	if (_list.size() != existing.size()) {
		changed = true;
		for (auto i = begin(_list); i != end(_list);) {
			if (!existing.contains(i->id)) {
				i = _list.erase(i);
			} else {
				++i;
			}
		}
	}
	auto parsed = base::flat_map<ReactionId, std::vector<RecentReaction>>();
	for (const auto &reaction : recent) {
		reaction.match([&](const MTPDmessagePeerReaction &data) {
			const auto id = ReactionFromMTP(data.vreaction());
			const auto i = ranges::find(_list, id, &MessageReaction::id);
			if (i == end(_list)) {
				return;
			}
			auto &list = parsed[id];
			if (list.size() >= i->count) {
				return;
			}
			const auto peerId = peerFromMTP(data.vpeer_id());
			const auto peer = owner.peer(peerId);
			const auto my = IsMyRecent(data, id, peer, _recent, min);
			list.push_back({
				.peer = peer,
				.unread = data.is_unread(),
				.big = data.is_big(),
				.my = my,
			});
		});
	}
	if (_recent != parsed) {
		_recent = std::move(parsed);
		changed = true;
	}
	return changed;
}
#endif

bool MessageReactions::change(const QVector<TLmessageReaction> &list) {
	auto &owner = _item->history()->owner();
	auto changed = false;
	auto existing = base::flat_set<ReactionId>();
	auto parsed = base::flat_map<ReactionId, std::vector<RecentReaction>>();
	for (const auto &tlReaction : list) {
		const auto &data = tlReaction.data();
		const auto id = ReactionFromTL(data.vtype());
		const auto chosen = data.vis_chosen().v;
		const auto i = ranges::find(_list, id, &MessageReaction::id);
		const auto nowCount = data.vtotal_count().v;
		if (i == end(_list)) {
			changed = true;
			_list.push_back({
				.id = id,
				.count = nowCount,
				.my = chosen
			});
		} else {
			if (i->count != nowCount || i->my != chosen) {
				i->count = nowCount;
				i->my = chosen;
				changed = true;
			}
		}
		existing.emplace(id);
		const auto j = _recent.find(id);
		for (const auto &sender : data.vrecent_sender_ids().v) {
			const auto peer = owner.peer(peerFromSender(sender));
			auto recent = RecentReaction{ .peer = peer };
			if (j != end(_recent)) {
				const auto k = ranges::find_if(j->second, [&](
						const RecentReaction &existing) {
					return (existing.peer == peer) && existing.unread;
				});
				if (k != end(j->second)) {
					recent.unread = true;
					recent.big = k->big;
				}
			}
			parsed[id].push_back(std::move(recent));
		}
	}
	if (_list.size() != existing.size()) {
		changed = true;
		for (auto i = begin(_list); i != end(_list);) {
			if (!existing.contains(i->id)) {
				i = _list.erase(i);
			} else {
				++i;
			}
		}
	}
	if (_recent != parsed) {
		changed = true;
		_recent = std::move(parsed);
	}
	return changed;
}

bool MessageReactions::change(const QVector<TLunreadReaction> &list) {
	enum class State {
		Read,
		Unread,
		UnreadBig,
	};
	const auto resolve = [&](not_null<PeerData*> peer) {
		const auto i = ranges::find(list, peer->id, [](
				const TLunreadReaction &reaction) {
			return peerFromSender(reaction.data().vsender_id());
		});
		return (i == list.end())
			? State::Read
			: i->data().vis_big().v
			? State::UnreadBig
			: State::Unread;
	};
	auto changed = false;
	for (auto &[emoji, list] : _recent) {
		for (auto &reaction : list) {
			const auto now = resolve(reaction.peer);
			const auto was = !reaction.unread
				? State::Read
				: reaction.big
				? State::UnreadBig
				: State::Unread;
			if (now != was) {
				changed = true;
				reaction.unread = (now != State::Read);
				reaction.big = (now == State::UnreadBig);
			}
		}
	}
	return changed;
}

const std::vector<MessageReaction> &MessageReactions::list() const {
	return _list;
}

auto MessageReactions::recent() const
-> const base::flat_map<ReactionId, std::vector<RecentReaction>> & {
	return _recent;
}

bool MessageReactions::empty() const {
	return _list.empty();
}

bool MessageReactions::hasUnread() const {
	for (auto &[emoji, list] : _recent) {
		if (ranges::contains(list, true, &RecentReaction::unread)) {
			return true;
		}
	}
	return false;
}

void MessageReactions::markRead() {
	for (auto &[emoji, list] : _recent) {
		for (auto &reaction : list) {
			reaction.unread = false;
		}
	}
}

std::vector<ReactionId> MessageReactions::chosen() const {
	return _list
		| ranges::views::filter(&MessageReaction::my)
		| ranges::views::transform(&MessageReaction::id)
		| ranges::to_vector;
}

} // namespace Data
