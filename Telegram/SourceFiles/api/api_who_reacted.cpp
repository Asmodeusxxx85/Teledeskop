/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_who_reacted.h"

#include "history/history_item.h"
#include "history/history.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_message_reaction_id.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "ui/controls/who_reacted_context_action.h"
#include "apiwrap.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

#include "tdb/tdb_tl_scheme.h"

namespace Api {
namespace {

using namespace Tdb;

constexpr auto kContextReactionsLimit = 50;

using Data::ReactionId;

struct Peers {
	std::vector<WhoReadPeer> list;
	bool unknown = false;

	friend inline bool operator==(
		const Peers &a,
		const Peers &b) noexcept = default;
};

struct PeerWithReaction {
	WhoReadPeer peerWithDate;
	ReactionId reaction;

	friend inline bool operator==(
		const PeerWithReaction &a,
		const PeerWithReaction &b) noexcept = default;
};

struct PeersWithReactions {
	std::vector<PeerWithReaction> list;
	std::vector<WhoReadPeer> read;
	int fullReactionsCount = 0;
	bool unknown = false;

	friend inline bool operator==(
		const PeersWithReactions &a,
		const PeersWithReactions &b) noexcept = default;
};

struct CachedRead {
	CachedRead()
	: data(Peers{ .unknown = true }) {
	}
	rpl::variable<Peers> data;
#if 0 // mtp
	mtpRequestId requestId = 0;
#endif
	RequestId requestId = 0;
};

struct CachedReacted {
	CachedReacted()
	: data(PeersWithReactions{ .unknown = true }) {
	}
	rpl::variable<PeersWithReactions> data;
#if 0 // mtp
	mtpRequestId requestId = 0;
#endif
	RequestId requestId = 0;
};

struct Context {
	base::flat_map<not_null<HistoryItem*>, CachedRead> cachedRead;
	base::flat_map<
		not_null<HistoryItem*>,
		base::flat_map<ReactionId, CachedReacted>> cachedReacted;
	base::flat_map<not_null<Main::Session*>, rpl::lifetime> subscriptions;

	[[nodiscard]] CachedRead &cacheRead(not_null<HistoryItem*> item) {
		const auto i = cachedRead.find(item);
		if (i != end(cachedRead)) {
			return i->second;
		}
		return cachedRead.emplace(item, CachedRead()).first->second;
	}

	[[nodiscard]] CachedReacted &cacheReacted(
			not_null<HistoryItem*> item,
			const ReactionId &reaction) {
		auto &map = cachedReacted[item];
		const auto i = map.find(reaction);
		if (i != end(map)) {
			return i->second;
		}
		return map.emplace(reaction, CachedReacted()).first->second;
	}
};

struct Userpic {
	not_null<PeerData*> peer;
	TimeId date = 0;
	bool dateReacted = false;
	QString customEntityData;
	mutable Ui::PeerUserpicView view;
	mutable InMemoryKey uniqueKey;
};

struct State {
	std::vector<Userpic> userpics;
	Ui::WhoReadContent current;
	base::has_weak_ptr guard;
	bool someUserpicsNotLoaded = false;
	bool scheduled = false;
};

[[nodiscard]] auto Contexts()
-> base::flat_map<not_null<QWidget*>, std::unique_ptr<Context>> & {
	static auto result = base::flat_map<
		not_null<QWidget*>,
		std::unique_ptr<Context>>();
	return result;
}

[[nodiscard]] not_null<Context*> ContextAt(not_null<QWidget*> key) {
	auto &contexts = Contexts();
	const auto i = contexts.find(key);
	if (i != end(contexts)) {
		return i->second.get();
	}
	const auto result = contexts.emplace(
		key,
		std::make_unique<Context>()).first->second.get();
	QObject::connect(key.get(), &QObject::destroyed, [=] {
		auto &contexts = Contexts();
		const auto i = contexts.find(key);
		for (auto &[item, entry] : i->second->cachedRead) {
			if (const auto requestId = entry.requestId) {
#if 0 // mtp
				item->history()->session().api().request(requestId).cancel();
#endif
				item->history()->session().sender().request(requestId).cancel();
			}
		}
		for (auto &[item, map] : i->second->cachedReacted) {
			for (auto &[reaction, entry] : map) {
				if (const auto requestId = entry.requestId) {
#if 0 // mtp
					item->history()->session().api().request(requestId).cancel();
#endif
					item->history()->session().sender().request(requestId).cancel();
				}
			}
		}
		contexts.erase(i);
	});
	return result;
}

[[nodiscard]] not_null<Context*> PreparedContextAt(
		not_null<QWidget*> key,
		not_null<Main::Session*> session) {
	const auto context = ContextAt(key);
	if (context->subscriptions.contains(session)) {
		return context;
	}
	session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		const auto i = context->cachedRead.find(update.item);
		if (i != end(context->cachedRead)) {
#if 0 // mtp
			session->api().request(i->second.requestId).cancel();
#endif
			session->sender().request(i->second.requestId).cancel();
			context->cachedRead.erase(i);
		}
		const auto j = context->cachedReacted.find(update.item);
		if (j != end(context->cachedReacted)) {
			for (auto &[reaction, entry] : j->second) {
#if 0 // mtp
				session->api().request(entry.requestId).cancel();
#endif
				session->sender().request(entry.requestId).cancel();
			}
			context->cachedReacted.erase(j);
		}
	}, context->subscriptions[session]);
	return context;
}

[[nodiscard]] QImage GenerateUserpic(Userpic &userpic, int size) {
	size *= style::DevicePixelRatio();
	auto result = userpic.peer->generateUserpicImage(userpic.view, size);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	return result;
}

[[nodiscard]] Ui::WhoReadType DetectSeenType(not_null<HistoryItem*> item) {
	if (const auto media = item->media()) {
		if (!media->webpage()) {
			if (const auto document = media->document()) {
				if (document->isVoiceMessage()) {
					return Ui::WhoReadType::Listened;
				} else if (document->isVideoMessage()) {
					return Ui::WhoReadType::Watched;
				}
			}
		}
	}
	return Ui::WhoReadType::Seen;
}

[[nodiscard]] rpl::producer<Peers> WhoReadIds(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context) {
	auto weak = QPointer<QWidget>(context.get());
	const auto session = &item->history()->session();
	return [=](auto consumer) {
		if (!weak) {
			return rpl::lifetime();
		}
		const auto context = PreparedContextAt(weak.data(), session);
		auto &entry = context->cacheRead(item);
		if (!entry.requestId) {
#if 0 // mtp
			entry.requestId = session->api().request(
				MTPmessages_GetMessageReadParticipants(
					item->history()->peer->input,
					MTP_int(item->id)
				)
			).done([=](const MTPVector<MTPReadParticipantDate> &result) {
				auto &entry = context->cacheRead(item);
				entry.requestId = 0;
				auto parsed = Peers();
				parsed.list.reserve(result.v.size());
				for (const auto &id : result.v) {
					parsed.list.push_back({
						.peer = UserId(id.data().vuser_id()),
						.date = id.data().vdate().v,
					});
				}
#endif
			entry.requestId = session->sender().request(
				TLgetMessageViewers(
					peerToTdbChat(item->history()->peer->id),
					tl_int53(item->id.bare))
			).done([=](const TLmessageViewers &result) {
				const auto &list = result.data().vviewers().v;
				auto &entry = context->cacheRead(item);
				entry.requestId = 0;
				auto parsed = Peers();
				parsed.list.reserve(list.size());
				for (const auto &viewer : list) {
					const auto &data = viewer.data();
					parsed.list.push_back({
						.peer = peerFromUser(data.vuser_id().v),
						.date = data.vview_date().v,
					});
				}
				entry.data = std::move(parsed);
			}).fail([=] {
				auto &entry = context->cacheRead(item);
				entry.requestId = 0;
				if (entry.data.current().unknown) {
					entry.data = Peers();
				}
			}).send();
		}
		return entry.data.value().start_existing(consumer);
	};
}

[[nodiscard]] PeersWithReactions WithEmptyReactions(
		Peers &&peers) {
	auto result = PeersWithReactions{
		.list = peers.list | ranges::views::transform([](WhoReadPeer peer) {
			return PeerWithReaction{ .peerWithDate = peer };
		}) | ranges::to_vector,
		.unknown = peers.unknown,
	};
	result.read = std::move(peers.list);
	return result;
}

[[nodiscard]] rpl::producer<PeersWithReactions> WhoReactedIds(
		not_null<HistoryItem*> item,
		const ReactionId &reaction,
		not_null<QWidget*> context) {
	auto weak = QPointer<QWidget>(context.get());
	const auto session = &item->history()->session();
	return [=](auto consumer) {
		if (!weak) {
			return rpl::lifetime();
		}
		const auto context = PreparedContextAt(weak.data(), session);
		auto &entry = context->cacheReacted(item, reaction);
		if (!entry.requestId) {
#if 0 // mtp
			using Flag = MTPmessages_GetMessageReactionsList::Flag;
			entry.requestId = session->api().request(
				MTPmessages_GetMessageReactionsList(
					MTP_flags(reaction.empty()
						? Flag(0)
						: Flag::f_reaction),
					item->history()->peer->input,
					MTP_int(item->id),
					ReactionToMTP(reaction),
					MTPstring(), // offset
					MTP_int(kContextReactionsLimit)
				)
			).done([=](const MTPmessages_MessageReactionsList &result) {
				auto &entry = context->cacheReacted(item, reaction);
				entry.requestId = 0;

				result.match([&](
						const MTPDmessages_messageReactionsList &data) {
					session->data().processUsers(data.vusers());
					session->data().processChats(data.vchats());

					auto parsed = PeersWithReactions{
						.fullReactionsCount = data.vcount().v,
					};
					parsed.list.reserve(data.vreactions().v.size());
					for (const auto &vote : data.vreactions().v) {
						const auto &data = vote.data();
						parsed.list.push_back(PeerWithReaction{
							.peerWithDate = {
								.peer = peerFromMTP(data.vpeer_id()),
								.date = data.vdate().v,
								.dateReacted = true,
							},
							.reaction = Data::ReactionFromMTP(
								data.vreaction()),
						});
					}
					entry.data = std::move(parsed);
				});
#endif
			entry.requestId = session->sender().request(
				TLgetMessageAddedReactions(
					peerToTdbChat(item->history()->peer->id),
					tl_int53(item->id.bare),
					ReactionToMaybeTL(reaction),
					tl_string(), // offset
					tl_int32(kContextReactionsLimit))
			).done([=](const TLaddedReactions &result) {
				auto &entry = context->cacheReacted(item, reaction);
				entry.requestId = 0;

				const auto &data = result.data();
				const auto &list = data.vreactions().v;
				auto parsed = PeersWithReactions{
					.fullReactionsCount = data.vtotal_count().v,
				};
				parsed.list.reserve(list.size());
				for (const auto &reaction : list) {
					const auto &data = reaction.data();
					const auto &type = data.vtype();
					parsed.list.push_back(PeerWithReaction{
						.peerWithDate = WhoReadPeer{
							.peer = peerFromSender(data.vsender_id()),
							.date = data.vdate().v,
						},
						.reaction = Data::ReactionFromTL(type),
					});
				}
				entry.data = std::move(parsed);
			}).fail([=] {
				auto &entry = context->cacheReacted(item, reaction);
				entry.requestId = 0;
				if (entry.data.current().unknown) {
					entry.data = PeersWithReactions();
				}
			}).send();
		}
		return entry.data.value().start_existing(consumer);
	};
}

[[nodiscard]] auto WhoReadOrReactedIds(
	not_null<HistoryItem*> item,
	not_null<QWidget*> context)
-> rpl::producer<PeersWithReactions> {
	return rpl::combine(
		WhoReactedIds(item, {}, context),
		WhoReadIds(item, context)
	) | rpl::map([=](PeersWithReactions &&reacted, Peers &&read) {
		if (reacted.unknown || read.unknown) {
			return PeersWithReactions{ .unknown = true };
		}
		auto &list = reacted.list;
		for (const auto &peerWithDate : read.list) {
			const auto i = ranges::find(
				list,
				peerWithDate.peer,
				[](const PeerWithReaction &p) {
					return p.peerWithDate.peer; });
			if (i == end(list)) {
				list.push_back({ .peerWithDate = peerWithDate });
			} else if (!i->peerWithDate.date) {
				i->peerWithDate.date = peerWithDate.date;
				i->peerWithDate.dateReacted = peerWithDate.dateReacted;
			}
		}
		reacted.read = std::move(read.list);
		return std::move(reacted);
	});
}

bool UpdateUserpics(
		not_null<State*> state,
		not_null<HistoryItem*> item,
		const std::vector<PeerWithReaction> &ids) {
	auto &owner = item->history()->owner();

	struct ResolvedPeer {
		PeerData *peer = nullptr;
		TimeId date = 0;
		bool dateReacted = false;
		ReactionId reaction;
	};
	const auto peers = ranges::views::all(
		ids
	) | ranges::views::transform([&](PeerWithReaction id) {
		return ResolvedPeer{
			.peer = owner.peerLoaded(id.peerWithDate.peer),
			.date = id.peerWithDate.date,
			.dateReacted = id.peerWithDate.dateReacted,
			.reaction = id.reaction,
		};
	}) | ranges::views::filter([](ResolvedPeer resolved) {
		return resolved.peer != nullptr;
	}) | ranges::to_vector;

	const auto same = ranges::equal(
		state->userpics,
		peers,
		ranges::equal_to(),
		[](const Userpic &u) { return std::pair(u.peer.get(), u.date); },
		[](const ResolvedPeer &r) { return std::pair(r.peer, r.date); });
	if (same) {
		return false;
	}
	auto &was = state->userpics;
	auto now = std::vector<Userpic>();
	for (const auto &resolved : peers) {
		const auto peer = not_null{ resolved.peer };
		const auto &data = ReactionEntityData(resolved.reaction);
		const auto i = ranges::find(was, peer, &Userpic::peer);
		if (i != end(was) && i->view.cloud) {
			i->date = resolved.date;
			i->dateReacted = resolved.dateReacted;
			now.push_back(std::move(*i));
			now.back().customEntityData = data;
			continue;
		}
		now.push_back(Userpic{
			.peer = peer,
			.date = resolved.date,
			.dateReacted = resolved.dateReacted,
			.customEntityData = data,
		});
		auto &userpic = now.back();
		userpic.uniqueKey = peer->userpicUniqueKey(userpic.view);
		peer->loadUserpic();
	}
	was = std::move(now);
	return true;
}

void RegenerateUserpics(not_null<State*> state, int small, int large) {
	Expects(state->userpics.size() == state->current.participants.size());

	state->someUserpicsNotLoaded = false;
	const auto count = int(state->userpics.size());
	for (auto i = 0; i != count; ++i) {
		auto &userpic = state->userpics[i];
		auto &participant = state->current.participants[i];
		const auto peer = userpic.peer;
		const auto key = peer->userpicUniqueKey(userpic.view);
		if (peer->hasUserpic() && peer->useEmptyUserpic(userpic.view)) {
			state->someUserpicsNotLoaded = true;
		}
		if (userpic.uniqueKey == key) {
			continue;
		}
		participant.userpicKey = userpic.uniqueKey = key;
		participant.userpicLarge = GenerateUserpic(userpic, large);
		if (i < Ui::WhoReadParticipant::kMaxSmallUserpics) {
			participant.userpicSmall = GenerateUserpic(userpic, small);
		}
	}
}

void RegenerateParticipants(not_null<State*> state, int small, int large) {
	const auto currentDate = QDateTime::currentDateTime();
	auto old = base::take(state->current.participants);
	auto &now = state->current.participants;
	now.reserve(state->userpics.size());
	for (auto &userpic : state->userpics) {
		const auto peer = userpic.peer;
		const auto date = userpic.date;
		const auto id = peer->id.value;
		const auto was = ranges::find(old, id, &Ui::WhoReadParticipant::id);
		if (was != end(old)) {
			was->name = peer->name();
			was->date = FormatReadDate(date, currentDate);
			was->dateReacted = userpic.dateReacted;
			now.push_back(std::move(*was));
			continue;
		}
		now.push_back({
			.name = peer->name(),
			.date = FormatReadDate(date, currentDate),
			.dateReacted = userpic.dateReacted,
			.customEntityData = userpic.customEntityData,
			.userpicLarge = GenerateUserpic(userpic, large),
			.userpicKey = userpic.uniqueKey,
			.id = id,
		});
		if (now.size() <= Ui::WhoReadParticipant::kMaxSmallUserpics) {
			now.back().userpicSmall = GenerateUserpic(userpic, small);
		}
	}
	RegenerateUserpics(state, small, large);
}

rpl::producer<Ui::WhoReadContent> WhoReacted(
		not_null<HistoryItem*> item,
		const ReactionId &reaction,
		not_null<QWidget*> context,
		const style::WhoRead &st,
		std::shared_ptr<WhoReadList> whoReadIds) {
	const auto small = st.userpics.size;
	const auto large = st.photoSize;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto resolveWhoRead = reaction.empty()
			&& WhoReadExists(item);

		const auto state = lifetime.make_state<State>();
		const auto pushNext = [=] {
			consumer.put_next_copy(state->current);
		};

		const auto resolveWhoReacted = !reaction.empty()
			|| item->canViewReactions();
		auto idsWithReactions = (resolveWhoRead && resolveWhoReacted)
			? WhoReadOrReactedIds(item, context)
			: resolveWhoRead
			? (WhoReadIds(item, context) | rpl::map(WithEmptyReactions))
			: WhoReactedIds(item, reaction, context);
		state->current.type = resolveWhoRead
			? DetectSeenType(item)
			: Ui::WhoReadType::Reacted;
		if (resolveWhoReacted) {
			const auto &list = item->reactions();
			state->current.fullReactionsCount = [&] {
				if (reaction.empty()) {
					return ranges::accumulate(
						list,
						0,
						ranges::plus{},
						&Data::MessageReaction::count);
				}
				const auto i = ranges::find(
					list,
					reaction,
					&Data::MessageReaction::id);
				return (i != end(list)) ? i->count : 0;
			}();
			state->current.singleCustomEntityData = ReactionEntityData(
				!reaction.empty()
				? reaction
				: (list.size() == 1)
				? list.front().id
				: ReactionId());
		}
		std::move(
			idsWithReactions
		) | rpl::start_with_next([=](PeersWithReactions &&peers) {
			if (peers.unknown) {
				state->userpics.clear();
				consumer.put_next(Ui::WhoReadContent{
					.type = state->current.type,
					.fullReactionsCount = state->current.fullReactionsCount,
					.fullReadCount = state->current.fullReadCount,
					.unknown = true,
				});
				return;
			}
			state->current.fullReadCount = int(peers.read.size());
			state->current.fullReactionsCount = peers.fullReactionsCount;
			if (whoReadIds) {
				const auto reacted = peers.list.size() - ranges::count(
					peers.list,
					ReactionId(),
					&PeerWithReaction::reaction);
				whoReadIds->list = (peers.read.size() > reacted)
					? std::move(peers.read)
					: std::vector<WhoReadPeer>();
			}
			if (UpdateUserpics(state, item, peers.list)) {
				RegenerateParticipants(state, small, large);
				pushNext();
			} else if (peers.list.empty()) {
				pushNext();
			}
		}, lifetime);

		item->history()->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return state->someUserpicsNotLoaded && !state->scheduled;
		}) | rpl::start_with_next([=] {
			for (const auto &userpic : state->userpics) {
				if (userpic.peer->userpicUniqueKey(userpic.view)
					!= userpic.uniqueKey) {
					state->scheduled = true;
					crl::on_main(&state->guard, [=] {
						state->scheduled = false;
						RegenerateUserpics(state, small, large);
						pushNext();
					});
					return;
				}
			}
		}, lifetime);

		return lifetime;
	};
}

} // namespace

QString FormatReadDate(TimeId date, const QDateTime &now) {
	if (!date) {
		return {};
	}
	const auto parsed = base::unixtime::parse(date);
	const auto readDate = parsed.date();
	const auto nowDate = now.date();
	if (readDate == nowDate) {
		return tr::lng_mediaview_today(
			tr::now,
			lt_time,
			QLocale().toString(parsed.time(), QLocale::ShortFormat));
	} else if (readDate.addDays(1) == nowDate) {
		return tr::lng_mediaview_yesterday(
			tr::now,
			lt_time,
			QLocale().toString(parsed.time(), QLocale::ShortFormat));
	}
	return tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		tr::lng_month_day(
			tr::now,
			lt_month,
			Lang::MonthDay(readDate.month())(tr::now),
			lt_day,
			QString::number(readDate.day())),
		lt_time,
		QLocale().toString(parsed.time(), QLocale::ShortFormat));
}

bool WhoReadExists(not_null<HistoryItem*> item) {
	if (!item->out()) {
		return false;
	}
	const auto type = DetectSeenType(item);
	const auto thread = item->topic()
		? (Data::Thread*)item->topic()
		: item->history();
	const auto unseen = (type == Ui::WhoReadType::Seen)
		? item->unread(thread)
		: item->isUnreadMedia();
	if (unseen) {
		return false;
	}
	const auto history = item->history();
	const auto peer = history->peer;
	const auto chat = peer->asChat();
	const auto megagroup = peer->asMegagroup();
	if ((!chat && !megagroup)
		|| (megagroup
			&& (megagroup->flags() & ChannelDataFlag::ParticipantsHidden))) {
		return false;
	}
	const auto &appConfig = peer->session().account().appConfig();
	const auto expirePeriod = appConfig.get<int>(
		"chat_read_mark_expire_period",
		7 * 86400);
	if (item->date() + int64(expirePeriod) <= int64(base::unixtime::now())) {
		return false;
	}
	const auto maxCount = appConfig.get<int>(
		"chat_read_mark_size_threshold",
		50);
	const auto count = megagroup ? megagroup->membersCount() : chat->count;
	if (count <= 0 || count > maxCount) {
		return false;
	}
	return true;
}

bool WhoReactedExists(
		not_null<HistoryItem*> item,
		WhoReactedList list) {
	if (item->canViewReactions() || WhoReadExists(item)) {
		return true;
	}
	return (list == WhoReactedList::One) && item->history()->peer->isUser();
}

rpl::producer<Ui::WhoReadContent> WhoReacted(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context,
		const style::WhoRead &st,
		std::shared_ptr<WhoReadList> whoReadIds) {
	return WhoReacted(item, {}, context, st, std::move(whoReadIds));
}

rpl::producer<Ui::WhoReadContent> WhoReacted(
		not_null<HistoryItem*> item,
		const Data::ReactionId &reaction,
		not_null<QWidget*> context,
		const style::WhoRead &st) {
	return WhoReacted(item, reaction, context, st, nullptr);
}

} // namespace Api
