/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tdb/details/tdb_tl_core.h"
#include "tdb_tl-scheme.h"

namespace Tdb {

TLstring tl_from_string(const std::string &value);
TLbytes tl_from_simple(const std::string &value);
TLint32 tl_from_simple(std::int32_t value);
TLint64 tl_from_simple(std::int64_t value);
TLdouble tl_from_simple(double value);
TLbool tl_from_simple(bool value);

template <typename U, typename T>
auto tl_from_vector_optional(const std::vector<T> &value) {
	using I = in_TLvector_t<U>;
	auto result = QVector<std::optional<I>>();
	result.reserve(value.size());
	for (const auto &element : value) {
		result.push_back(element.get()
			? std::make_optional(tl_from<I>(element.get()))
			: std::nullopt);
	}
	return tl_vector<std::optional<I>>(std::move(result));
}

template <typename U, typename T>
auto tl_from_vector(const std::vector<T> &value) {
	using I = in_TLvector_t<U>;
	auto result = QVector<I>();
	result.reserve(value.size());
	constexpr bool simple = std::is_same_v<TLint32, I>
		|| std::is_same_v<TLint64, I>
		|| std::is_same_v<TLbytes, I>
		|| std::is_same_v<TLdouble, I>
		|| std::is_same_v<TLbool, I>;
	constexpr bool string = !simple && std::is_same_v<TLstring, I>;
	constexpr bool vector = !simple && !string && is_TLvector_v<I>;
	for (const auto &element : value) {
		if constexpr (string) {
			result.push_back(tl_from_string(element));
		} else if constexpr (simple) {
			result.push_back(tl_from_simple(element));
		} else if constexpr (vector) {
			result.push_back(tl_from_vector<I>(element));
		} else {
			result.push_back(tl_from<I>(element.get()));
		}
	}
	return tl_vector<I>(std::move(result));
}

} // namespace Tdb
