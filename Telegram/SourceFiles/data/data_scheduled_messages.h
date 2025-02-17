/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"
#include "base/timer.h"

namespace Tdb {
class TLmessages;
} // namespace Tdb

class History;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
struct MessagesSlice;

#if 0 // mtp
[[nodiscard]] bool IsScheduledMsgId(MsgId id);
#endif

class ScheduledMessages final {
public:
	explicit ScheduledMessages(not_null<Session*> owner);
	ScheduledMessages(const ScheduledMessages &other) = delete;
	ScheduledMessages &operator=(const ScheduledMessages &other) = delete;
	~ScheduledMessages();

	[[nodiscard]] MsgId lookupId(not_null<const HistoryItem*> item) const;
	[[nodiscard]] HistoryItem *lookupItem(PeerId peer, MsgId msg) const;
	[[nodiscard]] HistoryItem *lookupItem(FullMsgId itemId) const;
	[[nodiscard]] int count(not_null<History*> history) const;
	[[nodiscard]] MsgId localMessageId(MsgId remoteId) const;

#if 0 // mtp
	void checkEntitiesAndUpdate(const MTPDmessage &data);
	void apply(const MTPDupdateNewScheduledMessage &update);
	void apply(const MTPDupdateDeleteScheduledMessages &update);
	void apply(
		const MTPDupdateMessageID &update,
		not_null<HistoryItem*> local);
#endif

	void append(not_null<HistoryItem*> item);

#if 0 // mtp
	void appendSending(not_null<HistoryItem*> item);
	void removeSending(not_null<HistoryItem*> item);

	void sendNowSimpleMessage(
		const MTPDupdateShortSentMessage &update,
		not_null<HistoryItem*> local);
#endif

	[[nodiscard]] rpl::producer<> updates(not_null<History*> history);
	[[nodiscard]] Data::MessagesSlice list(not_null<History*> history);

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct List {
		std::vector<OwnedItem> items;
		base::flat_map<MsgId, not_null<HistoryItem*>> itemById;
	};
	struct Request {
		mtpRequestId requestId = 0;
		crl::time lastReceived = 0;
	};

	void request(not_null<History*> history);

	void parse(not_null<History*> history, const Tdb::TLmessages &result);
#if 0 // mtp
	void parse(
		not_null<History*> history,
		const MTPmessages_Messages &list);
	HistoryItem *append(
		not_null<History*> history,
		List &list,
		const MTPMessage &message);
#endif

	void clearNotSending(not_null<History*> history);
	void updated(
		not_null<History*> history,
		const base::flat_set<not_null<HistoryItem*>> &added,
		const base::flat_set<not_null<HistoryItem*>> &clear);
	void sort(List &list);
	void remove(not_null<const HistoryItem*> item);
	[[nodiscard]] uint64 countListHash(const List &list) const;
	void clearOldRequests();

	const not_null<Main::Session*> _session;

	base::Timer _clearTimer;
	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<not_null<History*>, Request> _requests;
	rpl::event_stream<not_null<History*>> _updates;

	rpl::lifetime _lifetime;

};

} // namespace Data
