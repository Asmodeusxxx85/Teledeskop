/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tdb/tdb_sender.h"

namespace Tdb {
class TLok;
class Error;
} // namespace Tdb

class ApiWrap;

namespace Api {

class Authorizations final {
public:
	explicit Authorizations(not_null<ApiWrap*> api);

	struct Entry {
#if 0 // goodToRemove
		uint64 hash = 0;
#endif
		int64 hash = 0;

		bool incomplete = false;
		bool callsDisabled = false;
		int apiId = 0;
		TimeId activeTime = 0;
		QString name, active, info, ip, location, system, platform;
	};
	using List = std::vector<Entry>;

	void reload();
	void cancelCurrentRequest();
	void requestTerminate(
#if 0 // goodToRemove
		Fn<void(const MTPBool &result)> &&done,
		Fn<void(const MTP::Error &error)> &&fail,
#endif
		Fn<void()> &&done,
		Fn<void()> &&fail,
		std::optional<uint64> hash = std::nullopt);

	[[nodiscard]] crl::time lastReceivedTime();

	[[nodiscard]] List list() const;
	[[nodiscard]] rpl::producer<List> listValue() const;
	[[nodiscard]] int total() const;
	[[nodiscard]] rpl::producer<int> totalValue() const;

	void updateTTL(int days);
	[[nodiscard]] rpl::producer<int> ttlDays() const;

	void toggleCallsDisabledHere(bool disabled) {
		toggleCallsDisabled(0, disabled);
	}
	void toggleCallsDisabled(uint64 hash, bool disabled);
	[[nodiscard]] bool callsDisabledHere() const;
	[[nodiscard]] rpl::producer<bool> callsDisabledHereValue() const;
	[[nodiscard]] rpl::producer<bool> callsDisabledHereChanges() const;

	[[nodiscard]] static QString ActiveDateString(TimeId active);

private:
	void refreshCallsDisabledHereFromCloud();

#if 0 // goodToRemove
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
#endif
	Tdb::Sender _api;
	Tdb::RequestId _requestId = 0;

	List _list;
	rpl::event_stream<> _listChanges;

	mtpRequestId _ttlRequestId = 0;
	rpl::variable<int> _ttlDays = 0;

	base::flat_map<uint64, mtpRequestId> _toggleCallsDisabledRequests;
	rpl::variable<bool> _callsDisabledHere;

	crl::time _lastReceived = 0;
	rpl::lifetime _lifetime;

};

} // namespace Api
