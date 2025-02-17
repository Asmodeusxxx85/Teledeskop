/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_blocked_peers.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include "tdb/tdb_account.h"
#include "tdb/tdb_tl_scheme.h"

namespace Api {
namespace {

constexpr auto kBlockedFirstSlice = 16;
constexpr auto kBlockedPerPage = 40;

using namespace Tdb;

#if 0 // goodToRemove
BlockedPeers::Slice TLToSlice(
		const MTPcontacts_Blocked &blocked,
		Data::Session &owner) {
	const auto create = [&](int count, const QVector<MTPPeerBlocked> &list) {
		auto slice = BlockedPeers::Slice();
		slice.total = std::max(count, int(list.size()));
		slice.list.reserve(list.size());
		for (const auto &contact : list) {
			contact.match([&](const MTPDpeerBlocked &data) {
				slice.list.push_back({
					.id = peerFromMTP(data.vpeer_id()),
					.date = data.vdate().v,
				});
			});
		}
		return slice;
	};
	return blocked.match([&](const MTPDcontacts_blockedSlice &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return create(data.vcount().v, data.vblocked().v);
	}, [&](const MTPDcontacts_blocked &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return create(0, data.vblocked().v);
	});
}
#endif

BlockedPeers::Slice TLToSlice(const TLDmessageSenders &data) {
	auto slice = BlockedPeers::Slice();
	slice.total = data.vtotal_count().v;
	slice.list.reserve(data.vsenders().v.size());
	for (const auto &sender : data.vsenders().v) {
		const auto peerId = peerFromSender(sender);
#if 0 // mtp
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
#endif
		slice.list.push_back({
			.id = peerId,
			.date = 0, // No info from TDLib.
		});
	}
	return slice;
}

} // namespace

BlockedPeers::BlockedPeers(not_null<ApiWrap*> api)
: _session(&api->session())
#if 0 // goodToRemove
, _api(&api->instance()) {
#endif
, _api(&api->sender()) {
}

bool BlockedPeers::Slice::Item::operator==(const Item &other) const {
	return (id == other.id) && (date == other.date);
}

bool BlockedPeers::Slice::Item::operator!=(const Item &other) const {
	return !(*this == other);
}

bool BlockedPeers::Slice::operator==(const BlockedPeers::Slice &other) const {
	return (total == other.total) && (list == other.list);
}

bool BlockedPeers::Slice::operator!=(const BlockedPeers::Slice &other) const {
	return !(*this == other);
}

#if 0 // goodToRemove
void BlockedPeers::block(not_null<PeerData*> peer) {
	if (peer->isBlocked()) {
		_session->changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::IsBlocked);
		return;
	} else if (blockAlreadySent(peer, true)) {
		return;
	}
	const auto requestId = _api.request(MTPcontacts_Block(
		MTP_flags(0),
		peer->input
	)).done([=] {
		const auto data = _blockRequests.take(peer);
		peer->setIsBlocked(true);
		if (_slice) {
			_slice->list.insert(
				_slice->list.begin(),
				{ peer->id, base::unixtime::now() });
			++_slice->total;
			_changes.fire_copy(*_slice);
		}
		if (data) {
			for (const auto &callback : data->callbacks) {
				callback(false);
			}
		}
	}).fail([=] {
		if (const auto data = _blockRequests.take(peer)) {
			for (const auto &callback : data->callbacks) {
				callback(false);
			}
		}
	}).send();

	_blockRequests.emplace(peer, Request{
		.requestId = requestId,
		.blocking = true,
	});
}

void BlockedPeers::unblock(
		not_null<PeerData*> peer,
		Fn<void(bool success)> done,
		bool force) {
	if (!force && !peer->isBlocked()) {
		_session->changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::IsBlocked);
		return;
	} else if (blockAlreadySent(peer, false, done)) {
		return;
	}
	const auto requestId = _api.request(MTPcontacts_Unblock(
		MTP_flags(0),
		peer->input
	)).done([=] {
		const auto data = _blockRequests.take(peer);
		peer->setIsBlocked(false);
		if (_slice) {
			auto &list = _slice->list;
			for (auto i = list.begin(); i != list.end(); ++i) {
				if (i->id == peer->id) {
					list.erase(i);
					break;
				}
			}
			if (_slice->total > list.size()) {
				--_slice->total;
			}
			_changes.fire_copy(*_slice);
		}
		if (data) {
			for (const auto &callback : data->callbacks) {
				callback(true);
			}
		}
	}).fail([=] {
		if (const auto data = _blockRequests.take(peer)) {
			for (const auto &callback : data->callbacks) {
				callback(false);
			}
		}
	}).send();
	const auto i = _blockRequests.emplace(peer, Request{
		.requestId = requestId,
		.blocking = false,
	}).first;
	if (done) {
		i->second.callbacks.push_back(std::move(done));
	}
}
#endif

bool BlockedPeers::blockAlreadySent(
		not_null<PeerData*> peer,
		bool blocking,
		Fn<void(bool success)> done) {
	const auto i = _blockRequests.find(peer);
	if (i == end(_blockRequests)) {
		return false;
	} else if (i->second.blocking == blocking) {
		if (done) {
			i->second.callbacks.push_back(std::move(done));
		}
		return true;
	}
	const auto callbacks = base::take(i->second.callbacks);
	_blockRequests.erase(i);
	for (const auto &callback : callbacks) {
		callback(false);
	}
	return false;
}

#if 0 // mtp
void BlockedPeers::reload() {
	if (_requestId) {
		return;
	}
	request(0, [=](Slice &&slice) {
		if (!_slice || *_slice != slice) {
			_slice = slice;
			_changes.fire(std::move(slice));
		}
	});
}

auto BlockedPeers::slice() -> rpl::producer<BlockedPeers::Slice> {
	if (!_slice) {
		reload();
	}
	return _slice
		? _changes.events_starting_with_copy(*_slice)
		: (_changes.events() | rpl::type_erased());
}

void BlockedPeers::request(int offset, Fn<void(BlockedPeers::Slice)> done) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPcontacts_GetBlocked(
		MTP_flags(0),
		MTP_int(offset),
		MTP_int(offset ? kBlockedPerPage : kBlockedFirstSlice)
	)).done([=](const MTPcontacts_Blocked &result) {
		_requestId = 0;
		done(TLToSlice(result, _session->data()));
	}).fail([=] {
		_requestId = 0;
	}).send();
}
#endif

void BlockedPeers::changeBlockState(
		not_null<PeerData*> peer,
		bool blocked,
		bool force,
		Fn<void(bool success)> done) {
	if (!force && peer->isBlocked() == blocked) {
		_session->changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::IsBlocked);
		if (done) {
			done(true);
		}
		return;
	} else if (blockAlreadySent(peer, blocked, done)) {
		return;
	}
	const auto requestId = _api.request(TLsetMessageSenderBlockList(
		tl_messageSenderChat(peerToTdbChat(peer->id)),
		blocked ? tl_blockListMain() : std::optional<TLblockList>()
	)).done([=](const TLok &ok) {
		if (const auto data = _blockRequests.take(peer)) {
			for (const auto &callback : data->callbacks) {
				callback(true);
			}
		}
	}).fail([=](const Error &error) {
		if (const auto data = _blockRequests.take(peer)) {
			for (const auto &callback : data->callbacks) {
				callback(false);
			}
		}
	}).send();
	const auto i = _blockRequests.emplace(peer, Request{
		.requestId = requestId,
		.blocking = blocked,
	}).first;
	if (done) {
		i->second.callbacks.push_back(std::move(done));
	}
}

void BlockedPeers::block(not_null<PeerData*> peer) {
	changeBlockState(peer, true);
}

void BlockedPeers::unblock(
		not_null<PeerData*> peer,
		Fn<void(bool success)> done,
		bool force) {
	changeBlockState(peer, false, force, std::move(done));
}

rpl::producer<int> BlockedPeers::total() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		_api.request(TLgetBlockedMessageSenders(
			tl_blockListMain(),
			tl_int32(0),
			tl_int32(1)
		)).done([=](const TLmessageSenders &result) {
			_total = result.data().vtotal_count().v;
			consumer.put_next_copy(_total);
		}).send();

		_session->tdb().updates(
		) | rpl::start_with_next([=](const TLupdate &update) {
			update.match([=](const TLDupdateChatBlockList &data) {
				if (data.vblock_list()
					&& data.vblock_list()->type() == id_blockListMain) {
					_total++;
				} else if (_total > 0) {
					_total--;
				}
				consumer.put_next_copy(_total);
			}, [](const auto &) {
			});
		}, lifetime);

		if (_total) {
			consumer.put_next_copy(_total);
		}

		return lifetime;
	};
}

void BlockedPeers::request(int offset, Fn<void(BlockedPeers::Slice)> onDone) {
	_api.request(TLgetBlockedMessageSenders(
		tl_blockListMain(),
		tl_int32(offset),
		tl_int32(offset ? kBlockedPerPage : kBlockedFirstSlice)
	)).done([=](const TLDmessageSenders &data) {
		onDone(TLToSlice(data));
	}).send();
}

} // namespace Api
