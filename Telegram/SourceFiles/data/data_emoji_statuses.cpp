/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_emoji_statuses.h"

#include "main/main_session.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_stickers.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "base/call_delayed.h"
#include "apiwrap.h"
#include "ui/controls/tabbed_search.h"

#include "tdb/tdb_sender.h"
#include "tdb/tdb_tl_scheme.h"

namespace Data {
namespace {

using namespace Tdb;

constexpr auto kRefreshDefaultListEach = 60 * 60 * crl::time(1000);
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);
constexpr auto kMaxTimeout = 6 * 60 * 60 * crl::time(1000);

#if 0 // mtp
[[nodiscard]] std::vector<DocumentId> ListFromMTP(
		const MTPDaccount_emojiStatuses &data) {
	const auto &list = data.vstatuses().v;
	auto result = std::vector<DocumentId>();
	result.reserve(list.size());
	for (const auto &status : list) {
		const auto parsed = ParseEmojiStatus(status);
		if (!parsed.id) {
			LOG(("API Error: emojiStatusEmpty in account.emojiStatuses."));
		} else {
			result.push_back(parsed.id);
		}
	}
	return result;
}
#endif

[[nodiscard]] std::vector<DocumentId> ListFromTL(
		const TLDemojiStatuses &data) {
	const auto &list = data.vcustom_emoji_ids().v;
	auto result = std::vector<DocumentId>();
	result.reserve(list.size());
	for (const auto &status : list) {
		result.push_back(status.v);
	}
	return result;
}

} // namespace

EmojiStatuses::EmojiStatuses(not_null<Session*> owner)
: _owner(owner)
#if 0 // mtp
, _clearingTimer([=] { processClearing(); }) {
#endif
{
	refreshDefault();
	refreshColored();

	base::timer_each(
		kRefreshDefaultListEach
	) | rpl::start_with_next([=] {
		refreshDefault();
	}, _lifetime);
}

EmojiStatuses::~EmojiStatuses() = default;

Main::Session &EmojiStatuses::session() const {
	return _owner->session();
}

void EmojiStatuses::refreshRecent() {
	requestRecent();
}

void EmojiStatuses::refreshDefault() {
	requestDefault();
}

void EmojiStatuses::refreshColored() {
	requestColored();
}

void EmojiStatuses::refreshRecentDelayed() {
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

const std::vector<DocumentId> &EmojiStatuses::list(Type type) const {
	switch (type) {
	case Type::Recent: return _recent;
	case Type::Default: return _default;
	case Type::Colored: return _colored;
	}
	Unexpected("Type in EmojiStatuses::list.");
}

rpl::producer<> EmojiStatuses::recentUpdates() const {
	return _recentUpdated.events();
}

rpl::producer<> EmojiStatuses::defaultUpdates() const {
	return _defaultUpdated.events();
}

#if 0 // mtp
void EmojiStatuses::registerAutomaticClear(
		not_null<UserData*> user,
		TimeId until) {
	if (!until) {
		_clearing.remove(user);
		if (_clearing.empty()) {
			_clearingTimer.cancel();
		}
	} else if (auto &already = _clearing[user]; already != until) {
		already = until;
		const auto i = ranges::min_element(_clearing, {}, [](auto &&pair) {
			return pair.second;
		});
		if (i->first == user) {
			const auto now = base::unixtime::now();
			if (now < until) {
				processClearingIn(until - now);
			} else {
				processClearing();
			}
		}
	}
}
#endif

auto EmojiStatuses::emojiGroupsValue() const -> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestEmojiGroups();
	return _emojiGroups.data.value();
}

auto EmojiStatuses::statusGroupsValue() const -> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestStatusGroups();
	return _statusGroups.data.value();
}

auto EmojiStatuses::profilePhotoGroupsValue() const
-> rpl::producer<Groups> {
	const_cast<EmojiStatuses*>(this)->requestProfilePhotoGroups();
	return _profilePhotoGroups.data.value();
}

void EmojiStatuses::requestEmojiGroups() {
	requestGroups(
		&_emojiGroups,
		tl_emojiCategoryTypeDefault());
#if 0 // mtp
		MTPmessages_GetEmojiGroups(MTP_int(_emojiGroups.hash)));
#endif

}

void EmojiStatuses::requestStatusGroups() {
	requestGroups(
		&_statusGroups,
		tl_emojiCategoryTypeEmojiStatus());
#if 0 // mtp
		MTPmessages_GetEmojiStatusGroups(MTP_int(_statusGroups.hash)));
#endif
}

void EmojiStatuses::requestProfilePhotoGroups() {
	requestGroups(
		&_profilePhotoGroups,
		tl_emojiCategoryTypeChatPhoto());
#if 0 // mtp
		MTPmessages_GetEmojiProfilePhotoGroups(
			MTP_int(_profilePhotoGroups.hash)));
#endif
}

[[nodiscard]] std::vector<Ui::EmojiGroup> GroupsFromTL(
		not_null<Data::Session*> owner,
		const TLemojiCategories &categories) {
#if 0 // mtp
		const MTPDmessages_emojiGroups &data) {
	const auto &list = data.vgroups().v;
#endif
	const auto &list = categories.data().vcategories().v;
	auto result = std::vector<Ui::EmojiGroup>();
	result.reserve(list.size());
	for (const auto &group : list) {
		const auto &data = group.data();
		auto emoticons = ranges::views::all(
#if 0 // mtp
			data.vemoticons().v
		) | ranges::views::transform([](const MTPstring &emoticon) {
			return qs(emoticon);
#endif
			data.vemojis().v
		) | ranges::views::transform([](const TLstring &emoticon) {
			return emoticon.v;
		}) | ranges::to_vector;
		const auto icon = owner->processDocument(data.vicon());
		result.push_back({
#if 0 // mtp
			.iconId = QString::number(data.vicon_emoji_id().v),
#endif
			.iconId = QString::number(icon->id),
			.emoticons = std::move(emoticons),
		});
	}
	return result;
}

template <typename Request>
void EmojiStatuses::requestGroups(
		not_null<GroupsType*> type,
		Request &&request) {
	if (type->requestId) {
		return;
	}
#if 0 // mtp
	type->requestId = _owner->session().api().request(
		std::forward<Request>(request)
	).done([=](const MTPmessages_EmojiGroups &result) {
		type->requestId = 0;
	result.match([&](const MTPDmessages_emojiGroups &data) {
		type->hash = data.vhash().v;
	type->data = GroupsFromTL(data);
	}, [](const MTPDmessages_emojiGroupsNotModified&) {
	});
#endif
	type->requestId = _owner->session().sender().request(
		TLgetEmojiCategories(request)
	).done([=](const TLemojiCategories &result) {
		type->requestId = 0;
		type->data = GroupsFromTL(_owner, result);
	}).fail([=] {
		type->requestId = 0;
	}).send();
}

#if 0 // mtp
void EmojiStatuses::processClearing() {
	auto minWait = TimeId(0);
	const auto now = base::unixtime::now();
	auto clearing = base::take(_clearing);
	for (auto i = begin(clearing); i != end(clearing);) {
		const auto until = i->second;
		if (now < until) {
			const auto wait = (until - now);
			if (!minWait || minWait > wait) {
				minWait = wait;
			}
			++i;
		} else {
			i->first->setEmojiStatus(0, 0);
			i = clearing.erase(i);
		}
	}
	if (_clearing.empty()) {
		_clearing = std::move(clearing);
	} else {
		for (const auto &[user, until] : clearing) {
			_clearing.emplace(user, until);
		}
	}
	if (minWait) {
		processClearingIn(minWait);
	} else {
		_clearingTimer.cancel();
	}
}

void EmojiStatuses::processClearingIn(TimeId wait) {
	const auto waitms = wait * crl::time(1000);
	_clearingTimer.callOnce(std::min(waitms, kMaxTimeout));
}
#endif

void EmojiStatuses::requestRecent() {
	if (_recentRequestId) {
		return;
	}
#if 0 // mtp
	auto &api = _owner->session().api();
	_recentRequestScheduled = false;
	_recentRequestId = api.request(MTPaccount_GetRecentEmojiStatuses(
		MTP_long(_recentHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_recentRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateRecent(data);
		}, [](const MTPDaccount_emojiStatusesNotModified&) {
		});
#endif
	auto &api = _owner->session().sender();
	_recentRequestScheduled = false;
	_recentRequestId = api.request(TLgetRecentEmojiStatuses(
	)).done([=](const TLDemojiStatuses &result) {
		_recentRequestId = 0;
		updateRecent(result);
	}).fail([=] {
		_recentRequestId = 0;
		_recentHash = 0;
	}).send();
}

void EmojiStatuses::requestDefault() {
	if (_defaultRequestId) {
		return;
	}
#if 0 // mtp
	auto &api = _owner->session().api();
	_defaultRequestId = api.request(MTPaccount_GetDefaultEmojiStatuses(
		MTP_long(_recentHash)
	)).done([=](const MTPaccount_EmojiStatuses &result) {
		_defaultRequestId = 0;
		result.match([&](const MTPDaccount_emojiStatuses &data) {
			updateDefault(data);
		}, [&](const MTPDaccount_emojiStatusesNotModified &) {
		});
#endif
	auto &api = _owner->session().sender();
	_defaultRequestId = api.request(TLgetDefaultEmojiStatuses(
	)).done([=](const TLDemojiStatuses &result) {
		_defaultRequestId = 0;
		updateDefault(result);
	}).fail([=] {
		_defaultRequestId = 0;
		_defaultHash = 0;
	}).send();
}

void EmojiStatuses::requestColored() {
	if (_coloredRequestId) {
		return;
	}
#if 0 // mtp
	auto &api = _owner->session().api();
	_coloredRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiDefaultStatuses(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_coloredRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateColored(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
#endif
	auto &api = _owner->session().sender();
	_coloredRequestId = api.request(TLgetThemedEmojiStatuses(
	)).done([=](const TLDemojiStatuses &result) {
		_coloredRequestId = 0;
		updateColored(result);
	}).fail([=] {
		_coloredRequestId = 0;
	}).send();
}

#if 0 // mtp
void EmojiStatuses::updateRecent(const MTPDaccount_emojiStatuses &data) {
	_recentHash = data.vhash().v;
	_recent = ListFromMTP(data);
	_recentUpdated.fire({});
}

void EmojiStatuses::updateDefault(const MTPDaccount_emojiStatuses &data) {
	_defaultHash = data.vhash().v;
	_default = ListFromMTP(data);
	_defaultUpdated.fire({});
}

void EmojiStatuses::updateColored(const MTPDmessages_stickerSet &data) {
	const auto &list = data.vdocuments().v;
	_colored.clear();
	_colored.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		_colored.push_back(_owner->processDocument(sticker)->id);
	}
	_coloredUpdated.fire({});
}
#endif

void EmojiStatuses::updateRecent(const TLDemojiStatuses &data) {
	_recent = ListFromTL(data);
	_recentUpdated.fire({});
}

void EmojiStatuses::updateDefault(const TLDemojiStatuses &data) {
	_default = ListFromTL(data);
	_defaultUpdated.fire({});
}

void EmojiStatuses::updateColored(const TLDemojiStatuses &data) {
	_colored = ListFromTL(data);
	_coloredUpdated.fire({});
}

void EmojiStatuses::set(DocumentId id, TimeId until) {
#if 0 // mtp
	auto &api = _owner->session().api();
	if (_sentRequestId) {
		api.request(base::take(_sentRequestId)).cancel();
	}
	_owner->session().user()->setEmojiStatus(id, until);
	_sentRequestId = api.request(MTPaccount_UpdateEmojiStatus(
		!id
		? MTP_emojiStatusEmpty()
		: !until
		? MTP_emojiStatus(MTP_long(id))
		: MTP_emojiStatusUntil(MTP_long(id), MTP_int(until))
#endif
	auto &api = _owner->session().sender();
	if (_sentRequestId) {
		api.request(base::take(_sentRequestId)).cancel();
	}
	_owner->session().user()->setEmojiStatus(id, until);
	_sentRequestId = api.request(TLsetEmojiStatus(id
		? tl_emojiStatus(tl_int64(id), tl_int32(until))
		: std::optional<TLemojiStatus>()
	)).done([=] {
		_sentRequestId = 0;
	}).fail([=] {
		_sentRequestId = 0;
	}).send();
}

bool EmojiStatuses::setting() const {
	return _sentRequestId != 0;;
}

EmojiStatusData ParseEmojiStatus(const MTPEmojiStatus &status) {
	return status.match([](const MTPDemojiStatus &data) {
		return EmojiStatusData{ data.vdocument_id().v };
	}, [](const MTPDemojiStatusUntil &data) {
		return EmojiStatusData{ data.vdocument_id().v, data.vuntil().v };
	}, [](const MTPDemojiStatusEmpty &) {
		return EmojiStatusData();
	});
}

} // namespace Data
