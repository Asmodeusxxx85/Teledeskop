/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_item.h"

#include "history/admin_log/history_admin_log_inner.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_location_manager.h"
#include "api/api_chat_participants.h"
#include "api/api_text_entities.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_message_reaction_id.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "boxes/sticker_set_box.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"

#include "tdb/tdb_tl_scheme.h"

namespace AdminLog {
namespace {

TextWithEntities PrepareText(
		const QString &value,
		const QString &emptyValue) {
	auto result = TextWithEntities{ value };
	if (result.text.isEmpty()) {
		result.text = emptyValue;
		if (!emptyValue.isEmpty()) {
			result.entities.push_back({
				EntityType::Italic,
				0,
				int(emptyValue.size()) });
		}
	} else {
		TextUtilities::ParseEntities(
			result,
			TextParseLinks
				| TextParseMentions
				| TextParseHashtags
				| TextParseBotCommands);
	}
	return result;
}

#if 0 // goodToRemove
[[nodiscard]] TimeId ExtractSentDate(const MTPMessage &message) {
	return message.match([](const MTPDmessageEmpty &) {
		return 0;
	}, [](const MTPDmessageService &data) {
		return data.vdate().v;
	}, [](const MTPDmessage &data) {
		return data.vdate().v;
	});
}

[[nodiscard]] MsgId ExtractRealMsgId(const MTPMessage &message) {
	return MsgId(message.match([](const MTPDmessageEmpty &) {
		return 0;
	}, [](const MTPDmessageService &data) {
		return data.vid().v;
	}, [](const MTPDmessage &data) {
		return data.vid().v;
	}));
}

MTPMessage PrepareLogMessage(const MTPMessage &message, TimeId newDate) {
	return message.match([&](const MTPDmessageEmpty &data) {
		return MTP_messageEmpty(
			data.vflags(),
			data.vid(),
			data.vpeer_id() ? *data.vpeer_id() : MTPPeer());
	}, [&](const MTPDmessageService &data) {
		const auto removeFlags = MTPDmessageService::Flag::f_out
			| MTPDmessageService::Flag::f_post
			| MTPDmessageService::Flag::f_reply_to
			| MTPDmessageService::Flag::f_ttl_period;
		return MTP_messageService(
			MTP_flags(data.vflags().v & ~removeFlags),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			MTPMessageReplyHeader(),
			MTP_int(newDate),
			data.vaction(),
			MTPint()); // ttl_period
	}, [&](const MTPDmessage &data) {
		const auto removeFlags = MTPDmessage::Flag::f_out
			| MTPDmessage::Flag::f_post
			| MTPDmessage::Flag::f_reply_to
			| MTPDmessage::Flag::f_replies
			| MTPDmessage::Flag::f_edit_date
			| MTPDmessage::Flag::f_grouped_id
			| MTPDmessage::Flag::f_views
			| MTPDmessage::Flag::f_forwards
			//| MTPDmessage::Flag::f_reactions
			| MTPDmessage::Flag::f_restriction_reason
			| MTPDmessage::Flag::f_ttl_period;
		return MTP_message(
			MTP_flags(data.vflags().v & ~removeFlags),
			data.vid(),
			data.vfrom_id() ? *data.vfrom_id() : MTPPeer(),
			data.vpeer_id(),
			data.vfwd_from() ? *data.vfwd_from() : MTPMessageFwdHeader(),
			MTP_long(data.vvia_bot_id().value_or_empty()),
			MTPMessageReplyHeader(),
			MTP_int(newDate),
			data.vmessage(),
			data.vmedia() ? *data.vmedia() : MTPMessageMedia(),
			data.vreply_markup() ? *data.vreply_markup() : MTPReplyMarkup(),
			(data.ventities()
				? *data.ventities()
				: MTPVector<MTPMessageEntity>()),
			MTP_int(data.vviews().value_or_empty()),
			MTP_int(data.vforwards().value_or_empty()),
			MTPMessageReplies(),
			MTPint(), // edit_date
			MTP_string(),
			MTP_long(0), // grouped_id
			MTPMessageReactions(),
			MTPVector<MTPRestrictionReason>(),
			MTPint()); // ttl_period
	});
}

bool MediaCanHaveCaption(const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return false;
	}
	const auto &data = message.c_message();
	const auto media = data.vmedia();
	const auto mediaType = media ? media->type() : mtpc_messageMediaEmpty;
	return (mediaType == mtpc_messageMediaDocument)
		|| (mediaType == mtpc_messageMediaPhoto);
}

uint64 MediaId(const MTPMessage &message) {
	if (!MediaCanHaveCaption(message)) {
		return 0;
	}
	const auto &media = message.c_message().vmedia();
	return media
		? v::match(
			Data::GetFileReferences(*media).data.begin()->first,
			[](const auto &d) { return d.id; })
		: 0;
}

TextWithEntities ExtractEditedText(
		not_null<Main::Session*> session,
		const MTPMessage &message) {
	if (message.type() != mtpc_message) {
		return TextWithEntities();
	}
	const auto &data = message.c_message();
	return {
		qs(data.vmessage()),
		Api::EntitiesFromMTP(session, data.ventities().value_or_empty())
	};
}
#endif

Tdb::TLmessage PrepareLogMessage(
		const Tdb::TLmessage &message,
		TimeId newDate) {
	return Tdb::tl_message(
		message.data().vid(),
		message.data().vsender_id(),
		message.data().vchat_id(),
		message.data().vsending_state()
			? std::make_optional(*message.data().vsending_state())
			: std::nullopt,
		message.data().vscheduling_state()
			? std::make_optional(*message.data().vscheduling_state())
			: std::nullopt,
		Tdb::tl_bool(false), // is_outgoing
		message.data().vis_pinned(),
		Tdb::tl_bool(false), // can_be_edited
		Tdb::tl_bool(false), // can_be_forwarded
		Tdb::tl_bool(false), // can_be_replied_in_another_chat
		Tdb::tl_bool(false), // can_be_saved
		Tdb::tl_bool(false), // can_be_deleted_only_for_self
		Tdb::tl_bool(false), // can_be_deleted_for_all_users
		Tdb::tl_bool(false), // can_get_added_reactions
		Tdb::tl_bool(false), // can_get_statistics
		Tdb::tl_bool(false), // can_get_message_thread
		Tdb::tl_bool(false), // can_get_viewers
		message.data().vcan_get_media_timestamp_links(),
		Tdb::tl_bool(false), // can_report_reactions
		message.data().vhas_timestamped_media(),
		Tdb::tl_bool(false), // is_channel_post
		Tdb::tl_bool(false), // is_topic_message
		Tdb::tl_bool(false), // contains_unread_mention
		Tdb::tl_int32(newDate),
		Tdb::tl_int32(0), // edit_date
		message.data().vforward_info()
			? std::make_optional(*message.data().vforward_info())
			: std::nullopt,
		std::nullopt, // import_info
		std::nullopt, // interaction_info
		Tdb::tl_vector<Tdb::TLunreadReaction>(),
		std::nullopt, // reply_to
		Tdb::tl_int53(0), // message_thread_id
		std::nullopt, // self_destruct_type
		Tdb::tl_double(0), // self_destruct_in
		Tdb::tl_double(0), // auto_delete_in
		message.data().vvia_bot_user_id(),
		message.data().vauthor_signature(),
		Tdb::tl_int64(0), // media_album_id
		Tdb::tl_string(), // restriction_reason
		message.data().vcontent(),
		message.data().vreply_markup()
			? std::make_optional(*message.data().vreply_markup())
			: std::nullopt);
}

bool MediaCanHaveCaption(const Tdb::TLmessage &message) {
	using namespace Tdb;
	return message.data().vcontent().match([](const auto &data) {
		using T = decltype(data);
		return TLDmessagePhoto::Is<T>()
			|| TLDmessageDocument::Is<T>()
			|| TLDmessageAudio::Is<T>()
			|| TLDmessageAnimation::Is<T>()
			|| TLDmessageExpiredPhoto::Is<T>()
			|| TLDmessageVideo::Is<T>()
			|| TLDmessageExpiredVideo::Is<T>()
			|| TLDmessageVoiceNote::Is<T>();
	});
}

FileId MediaId(const Tdb::TLmessage &message) {
	if (!MediaCanHaveCaption(message)) {
		return FileId();
	}

	using namespace Tdb;
	const auto &content = message.data().vcontent();
	return content.match([&](const TLDmessageAnimation &data) {
		return data.vanimation().data().vanimation().data().vid().v;
	}, [&](const TLDmessageAudio &data) {
		return data.vaudio().data().vaudio().data().vid().v;
	}, [&](const TLDmessageDocument &data) {
		return data.vdocument().data().vdocument().data().vid().v;
	}, [&](const TLDmessagePhoto &data) {
		const auto &sizes = data.vphoto().data().vsizes().v;
		return sizes.empty()
			? FileId()
			: sizes.front().data().vphoto().data().vid().v;
	}, [&](const TLDmessageSticker &data) {
		return data.vsticker().data().vsticker().data().vid().v;
	}, [&](const TLDmessageVideo &data) {
		return data.vvideo().data().vvideo().data().vid().v;
	}, [&](const TLDmessageVideoNote &data) {
		return data.vvideo_note().data().vvideo().data().vid().v;
	}, [&](const TLDmessageVoiceNote &data) {
		return data.vvoice_note().data().vvoice().data().vid().v;
	}, [](const auto &data) {
		return FileId();
	});
}

[[nodiscard]] MsgId ExtractRealMsgId(const Tdb::TLmessage &message) {
	return message.data().vid().v;
}

[[nodiscard]] TimeId ExtractSentDate(const Tdb::TLmessage &message) {
	return message.data().vdate().v;
}

TextWithEntities ExtractEditedText(
		not_null<Main::Session*> session,
		const Tdb::TLmessage &message) {
	return message.data().vcontent().match([&](
			const Tdb::TLDmessageText &data) {
		return Api::FormattedTextFromTdb(data.vtext());
	}, [](const auto &) {
		return TextWithEntities();
	});
}

const auto CollectChanges = [](
		auto &phraseMap,
		auto plusFlags,
		auto minusFlags) {
	auto withPrefix = [&phraseMap](auto flags, QChar prefix) {
		auto result = QString();
		for (auto &phrase : phraseMap) {
			if (flags & phrase.first) {
				result.append('\n' + (prefix + phrase.second(tr::now)));
			}
		}
		return result;
	};
	const auto kMinus = QChar(0x2212);
	return withPrefix(plusFlags & ~minusFlags, '+')
		+ withPrefix(minusFlags & ~plusFlags, kMinus);
};

TextWithEntities GenerateAdminChangeText(
		not_null<ChannelData*> channel,
		const TextWithEntities &user,
		ChatAdminRightsInfo newRights,
		ChatAdminRightsInfo prevRights) {
	using Flag = ChatAdminRight;
	using Flags = ChatAdminRights;

	auto result = tr::lng_admin_log_promoted(
		tr::now,
		lt_user,
		user,
		Ui::Text::WithEntities);

	const auto useInviteLinkPhrase = channel->isMegagroup()
		&& channel->anyoneCanAddMembers();
	const auto invitePhrase = useInviteLinkPhrase
		? tr::lng_admin_log_admin_invite_link
		: tr::lng_admin_log_admin_invite_users;
	const auto callPhrase = channel->isBroadcast()
		? tr::lng_admin_log_admin_manage_calls_channel
		: tr::lng_admin_log_admin_manage_calls;
	static auto phraseMap = std::map<Flags, tr::phrase<>> {
		{ Flag::ChangeInfo, tr::lng_admin_log_admin_change_info },
		{ Flag::PostMessages, tr::lng_admin_log_admin_post_messages },
		{ Flag::EditMessages, tr::lng_admin_log_admin_edit_messages },
		{ Flag::DeleteMessages, tr::lng_admin_log_admin_delete_messages },
		{ Flag::BanUsers, tr::lng_admin_log_admin_ban_users },
		{ Flag::InviteByLinkOrAdd, invitePhrase },
		{ Flag::ManageTopics, tr::lng_admin_log_admin_manage_topics },
		{ Flag::PinMessages, tr::lng_admin_log_admin_pin_messages },
		{ Flag::ManageCall, tr::lng_admin_log_admin_manage_calls },
		{ Flag::AddAdmins, tr::lng_admin_log_admin_add_admins },
		{ Flag::Anonymous, tr::lng_admin_log_admin_remain_anonymous },
	};
	phraseMap[Flag::InviteByLinkOrAdd] = invitePhrase;
	phraseMap[Flag::ManageCall] = callPhrase;

	if (!channel->isMegagroup()) {
		// Don't display "Ban users" changes in channels.
		newRights.flags &= ~Flag::BanUsers;
		prevRights.flags &= ~Flag::BanUsers;
	}

	const auto changes = CollectChanges(
		phraseMap,
		newRights.flags,
		prevRights.flags);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}

	return result;
};

QString GeneratePermissionsChangeText(
		ChatRestrictionsInfo newRights,
		ChatRestrictionsInfo prevRights) {
	using Flag = ChatRestriction;
	using Flags = ChatRestrictions;

	static auto phraseMap = std::map<Flags, tr::phrase<>>{
		{ Flag::ViewMessages, tr::lng_admin_log_banned_view_messages },
		{ Flag::SendOther, tr::lng_admin_log_banned_send_messages },
		{ Flag::SendPhotos, tr::lng_admin_log_banned_send_photos },
		{ Flag::SendVideos, tr::lng_admin_log_banned_send_videos },
		{ Flag::SendMusic, tr::lng_admin_log_banned_send_music },
		{ Flag::SendFiles, tr::lng_admin_log_banned_send_files },
		{
			Flag::SendVoiceMessages,
			tr::lng_admin_log_banned_send_voice_messages },
		{
			Flag::SendVideoMessages,
			tr::lng_admin_log_banned_send_video_messages },
		{ Flag::SendStickers
			| Flag::SendGifs
			| Flag::SendInline
			| Flag::SendGames, tr::lng_admin_log_banned_send_stickers },
		{ Flag::EmbedLinks, tr::lng_admin_log_banned_embed_links },
		{ Flag::SendPolls, tr::lng_admin_log_banned_send_polls },
		{ Flag::ChangeInfo, tr::lng_admin_log_admin_change_info },
		{ Flag::AddParticipants, tr::lng_admin_log_admin_invite_users },
		{ Flag::CreateTopics, tr::lng_admin_log_admin_create_topics },
		{ Flag::PinMessages, tr::lng_admin_log_admin_pin_messages },
	};
	return CollectChanges(phraseMap, prevRights.flags, newRights.flags);
}

TextWithEntities GeneratePermissionsChangeText(
		PeerId participantId,
		const TextWithEntities &user,
		ChatRestrictionsInfo newRights,
		ChatRestrictionsInfo prevRights) {
	using Flag = ChatRestriction;

	const auto newFlags = newRights.flags;
	const auto newUntil = newRights.until;
	const auto prevFlags = prevRights.flags;
	const auto indefinitely = ChannelData::IsRestrictedForever(newUntil);
	if (newFlags & Flag::ViewMessages) {
		return tr::lng_admin_log_banned(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	} else if (newFlags == 0
		&& (prevFlags & Flag::ViewMessages)
		&& !peerIsUser(participantId)) {
		return tr::lng_admin_log_unbanned(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	}
	const auto untilText = indefinitely
		? tr::lng_admin_log_restricted_forever(tr::now)
		: tr::lng_admin_log_restricted_until(
			tr::now,
			lt_date,
			langDateTime(base::unixtime::parse(newUntil)));
	auto result = tr::lng_admin_log_restricted(
		tr::now,
		lt_user,
		user,
		lt_until,
		TextWithEntities { untilText },
		Ui::Text::WithEntities);
	const auto changes = GeneratePermissionsChangeText(newRights, prevRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	return result;
}

QString PublicJoinLink() {
	return u"(public_join_link)"_q;
}

#if 0 // goodToRemove
QString ExtractInviteLink(const MTPExportedChatInvite &data) {
	return data.match([&](const MTPDchatInviteExported &data) {
		return qs(data.vlink());
	}, [&](const MTPDchatInvitePublicJoinRequests &data) {
		return PublicJoinLink();
	});
}

QString ExtractInviteLinkLabel(const MTPExportedChatInvite &data) {
	return data.match([&](const MTPDchatInviteExported &data) {
		return qs(data.vtitle().value_or_empty());
	}, [&](const MTPDchatInvitePublicJoinRequests &data) {
		return PublicJoinLink();
	});
}

QString InternalInviteLinkUrl(const MTPExportedChatInvite &data) {
	const auto base64 = ExtractInviteLink(data).toUtf8().toBase64();
#endif
QString InternalInviteLinkUrl(const Tdb::TLDchatInviteLink &data) {
	const auto base64 = data.vinvite_link().v.toUtf8().toBase64();
	return "internal:show_invite_link/?link=" + QString::fromLatin1(base64);
}

#if 0 // goodToRemove
QString GenerateInviteLinkText(const MTPExportedChatInvite &data) {
	const auto label = ExtractInviteLinkLabel(data);
#endif
QString GenerateInviteLinkText(const Tdb::TLDchatInviteLink &data) {
	const auto label = data.vname().v;
#if 0 // goodToRemove
	return label.isEmpty() ? ExtractInviteLink(data).replace(
#endif
	return label.isEmpty() ? QString(data.vinvite_link().v).replace(
		u"https://"_q,
		QString()
	).replace(
		u"t.me/joinchat/"_q,
		QString()
	) : label;
}

#if 0 // goodToRemove
TextWithEntities GenerateInviteLinkLink(const MTPExportedChatInvite &data) {
#endif
TextWithEntities GenerateInviteLinkLink(const Tdb::TLDchatInviteLink &data) {
	const auto text = GenerateInviteLinkText(data);
	return text.endsWith(Ui::kQEllipsis)
		? TextWithEntities{ .text = text }
		: Ui::Text::Link(text, InternalInviteLinkUrl(data));
}

#if 0 // goodToRemove
TextWithEntities GenerateInviteLinkChangeText(
		const MTPExportedChatInvite &newLink,
		const MTPExportedChatInvite &prevLink) {
#endif
TextWithEntities GenerateInviteLinkChangeText(
		const Tdb::TLDchatInviteLink &newLink,
		const Tdb::TLDchatInviteLink &prevLink) {
	auto link = TextWithEntities{ GenerateInviteLinkText(newLink) };
	if (!link.text.endsWith(Ui::kQEllipsis)) {
		link.entities.push_back({
			EntityType::CustomUrl,
			0,
			int(link.text.size()),
			InternalInviteLinkUrl(newLink) });
	}
	auto result = tr::lng_admin_log_edited_invite_link(
		tr::now,
		lt_link,
		link,
		Ui::Text::WithEntities);
	result.text.append('\n');

#if 0 // goodToRemove
	const auto label = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return qs(data.vtitle().value_or_empty());
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return PublicJoinLink();
		});
	};
	const auto expireDate = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vexpire_date().value_or_empty();
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return TimeId();
		});
	};
	const auto usageLimit = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.vusage_limit().value_or_empty();
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return 0;
		});
	};
	const auto requestApproval = [](const MTPExportedChatInvite &link) {
		return link.match([](const MTPDchatInviteExported &data) {
			return data.is_request_needed();
		}, [&](const MTPDchatInvitePublicJoinRequests &data) {
			return true;
		});
	};
#endif
	const auto wrapDate = [](TimeId date) {
		return date
			? langDateTime(base::unixtime::parse(date))
			: tr::lng_group_invite_expire_never(tr::now);
	};
	const auto wrapUsage = [](int count) {
		return count
			? QString::number(count)
			: tr::lng_group_invite_usage_any(tr::now);
	};
#if 0 // goodToRemove
	const auto wasLabel = label(prevLink);
	const auto nowLabel = label(newLink);
	const auto wasExpireDate = expireDate(prevLink);
	const auto nowExpireDate = expireDate(newLink);
	const auto wasUsageLimit = usageLimit(prevLink);
	const auto nowUsageLimit = usageLimit(newLink);
	const auto wasRequestApproval = requestApproval(prevLink);
	const auto nowRequestApproval = requestApproval(newLink);
#endif
	const auto wasLabel = prevLink.vname().v;
	const auto nowLabel = newLink.vname().v;
	const auto wasExpireDate = prevLink.vexpiration_date().v;
	const auto nowExpireDate = newLink.vexpiration_date().v;
	const auto wasUsageLimit = prevLink.vmember_limit().v;
	const auto nowUsageLimit = newLink.vmember_limit().v;
	const auto wasRequestApproval = prevLink.vcreates_join_request().v;
	const auto nowRequestApproval = newLink.vcreates_join_request().v;
	if (wasLabel != nowLabel) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_label(
				tr::now,
				lt_previous,
				wasLabel,
				lt_limit,
				nowLabel));
	}
	if (wasExpireDate != nowExpireDate) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_expire_date(
				tr::now,
				lt_previous,
				wrapDate(wasExpireDate),
				lt_limit,
				wrapDate(nowExpireDate)));
	}
	if (wasUsageLimit != nowUsageLimit) {
		result.text.append('\n').append(
			tr::lng_admin_log_invite_link_usage_limit(
				tr::now,
				lt_previous,
				wrapUsage(wasUsageLimit),
				lt_limit,
				wrapUsage(nowUsageLimit)));
	}
	if (wasRequestApproval != nowRequestApproval) {
		result.text.append('\n').append(
			nowRequestApproval
				? tr::lng_admin_log_invite_link_request_needed(tr::now)
				: tr::lng_admin_log_invite_link_request_not_needed(tr::now));
	}

	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
};

auto GenerateParticipantString(
		not_null<Main::Session*> session,
		PeerId participantId) {
	// User name in "User name (@username)" format with entities.
	const auto peer = session->data().peer(participantId);
	auto name = TextWithEntities { peer->name()};
	if (const auto user = peer->asUser()) {
		const auto data = TextUtilities::MentionNameDataFromFields({
			.selfId = session->userId().bare,
			.userId = peerToUser(user->id).bare,
			.accessHash = user->accessHash(),
		});
		name.entities.push_back({
			EntityType::MentionName,
			0,
			int(name.text.size()),
			data,
		});
	}
	const auto username = peer->userName();
	if (username.isEmpty()) {
		return name;
	}
	auto mention = TextWithEntities { '@' + username };
	mention.entities.push_back({
		EntityType::Mention,
		0,
		int(mention.text.size()) });
	return tr::lng_admin_log_user_with_username(
		tr::now,
		lt_name,
		name,
		lt_mention,
		mention,
		Ui::Text::WithEntities);
}

auto GenerateParticipantChangeText(
		not_null<ChannelData*> channel,
		const Api::ChatParticipant &participant,
		std::optional<Api::ChatParticipant> oldParticipant = std::nullopt) {
	using Type = Api::ChatParticipant::Type;
	const auto oldRights = oldParticipant
		? oldParticipant->rights()
		: ChatAdminRightsInfo();
	const auto oldRestrictions = oldParticipant
		? oldParticipant->restrictions()
		: ChatRestrictionsInfo();

	const auto generateOther = [&](PeerId participantId) {
		auto user = GenerateParticipantString(
			&channel->session(),
			participantId);
		if (oldParticipant && oldParticipant->type() == Type::Admin) {
			return GenerateAdminChangeText(
				channel,
				user,
				ChatAdminRightsInfo(),
				oldRights);
		} else if (oldParticipant && oldParticipant->type() == Type::Banned) {
			return GeneratePermissionsChangeText(
				participantId,
				user,
				ChatRestrictionsInfo(),
				oldRestrictions);
		} else if (oldParticipant
				&& oldParticipant->type() == Type::Restricted
				&& (participant.type() == Type::Member
						|| participant.type() == Type::Left)) {
			return GeneratePermissionsChangeText(
				participantId,
				user,
				ChatRestrictionsInfo(),
				oldRestrictions);
		}
		return tr::lng_admin_log_invited(
			tr::now,
			lt_user,
			user,
			Ui::Text::WithEntities);
	};

	auto result = [&] {
		const auto &peerId = participant.id();
		switch (participant.type()) {
		case Api::ChatParticipant::Type::Creator: {
			// No valid string here :(
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			if (peerId == channel->session().userPeerId()) {
				return GenerateAdminChangeText(
					channel,
					user,
					participant.rights(),
					oldRights);
			}
			return tr::lng_admin_log_transferred(
				tr::now,
				lt_user,
				user,
				Ui::Text::WithEntities);
		}
		case Api::ChatParticipant::Type::Admin: {
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			return GenerateAdminChangeText(
				channel,
				user,
				participant.rights(),
				oldRights);
		}
		case Api::ChatParticipant::Type::Restricted:
		case Api::ChatParticipant::Type::Banned: {
			const auto user = GenerateParticipantString(
				&channel->session(),
				peerId);
			return GeneratePermissionsChangeText(
				peerId,
				user,
				participant.restrictions(),
				oldRestrictions);
		}
		case Api::ChatParticipant::Type::Left:
		case Api::ChatParticipant::Type::Member:
			return generateOther(peerId);
		};
		Unexpected("Participant type in GenerateParticipantChangeText.");
	}();

	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

TextWithEntities GenerateParticipantChangeText(
		not_null<ChannelData*> channel,
		PeerId peerId,
		const Tdb::TLchatMemberStatus &status,
		std::optional<Tdb::TLchatMemberStatus>oldStatus = std::nullopt) {
#if 0 // goodToRemove
		const MTPChannelParticipant &participant,
		std::optional<MTPChannelParticipant>oldParticipant = std::nullopt) {
#endif
	const auto tlSender = peerToSender(peerId);
	using namespace Tdb;
	return GenerateParticipantChangeText(
		channel,
		Api::ChatParticipant(
			tl_chatMember(tlSender, tl_int53(0), tl_int32(0), status),
			channel),
		oldStatus
			? std::make_optional(Api::ChatParticipant(
				tl_chatMember(tlSender, tl_int53(0), tl_int32(0), *oldStatus),
				channel))
			: std::nullopt);
}

TextWithEntities GenerateDefaultBannedRightsChangeText(
		not_null<ChannelData*> channel,
		ChatRestrictionsInfo rights,
		ChatRestrictionsInfo oldRights) {
	auto result = TextWithEntities{
		tr::lng_admin_log_changed_default_permissions(tr::now)
	};
	const auto changes = GeneratePermissionsChangeText(rights, oldRights);
	if (!changes.isEmpty()) {
		result.text.append('\n' + changes);
	}
	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

#if 0 // mtp
[[nodiscard]] bool IsTopicClosed(const MTPForumTopic &topic) {
	return topic.match([](const MTPDforumTopic &data) {
		return data.is_closed();
	}, [](const MTPDforumTopicDeleted &) {
		return false;
	});
}

[[nodiscard]] bool IsTopicHidden(const MTPForumTopic &topic) {
	return topic.match([](const MTPDforumTopic &data) {
		return data.is_hidden();
	}, [](const MTPDforumTopicDeleted &) {
		return false;
	});
}

[[nodiscard]] TextWithEntities GenerateTopicLink(
		not_null<ChannelData*> channel,
		const MTPForumTopic &topic) {
	return topic.match([&](const MTPDforumTopic &data) {
		return Ui::Text::Link(
			Data::ForumTopicIconWithTitle(
				data.vid().v,
				data.vicon_emoji_id().value_or_empty(),
				qs(data.vtitle())),
			u"internal:url:https://t.me/c/%1/%2"_q.arg(
				peerToChannel(channel->id).bare).arg(
					data.vid().v));
	}, [](const MTPDforumTopicDeleted &) {
		return TextWithEntities{ u"Deleted"_q };
	});
}
#endif

[[nodiscard]] TextWithEntities GenerateTopicLink(
		not_null<ChannelData*> channel,
		const Tdb::TLforumTopicInfo &topic) {
	const auto &data = topic.data();
	return Ui::Text::Link(
		Data::ForumTopicIconWithTitle(
			data.vmessage_thread_id().v,
			data.vicon().data().vcustom_emoji_id().v,
			data.vname().v),
		u"internal:url:https://t.me/c/%1/%2"_q.arg(
			peerToChannel(channel->id).bare).arg(
				data.vmessage_thread_id().v));
}

} // namespace

OwnedItem::OwnedItem(std::nullptr_t) {
}

OwnedItem::OwnedItem(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<HistoryItem*> data)
: _data(data)
, _view(_data->createView(delegate)) {
}

OwnedItem::OwnedItem(OwnedItem &&other)
: _data(base::take(other._data))
, _view(base::take(other._view)) {
}

OwnedItem &OwnedItem::operator=(OwnedItem &&other) {
	_data = base::take(other._data);
	_view = base::take(other._view);
	return *this;
}

OwnedItem::~OwnedItem() {
	clearView();
	if (_data) {
		_data->destroy();
	}
}

void OwnedItem::refreshView(
		not_null<HistoryView::ElementDelegate*> delegate) {
	_view = _data->createView(delegate);
}

void OwnedItem::clearView() {
	_view = nullptr;
}

void GenerateItems(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
#if 0 // goodToRemove
		const MTPDchannelAdminLogEvent &event,
#endif
		const Tdb::TLDchatEvent &event,
		Fn<void(OwnedItem item, TimeId sentDate, MsgId)> callback) {
	Expects(history->peer->isChannel());
	using namespace Tdb;

#if 0 // goodToRemove
	using LogTitle = MTPDchannelAdminLogEventActionChangeTitle;
	using LogAbout = MTPDchannelAdminLogEventActionChangeAbout;
	using LogUsername = MTPDchannelAdminLogEventActionChangeUsername;
	using LogPhoto = MTPDchannelAdminLogEventActionChangePhoto;
	using LogInvites = MTPDchannelAdminLogEventActionToggleInvites;
	using LogSign = MTPDchannelAdminLogEventActionToggleSignatures;
	using LogPin = MTPDchannelAdminLogEventActionUpdatePinned;
	using LogEdit = MTPDchannelAdminLogEventActionEditMessage;
	using LogDelete = MTPDchannelAdminLogEventActionDeleteMessage;
	using LogJoin = MTPDchannelAdminLogEventActionParticipantJoin;
	using LogLeave = MTPDchannelAdminLogEventActionParticipantLeave;
	using LogInvite = MTPDchannelAdminLogEventActionParticipantInvite;
	using LogBan = MTPDchannelAdminLogEventActionParticipantToggleBan;
	using LogPromote = MTPDchannelAdminLogEventActionParticipantToggleAdmin;
	using LogSticker = MTPDchannelAdminLogEventActionChangeStickerSet;
	using LogPreHistory =
		MTPDchannelAdminLogEventActionTogglePreHistoryHidden;
	using LogPermissions = MTPDchannelAdminLogEventActionDefaultBannedRights;
	using LogPoll = MTPDchannelAdminLogEventActionStopPoll;
	using LogDiscussion = MTPDchannelAdminLogEventActionChangeLinkedChat;
	using LogLocation = MTPDchannelAdminLogEventActionChangeLocation;
	using LogSlowMode = MTPDchannelAdminLogEventActionToggleSlowMode;
	using LogStartCall = MTPDchannelAdminLogEventActionStartGroupCall;
	using LogDiscardCall = MTPDchannelAdminLogEventActionDiscardGroupCall;
	using LogMute = MTPDchannelAdminLogEventActionParticipantMute;
	using LogUnmute = MTPDchannelAdminLogEventActionParticipantUnmute;
	using LogCallSetting =
		MTPDchannelAdminLogEventActionToggleGroupCallSetting;
	using LogJoinByInvite =
		MTPDchannelAdminLogEventActionParticipantJoinByInvite;
	using LogInviteDelete =
		MTPDchannelAdminLogEventActionExportedInviteDelete;
	using LogInviteRevoke =
		MTPDchannelAdminLogEventActionExportedInviteRevoke;
	using LogInviteEdit = MTPDchannelAdminLogEventActionExportedInviteEdit;
	using LogVolume = MTPDchannelAdminLogEventActionParticipantVolume;
	using LogTTL = MTPDchannelAdminLogEventActionChangeHistoryTTL;
	using LogJoinByRequest =
		MTPDchannelAdminLogEventActionParticipantJoinByRequest;
	using LogNoForwards = MTPDchannelAdminLogEventActionToggleNoForwards;
	using LogSendMessage = MTPDchannelAdminLogEventActionSendMessage;
	using LogChangeAvailableReactions = MTPDchannelAdminLogEventActionChangeAvailableReactions;
	using LogChangeUsernames = MTPDchannelAdminLogEventActionChangeUsernames;
	using LogToggleForum = MTPDchannelAdminLogEventActionToggleForum;
	using LogCreateTopic = MTPDchannelAdminLogEventActionCreateTopic;
	using LogEditTopic = MTPDchannelAdminLogEventActionEditTopic;
	using LogDeleteTopic = MTPDchannelAdminLogEventActionDeleteTopic;
	using LogPinTopic = MTPDchannelAdminLogEventActionPinTopic;
	using LogToggleAntiSpam = MTPDchannelAdminLogEventActionToggleAntiSpam;
	using LogChangeColor = MTPDchannelAdminLogEventActionChangeColor;
	using LogChangeBackgroundEmoji = MTPDchannelAdminLogEventActionChangeBackgroundEmoji;
#endif
	using LogTitle = TLDchatEventTitleChanged;
	using LogAbout = TLDchatEventDescriptionChanged;
	using LogUsername = TLDchatEventUsernameChanged;
	using LogPhoto = TLDchatEventPhotoChanged;
	using LogInvites = TLDchatEventInvitesToggled;
	using LogSign = TLDchatEventSignMessagesToggled;
	using LogPin = TLDchatEventMessagePinned;
	using LogUnpin = TLDchatEventMessageUnpinned;
	using LogEdit = TLDchatEventMessageEdited;
	using LogDelete = TLDchatEventMessageDeleted;
	using LogJoin = TLDchatEventMemberJoined;
	using LogLeave = TLDchatEventMemberLeft;
	using LogInvite = TLDchatEventMemberInvited;
	using LogBan = TLDchatEventMemberRestricted;
	using LogPromote = TLDchatEventMemberPromoted;
	using LogSticker = TLDchatEventStickerSetChanged;
	using LogPreHistory = TLDchatEventIsAllHistoryAvailableToggled;
	using LogPermissions = TLDchatEventPermissionsChanged;
	using LogPoll = TLDchatEventPollStopped;
	using LogDiscussion = TLDchatEventLinkedChatChanged;
	using LogLocation = TLDchatEventLocationChanged;
	using LogSlowMode = TLDchatEventSlowModeDelayChanged;
	using LogStartCall = TLDchatEventVideoChatCreated;
	using LogDiscardCall = TLDchatEventVideoChatEnded;
	using LogMute = TLDchatEventVideoChatParticipantIsMutedToggled;
	using LogCallSetting = TLDchatEventVideoChatMuteNewParticipantsToggled;
	using LogJoinByInvite = TLDchatEventMemberJoinedByInviteLink;
	using LogInviteDelete = TLDchatEventInviteLinkDeleted;
	using LogInviteRevoke = TLDchatEventInviteLinkRevoked;
	using LogInviteEdit = TLDchatEventInviteLinkEdited;
	using LogVolume = TLDchatEventVideoChatParticipantVolumeLevelChanged;
	using LogTTL = TLDchatEventMessageAutoDeleteTimeChanged;
	using LogJoinByRequest = TLDchatEventMemberJoinedByRequest;
	using LogNoForwards = TLDchatEventHasProtectedContentToggled;
	using LogChangeAvailableReactions = TLDchatEventAvailableReactionsChanged;
	using LogChangeUsernames = TLDchatEventActiveUsernamesChanged;
	using LogToggleForum = TLDchatEventIsForumToggled;
	using LogCreateTopic = TLDchatEventForumTopicCreated;
	using LogEditTopic = TLDchatEventForumTopicEdited;
	using LogDeleteTopic = TLDchatEventForumTopicDeleted;
	using LogPinTopic = TLDchatEventForumTopicPinned;
	using LogToggleAntiSpam = TLDchatEventHasAggressiveAntiSpamEnabledToggled;
	using LogToggleTopicClosed = TLDchatEventForumTopicToggleIsClosed;
	using LogToggleTopicHidden = TLDchatEventForumTopicToggleIsHidden;
	using LogChangeColor = TLDchatEventAccentColorChanged;
	using LogChangeBackgroundEmoji = TLDchatEventBackgroundCustomEmojiChanged;

	const auto session = &history->session();
	const auto id = event.vid().v;
	const auto from = history->owner().peer(
		peerFromSender(event.vmember_id()));
	const auto channel = history->peer->asChannel();
	const auto broadcast = channel->isBroadcast();
	const auto &action = event.vaction();
	const auto date = event.vdate().v;
	const auto addPart = [&](
			not_null<HistoryItem*> item,
			TimeId sentDate = 0,
			MsgId realId = MsgId()) {
		return callback(OwnedItem(delegate, item), sentDate, realId);
	};

	const auto fromName = from->name();
	const auto fromLink = from->createOpenLink();
	const auto fromLinkText = Ui::Text::Link(fromName, QString());

	const auto addSimpleServiceMessage = [&](
			const TextWithEntities &text,
			MsgId realId = MsgId(),
			PhotoData *photo = nullptr) {
		auto message = PreparedServiceText{ text };
		message.links.push_back(fromLink);
		addPart(
			history->makeMessage(
				history->nextNonHistoryEntryId(),
				MessageFlag::AdminLogEntry,
				date,
				std::move(message),
				peerToUser(from->id),
				photo),
			0,
			realId);
	};

	const auto createChangeTitle = [&](const LogTitle &action) {
		auto text = (channel->isMegagroup()
			? tr::lng_action_changed_title
			: tr::lng_admin_log_changed_title_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_title,
				{ .text = action.vnew_title().v },
#if 0 // goodToRemove
				{ .text = qs(action.vnew_value()) },
#endif
				Ui::Text::WithEntities);
		addSimpleServiceMessage(std::move(text));
	};

	const auto makeSimpleTextMessage = [&](TextWithEntities &&text) {
		const auto bodyFlags = MessageFlag::HasFromId
			| MessageFlag::AdminLogEntry;
		const auto bodyReplyTo = FullReplyTo();
		const auto bodyViaBotId = UserId();
		const auto bodyGroupedId = uint64();
		return history->makeMessage(
			history->nextNonHistoryEntryId(),
			bodyFlags,
			bodyReplyTo,
			bodyViaBotId,
			date,
			from->id,
			QString(),
			std::move(text),
#if 0 // mtp
			MTP_messageMediaEmpty(),
#endif
			HistoryMessageMarkupData(),
			bodyGroupedId);
	};

	const auto addSimpleTextMessage = [&](TextWithEntities &&text) {
		addPart(makeSimpleTextMessage(std::move(text)));
	};

	const auto createChangeAbout = [&](const LogAbout &action) {
#if 0 // goodToRemove
		const auto newValue = qs(action.vnew_value());
		const auto oldValue = qs(action.vprev_value());
#endif
		const auto newValue = action.vold_description().v;
		const auto oldValue = action.vnew_description().v;
		const auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_group
				: tr::lng_admin_log_changed_description_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_description_channel
				: tr::lng_admin_log_changed_description_channel)
			)(tr::now, lt_from, fromLinkText, Ui::Text::WithEntities);
		addSimpleServiceMessage(text);

		const auto body = makeSimpleTextMessage(
			PrepareText(newValue, QString()));
		if (!oldValue.isEmpty()) {
			const auto oldDescription = PrepareText(oldValue, QString());
			body->addLogEntryOriginal(
				id,
				tr::lng_admin_log_previous_description(tr::now),
				oldDescription);
		}
		addPart(body);
	};

	const auto createChangeUsername = [&](const LogUsername &action) {
#if 0 // goodToRemove
		const auto newValue = qs(action.vnew_value());
		const auto oldValue = qs(action.vprev_value());
#endif
		const auto newValue = action.vold_username().v;
		const auto oldValue = action.vnew_username().v;
		const auto text = (channel->isMegagroup()
			? (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_group
				: tr::lng_admin_log_changed_link_group)
			: (newValue.isEmpty()
				? tr::lng_admin_log_removed_link_channel
				: tr::lng_admin_log_changed_link_channel)
			)(tr::now, lt_from, fromLinkText, Ui::Text::WithEntities);
		addSimpleServiceMessage(text);

		const auto body = makeSimpleTextMessage(newValue.isEmpty()
			? TextWithEntities()
			: PrepareText(
				history->session().createInternalLinkFull(newValue),
				QString()));
		if (!oldValue.isEmpty()) {
			const auto oldLink = PrepareText(
				history->session().createInternalLinkFull(oldValue),
				QString());
			body->addLogEntryOriginal(
				id,
				tr::lng_admin_log_previous_link(tr::now),
				oldLink);
		}
		addPart(body);
	};

	const auto createChangePhoto = [&](const LogPhoto &action) {
#if 0 // mtp
		action.vnew_photo().match([&](const MTPDphoto &data) {
#endif
		if (const auto newPhoto = action.vnew_photo()) {
			const auto &data = *newPhoto;
			const auto photo = history->owner().processPhoto(data);
			const auto text = (channel->isMegagroup()
				? tr::lng_admin_log_changed_photo_group
				: tr::lng_admin_log_changed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text, MsgId(), photo);
		} else {
#if 0 // mtp
		}, [&](const MTPDphotoEmpty &data) {
#endif
			const auto text = (channel->isMegagroup()
				? tr::lng_admin_log_removed_photo_group
				: tr::lng_admin_log_removed_photo_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		}
#if 0 // mtp
		});
#endif
	};

	const auto createToggleInvites = [&](const LogInvites &action) {
#if 0 // goodToRemove
		const auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
#endif
		const auto enabled = action.vcan_invite_users().v;
		const auto text = (enabled
			? tr::lng_admin_log_invites_enabled
			: tr::lng_admin_log_invites_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createToggleSignatures = [&](const LogSign &action) {
#if 0 // goodToRemove
		const auto enabled = (action.vnew_value().type() == mtpc_boolTrue);
#endif
		const auto enabled = action.vsign_messages().v;
		const auto text = (enabled
			? tr::lng_admin_log_signatures_enabled
			: tr::lng_admin_log_signatures_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createUpdatePinned = [&](
			const TLmessage &message,
			bool pinned) {
		const auto text = (pinned
			? tr::lng_admin_log_pinned_message
			: tr::lng_admin_log_unpinned_message)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(message, date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			TimeId(message.data().vdate().v));
	};
#if 0 // goodToRemove
	const auto createUpdatePinned = [&](const LogPin &action) {
		action.vmessage().match([&](const MTPDmessage &data) {
			const auto pinned = data.is_pinned();
			const auto realId = ExtractRealMsgId(action.vmessage());
			const auto text = (pinned
				? tr::lng_admin_log_pinned_message
				: tr::lng_admin_log_unpinned_message)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text, realId);

			const auto detachExistingItem = false;
			addPart(
				history->createItem(
					history->nextNonHistoryEntryId(),
					PrepareLogMessage(action.vmessage(), date),
					MessageFlag::AdminLogEntry,
					detachExistingItem),
				ExtractSentDate(action.vmessage()),
				realId);
		}, [&](const auto &) {
			const auto text = tr::lng_admin_log_unpinned_message(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		});
	};
#endif

	const auto createEditMessage = [&](const LogEdit &action) {
		const auto realId = ExtractRealMsgId(action.vnew_message());
		const auto sentDate = ExtractSentDate(action.vnew_message());
		const auto newValue = ExtractEditedText(
			session,
			action.vnew_message());
#if 0 // mtp
		auto oldValue = ExtractEditedText(
			session,
			action.vprev_message());
#endif
		auto oldValue = ExtractEditedText(
			session,
			action.vold_message());

		const auto canHaveCaption = MediaCanHaveCaption(
			action.vnew_message());
		const auto changedCaption = (newValue != oldValue);
#if 0 // mtp
		const auto changedMedia = MediaId(action.vnew_message())
			!= MediaId(action.vprev_message());
#endif
		const auto changedMedia = MediaId(action.vnew_message())
			!= MediaId(action.vold_message());
		const auto removedCaption = !oldValue.text.isEmpty()
			&& newValue.text.isEmpty();
		const auto text = (!canHaveCaption
			? tr::lng_admin_log_edited_message
			: (changedMedia && removedCaption)
			? tr::lng_admin_log_edited_media_and_removed_caption
			: (changedMedia && changedCaption)
			? tr::lng_admin_log_edited_media_and_caption
			: changedMedia
			? tr::lng_admin_log_edited_media
			: removedCaption
			? tr::lng_admin_log_removed_caption
			: changedCaption
			? tr::lng_admin_log_edited_caption
			: tr::lng_admin_log_edited_message)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		const auto body = history->createItem(
			history->nextNonHistoryEntryId(),
			PrepareLogMessage(action.vnew_message(), date),
			MessageFlag::AdminLogEntry,
			detachExistingItem);
		if (oldValue.text.isEmpty()) {
			oldValue = PrepareText(
				QString(),
				tr::lng_admin_log_empty_text(tr::now));
		}

		body->addLogEntryOriginal(
			id,
			(canHaveCaption
				? tr::lng_admin_log_previous_caption
				: tr::lng_admin_log_previous_message)(tr::now),
			oldValue);
		addPart(body, sentDate, realId);
	};

	const auto createDeleteMessage = [&](const LogDelete &action) {
		const auto realId = ExtractRealMsgId(action.vmessage());
		const auto text = tr::lng_admin_log_deleted_message(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(action.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()),
			realId);
	};

	const auto createParticipantJoin = [&](const LogJoin&) {
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_joined
			: tr::lng_admin_log_participant_joined_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createParticipantLeave = [&](const LogLeave&) {
		const auto text = (channel->isMegagroup()
			? tr::lng_admin_log_participant_left
			: tr::lng_admin_log_participant_left_channel)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createParticipantInvite = [&](const LogInvite &action) {
		addSimpleTextMessage(
			GenerateParticipantChangeText(
				channel,
				peerFromUser(UserId(action.vuser_id().v)),
				action.vstatus()));
#if 0 // goodToRemove
			GenerateParticipantChangeText(channel, action.vparticipant()));
#endif
	};

	const auto createParticipantToggleBan = [&](const LogBan &action) {
		addSimpleTextMessage(
			GenerateParticipantChangeText(
				channel,
				peerFromSender(action.vmember_id()),
				action.vnew_status(),
				action.vold_status()));
#if 0 // goodToRemove
				action.vnew_participant(),
				action.vprev_participant()));
#endif
	};

	const auto createParticipantToggleAdmin = [&](const LogPromote &action) {
		if ((action.vold_status().type() == Tdb::id_chatMemberStatusCreator)
			&& (action.vnew_status().type()
				== Tdb::id_chatMemberStatusAdministrator)) {
#if 0 // goodToRemove
		if ((action.vnew_participant().type() == mtpc_channelParticipantAdmin)
			&& (action.vprev_participant().type()
				== mtpc_channelParticipantCreator)) {
#endif
			// In case of ownership transfer we show that message in
			// the "User > Creator" part and skip the "Creator > Admin" part.
			return;
		}
		addSimpleTextMessage(
			GenerateParticipantChangeText(
				channel,
				peerFromUser(UserId(action.vuser_id().v)),
				action.vnew_status(),
				action.vold_status()));
#if 0 // goodToRemove
				action.vnew_participant(),
				action.vprev_participant()));
#endif
	};

	const auto createChangeStickerSet = [&](const LogSticker &action) {
#if 0 // goodToRemove
		const auto set = action.vnew_stickerset();
#endif
		const auto newSetId = action.vnew_sticker_set_id().v;
#if 0 // goodToRemove
		const auto removed = (set.type() == mtpc_inputStickerSetEmpty);
#endif
		const auto removed = (newSetId == 0);
		if (removed) {
			const auto text = tr::lng_admin_log_removed_stickers_group(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_changed_stickers_group(
				tr::now,
				lt_from,
				fromLinkText,
				lt_sticker_set,
				Ui::Text::Link(
					tr::lng_admin_log_changed_stickers_set(tr::now),
					QString()),
				Ui::Text::WithEntities);
			const auto setLink = std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				const auto my = context.other.value<ClickHandlerContext>();
				if (const auto controller = my.sessionWindow.get()) {
					controller->show(
						Box<StickerSetBox>(
							controller->uiShow(),
							StickerSetIdentifier{ .id = uint64(newSetId) },
#if 0 // goodToRemove
							Data::FromInputSet(set),
#endif
							Data::StickersType::Stickers),
						Ui::LayerOption::CloseOther);
				}
			});
			auto message = PreparedServiceText{ text };
			message.links.push_back(fromLink);
			message.links.push_back(setLink);
			addPart(history->makeMessage(
				history->nextNonHistoryEntryId(),
				MessageFlag::AdminLogEntry,
				date,
				std::move(message),
				from->id));
		}
	};

	const auto createTogglePreHistoryHidden = [&](
			const LogPreHistory &action) {
#if 0 // goodToRemove
		const auto hidden = (action.vnew_value().type() == mtpc_boolTrue);
#endif
		const auto hidden = !action.vis_all_history_available().v;
		const auto text = (hidden
			? tr::lng_admin_log_history_made_hidden
			: tr::lng_admin_log_history_made_visible)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createDefaultBannedRights = [&](
			const LogPermissions &action) {
		addSimpleTextMessage(
			GenerateDefaultBannedRightsChangeText(
				channel,
				ChatRestrictionsInfo(Tdb::tl_chatMemberStatusRestricted(
					Tdb::tl_bool(false),
					Tdb::tl_int32(0),
					action.vold_permissions())),
				ChatRestrictionsInfo(Tdb::tl_chatMemberStatusRestricted(
					Tdb::tl_bool(false),
					Tdb::tl_int32(0),
					action.vnew_permissions()))));
#if 0 // goodToRemove
				ChatRestrictionsInfo(action.vnew_banned_rights()),
				ChatRestrictionsInfo(action.vprev_banned_rights())));
#endif
	};

	const auto createStopPoll = [&](const LogPoll &action) {
		const auto realId = ExtractRealMsgId(action.vmessage());
		const auto text = tr::lng_admin_log_stopped_poll(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(action.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(action.vmessage()),
			realId);
	};

	const auto createChangeLinkedChat = [&](const LogDiscussion &action) {
		const auto now = history->owner().channelLoaded(
			peerToChannel(peerFromTdbChat(action.vnew_linked_chat_id())));
#if 0 // goodToRemove
			action.vnew_value().v);
#endif
		if (!now) {
			const auto text = (broadcast
				? tr::lng_admin_log_removed_linked_chat
				: tr::lng_admin_log_removed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		} else {
			const auto text = (broadcast
				? tr::lng_admin_log_changed_linked_chat
				: tr::lng_admin_log_changed_linked_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_chat,
					Ui::Text::Link(now->name(), QString()),
					Ui::Text::WithEntities);
			const auto chatLink = std::make_shared<LambdaClickHandler>([=] {
				if (const auto window = now->session().tryResolveWindow()) {
					window->showPeerHistory(now);
				}
			});
			auto message = PreparedServiceText{ text };
			message.links.push_back(fromLink);
			message.links.push_back(chatLink);
			addPart(history->makeMessage(
				history->nextNonHistoryEntryId(),
				MessageFlag::AdminLogEntry,
				date,
				std::move(message),
				from->id));
		}
	};

	const auto createChangeLocation = [&](const LogLocation &action) {
		if (!action.vnew_location()) {
			const auto text = tr::lng_admin_log_removed_location_chat(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
			return;
		}
		const auto &data = action.vnew_location()->data();
		const auto address = data.vaddress().v;
		const auto link = Ui::Text::Link(
			address,
			LocationClickHandler::Url(
				Data::LocationPoint(data.vlocation())));
		const auto text = tr::lng_admin_log_changed_location_chat(
			tr::now,
			lt_from,
			fromLinkText,
			lt_address,
			link,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
#if 0 // goodToRemove
		const auto createChangeLocation = [&](const LogLocation &action) {
			const auto address = qs(data.vaddress());
			const auto link = data.vgeo_point().match([&](
					const MTPDgeoPoint &data) {
				return Ui::Text::Link(
					address,
					LocationClickHandler::Url(Data::LocationPoint(data)));
			}, [&](const MTPDgeoPointEmpty &) {
				return TextWithEntities{ .text = address };
			});
			const auto text = tr::lng_admin_log_changed_location_chat(
				tr::now,
				lt_from,
				fromLinkText,
				lt_address,
				link,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		}, [&](const MTPDchannelLocationEmpty &) {
			const auto text = tr::lng_admin_log_removed_location_chat(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		});
#endif
	};

	const auto createToggleSlowMode = [&](const LogSlowMode &action) {
		if (const auto seconds = action.vnew_slow_mode_delay().v) {
#if 0 // goodToRemove
		if (const auto seconds = action.vnew_value().v) {
#endif
			const auto duration = (seconds >= 60)
				? tr::lng_minutes(tr::now, lt_count, seconds / 60)
				: tr::lng_seconds(tr::now, lt_count, seconds);
			const auto text = tr::lng_admin_log_changed_slow_mode(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				{ .text = duration },
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		} else {
			const auto text = tr::lng_admin_log_removed_slow_mode(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
			addSimpleServiceMessage(text);
		}
	};

	const auto createStartGroupCall = [&](const LogStartCall &data) {
		const auto text = (broadcast
			? tr::lng_admin_log_started_group_call_channel
			: tr::lng_admin_log_started_group_call)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createDiscardGroupCall = [&](const LogDiscardCall &data) {
		const auto text = (broadcast
			? tr::lng_admin_log_discarded_group_call_channel
			: tr::lng_admin_log_discarded_group_call)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

#if 0 // goodToRemove
	const auto groupCallParticipantPeer = [&](
			const MTPGroupCallParticipant &data) {
		return data.match([&](const MTPDgroupCallParticipant &data) {
			return history->owner().peer(peerFromMTP(data.vpeer()));
		});
	};
#endif

	const auto addServiceMessageWithLink = [&](
			const TextWithEntities &text,
			const ClickHandlerPtr &link) {
		auto message = PreparedServiceText{ text };
		message.links.push_back(fromLink);
		message.links.push_back(link);
		addPart(history->makeMessage(
			history->nextNonHistoryEntryId(),
			MessageFlag::AdminLogEntry,
			date,
			std::move(message),
			from->id));
	};

	const auto createParticipantMute = [&](const LogMute &data) {
		const auto participantPeer = history->owner().peer(
			peerFromSender(data.vparticipant_id()));
#if 0 // goodToRemove
	const auto createParticipantMute = [&](const LMemberMute &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
#endif
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = Ui::Text::Link(
			participantPeer->name(),
			QString());
		const auto text = (broadcast
			? tr::lng_admin_log_muted_participant_channel
			: tr::lng_admin_log_muted_participant)(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText,
			Ui::Text::WithEntities);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createParticipantUnmute = [&](const LogMute &data) {
		const auto participantPeer = history->owner().peer(
			peerFromSender(data.vparticipant_id()));
#if 0 // goodToRemove
	const auto createParticipantUnmute = [&](const LogUnmute &data) {
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
#endif
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = Ui::Text::Link(
			participantPeer->name(),
			QString());
		const auto text = (broadcast
			? tr::lng_admin_log_unmuted_participant_channel
			: tr::lng_admin_log_unmuted_participant)(
			tr::now,
			lt_from,
			fromLinkText,
			lt_user,
			participantPeerLinkText,
			Ui::Text::WithEntities);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createToggleGroupCallSetting = [&](
			const LogCallSetting &data) {
#if 0 // goodToRemove
		const auto text = (mtpIsTrue(data.vjoin_muted())
#endif
		const auto text = (data.vmute_new_participants().v
			? (broadcast
				? tr::lng_admin_log_disallowed_unmute_self_channel
				: tr::lng_admin_log_disallowed_unmute_self)
			: (broadcast
				? tr::lng_admin_log_allowed_unmute_self_channel
				: tr::lng_admin_log_allowed_unmute_self))(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto addInviteLinkServiceMessage = [&](
			const TextWithEntities &text,
			const Tdb::TLDchatInviteLink &data,
#if 0 // goodToRemove
			const MTPExportedChatInvite &data,
#endif
			ClickHandlerPtr additional = nullptr) {
		auto message = PreparedServiceText{ text };
		message.links.push_back(fromLink);
#if 0 // goodToRemove
		if (!ExtractInviteLink(data).endsWith(Ui::kQEllipsis)) {
#endif
		if (!data.vinvite_link().v.endsWith(Ui::kQEllipsis)) {
			message.links.push_back(std::make_shared<UrlClickHandler>(
				InternalInviteLinkUrl(data)));
		}
		if (additional) {
			message.links.push_back(std::move(additional));
		}
		addPart(history->makeMessage(
			history->nextNonHistoryEntryId(),
			MessageFlag::AdminLogEntry,
			date,
			std::move(message),
			from->id,
			nullptr));
	};

	const auto createParticipantJoinByInvite = [&](
			const LogJoinByInvite &data) {
#if 0 // mtp
		const auto text = data.is_via_chatlist()
#endif
		const auto text = data.vvia_chat_folder_invite_link().v
			? (channel->isMegagroup()
				? tr::lng_admin_log_participant_joined_by_filter_link
				: tr::lng_admin_log_participant_joined_by_filter_link_channel)
			: (channel->isMegagroup()
				? tr::lng_admin_log_participant_joined_by_link
				: tr::lng_admin_log_participant_joined_by_link_channel);
		addInviteLinkServiceMessage(
			text(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite_link().data()),
				Ui::Text::WithEntities),
			data.vinvite_link().data());
#if 0 // goodToRemove
				GenerateInviteLinkLink(data.vinvite()),
				Ui::Text::WithEntities),
			data.vinvite());
#endif
	};

	const auto createExportedInviteDelete = [&](const LogInviteDelete &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_delete_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite_link().data()),
				Ui::Text::WithEntities),
			data.vinvite_link().data());
#if 0 // goodToRemove
				GenerateInviteLinkLink(data.vinvite()),
				Ui::Text::WithEntities),
			data.vinvite());
#endif
	};

	const auto createExportedInviteRevoke = [&](const LogInviteRevoke &data) {
		addInviteLinkServiceMessage(
			tr::lng_admin_log_revoke_invite_link(
				tr::now,
				lt_from,
				fromLinkText,
				lt_link,
				GenerateInviteLinkLink(data.vinvite_link().data()),
				Ui::Text::WithEntities),
			data.vinvite_link().data());
#if 0 // goodToRemove
				GenerateInviteLinkLink(data.vinvite()),
				Ui::Text::WithEntities),
			data.vinvite());
#endif
	};

	const auto createExportedInviteEdit = [&](const LogInviteEdit &data) {
		addSimpleTextMessage(
			GenerateInviteLinkChangeText(
				data.vnew_invite_link().data(),
				data.vold_invite_link().data()));
#if 0 // goodToRemove
				data.vnew_invite(),
				data.vprev_invite()));
#endif
	};

	const auto createParticipantVolume = [&](const LogVolume &data) {
		const auto participantPeer = history->owner().peer(
			peerFromSender(data.vparticipant_id()));
#if 0 // goodToRemove
		const auto participantPeer = groupCallParticipantPeer(
			data.vparticipant());
#endif
		const auto participantPeerLink = participantPeer->createOpenLink();
		const auto participantPeerLinkText = Ui::Text::Link(
			participantPeer->name(),
			QString());
#if 0 // goodToRemove
		const auto volume = data.vparticipant().match([&](
				const MTPDgroupCallParticipant &data) {
			return data.vvolume().value_or(10000);
		});
#endif
		const auto volume = data.vvolume_level().v;
		const auto volumeText = QString::number(volume / 100) + '%';
		auto text = (broadcast
			? tr::lng_admin_log_participant_volume_channel
			: tr::lng_admin_log_participant_volume)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_user,
				participantPeerLinkText,
				lt_percent,
				{ .text = volumeText },
				Ui::Text::WithEntities);
		addServiceMessageWithLink(text, participantPeerLink);
	};

	const auto createChangeHistoryTTL = [&](const LogTTL &data) {
#if 0 // goodToRemove
		const auto was = data.vprev_value().v;
		const auto now = data.vnew_value().v;
#endif
		const auto was = data.vold_message_auto_delete_time().v;
		const auto now = data.vnew_message_auto_delete_time().v;
		const auto wrap = [](int duration) -> TextWithEntities {
			const auto text = (duration == 5)
				? u"5 seconds"_q
				: Ui::FormatTTL(duration);
			return { .text = text };
		};
		const auto text = !was
			? tr::lng_admin_log_messages_ttl_set(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				wrap(now),
				Ui::Text::WithEntities)
			: !now
			? tr::lng_admin_log_messages_ttl_removed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_duration,
				wrap(was),
				Ui::Text::WithEntities)
			: tr::lng_admin_log_messages_ttl_changed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_previous,
				wrap(was),
				lt_duration,
				wrap(now),
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createParticipantJoinByRequest = [&](
			const LogJoinByRequest &data) {
#if 0 // goodToRemove
		const auto user = channel->owner().user(UserId(data.vapproved_by()));
		const auto linkText = GenerateInviteLinkLink(data.vinvite());
#endif
		const auto user = channel->owner().user(
			UserId(data.vapprover_user_id().v));
		const auto linkText = GenerateInviteLinkLink(
			data.vinvite_link()->data());
		const auto text = (linkText.text == PublicJoinLink())
			? (channel->isMegagroup()
				? tr::lng_admin_log_participant_approved_by_request
				: tr::lng_admin_log_participant_approved_by_request_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_user,
					Ui::Text::Link(user->name(), QString()),
					Ui::Text::WithEntities)
			: (channel->isMegagroup()
				? tr::lng_admin_log_participant_approved_by_link
				: tr::lng_admin_log_participant_approved_by_link_channel)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_link,
					linkText,
					lt_user,
					Ui::Text::Link(user->name(), QString()),
					Ui::Text::WithEntities);
		addInviteLinkServiceMessage(
			text,
			data.vinvite_link()->data(),
#if 0 // goodToRemove
			data.vinvite(),
#endif
			user->createOpenLink());
	};

	const auto createToggleNoForwards = [&](const LogNoForwards &data) {
#if 0 // goodToRemove
		const auto disabled = (data.vnew_value().type() == mtpc_boolTrue);
#endif
		const auto disabled = data.vhas_protected_content().v;
		const auto text = (disabled
			? tr::lng_admin_log_forwards_disabled
			: tr::lng_admin_log_forwards_enabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

#if 0 // later
	const auto createSendMessage = [&](const LogSendMessage &data) {
		const auto realId = ExtractRealMsgId(data.vmessage());
		const auto text = tr::lng_admin_log_sent_message(
			tr::now,
			lt_from,
			fromLinkText,
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text, realId);

		const auto detachExistingItem = false;
		addPart(
			history->createItem(
				history->nextNonHistoryEntryId(),
				PrepareLogMessage(data.vmessage(), date),
				MessageFlag::AdminLogEntry,
				detachExistingItem),
			ExtractSentDate(data.vmessage()),
			realId);
	};
#endif

	const auto createChangeAvailableReactions = [&](
			const LogChangeAvailableReactions &data) {
#if 0 // mtp
		const auto text = data.vnew_value().match([&](
				const MTPDchatReactionsNone&) {
			return tr::lng_admin_log_reactions_disabled(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		}, [&](const MTPDchatReactionsSome &data) {
			using namespace Window::Notifications;
			auto list = TextWithEntities();
			for (const auto &one : data.vreactions().v) {
				if (!list.empty()) {
					list.append(", ");
				}
				list.append(Manager::ComposeReactionEmoji(
					session,
					Data::ReactionFromMTP(one)));
			}
			return tr::lng_admin_log_reactions_updated(
				tr::now,
				lt_from,
				fromLinkText,
				lt_emoji,
				list,
				Ui::Text::WithEntities);
		}, [&](const MTPDchatReactionsAll &data) {
			return (data.is_allow_custom()
				? tr::lng_admin_log_reactions_allowed_all
				: tr::lng_admin_log_reactions_allowed_official)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
		});
#endif
		const auto text = data.vnew_available_reactions().match([&](
				const TLDchatAvailableReactionsSome &data) {
			if (data.vreactions().v.isEmpty()) {
				return tr::lng_admin_log_reactions_disabled(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
			}
			using namespace Window::Notifications;
			auto list = TextWithEntities();
			for (const auto &one : data.vreactions().v) {
				if (!list.empty()) {
					list.append(", ");
				}
				list.append(Manager::ComposeReactionEmoji(
					session,
					Data::ReactionFromTL(one)));
			}
			return tr::lng_admin_log_reactions_updated(
				tr::now,
				lt_from,
				fromLinkText,
				lt_emoji,
				list,
				Ui::Text::WithEntities);
		}, [&](const TLDchatAvailableReactionsAll &data) {
			return (!history->peer->isBroadcast()
				? tr::lng_admin_log_reactions_allowed_all
				: tr::lng_admin_log_reactions_allowed_official)(
					tr::now,
					lt_from,
					fromLinkText,
					Ui::Text::WithEntities);
		});
		addSimpleServiceMessage(text);
	};

	const auto createChangeUsernames = [&](const LogChangeUsernames &data) {
#if 0 // mtp
		const auto newValue = data.vnew_value().v;
		const auto oldValue = data.vprev_value().v;
#endif
		const auto newValue = data.vnew_usernames().v;
		const auto oldValue = data.vold_usernames().v;
		const auto qs = [](const Tdb::TLstring &value) {
			return value.v;
		};

		const auto list = [&](const auto &tlList) {
			auto result = TextWithEntities();
			for (const auto &tlValue : tlList) {
				result.append(PrepareText(
					history->session().createInternalLinkFull(qs(tlValue)),
					QString()));
				result.append('\n');
			}
			return result;
		};

		if (newValue.size() == oldValue.size()) {
			if (newValue.size() == 1) {
				createChangeUsername(tl_chatEventUsernameChanged(
					oldValue.front(),
					newValue.front()
				).c_chatEventUsernameChanged());
#if 0 // mtp
				const auto tl = MTP_channelAdminLogEventActionChangeUsername(
					newValue.front(),
					oldValue.front());
				tl.match([&](const LogUsername &data) {
					createChangeUsername(data);
				}, [](const auto &) {
				});
#endif
				return;
			} else {
				const auto wasReordered = [&] {
					for (const auto &newLink : newValue) {
						if (!ranges::contains(oldValue, newLink)) {
							return false;
						}
					}
					return true;
				}();
				if (wasReordered) {
					addSimpleServiceMessage((channel->isMegagroup()
						? tr::lng_admin_log_reordered_link_group
						: tr::lng_admin_log_reordered_link_channel)(
							tr::now,
							lt_from,
							fromLinkText,
							Ui::Text::WithEntities));
					const auto body = makeSimpleTextMessage(list(newValue));
					body->addLogEntryOriginal(
						id,
						tr::lng_admin_log_previous_links_order(tr::now),
						list(oldValue));
					addPart(body);
					return;
				}
			}
		} else if (std::abs(newValue.size() - oldValue.size()) == 1) {
			const auto activated = newValue.size() > oldValue.size();
			const auto changed = [&] {
				const auto value = activated ? oldValue : newValue;
				for (const auto &link : (activated ? newValue : oldValue)) {
					if (!ranges::contains(value, link)) {
						return qs(link);
					}
				}
				return QString();
			}();
			addSimpleServiceMessage((activated
				? tr::lng_admin_log_activated_link
				: tr::lng_admin_log_deactivated_link)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_link,
					{ changed },
					Ui::Text::WithEntities));
			return;
		}
		// Probably will never happen.
		auto resultText = fromLinkText;
		addSimpleServiceMessage(resultText.append({
			.text = channel->isMegagroup()
				? u" changed list of group links:"_q
				: u" changed list of channel links:"_q,
		}));
		const auto body = makeSimpleTextMessage(list(newValue));
		body->addLogEntryOriginal(
			id,
			"Previous links",
			list(oldValue));
		addPart(body);
	};

	const auto createToggleForum = [&](const LogToggleForum &data) {
#if 0 // mtp
		const auto enabled = (data.vnew_value().type() == mtpc_boolTrue);
#endif
		const auto enabled = data.vis_forum().v;
		const auto text = (enabled
			? tr::lng_admin_log_topics_enabled
			: tr::lng_admin_log_topics_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createCreateTopic = [&](const LogCreateTopic &data) {
#if 0 // mtp
		auto topicLink = GenerateTopicLink(channel, data.vtopic());
#endif
		auto topicLink = GenerateTopicLink(channel, data.vtopic_info());
		addSimpleServiceMessage(tr::lng_admin_log_topics_created(
			tr::now,
			lt_from,
			fromLinkText,
			lt_topic,
			topicLink,
			Ui::Text::WithEntities));
	};

	const auto createEditTopic = [&](const LogEditTopic &data) {
#if 0 // mtp
		const auto prevLink = GenerateTopicLink(channel, data.vprev_topic());
		const auto nowLink = GenerateTopicLink(channel, data.vnew_topic());
#endif
		const auto prevLink = GenerateTopicLink(channel, data.vold_topic_info());
		const auto nowLink = GenerateTopicLink(channel, data.vnew_topic_info());
		if (prevLink != nowLink) {
			addSimpleServiceMessage(tr::lng_admin_log_topics_changed(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				prevLink,
				lt_new_topic,
				nowLink,
				Ui::Text::WithEntities));
		}
#if 0 // mtp
		const auto wasClosed = IsTopicClosed(data.vprev_topic());
		const auto nowClosed = IsTopicClosed(data.vnew_topic());
		if (nowClosed != wasClosed) {
			addSimpleServiceMessage((nowClosed
				? tr::lng_admin_log_topics_closed
				: tr::lng_admin_log_topics_reopened)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_topic,
					nowLink,
					Ui::Text::WithEntities));
		}
		const auto wasHidden = IsTopicHidden(data.vprev_topic());
		const auto nowHidden = IsTopicHidden(data.vnew_topic());
		if (nowHidden != wasHidden) {
			addSimpleServiceMessage((nowHidden
				? tr::lng_admin_log_topics_hidden
				: tr::lng_admin_log_topics_unhidden)(
					tr::now,
					lt_from,
					fromLinkText,
					lt_topic,
					nowLink,
					Ui::Text::WithEntities));
		}
#endif
	};

	const auto createToggleTopicClosed = [&](const LogToggleTopicClosed &data) {
		const auto nowLink = GenerateTopicLink(channel, data.vtopic_info());
		addSimpleServiceMessage((data.vtopic_info().data().vis_closed().v
			? tr::lng_admin_log_topics_closed
			: tr::lng_admin_log_topics_reopened)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				nowLink,
				Ui::Text::WithEntities));
	};

	const auto createToggleTopicHidden = [&](const LogToggleTopicHidden &data) {
		const auto nowLink = GenerateTopicLink(channel, data.vtopic_info());
		addSimpleServiceMessage((data.vtopic_info().data().vis_hidden().v
			? tr::lng_admin_log_topics_hidden
			: tr::lng_admin_log_topics_unhidden)(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				nowLink,
				Ui::Text::WithEntities));
	};

	const auto createDeleteTopic = [&](const LogDeleteTopic &data) {
#if 0 // mtp
		auto topicLink = GenerateTopicLink(channel, data.vtopic());
#endif
		auto topicLink = GenerateTopicLink(channel, data.vtopic_info());
		if (!topicLink.entities.empty()) {
			topicLink.entities.erase(topicLink.entities.begin());
		}
		addSimpleServiceMessage(tr::lng_admin_log_topics_deleted(
			tr::now,
			lt_from,
			fromLinkText,
			lt_topic,
			topicLink,
			Ui::Text::WithEntities));
	};

	const auto createPinTopic = [&](const LogPinTopic &data) {
		if (const auto &topic = data.vnew_topic_info()) {
#if 0 // mtp
		if (const auto &topic = data.vnew_topic()) {
#endif
			auto topicLink = GenerateTopicLink(channel, *topic);
			addSimpleServiceMessage(tr::lng_admin_log_topics_pinned(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				topicLink,
				Ui::Text::WithEntities));
		} else if (const auto &previous = data.vold_topic_info()) {
#if 0 // mtp
		} else if (const auto &previous = data.vprev_topic()) {
#endif
			auto topicLink = GenerateTopicLink(channel, *previous);
			addSimpleServiceMessage(tr::lng_admin_log_topics_unpinned(
				tr::now,
				lt_from,
				fromLinkText,
				lt_topic,
				topicLink,
				Ui::Text::WithEntities));
		}
	};

	const auto createToggleAntiSpam = [&](const LogToggleAntiSpam &data) {
#if 0 // mtp
		const auto enabled = (data.vnew_value().type() == mtpc_boolTrue);
#endif
		const auto enabled = data.vhas_aggressive_anti_spam_enabled().v;
		const auto text = (enabled
			? tr::lng_admin_log_antispam_enabled
			: tr::lng_admin_log_antispam_disabled)(
				tr::now,
				lt_from,
				fromLinkText,
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createChangeColor = [&](const LogChangeColor &data) {
		const auto text = tr::lng_admin_log_change_color(
			tr::now,
			lt_from,
			fromLinkText,
			lt_previous,
#if 0 // mtp
			{ '#' + QString::number(data.vprev_value().v + 1) },
			lt_color,
			{ '#' + QString::number(data.vnew_value().v + 1) },
#endif
			{ '#' + QString::number(data.vold_accent_color_id().v + 1) },
			lt_color,
			{ '#' + QString::number(data.vnew_accent_color_id().v + 1) },
			Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	const auto createChangeBackgroundEmoji = [&](const LogChangeBackgroundEmoji &data) {
#if 0 // mtp
		const auto was = data.vprev_value().v;
		const auto now = data.vnew_value().v;
#endif
		const auto was = data.vold_background_custom_emoji_id().v;
		const auto now = data.vnew_background_custom_emoji_id().v;
		const auto text = !was
			? tr::lng_admin_log_set_background_emoji(
				tr::now,
				lt_from,
				fromLinkText,
				lt_emoji,
				Ui::Text::SingleCustomEmoji(
					Data::SerializeCustomEmojiId(now)),
				Ui::Text::WithEntities)
			: !now
			? tr::lng_admin_log_removed_background_emoji(
				tr::now,
				lt_from,
				fromLinkText,
				lt_emoji,
				Ui::Text::SingleCustomEmoji(
					Data::SerializeCustomEmojiId(was)),
				Ui::Text::WithEntities)
			: tr::lng_admin_log_change_background_emoji(
				tr::now,
				lt_from,
				fromLinkText,
				lt_previous,
				Ui::Text::SingleCustomEmoji(
					Data::SerializeCustomEmojiId(was)),
				lt_emoji,
				Ui::Text::SingleCustomEmoji(
					Data::SerializeCustomEmojiId(now)),
				Ui::Text::WithEntities);
		addSimpleServiceMessage(text);
	};

	action.match(
		createChangeTitle,
		createChangeAbout,
		createChangeUsername,
		createChangePhoto,
		createToggleInvites,
		createToggleSignatures,
#if 0 // goodToRemove
		createUpdatePinned,
#endif
		[&](const LogPin &data) { createUpdatePinned(data.vmessage(), true); },
		[&](const LogUnpin &data) { createUpdatePinned(data.vmessage(), false); },
		createEditMessage,
		createDeleteMessage,
		createParticipantJoin,
		createParticipantLeave,
		createParticipantInvite,
		createParticipantToggleBan,
		createParticipantToggleAdmin,
		createChangeStickerSet,
		createTogglePreHistoryHidden,
		createDefaultBannedRights,
		createStopPoll,
		createChangeLinkedChat,
		createChangeLocation,
		createToggleSlowMode,
		createStartGroupCall,
		createDiscardGroupCall,
#if 0 // goodToRemove
		createParticipantMute,
		createParticipantUnmute,
#endif
		[&](const LogMute &data) {
			if (data.vis_muted().v) {
				createParticipantMute(data);
			} else {
				createParticipantUnmute(data);
			}
		},
		createToggleGroupCallSetting,
		createParticipantJoinByInvite,
		createExportedInviteDelete,
		createExportedInviteRevoke,
		createExportedInviteEdit,
		createParticipantVolume,
		createChangeHistoryTTL,
		createParticipantJoinByRequest,
		createToggleNoForwards,
#if 0 // later
		createSendMessage,
#endif
		createChangeAvailableReactions,
		createChangeUsernames,
		createToggleForum,
		createCreateTopic,
		createEditTopic,
		createToggleTopicClosed,
		createToggleTopicHidden,
		createDeleteTopic,
		createPinTopic,
		createToggleAntiSpam,
		createChangeColor,
		createChangeBackgroundEmoji);
}

} // namespace AdminLog
