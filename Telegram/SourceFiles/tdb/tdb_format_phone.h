/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Tdb {

[[nodiscard]] QString FormatPhone(const QString &phone);
[[nodiscard]] QVector<int> PhonePatternGroups(const QString &phone);

} // namespace Tdb
