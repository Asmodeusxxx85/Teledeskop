/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_send_data.h"

#include "api/api_text_entities.h"
#include "data/data_document.h"
#include "inline_bots/inline_bot_result.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_channel.h"
#include "tdb/tdb_format_phone.h" // Tdb::FormatPhone

namespace InlineBots {
namespace internal {

QString SendData::getLayoutTitle(const Result *owner) const {
	return owner->_title;
}

QString SendData::getLayoutDescription(const Result *owner) const {
	return owner->_description;
}

void SendDataCommon::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MessageFlags flags,
		MsgId msgId,
		PeerId fromId,
		TimeId date,
		UserId viaBotId,
		FullReplyTo replyTo,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup) const {
#if 0 // mtp
	auto fields = getSentMessageFields();
	if (replyTo) {
		flags |= MessageFlag::HasReplyInfo;
	}
	history->addNewLocalMessage(
		msgId,
		flags,
		viaBotId,
		replyTo,
		date,
		fromId,
		postAuthor,
		std::move(fields.text),
		std::move(fields.media),
		std::move(markup));
#endif
}

QString SendDataCommon::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = ChatRestriction::SendOther;
	return Data::RestrictionError(history->peer, type).value_or(QString());
}

#if 0 // mtp
SendDataCommon::SentMessageFields SendText::getSentMessageFields() const {
	return { .text = { _message, _entities } };
}

SendDataCommon::SentMessageFields SendGeo::getSentMessageFields() const {
	if (_period) {
		using Flag = MTPDmessageMediaGeoLive::Flag;
		return { .media = MTP_messageMediaGeoLive(
			MTP_flags((_heading ? Flag::f_heading : Flag(0))
				| (_proximityNotificationRadius
					? Flag::f_proximity_notification_radius
					: Flag(0))),
			_location.toMTP(),
			MTP_int(_heading.value_or(0)),
			MTP_int(*_period),
			MTP_int(_proximityNotificationRadius.value_or(0))) };
	}
	return { .media = MTP_messageMediaGeo(_location.toMTP()) };
}

SendDataCommon::SentMessageFields SendVenue::getSentMessageFields() const {
	const auto venueType = QString();
	return { .media = MTP_messageMediaVenue(
		_location.toMTP(),
		MTP_string(_title),
		MTP_string(_address),
		MTP_string(_provider),
		MTP_string(_venueId),
		MTP_string(QString())) }; // venue_type
}

SendDataCommon::SentMessageFields SendContact::getSentMessageFields() const {
	return { .media = MTP_messageMediaContact(
		MTP_string(_phoneNumber),
		MTP_string(_firstName),
		MTP_string(_lastName),
		MTP_string(), // vcard
		MTP_long(0)) }; // user_id
}
#endif

QString SendContact::getLayoutDescription(const Result *owner) const {
	auto result = SendData::getLayoutDescription(owner);
	if (result.isEmpty()) {
		return Tdb::FormatPhone(_phoneNumber);
	}
	return result;
}

void SendPhoto::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MessageFlags flags,
		MsgId msgId,
		PeerId fromId,
		TimeId date,
		UserId viaBotId,
		FullReplyTo replyTo,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup) const {
	history->addNewLocalMessage(
		msgId,
		flags,
		viaBotId,
		replyTo,
		date,
		fromId,
		postAuthor,
		_photo,
		{ _message, _entities },
		std::move(markup));
}

QString SendPhoto::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = ChatRestriction::SendPhotos;
	return Data::RestrictionError(history->peer, type).value_or(QString());
}

void SendFile::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MessageFlags flags,
		MsgId msgId,
		PeerId fromId,
		TimeId date,
		UserId viaBotId,
		FullReplyTo replyTo,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup) const {
	history->addNewLocalMessage(
		msgId,
		flags,
		viaBotId,
		replyTo,
		date,
		fromId,
		postAuthor,
		_document,
		{ _message, _entities },
		std::move(markup));
}

QString SendFile::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = _document->requiredSendRight();
	return Data::RestrictionError(history->peer, type).value_or(QString());
}

void SendGame::addToHistory(
		const Result *owner,
		not_null<History*> history,
		MessageFlags flags,
		MsgId msgId,
		PeerId fromId,
		TimeId date,
		UserId viaBotId,
		FullReplyTo replyTo,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup) const {
	history->addNewLocalMessage(
		msgId,
		flags,
		viaBotId,
		replyTo,
		date,
		fromId,
		postAuthor,
		_game,
		std::move(markup));
}

QString SendGame::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = ChatRestriction::SendGames;
	return Data::RestrictionError(history->peer, type).value_or(QString());
}

#if 0 // mtp
SendDataCommon::SentMessageFields SendInvoice::getSentMessageFields() const {
	return { .media = _media };
}

QString SendInvoice::getLayoutDescription(const Result *owner) const {
	return qs(_media.c_messageMediaInvoice().vdescription());
}
#endif

} // namespace internal
} // namespace InlineBots
