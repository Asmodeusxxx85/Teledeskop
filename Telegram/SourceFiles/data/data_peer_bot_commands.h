/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_bot_command.h"

namespace Tdb {
class TLbotCommands;
} // namespace Tdb

namespace Data {

struct BotCommands final {
	UserId userId;
	std::vector<BotCommand> commands;
};

struct ChatBotCommands final : public base::flat_map<
	UserId,
	std::vector<BotCommand>> {
public:
	using Changed = bool;

	using base::flat_map<UserId, std::vector<BotCommand>>::flat_map;

	Changed update(const std::vector<BotCommands> &list);
};

#if 0 // mtp
[[nodiscard]] BotCommands BotCommandsFromTL(const MTPBotInfo &result);
#endif
[[nodiscard]] BotCommands BotCommandsFromTL(const Tdb::TLbotCommands &result);

} // namespace Data
