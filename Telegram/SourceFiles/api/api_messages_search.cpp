/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include "tdb/tdb_sender.h"
#include "tdb/tdb_tl_scheme.h"

namespace Api {
namespace {

using namespace Tdb;

constexpr auto kSearchPerPage = 50;

[[nodiscard]] MessageIdsList HistoryItemsFromTL(
		not_null<Data::Session*> data,
		const QVector<TLmessage> &messages) {
	auto result = MessageIdsList();
	for (const auto &message : messages) {
		const auto item = data->processMessage(
			message,
			NewMessageType::Existing);
		result.push_back(item->fullId());
	}
	return result;
}
#if 0 // mtp
[[nodiscard]] MessageIdsList HistoryItemsFromTL(
		not_null<Data::Session*> data,
		const QVector<MTPMessage> &messages) {
	auto result = MessageIdsList();
	for (const auto &message : messages) {
		const auto peerId = PeerFromMessage(message);
		if (const auto peer = data->peerLoaded(peerId)) {
			if (const auto lastDate = DateFromMessage(message)) {
				const auto item = data->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				result.push_back(item->fullId());
			}
		} else {
			LOG(("API Error: a search results with not loaded peer %1"
				).arg(peerId.value));
		}
	}
	return result;
}
#endif

} // namespace

MessagesSearch::MessagesSearch(not_null<History*> history)
: _history(history) {
}

MessagesSearch::~MessagesSearch() {
	_history->session().sender().request(_requestId).cancel();
#if 0 // mtp
	_history->owner().histories().cancelRequest(
		base::take(_searchInHistoryRequest));
#endif
}

void MessagesSearch::searchMessages(const QString &query, PeerData *from) {
	_query = query;
	_from = from;
	_offsetId = {};
	_full = false;
	searchRequest();
}

void MessagesSearch::searchMore() {
	if (_searchInHistoryRequest || _requestId || _full) {
		return;
	}
	searchRequest();
}

void MessagesSearch::searchRequest() {
	const auto nextToken = _query
		+ QString::number(_from ? _from->id.value : 0);
	if (!_offsetId) {
		const auto it = _cacheOfStartByToken.find(nextToken);
		if (it != end(_cacheOfStartByToken)) {
			_requestId = 0;
			searchReceived(it->second, _requestId, nextToken);
			return;
		}
	}
	if (_requestId) {
		_history->session().sender().request(_requestId).cancel();
	}
	_requestId = _history->session().sender().request(TLsearchChatMessages(
		peerToTdbChat(_history->peer->id),
		tl_string(_query),
		(_from
			? peerToSender(_from->id)
			: std::optional<TLmessageSender>()),
		tl_int53(_offsetId.bare), // from_message_id
		tl_int32(0), // offset
		tl_int32(kSearchPerPage),
		std::nullopt, // filter
		tl_int53(0) // message_thread_id
	)).done([=](const TLfoundChatMessages &result, RequestId id) {
		searchReceived(result, id, nextToken);
	}).fail([=](const Error &error) {
		_requestId = 0;
		if (error.message == u"SEARCH_QUERY_EMPTY"_q) {
			_messagesFounds.fire({ 0, MessageIdsList(), nextToken });
		}
	}).send();
#if 0 // mtp
	auto callback = [=](Fn<void()> finish) {
		const auto flags = _from
			? MTP_flags(MTPmessages_Search::Flag::f_from_id)
			: MTP_flags(0);
		_requestId = _history->session().api().request(MTPmessages_Search(
			flags,
			_history->peer->input,
			MTP_string(_query),
			(_from
				? _from->input
				: MTP_inputPeerEmpty()),
			MTPint(), // top_msg_id
			MTP_inputMessagesFilterEmpty(),
			MTP_int(0), // min_date
			MTP_int(0), // max_date
			MTP_int(_offsetId), // offset_id
			MTP_int(0), // add_offset
			MTP_int(kSearchPerPage),
			MTP_int(0), // max_id
			MTP_int(0), // min_id
			MTP_long(0) // hash
		)).done([=](const TLMessages &result, mtpRequestId id) {
			_searchInHistoryRequest = 0;
			searchReceived(result, id, nextToken);
			finish();
		}).fail([=](const MTP::Error &error, mtpRequestId id) {
			_searchInHistoryRequest = 0;

			if (_requestId == id) {
				_requestId = 0;
			}
			if (error.type() == u"SEARCH_QUERY_EMPTY"_q) {
				_messagesFounds.fire({ 0, MessageIdsList(), nextToken });
			}

			finish();
		}).send();
		return _requestId;
	};
	_searchInHistoryRequest = _history->owner().histories().sendRequest(
		_history,
		Data::Histories::RequestType::History,
		std::move(callback));
#endif
}

void MessagesSearch::searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken) {
	Expects(_requestId == requestId);

	const auto &data = result.data();
	auto items = HistoryItemsFromTL(&_history->owner(), data.vmessages().v);
	const auto total = int(data.vtotal_count().v);
	auto found = FoundMessages{ total, std::move(items), nextToken };
#if 0 // mtp
	if (requestId != _requestId) {
		return;
	}
	auto &owner = _history->owner();
	auto found = result.match([&](const MTPDmessages_messages &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		const auto total = int(data.vmessages().v.size());
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_messagesSlice &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		// data.vnext_rate() is used only in global search.
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [&](const MTPDmessages_channelMessages &data) {
		if (_requestId != 0) {
			// Don't apply cached data!
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		}
		if (const auto channel = _history->peer->asChannel()) {
			channel->ptsReceived(data.vpts().v);
			if (_requestId != 0) {
				// Don't apply cached data!
				channel->processTopics(data.vtopics());
			}
		} else {
			LOG(("API Error: "
				"received messages.channelMessages when no channel "
				"was passed!"));
		}
		auto items = HistoryItemsFromTL(&owner, data.vmessages().v);
		const auto total = int(data.vcount().v);
		return FoundMessages{ total, std::move(items), nextToken };
	}, [](const MTPDmessages_messagesNotModified &data) {
		return FoundMessages{};
	});
#endif
	if (!_offsetId) {
		_cacheOfStartByToken.emplace(nextToken, result);
	}
	_requestId = 0;
	_offsetId = data.vnext_from_message_id().v;
	_full = !_offsetId;
	found.full = _full;
	_messagesFounds.fire(std::move(found));
}

rpl::producer<FoundMessages> MessagesSearch::messagesFounds() const {
	return _messagesFounds.events();
}

} // namespace Api
