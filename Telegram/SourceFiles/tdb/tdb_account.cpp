/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tdb/tdb_account.h"

#include "tdb/tdb_file_generator.h"

namespace Tdb {

Account::Account(AccountConfig &&config)
: _instance(std::move(config))
, _sender(&_instance) {
	_instance.updates(
	) | rpl::start_with_next([=](TLupdate &&update) {
		if (!consumeUpdate(update)) {
			_updates.fire(std::move(update));
		}
	}, _lifetime);
}

void Account::logout() {
	_instance.logout();
}

void Account::reset() {
	_instance.reset();
}

rpl::producer<TLupdate> Account::updates() const {
	return _updates.events();
}

void Account::registerFileGenerator(not_null<FileGenerator*> generator) {
	_generators.emplace(generator->conversion(), generator);
}

void Account::unregisterFileGenerator(not_null<FileGenerator*> generator) {
	_generators.remove(generator->conversion());
}

void Account::registerFileGeneration(
		int64 id,
		not_null<FileGenerator*> generator) {
	_generations.emplace(id, generator);
}

void Account::unregisterFileGeneration(
		int64 id,
		not_null<FileGenerator*> generator) {
	_generations.remove(id);
}

bool Account::consumeUpdate(const TLupdate &update) {
	return update.match([&](const TLDupdateFileGenerationStart &data) {
		const auto &conversion = data.vconversion().v;
		const auto id = data.vgeneration_id().v;
		const auto i = _generators.find(conversion);
		if (i != end(_generators)) {
			i->second->start(id);
#if 0 // todo
		} else if (conversion == "#url#") {
			download(data.voriginal_path().v);
#endif
		} else {
			_sender.request(TLfinishFileGeneration(
				data.vgeneration_id(),
				tl_error(tl_int32(0), tl_string("unknown file generation"))
			)).send();
		}
		return true;
	}, [&](const TLDupdateFileGenerationStop &data) {
		const auto id = data.vgeneration_id().v;
		const auto i = _generations.find(id);
		if (i != end(_generations)) {
			i->second->finish();
			_generations.erase(i);
		}
		return true;
	}, [](const auto&) {
		return false;
	});
}

} // namespace Tdb
