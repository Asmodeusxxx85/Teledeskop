/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

class History;
class PhotoData;
class DocumentData;
struct FileLoadResult;

namespace tl {
class int64_type;
} // namespace tl

namespace Tdb {
class TLinputMessageContent;
class TLmessageSchedulingState;
class TLmessageSendOptions;
class TLinputMessageReplyTo;
using TLint53 = tl::int64_type;
} // namespace Tdb

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Api {

struct MessageToSend;
struct SendAction;

void SendExistingDocument(
	MessageToSend &&message,
	not_null<DocumentData*> document,
	std::optional<MsgId> localMessageId = std::nullopt);

void SendExistingPhoto(
	MessageToSend &&message,
	not_null<PhotoData*> photo,
	std::optional<MsgId> localMessageId = std::nullopt);

bool SendDice(MessageToSend &message);

void FillMessagePostFlags(
	const SendAction &action,
	not_null<PeerData*> peer,
	MessageFlags &flags);

void SendConfirmedFile(
	not_null<Main::Session*> session,
	const std::shared_ptr<FileLoadResult> &file);

[[nodiscard]] Tdb::TLmessageSendOptions MessageSendOptions(
	not_null<PeerData*> peer,
	const SendAction &action,
	int32 sendingId = 0);
[[nodiscard]] std::optional<Tdb::TLinputMessageReplyTo> MessageReplyTo(
	not_null<History*> history,
	const FullReplyTo &replyTo);
[[nodiscard]] std::optional<Tdb::TLinputMessageReplyTo> MessageReplyTo(
	const SendAction &action);
[[nodiscard]] Tdb::TLint53 MessageThreadId(
	not_null<PeerData*> peer,
	const SendAction &action);

void SendPreparedMessage(
	const SendAction &action,
	Tdb::TLinputMessageContent content,
	std::optional<MsgId> localMessageId = std::nullopt);

[[nodiscard]] std::optional<Tdb::TLmessageSchedulingState> ScheduledToTL(
	TimeId scheduled);

void TryGenerateLocalInlineResultMessage(
	not_null<UserData*> bot,
	not_null<InlineBots::Result*> data,
	const SendAction &action,
	MsgId localId);

} // namespace Api
