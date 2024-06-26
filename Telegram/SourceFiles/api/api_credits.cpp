/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits.h"

#include "apiwrap.h"
#include "api/api_updates.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

#include "tdb/tdb_tl_scheme.h"
#include "api/api_text_entities.h"

namespace Api {
namespace {

using namespace Tdb;

constexpr auto kTransactionsPerStatus = 3;
constexpr auto kTransactionsPerPage = 50;

#if 0 // mtp
[[nodiscard]] Data::CreditsHistoryEntry HistoryFromTL(
		const MTPStarsTransaction &tl,
		not_null<PeerData*> peer) {
	using HistoryPeerTL = MTPDstarsTransactionPeer;
	const auto photo = tl.data().vphoto()
		? peer->owner().photoFromWeb(*tl.data().vphoto(), ImageLocation())
		: nullptr;
	return Data::CreditsHistoryEntry{
		.id = qs(tl.data().vid()),
		.title = qs(tl.data().vtitle().value_or_empty()),
		.description = qs(tl.data().vdescription().value_or_empty()),
		.date = base::unixtime::parse(tl.data().vdate().v),
		.photoId = photo ? photo->id : 0,
		.credits = tl.data().vstars().v,
		.bareId = tl.data().vpeer().match([](const HistoryPeerTL &p) {
			return peerFromMTP(p.vpeer());
		}, [](const auto &) {
			return PeerId(0);
		}).value,
		.peerType = tl.data().vpeer().match([](const HistoryPeerTL &) {
			return Data::CreditsHistoryEntry::PeerType::Peer;
		}, [](const MTPDstarsTransactionPeerPlayMarket &) {
			return Data::CreditsHistoryEntry::PeerType::PlayMarket;
		}, [](const MTPDstarsTransactionPeerFragment &) {
			return Data::CreditsHistoryEntry::PeerType::Fragment;
		}, [](const MTPDstarsTransactionPeerAppStore &) {
			return Data::CreditsHistoryEntry::PeerType::AppStore;
		}, [](const MTPDstarsTransactionPeerUnsupported &) {
			return Data::CreditsHistoryEntry::PeerType::Unsupported;
		}, [](const MTPDstarsTransactionPeerPremiumBot &) {
			return Data::CreditsHistoryEntry::PeerType::PremiumBot;
		}),
		.refunded = tl.data().is_refund(),
	};
}

[[nodiscard]] Data::CreditsStatusSlice StatusFromTL(
		const MTPpayments_StarsStatus &status,
		not_null<PeerData*> peer) {
	peer->owner().processUsers(status.data().vusers());
	peer->owner().processChats(status.data().vchats());
	return Data::CreditsStatusSlice{
		.list = ranges::views::all(
			status.data().vhistory().v
		) | ranges::views::transform([&](const MTPStarsTransaction &tl) {
			return HistoryFromTL(tl, peer);
		}) | ranges::to_vector,
		.balance = status.data().vbalance().v,
		.allLoaded = !status.data().vnext_offset().has_value(),
		.token = qs(status.data().vnext_offset().value_or_empty()),
	};
}
#endif

[[nodiscard]] Data::CreditsHistoryEntry HistoryFromTL(
		const TLstarTransaction &tl,
		not_null<PeerData*> peer) {
	const auto &data = tl.data();
	auto result = Data::CreditsHistoryEntry{
		.id = data.vid().v,
		.date = base::unixtime::parse(data.vdate().v),
		.credits = uint64(data.vstar_count().v),
		.refunded = data.vis_refund().v,
	};
	using Type = Data::CreditsHistoryEntry::PeerType;
	data.vpartner().match([&](const TLDstarTransactionPartnerTelegram &) {
		result.peerType = Type::PremiumBot;
	}, [&](const TLDstarTransactionPartnerAppStore &) {
		result.peerType = Type::AppStore;
	}, [&](const TLDstarTransactionPartnerGooglePlay &) {
		result.peerType = Type::PlayMarket;
	}, [&](const TLDstarTransactionPartnerFragment &data) {
		result.peerType = Type::Fragment;
		//data.vwithdrawal_state();
	}, [&](const TLDstarTransactionPartnerUser &data) {
		if (const auto product = data.vproduct_info()) {
			const auto &data = product->data();
			result.title = data.vtitle().v;
			result.description = Api::FormattedTextFromTdb(
				data.vdescription()
			).text;
			if (const auto photo = data.vphoto()) {
				result.photoId = peer->owner().processPhoto(*photo)->id;
			}
		}
		result.bareId = peerFromUser(data.vuser_id().v).value;
		result.peerType = Type::Peer;
	}, [&](const TLDstarTransactionPartnerChannel &data) {
		result.bareId = peerFromTdbChat(data.vchat_id()).value;
		result.peerType = Type::Peer;
	}, [&](const TLDstarTransactionPartnerUnsupported &) {
		result.peerType = Type::Unsupported;
	});
	return result;
}

[[nodiscard]] Data::CreditsStatusSlice StatusFromTL(
		const TLstarTransactions &status,
		not_null<PeerData*> peer) {
	const auto &data = status.data();
	return Data::CreditsStatusSlice{
		.list = ranges::views::all(
			data.vtransactions().v
		) | ranges::views::transform([&](const TLstarTransaction &tl) {
			return HistoryFromTL(tl, peer);
		}) | ranges::to_vector,
		.balance = uint64(data.vstar_count().v),
		.allLoaded = data.vnext_offset().v.isEmpty(),
		.token = data.vnext_offset().v,
	};
}

} // namespace

CreditsTopupOptions::CreditsTopupOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> CreditsTopupOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

#if 0 // mtp
		using TLOption = MTPStarsTopupOption;
		_api.request(MTPpayments_GetStarsTopupOptions(
		)).done([=](const MTPVector<TLOption> &result) {
			_options = ranges::views::all(
				result.v
			) | ranges::views::transform([](const TLOption &option) {
				return Data::CreditTopupOption{
					.credits = option.data().vstars().v,
					.product = qs(
						option.data().vstore_product().value_or_empty()),
					.currency = qs(option.data().vcurrency()),
					.amount = option.data().vamount().v,
					.extended = option.data().is_extended(),
				};
			}) | ranges::to_vector;
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
#endif
		_api.request(TLgetStarPaymentOptions(
		)).done([=](const TLDstarPaymentOptions &result) {
			_options = ranges::views::all(
				result.voptions().v
			) | ranges::views::transform([](const TLstarPaymentOption &option) {
				const auto &data = option.data();
				return Data::CreditTopupOption{
					.credits = uint64(data.vstar_count().v),
					.product = data.vstore_product_id().v,
					.currency = data.vcurrency().v,
					.amount = uint64(data.vamount().v),
					.extended = data.vis_additional().v,
				};
			}) | ranges::to_vector;
			consumer.put_done();
		}).fail([=](const Error &error) {
			consumer.put_error_copy(error.message);
		}).send();

		return lifetime;
	};
}

CreditsStatus::CreditsStatus(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

void CreditsStatus::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}

#if 0 // mtp
	using TLResult = MTPpayments_StarsStatus;

	_requestId = _api.request(MTPpayments_GetStarsStatus(
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input
#endif
	using TLResult = TLstarTransactions;
	_requestId = _api.request(TLgetStarTransactions(
		peerToSender(_peer->id),
		null,
		tl_string(),
		tl_int32(kTransactionsPerStatus)
	)).done([=](const TLResult &result) {
		_requestId = 0;
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

CreditsHistory::CreditsHistory(not_null<PeerData*> peer, bool in, bool out)
: _peer(peer)
#if 0 // mtp
, _flags((in == out)
	? HistoryTL::Flags(0)
	: HistoryTL::Flags(0)
		| (in ? HistoryTL::Flag::f_inbound : HistoryTL::Flags(0))
		| (out ? HistoryTL::Flag::f_outbound : HistoryTL::Flags(0)))
#endif
, _in(in)
, _out(out)
, _api(&peer->session().api().instance()) {
}

void CreditsHistory::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}
#if 0 // mtp
	_requestId = _api.request(MTPpayments_GetStarsTransactions(
		MTP_flags(_flags),
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input,
		MTP_string(token)
	)).done([=](const MTPpayments_StarsStatus &result) {
#endif
	_requestId = _api.request(TLgetStarTransactions(
		peerToSender(_peer->id),
		((_in == _out)
			? std::optional<TLstarTransactionDirection>()
			: _in
			? tl_starTransactionDirectionIncoming()
			: tl_starTransactionDirectionOutgoing()),
		tl_string(token),
		tl_int32(kTransactionsPerPage)
	)).done([=](const TLstarTransactions &result) {
		_requestId = 0;
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

Data::CreditTopupOptions CreditsTopupOptions::options() const {
	return _options;
}

#if 0 // mtp
rpl::producer<not_null<PeerData*>> PremiumPeerBot(
		not_null<Main::Session*> session) {
	const auto username = session->appConfig().get<QString>(
		u"premium_bot_username"_q,
		QString());
	if (username.isEmpty()) {
		return rpl::never<not_null<PeerData*>>();
	}
	if (const auto p = session->data().peerByUsername(username)) {
		return rpl::single<not_null<PeerData*>>(p);
	}
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto api = lifetime.make_state<MTP::Sender>(&session->mtp());

		api->request(MTPcontacts_ResolveUsername(
			MTP_string(username)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			session->data().processUsers(result.data().vusers());
			session->data().processChats(result.data().vchats());
			const auto botPeer = session->data().peerLoaded(
				peerFromMTP(result.data().vpeer()));
			if (!botPeer) {
				return consumer.put_done();
			}
			consumer.put_next(not_null{ botPeer });
		}).send();

		return lifetime;
	};
}
#endif

} // namespace Api
