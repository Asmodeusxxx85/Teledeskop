/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/algorithm.h"

#include "tdb/tdb_sender.h"

namespace Tdb {
class TLjsonValue;
} // namespace Tdb

#if 0 // mtp
namespace Ui {
struct ColorIndicesCompressed;
} // namespace Ui
#endif

namespace Main {

class Account;

class AppConfig final {
public:
	explicit AppConfig(not_null<Account*> account);
	~AppConfig();

	void start();

	template <typename Type>
	[[nodiscard]] Type get(const QString &key, Type fallback) const {
		if constexpr (std::is_same_v<Type, double>) {
			return getDouble(key, fallback);
		} else if constexpr (std::is_same_v<Type, int>) {
			return int(base::SafeRound(getDouble(key, double(fallback))));
		} else if constexpr (std::is_same_v<Type, QString>) {
			return getString(key, fallback);
		} else if constexpr (std::is_same_v<Type, std::vector<QString>>) {
			return getStringArray(key, std::move(fallback));
#if 0 // mtp
		} else if constexpr (std::is_same_v<Type, std::vector<int>>) {
			return getIntArray(key, std::move(fallback));
#endif
		} else if constexpr (std::is_same_v<
				Type,
				std::vector<std::map<QString, QString>>>) {
			return getStringMapArray(key, std::move(fallback));
		} else if constexpr (std::is_same_v<Type, bool>) {
			return getBool(key, fallback);
		}
	}

	[[nodiscard]] rpl::producer<> refreshed() const;
	[[nodiscard]] rpl::producer<> value() const;

	[[nodiscard]] bool suggestionCurrent(const QString &key) const;
	[[nodiscard]] rpl::producer<> suggestionRequested(
		const QString &key) const;
	void dismissSuggestion(const QString &key);

#if 0 // mtp
	[[nodiscard]] auto colorIndicesValue() const
		-> rpl::producer<Ui::ColorIndicesCompressed>;
#endif

	void refresh();

private:
	void refreshDelayed();
#if 0 // mtp
	void parseColorIndices();
#endif

	template <typename Extractor>
	[[nodiscard]] auto getValue(
		const QString &key,
		Extractor &&extractor) const;

	[[nodiscard]] bool getBool(
		const QString &key,
		bool fallback) const;
	[[nodiscard]] double getDouble(
		const QString &key,
		double fallback) const;
	[[nodiscard]] QString getString(
		const QString &key,
		const QString &fallback) const;
	[[nodiscard]] std::vector<QString> getStringArray(
		const QString &key,
		std::vector<QString> &&fallback) const;
	[[nodiscard]] std::vector<std::map<QString, QString>> getStringMapArray(
		const QString &key,
		std::vector<std::map<QString, QString>> &&fallback) const;
	[[nodiscard]] std::vector<int> getIntArray(
		const QString &key,
		std::vector<int> &&fallback) const;

	const not_null<Account*> _account;
#if 0 // goodToRemove
	std::optional<MTP::Sender> _api;
	mtpRequestId _requestId = 0;
	int32 _hash = 0;
	base::flat_map<QString, MTPJSONValue> _data;
#endif
	std::optional<Tdb::Sender> _api;
	Tdb::RequestId _requestId = 0;
	base::flat_map<QString, Tdb::TLjsonValue> _data;
	rpl::event_stream<> _refreshed;
	base::flat_set<QString> _dismissedSuggestions;

#if 0 // mtp
	rpl::event_stream<> _colorIndicesChanged;
	std::unique_ptr<Ui::ColorIndicesCompressed> _colorIndicesCurrent;
#endif

	rpl::lifetime _lifetime;

};

} // namespace Main
