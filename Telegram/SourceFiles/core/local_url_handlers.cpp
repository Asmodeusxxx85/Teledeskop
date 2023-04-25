/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/local_url_handlers.h"

#include "api/api_authorizations.h"
#include "api/api_confirm_phone.h"
#include "api/api_text_entities.h"
#include "api/api_chat_filters.h"
#include "api/api_chat_invite.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "boxes/background_preview_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "boxes/connection_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/sessions_box.h"
#include "boxes/language_box.h"
#include "passport/passport_form_controller.h"
#include "window/window_session_controller.h"
#include "ui/toast/toast.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_cloud_themes.h"
#include "data/data_channel.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_view_open_common.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/themes/window_theme_editor_box.h" // GenerateSlug.
#include "payments/payments_checkout_process.h"
#include "settings/settings_common.h"
#include "settings/settings_information.h"
#include "settings/settings_global_ttl.h"
#include "settings/settings_folders.h"
#include "settings/settings_main.h"
#include "settings/settings_privacy_security.h"
#include "settings/settings_chat.h"
#include "settings/settings_premium.h"
#include "mainwidget.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "inline_bots/bot_attach_web_view.h"
#include "history/history.h"
#include "history/history_item.h"
#include "base/qt/qt_common_adapters.h"
#include "apiwrap.h"

#include "tdb/tdb_tl_scheme.h"
#include "core/file_utilities.h"
#include "data/data_user.h"
#include "main/main_domain.h"

#include <QtGui/QGuiApplication>

namespace Core {
namespace {

using namespace Tdb;

using Match = qthelp::RegularExpressionMatch;

#if 0 // mtp
bool JoinGroupByHash(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	Api::CheckChatInvite(controller, match->captured(1));
	controller->window().activate();
	return true;
}

bool JoinFilterBySlug(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	Api::CheckFilterInvite(controller, match->captured(1));
	controller->window().activate();
	return true;
}

bool ShowStickerSet(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	Core::App().hideMediaView();
	controller->show(Box<StickerSetBox>(
		controller->uiShow(),
		StickerSetIdentifier{ .shortName = match->captured(2) },
		(match->captured(1) == "addemoji"
			? Data::StickersType::Emoji
			: Data::StickersType::Stickers)));
	controller->window().activate();
	return true;
}

bool ShowTheme(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto fromMessageId = context.value<ClickHandlerContext>().itemId;
	Core::App().hideMediaView();
	controller->session().data().cloudThemes().resolve(
		&controller->window(),
		match->captured(1),
		fromMessageId);
	controller->window().activate();
	return true;
}
#endif

void ShowLanguagesBox(Window::SessionController *controller) {
	static auto Guard = base::binary_guard();
	Guard = LanguageBox::Show(controller);
}

#if 0 // mtp
bool SetLanguage(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (match->capturedView(1).isEmpty()) {
		ShowLanguagesBox(controller);
	} else {
		const auto languageId = match->captured(2);
		Lang::CurrentCloudManager().switchWithWarning(languageId);
	}
	if (controller) {
		controller->window().activate();
	}
	return true;
}

bool ShareUrl(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto url = params.value(u"url"_q);
	if (url.isEmpty() || url.trimmed().startsWith('@')) {
		// Don't allow to insert an inline bot query by share url link.
		return false;
	}

	const auto text = params.value("text");
	const auto chosen = [=](not_null<Data::Thread*> thread) {
		const auto content = controller->content();
		return content->shareUrl(thread, url, text);
	};
	Window::ShowChooseRecipientBox(controller, chosen);
	controller->window().activate();
	return true;
}

bool ConfirmPhone(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto phone = params.value(u"phone"_q);
	const auto hash = params.value(u"hash"_q);
	if (phone.isEmpty() || hash.isEmpty()) {
		return false;
	}
	controller->session().api().confirmPhone().resolve(
		controller,
		phone,
		hash);
	controller->window().activate();
	return true;
}

bool ShareGameScore(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ShareGameScoreByHash(controller, params.value(u"hash"_q));
	controller->window().activate();
	return true;
}

bool ApplySocksProxy(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ProxiesBoxController::ShowApplyConfirmation(
		MTP::ProxyData::Type::Socks5,
		params);
	if (controller) {
		controller->window().activate();
	}
	return true;
}

bool ApplyMtprotoProxy(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	ProxiesBoxController::ShowApplyConfirmation(
		MTP::ProxyData::Type::Mtproto,
		params);
	if (controller) {
		controller->window().activate();
	}
	return true;
}

bool ShowPassportForm(
		Window::SessionController *controller,
		const QMap<QString, QString> &params) {
	if (!controller) {
		return false;
	}
	const auto botId = params.value("bot_id", QString()).toULongLong();
	const auto scope = params.value("scope", QString());
	const auto callback = params.value("callback_url", QString());
	const auto publicKey = params.value("public_key", QString());
	const auto nonce = params.value(
		Passport::NonceNameByScope(scope),
		QString());
	controller->showPassportForm(Passport::FormRequest(
		botId,
		scope,
		callback,
		publicKey,
		nonce));
	return true;
}

bool ShowPassport(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	return ShowPassportForm(
		controller,
		url_parse_params(
			match->captured(1),
			qthelp::UrlParamNameTransform::ToLower));
}

bool ShowWallPaper(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto bg = params.value(u"bg_color"_q);
	const auto color = params.value(u"color"_q);
	const auto gradient = params.value(u"gradient"_q);
	const auto result = BackgroundPreviewBox::Start(
		controller,
		(!color.isEmpty()
			? color
			: !gradient.isEmpty()
			? gradient
			: params.value(u"slug"_q)),
		params);
	controller->window().activate();
	return result;
}

[[nodiscard]] ChatAdminRights ParseRequestedAdminRights(
		const QString &value) {
	auto result = ChatAdminRights();
	for (const auto &element : value.split(QRegularExpression(u"[+ ]"_q))) {
		if (element == u"change_info"_q) {
			result |= ChatAdminRight::ChangeInfo;
		} else if (element == u"post_messages"_q) {
			result |= ChatAdminRight::PostMessages;
		} else if (element == u"edit_messages"_q) {
			result |= ChatAdminRight::EditMessages;
		} else if (element == u"delete_messages"_q) {
			result |= ChatAdminRight::DeleteMessages;
		} else if (element == u"restrict_members"_q) {
			result |= ChatAdminRight::BanUsers;
		} else if (element == u"invite_users"_q) {
			result |= ChatAdminRight::InviteByLinkOrAdd;
		} else if (element == u"manage_topics"_q) {
			result |= ChatAdminRight::ManageTopics;
		} else if (element == u"pin_messages"_q) {
			result |= ChatAdminRight::PinMessages;
		} else if (element == u"promote_members"_q) {
			result |= ChatAdminRight::AddAdmins;
		} else if (element == u"manage_video_chats"_q) {
			result |= ChatAdminRight::ManageCall;
		} else if (element == u"anonymous"_q) {
			result |= ChatAdminRight::Anonymous;
		} else if (element == u"manage_chat"_q) {
			result |= ChatAdminRight::Other;
		} else {
			return {};
		}
	}
	return result;
}

bool ResolveUsernameOrPhone(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto domainParam = params.value(u"domain"_q);
	const auto appnameParam = params.value(u"appname"_q);

	// Fix t.me/s/username links.
	const auto webChannelPreviewLink = (domainParam == u"s"_q)
		&& !appnameParam.isEmpty();
	const auto domain = webChannelPreviewLink ? appnameParam : domainParam;
	const auto phone = params.value(u"phone"_q);
	const auto validDomain = [](const QString &domain) {
		return qthelp::regex_match(
			u"^[a-zA-Z0-9\\.\\_]+$"_q,
			domain,
			{}
		).valid();
	};
	const auto validPhone = [](const QString &phone) {
		return qthelp::regex_match(u"^[0-9]+$"_q, phone, {}).valid();
	};
	if (domain == u"telegrampassport"_q) {
		return ShowPassportForm(controller, params);
	} else if (!validDomain(domain) && !validPhone(phone)) {
		return false;
	}
	using ResolveType = Window::ResolveType;
	auto resolveType = ResolveType::Default;
	auto startToken = params.value(u"start"_q);
	if (!startToken.isEmpty()) {
		resolveType = ResolveType::BotStart;
	} else if (params.contains(u"startgroup"_q)) {
		resolveType = ResolveType::AddToGroup;
		startToken = params.value(u"startgroup"_q);
	} else if (params.contains(u"startchannel"_q)) {
		resolveType = ResolveType::AddToChannel;
	} else if (params.contains(u"boost"_q)) {
		resolveType = ResolveType::Boost;
	}
	auto post = ShowAtUnreadMsgId;
	auto adminRights = ChatAdminRights();
	if (resolveType == ResolveType::AddToGroup
		|| resolveType == ResolveType::AddToChannel) {
		adminRights = ParseRequestedAdminRights(params.value(u"admin"_q));
	}
	const auto postParam = params.value(u"post"_q);
	if (const auto postId = postParam.toInt()) {
		post = postId;
	}
	const auto storyParam = params.value(u"story"_q);
	const auto storyId = storyParam.toInt();
	const auto appname = webChannelPreviewLink ? QString() : appnameParam;
	const auto commentParam = params.value(u"comment"_q);
	const auto commentId = commentParam.toInt();
	const auto topicParam = params.value(u"topic"_q);
	const auto topicId = topicParam.toInt();
	const auto threadParam = params.value(u"thread"_q);
	const auto threadId = topicId ? topicId : threadParam.toInt();
	const auto gameParam = params.value(u"game"_q);
	if (!gameParam.isEmpty() && validDomain(gameParam)) {
		startToken = gameParam;
		resolveType = ResolveType::ShareGame;
	}
	if (!appname.isEmpty()) {
		resolveType = ResolveType::BotApp;
		if (startToken.isEmpty() && params.contains(u"startapp"_q)) {
			startToken = params.value(u"startapp"_q);
		}
	}
	const auto myContext = context.value<ClickHandlerContext>();
	using Navigation = Window::SessionNavigation;
	controller->window().activate();
	controller->showPeerByLink(Navigation::PeerByLinkInfo{
		.usernameOrId = domain,
		.phone = phone,
		.messageId = post,
		.storyId = storyId,
		.repliesInfo = commentId
			? Navigation::RepliesByLinkInfo{
				Navigation::CommentId{ commentId }
			}
			: threadId
			? Navigation::RepliesByLinkInfo{
				Navigation::ThreadId{ threadId }
			}
			: Navigation::RepliesByLinkInfo{ v::null },
		.resolveType = resolveType,
		.startToken = startToken,
		.startAdminRights = adminRights,
		.startAutoSubmit = myContext.botStartAutoSubmit,
		.botAppName = (appname.isEmpty() ? postParam : appname),
		.botAppForceConfirmation = myContext.mayShowConfirmation,
		.attachBotUsername = params.value(u"attach"_q),
		.attachBotToggleCommand = (params.contains(u"startattach"_q)
			? params.value(u"startattach"_q)
			: (appname.isEmpty() && params.contains(u"startapp"_q))
			? params.value(u"startapp"_q)
			: std::optional<QString>()),
		.attachBotMenuOpen = (appname.isEmpty()
			&& params.contains(u"startapp"_q)),
		.attachBotChooseTypes = InlineBots::ParseChooseTypes(
			params.value(u"choose"_q)),
		.voicechatHash = (params.contains(u"livestream"_q)
			? std::make_optional(params.value(u"livestream"_q))
			: params.contains(u"videochat"_q)
			? std::make_optional(params.value(u"videochat"_q))
			: params.contains(u"voicechat"_q)
			? std::make_optional(params.value(u"voicechat"_q))
			: std::nullopt),
		.clickFromMessageId = myContext.itemId,
		.clickFromAttachBotWebviewUrl = myContext.attachBotWebviewUrl,
	});
	return true;
}

bool ResolvePrivatePost(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto channelId = ChannelId(
		params.value(u"channel"_q).toULongLong());
	const auto msgId = params.value(u"post"_q).toInt();
	const auto commentParam = params.value(u"comment"_q);
	const auto commentId = commentParam.toInt();
	const auto topicParam = params.value(u"topic"_q);
	const auto topicId = topicParam.toInt();
	const auto threadParam = params.value(u"thread"_q);
	const auto threadId = topicId ? topicId : threadParam.toInt();
	if (!channelId || (msgId && !IsServerMsgId(msgId))) {
		return false;
	}
	const auto my = context.value<ClickHandlerContext>();
	using Navigation = Window::SessionNavigation;
	controller->showPeerByLink(Navigation::PeerByLinkInfo{
		.usernameOrId = channelId,
		.messageId = msgId,
		.repliesInfo = commentId
			? Navigation::RepliesByLinkInfo{
				Navigation::CommentId{ commentId }
			}
			: threadId
			? Navigation::RepliesByLinkInfo{
				Navigation::ThreadId{ threadId }
			}
			: Navigation::RepliesByLinkInfo{ v::null },
		.clickFromMessageId = my.itemId,
		.clickFromAttachBotWebviewUrl = my.attachBotWebviewUrl,
	});
	controller->window().activate();
	return true;
}

bool ResolveSettings(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	const auto section = match->captured(1).mid(1).toLower();
	const auto type = [&]() -> std::optional<::Settings::Type> {
		if (section == u"language"_q) {
			ShowLanguagesBox(controller);
			return {};
		} else if (section == u"devices"_q) {
			return ::Settings::Sessions::Id();
		} else if (section == u"folders"_q) {
			return ::Settings::Folders::Id();
		} else if (section == u"privacy"_q) {
			return ::Settings::PrivacySecurity::Id();
		} else if (section == u"themes"_q) {
			return ::Settings::Chat::Id();
		} else if (section == u"change_number"_q) {
			controller->show(
				Ui::MakeInformBox(tr::lng_change_phone_error()));
			return {};
		} else if (section == u"auto_delete"_q) {
			return ::Settings::GlobalTTLId();
		} else if (section == u"information"_q) {
			return ::Settings::Information::Id();
		}
		return ::Settings::Main::Id();
	}();

	if (type.has_value()) {
		if (!controller) {
			return false;
		} else if (*type == ::Settings::Sessions::Id()) {
			controller->session().api().authorizations().reload();
		}
		controller->showSettings(*type);
		controller->window().activate();
	}
	return true;
}

bool HandleUnknown(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto request = match->captured(1);
	const auto callback = crl::guard(controller, [=](
			TextWithEntities message,
			bool updateRequired) {
		if (updateRequired) {
			const auto callback = [=](Fn<void()> &&close) {
				Core::UpdateApplication();
				close();
			};
			controller->show(Ui::MakeConfirmBox({
				.text = message,
				.confirmed = callback,
				.confirmText = tr::lng_menu_update(),
			}));
		} else {
			controller->show(Ui::MakeInformBox(message));
		}
	});
	controller->session().api().requestDeepLinkInfo(request, callback);
	return true;
}
#endif

bool OpenMediaTimestamp(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto time = match->captured(2).toInt();
	if (time < 0) {
		return false;
	}
	const auto base = match->captured(1);
	if (base.startsWith(u"doc"_q)) {
		const auto parts = base.mid(3).split('_');
		const auto documentId = parts.value(0).toULongLong();
		const auto itemId = FullMsgId(
			PeerId(parts.value(1).toULongLong()),
			MsgId(parts.value(2).toLongLong()));
		const auto session = &controller->session();
		const auto document = session->data().document(documentId);
		const auto context = session->data().message(itemId);
		const auto timeMs = time * crl::time(1000);
		if (document->isVideoFile()) {
			controller->window().openInMediaView(Media::View::OpenRequest(
				controller,
				document,
				context,
				context ? context->topicRootId() : MsgId(0),
				false,
				timeMs));
		} else if (document->isSong() || document->isVoiceMessage()) {
			session->settings().setMediaLastPlaybackPosition(
				documentId,
				timeMs);
			Media::Player::instance()->play({ document, itemId });
		}
		return true;
	}
	return false;
}

bool ShowInviteLink(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto base64link = match->captured(1).toLatin1();
	const auto link = QString::fromUtf8(QByteArray::fromBase64(base64link));
	if (link.isEmpty()) {
		return false;
	}
	QGuiApplication::clipboard()->setText(link);
	controller->showToast(tr::lng_group_invite_copied(tr::now));
	return true;
}

bool OpenExternalLink(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	return Ui::Integration::Instance().handleUrlClick(
		match->captured(1),
		context);
}

void ExportTestChatTheme(
		not_null<Window::SessionController*> controller,
		not_null<const Data::CloudTheme*> theme) {
	const auto session = &controller->session();
	const auto show = controller->uiShow();
#if 0 // tdlib todo
	const auto inputSettings = [&](Data::CloudThemeType type)
	-> std::optional<MTPInputThemeSettings> {
		const auto i = theme->settings.find(type);
		if (i == end(theme->settings)) {
			show->showToast(u"Something went wrong :("_q);
			return std::nullopt;
		}
		const auto &fields = i->second;
		if (!fields.paper
			|| !fields.paper->isPattern()
			|| fields.paper->backgroundColors().empty()
			|| !fields.paper->hasShareUrl()) {
			show->showToast(u"Something went wrong :("_q);
			return std::nullopt;
		}
		const auto &bg = fields.paper->backgroundColors();
		const auto url = fields.paper->shareUrl(&show->session());
		const auto from = url.indexOf("bg/");
		const auto till = url.indexOf("?");
		if (from < 0 || till <= from) {
			show->showToast(u"Bad WallPaper link: "_q + url);
			return std::nullopt;
		}

		using Setting = MTPDinputThemeSettings::Flag;
		using Paper = MTPDwallPaperSettings::Flag;
		const auto color = [](const QColor &color) {
			const auto red = color.red();
			const auto green = color.green();
			const auto blue = color.blue();
			return int(((uint32(red) & 0xFFU) << 16)
				| ((uint32(green) & 0xFFU) << 8)
				| (uint32(blue) & 0xFFU));
		};
		const auto colors = [&](const std::vector<QColor> &colors) {
			auto result = QVector<MTPint>();
			result.reserve(colors.size());
			for (const auto &single : colors) {
				result.push_back(MTP_int(color(single)));
			}
			return result;
		};
		const auto slug = url.mid(from + 3, till - from - 3);
		const auto settings = Setting::f_wallpaper
			| Setting::f_wallpaper_settings
			| (fields.outgoingAccentColor
				? Setting::f_outbox_accent_color
				: Setting(0))
			| (!fields.outgoingMessagesColors.empty()
				? Setting::f_message_colors
				: Setting(0));
		const auto papers = Paper::f_background_color
			| Paper::f_intensity
			| (bg.size() > 1
				? Paper::f_second_background_color
				: Paper(0))
			| (bg.size() > 2
				? Paper::f_third_background_color
				: Paper(0))
			| (bg.size() > 3
				? Paper::f_fourth_background_color
				: Paper(0));
		return MTP_inputThemeSettings(
			MTP_flags(settings),
			((type == Data::CloudThemeType::Dark)
				? MTP_baseThemeTinted()
				: MTP_baseThemeClassic()),
			MTP_int(color(fields.accentColor)),
			MTP_int(color(fields.outgoingAccentColor.value_or(
				Qt::black))),
			MTP_vector<MTPint>(colors(fields.outgoingMessagesColors)),
			MTP_inputWallPaperSlug(MTP_string(slug)),
			MTP_wallPaperSettings(
				MTP_flags(papers),
				MTP_int(color(bg[0])),
				MTP_int(color(bg.size() > 1 ? bg[1] : Qt::black)),
				MTP_int(color(bg.size() > 2 ? bg[2] : Qt::black)),
				MTP_int(color(bg.size() > 3 ? bg[3] : Qt::black)),
				MTP_int(fields.paper->patternIntensity()),
				MTP_int(0)));
	};
	const auto light = inputSettings(Data::CloudThemeType::Light);
	if (!light) {
		return;
	}
	const auto dark = inputSettings(Data::CloudThemeType::Dark);
	if (!dark) {
		return;
	}
	session->api().request(MTPaccount_CreateTheme(
		MTP_flags(MTPaccount_CreateTheme::Flag::f_settings),
		MTP_string(Window::Theme::GenerateSlug()),
		MTP_string(theme->title + " Desktop"),
		MTPInputDocument(),
		MTP_vector<MTPInputThemeSettings>(QVector<MTPInputThemeSettings>{
			*light,
			*dark,
		})
	)).done([=](const MTPTheme &result) {
		const auto slug = Data::CloudTheme::Parse(session, result, true).slug;
		QGuiApplication::clipboard()->setText(
			session->createInternalLinkFull("addtheme/" + slug));
		show->showToast(tr::lng_background_link_copied(tr::now));
	}).fail([=](const MTP::Error &error) {
		show->showToast(u"Error: "_q + error.type());
	}).send();
#endif
}

bool ResolveTestChatTheme(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	if (const auto history = controller->activeChatCurrent().history()) {
		controller->clearCachedChatThemes();
		const auto theme = history->owner().cloudThemes().updateThemeFromLink(
			history->peer->themeEmoji(),
			params);
		if (theme) {
			if (!params["export"].isEmpty()) {
				ExportTestChatTheme(controller, &*theme);
			}
			const auto recache = [&](Data::CloudThemeType type) {
				[[maybe_unused]] auto value = theme->settings.contains(type)
					? controller->cachedChatThemeValue(
						*theme,
						Data::WallPaper(0),
						type)
					: nullptr;
			};
			recache(Data::CloudThemeType::Dark);
			recache(Data::CloudThemeType::Light);
		}
	}
	return true;
}

#if 0 // mtp
bool ResolveInvoice(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto slug = params.value(u"slug"_q);
	if (slug.isEmpty()) {
		return false;
	}
	const auto window = &controller->window();
	Payments::CheckoutProcess::Start(
		&controller->session(),
		slug,
		crl::guard(window, [=](auto) { window->activate(); }));
	return true;
}

bool ResolvePremiumOffer(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1).mid(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto refAddition = params.value(u"ref"_q);
	const auto ref = "deeplink"
		+ (refAddition.isEmpty() ? QString() : '_' + refAddition);
	::Settings::ShowPremium(controller, ref);
	controller->window().activate();
	return true;
}
#endif

bool ResolveLoginCode(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	const auto loginCode = match->captured(2);
	const auto &domain = Core::App().domain();
	if (loginCode.isEmpty() || (!controller && !domain.started())) {
		return false;
	};
	(controller
		? controller->session().account()
		: domain.active()).handleLoginCode(loginCode);
	if (controller) {
		controller->window().activate();
	} else if (const auto window = Core::App().activeWindow()) {
		window->activate();
	}
	return true;
}

bool ResolveBoost(
		Window::SessionController *controller,
		const Match &match,
		const QVariant &context) {
	if (!controller) {
		return false;
	}
	const auto params = url_parse_params(
		match->captured(1),
		qthelp::UrlParamNameTransform::ToLower);
	const auto domainParam = params.value(u"domain"_q);
	const auto channelParam = params.contains(u"c"_q)
		? params.value(u"c"_q)
		: params.value(u"channel"_q);

	const auto myContext = context.value<ClickHandlerContext>();
	using Navigation = Window::SessionNavigation;
	controller->window().activate();
	controller->showPeerByLink(Navigation::PeerByLinkInfo{
		.usernameOrId = (!domainParam.isEmpty()
			? std::variant<QString, ChannelId>(domainParam)
			: ChannelId(BareId(channelParam.toULongLong()))),
		.resolveType = Window::ResolveType::Boost,
		.clickFromMessageId = myContext.itemId,
	});
	return true;
}

} // namespace

#if 0 // mtp
const std::vector<LocalUrlHandler> &LocalUrlHandlers() {
	static auto Result = std::vector<LocalUrlHandler>{
		{
			u"^join/?\\?invite=([a-zA-Z0-9\\.\\_\\-]+)(&|$)"_q,
			JoinGroupByHash
		},
		{
			u"^addlist/?\\?slug=([a-zA-Z0-9\\.\\_\\-]+)(&|$)"_q,
			JoinFilterBySlug
		},
		{
			u"^(addstickers|addemoji)/?\\?set=([a-zA-Z0-9\\.\\_]+)(&|$)"_q,
			ShowStickerSet
		},
		{
			u"^addtheme/?\\?slug=([a-zA-Z0-9\\.\\_]+)(&|$)"_q,
			ShowTheme
		},
		{
			u"^setlanguage/?(\\?lang=([a-zA-Z0-9\\.\\_\\-]+))?(&|$)"_q,
			SetLanguage
		},
		{
			u"^msg_url/?\\?(.+)(#|$)"_q,
			ShareUrl
		},
		{
			u"^confirmphone/?\\?(.+)(#|$)"_q,
			ConfirmPhone
		},
		{
			u"^share_game_score/?\\?(.+)(#|$)"_q,
			ShareGameScore
		},
		{
			u"^socks/?\\?(.+)(#|$)"_q,
			ApplySocksProxy
		},
		{
			u"^proxy/?\\?(.+)(#|$)"_q,
			ApplyMtprotoProxy
		},
		{
			u"^passport/?\\?(.+)(#|$)"_q,
			ShowPassport
		},
		{
			u"^bg/?\\?(.+)(#|$)"_q,
			ShowWallPaper
		},
		{
			u"^resolve/?\\?(.+)(#|$)"_q,
			ResolveUsernameOrPhone
		},
		{
			u"^privatepost/?\\?(.+)(#|$)"_q,
			ResolvePrivatePost
		},
		{
			u"^settings(/language|/devices|/folders|/privacy|/themes|/change_number|/auto_delete|/information|/edit_profile)?$"_q,
			ResolveSettings
		},
		{
			u"^test_chat_theme/?\\?(.+)(#|$)"_q,
			ResolveTestChatTheme,
		},
		{
			u"invoice/?\\?(.+)(#|$)"_q,
			ResolveInvoice,
		},
		{
			u"premium_offer/?(\\?.+)?(#|$)"_q,
			ResolvePremiumOffer,
		},
		{
			u"^login/?(\\?code=([0-9]+))(&|$)"_q,
			ResolveLoginCode
		},
		{
			u"^boost/?\\?(.+)(#|$)"_q,
			ResolveBoost,
		},
		{
			u"^([^\\?]+)(\\?|#|$)"_q,
			HandleUnknown
		},
	};
	return Result;
}
#endif

const std::vector<LocalUrlHandler> &InternalUrlHandlers() {
	static auto Result = std::vector<LocalUrlHandler>{
		{
			u"^media_timestamp/?\\?base=([a-zA-Z0-9\\.\\_\\-]+)&t=(\\d+)(&|$)"_q,
			OpenMediaTimestamp
		},
		{
			u"^show_invite_link/?\\?link=([a-zA-Z0-9_\\+\\/\\=\\-]+)(&|$)"_q,
			ShowInviteLink
		},
		{
			u"^url:(.+)$"_q,
			OpenExternalLink
		},
	};
	return Result;
}

QString TryConvertUrlToLocal(QString url) {
	if (url.size() > 8192) {
		url = url.mid(0, 8192);
	}

	using namespace qthelp;
	auto matchOptions = RegExOption::CaseInsensitive;
	auto subdomainMatch = regex_match(u"^(https?://)?([a-zA-Z0-9\\_]+)\\.t\\.me(/\\d+)?/?(\\?.+)?"_q, url, matchOptions);
	if (subdomainMatch) {
		const auto name = subdomainMatch->captured(2);
		if (name.size() > 1 && name != "www") {
			const auto result = TryConvertUrlToLocal(
				subdomainMatch->captured(1)
				+ "t.me/"
				+ name
				+ subdomainMatch->captured(3)
				+ subdomainMatch->captured(4));
			return result.startsWith("tg://resolve?domain=")
				? result
				: url;
		}
	}
	auto telegramMeMatch = regex_match(u"^(https?://)?(www\\.)?(telegram\\.(me|dog)|t\\.me)/(.+)$"_q, url, matchOptions);
	if (telegramMeMatch) {
		const auto query = telegramMeMatch->capturedView(5);
		if (const auto phoneMatch = regex_match(u"^\\+([0-9]+)(\\?|$)"_q, query, matchOptions)) {
			const auto params = query.mid(phoneMatch->captured(0).size()).toString();
			return u"tg://resolve?phone="_q + phoneMatch->captured(1) + (params.isEmpty() ? QString() : '&' + params);
		} else if (const auto joinChatMatch = regex_match(u"^(joinchat/|\\+|\\%20)([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"_q, query, matchOptions)) {
			return u"tg://join?invite="_q + url_encode(joinChatMatch->captured(2));
		} else if (const auto joinFilterMatch = regex_match(u"^(addlist/)([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"_q, query, matchOptions)) {
			return u"tg://addlist?slug="_q + url_encode(joinFilterMatch->captured(2));
		} else if (const auto stickerSetMatch = regex_match(u"^(addstickers|addemoji)/([a-zA-Z0-9\\.\\_]+)(\\?|$)"_q, query, matchOptions)) {
			return u"tg://"_q + stickerSetMatch->captured(1) + "?set=" + url_encode(stickerSetMatch->captured(2));
		} else if (const auto themeMatch = regex_match(u"^addtheme/([a-zA-Z0-9\\.\\_]+)(\\?|$)"_q, query, matchOptions)) {
			return u"tg://addtheme?slug="_q + url_encode(themeMatch->captured(1));
		} else if (const auto languageMatch = regex_match(u"^setlanguage/([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"_q, query, matchOptions)) {
			return u"tg://setlanguage?lang="_q + url_encode(languageMatch->captured(1));
		} else if (const auto shareUrlMatch = regex_match(u"^share/url/?\\?(.+)$"_q, query, matchOptions)) {
			return u"tg://msg_url?"_q + shareUrlMatch->captured(1);
		} else if (const auto confirmPhoneMatch = regex_match(u"^confirmphone/?\\?(.+)"_q, query, matchOptions)) {
			return u"tg://confirmphone?"_q + confirmPhoneMatch->captured(1);
		} else if (const auto ivMatch = regex_match(u"^iv/?\\?(.+)(#|$)"_q, query, matchOptions)) {
			//
			// We need to show our t.me page, not the url directly.
			//
			//auto params = url_parse_params(ivMatch->captured(1), UrlParamNameTransform::ToLower);
			//auto previewedUrl = params.value(u"url"_q);
			//if (previewedUrl.startsWith(u"http://"_q, Qt::CaseInsensitive)
			//	|| previewedUrl.startsWith(u"https://"_q, Qt::CaseInsensitive)) {
			//	return previewedUrl;
			//}
			return url;
		} else if (const auto socksMatch = regex_match(u"^socks/?\\?(.+)(#|$)"_q, query, matchOptions)) {
			return u"tg://socks?"_q + socksMatch->captured(1);
		} else if (const auto proxyMatch = regex_match(u"^proxy/?\\?(.+)(#|$)"_q, query, matchOptions)) {
			return u"tg://proxy?"_q + proxyMatch->captured(1);
		} else if (const auto invoiceMatch = regex_match(u"^(invoice/|\\$)([a-zA-Z0-9_\\-]+)(\\?|#|$)"_q, query, matchOptions)) {
			return u"tg://invoice?slug="_q + invoiceMatch->captured(2);
		} else if (const auto bgMatch = regex_match(u"^bg/([a-zA-Z0-9\\.\\_\\-\\~]+)(\\?(.+)?)?$"_q, query, matchOptions)) {
			const auto params = bgMatch->captured(3);
			const auto bg = bgMatch->captured(1);
			const auto type = regex_match(u"^[a-fA-F0-9]{6}^"_q, bg)
				? "color"
				: (regex_match(u"^[a-fA-F0-9]{6}\\-[a-fA-F0-9]{6}$"_q, bg)
					|| regex_match(u"^[a-fA-F0-9]{6}(\\~[a-fA-F0-9]{6}){1,3}$"_q, bg))
				? "gradient"
				: "slug";
			return u"tg://bg?"_q + type + '=' + bg + (params.isEmpty() ? QString() : '&' + params);
		} else if (const auto privateMatch = regex_match(u"^"
			"c/(\\-?\\d+)"
			"("
				"/?\\?|"
				"/?$|"
				"/\\d+/?(\\?|$)|"
				"/\\d+/\\d+/?(\\?|$)"
			")"_q, query, matchOptions)) {
			const auto channel = privateMatch->captured(1);
			const auto params = query.mid(privateMatch->captured(0).size()).toString();
			if (params.indexOf("boost", 0, Qt::CaseInsensitive) >= 0
				&& params.toLower().split('&').contains(u"boost"_q)) {
				return u"tg://boost?channel="_q + channel;
			}
			const auto base = u"tg://privatepost?channel="_q + channel;
			auto added = QString();
			if (const auto threadPostMatch = regex_match(u"^/(\\d+)/(\\d+)(/?\\?|/?$)"_q, privateMatch->captured(2))) {
				added = u"&topic=%1&post=%2"_q.arg(threadPostMatch->captured(1)).arg(threadPostMatch->captured(2));
			} else if (const auto postMatch = regex_match(u"^/(\\d+)(/?\\?|/?$)"_q, privateMatch->captured(2))) {
				added = u"&post="_q + postMatch->captured(1);
			}
			return base + added + (params.isEmpty() ? QString() : '&' + params);
		} else if (const auto usernameMatch = regex_match(u"^"
			"([a-zA-Z0-9\\.\\_]+)"
			"("
				"/?\\?|"
				"/?$|"
				"/[a-zA-Z0-9\\.\\_]+/?(\\?|$)|"
				"/\\d+/?(\\?|$)|"
				"/s/\\d+/?(\\?|$)|"
				"/\\d+/\\d+/?(\\?|$)"
			")"_q, query, matchOptions)) {
			const auto domain = usernameMatch->captured(1);
			const auto params = query.mid(usernameMatch->captured(0).size()).toString();
			if (params.indexOf("boost", 0, Qt::CaseInsensitive) >= 0
				&& params.toLower().split('&').contains(u"boost"_q)) {
				return u"tg://boost?domain="_q + domain;
			} else if (domain == u"boost"_q) {
				if (const auto domainMatch = regex_match(u"^/([a-zA-Z0-9\\.\\_]+)(/?\\?|/?$)"_q, usernameMatch->captured(2))) {
					return u"tg://boost?domain="_q + domainMatch->captured(1);
				} else if (params.indexOf("c=", 0, Qt::CaseInsensitive) >= 0) {
					return u"tg://boost?"_q + params;
				}
			}
			const auto base = u"tg://resolve?domain="_q + url_encode(usernameMatch->captured(1));
			auto added = QString();
			if (const auto threadPostMatch = regex_match(u"^/(\\d+)/(\\d+)(/?\\?|/?$)"_q, usernameMatch->captured(2))) {
				added = u"&topic=%1&post=%2"_q.arg(threadPostMatch->captured(1)).arg(threadPostMatch->captured(2));
			} else if (const auto postMatch = regex_match(u"^/(\\d+)(/?\\?|/?$)"_q, usernameMatch->captured(2))) {
				added = u"&post="_q + postMatch->captured(1);
			} else if (const auto storyMatch = regex_match(u"^/s/(\\d+)(/?\\?|/?$)"_q, usernameMatch->captured(2))) {
				added = u"&story="_q + storyMatch->captured(1);
			} else if (const auto appNameMatch = regex_match(u"^/([a-zA-Z0-9\\.\\_]+)(/?\\?|/?$)"_q, usernameMatch->captured(2))) {
				added = u"&appname="_q + appNameMatch->captured(1);
			}
			return base + added + (params.isEmpty() ? QString() : '&' + params);
		}
	}
	return url;
}

bool InternalPassportLink(const QString &url) {
	const auto urlTrimmed = url.trimmed();
	if (!urlTrimmed.startsWith(u"tg://"_q, Qt::CaseInsensitive)) {
		return false;
	}
	const auto command = base::StringViewMid(urlTrimmed, u"tg://"_q.size());

	using namespace qthelp;
	const auto matchOptions = RegExOption::CaseInsensitive;
	const auto authMatch = regex_match(
		u"^passport/?\\?(.+)(#|$)"_q,
		command,
		matchOptions);
	const auto usernameMatch = regex_match(
		u"^resolve/?\\?(.+)(#|$)"_q,
		command,
		matchOptions);
	const auto usernameValue = usernameMatch->hasMatch()
		? url_parse_params(
			usernameMatch->captured(1),
			UrlParamNameTransform::ToLower).value(u"domain"_q)
		: QString();
	const auto authLegacy = (usernameValue == u"telegrampassport"_q);
	return authMatch->hasMatch() || authLegacy;
}

bool StartUrlRequiresActivate(const QString &url) {
	return Core::App().passcodeLocked()
		? true
		: !InternalPassportLink(url);
}

bool HandleLocalUrl(
		const TLinternalLinkType &link,
		const QVariant &context) {
	using Navigation = Window::SessionNavigation;
	const auto my = context.value<ClickHandlerContext>();
	const auto attach = context.value<StartAttachContext>();
	const auto external = context.canConvert<OpenFromExternalContext>();
	const auto controller = my.sessionWindow.get()
		? my.sessionWindow.get()
		: attach.controller
		? attach.controller
		: Core::App().activePrimaryWindow()
		? Core::App().activePrimaryWindow()->sessionController()
		: nullptr;
	const auto session = controller ? &controller->session() : nullptr;
	const auto sender = session ? &session->sender() : nullptr;
	const auto settingsSection = [&](::Settings::Type type) {
		if (!controller) {
			return false;
		}
		controller->showSettings(type);
		controller->window().activate();
		return true;
	};
	return link.match([&](const TLDinternalLinkTypeActiveSessions &) {
		if (session) {
			session->api().authorizations().reload();
		}
		return settingsSection(::Settings::Sessions::Id());
	}, [&](const TLDinternalLinkTypeAttachmentMenuBot &data) {
		if (!controller) {
			return false;
		}
		const auto botUsername = data.vbot_username().v;
		const auto openWebAppUrl = data.vurl().v;
		return data.vtarget_chat().match([&](const TLDtargetChatCurrent &) {
			controller->showPeerByLink(Navigation::PeerByLinkInfo{
				.usernameOrId = botUsername,
				.attachBotToggleCommand = openWebAppUrl,
				.clickFromMessageId = my.itemId,
			});
			controller->window().activate();
			return true;
		}, [&](const TLDtargetChatChosen &data) {
			using Type = InlineBots::PeerType;
			controller->showPeerByLink(Navigation::PeerByLinkInfo{
				.usernameOrId = botUsername,
				.attachBotToggleCommand = openWebAppUrl,
				.attachBotChooseTypes = (Type()
					| (data.vallow_bot_chats().v ? Type::Bot : Type())
					| (data.vallow_user_chats().v ? Type::User : Type())
					| (data.vallow_group_chats().v ? Type::Group : Type())
					| (data.vallow_channel_chats().v ? Type::Broadcast : Type())),
				.clickFromMessageId = my.itemId,
			});
			controller->window().activate();
			return true;
		}, [&](const TLDtargetChatInternalLink &data) {
			return HandleLocalUrl(data.vlink(), QVariant::fromValue(
				StartAttachContext{
					.controller = controller,
					.botUsername = botUsername,
					.openWebAppUrl = openWebAppUrl,
				}));
		});
	}, [&](const TLDinternalLinkTypeAuthenticationCode &data) {
		const auto account = controller
			? &controller->session().account()
			: Core::App().domain().started()
			? &Core::App().domain().active()
			: nullptr;
		if (!account) {
			return false;
		}
		account->handleLoginCode(data.vcode().v);
		if (controller) {
			controller->window().activate();
		} else if (const auto window = Core::App().activeWindow()) {
			window->activate();
		}
		return true;
	}, [&](const TLDinternalLinkTypeBackground &data) {
		if (!controller) {
			return false;
		}
		sender->request(TLsearchBackground(
			data.vbackground_name()
		)).done(crl::guard(controller, [=](const TLbackground &result) {
			const auto paper = Data::WallPaper::Create(
				&controller->session(),
				result);
			if (paper) {
				controller->show(
					Box<BackgroundPreviewBox>(controller, *paper));
			} else {
				controller->show(
					Ui::MakeInformBox(tr::lng_background_bad_link()));
			}
			controller->window().activate();
		})).send();
		return true;
	}, [&](const TLDinternalLinkTypeBotStart &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.usernameOrId = data.vbot_username().v,
			.resolveType = Window::ResolveType::BotStart,
			.startToken = data.vstart_parameter().v,
			.startAutoSubmit = data.vautostart().v || my.botStartAutoSubmit,
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeBotStartInGroup &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.usernameOrId = data.vbot_username().v,
			.resolveType = Window::ResolveType::AddToGroup,
			.startToken = data.vstart_parameter().v,
			.startAdminRights = (data.vadministrator_rights()
				? AdminRightsFromChatAdministratorRights(
					*data.vadministrator_rights())
				: ChatAdminRights()),
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeBotAddToChannel &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.usernameOrId = data.vbot_username().v,
			.resolveType = Window::ResolveType::AddToChannel,
			.startAdminRights = AdminRightsFromChatAdministratorRights(
				data.vadministrator_rights()),
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeChangePhoneNumber &) {
		if (!controller) {
			return false;
		}
		controller->show(Ui::MakeInformBox(tr::lng_change_phone_error()));
		return true;
	}, [&](const TLDinternalLinkTypeChatInvite &data) {
		if (!controller) {
			return false;
		}
		Api::CheckChatInvite(controller, data.vinvite_link().v);
		return true;
	}, [&](const TLDinternalLinkTypeChatFolderInvite &data) {
		if (!controller) {
			return false;
		}
		Api::CheckFilterInvite(controller, data.vinvite_link().v);
		return true;
	}, [&](const TLDinternalLinkTypeDefaultMessageAutoDeleteTimerSettings &) {
		return settingsSection(::Settings::GlobalTTLId());
	}, [&](const TLDinternalLinkTypeEditProfileSettings &) {
		return settingsSection(::Settings::Information::Id());
	}, [&](const TLDinternalLinkTypeChatFolderSettings &) {
		return settingsSection(::Settings::Folders::Id());
	}, [&](const TLDinternalLinkTypeGame &data) {
		// tdlib tg://share_game_score links
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.usernameOrId = data.vbot_username().v,
			.resolveType = Window::ResolveType::ShareGame,
			.startToken = data.vgame_short_name().v,
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeInstantView &data) {
		File::OpenUrl(data.vfallback_url().v);
		return true;
	}, [&](const TLDinternalLinkTypeInvoice &data) {
		if (!controller) {
			return false;
		}
		const auto window = &controller->window();
		Payments::CheckoutProcess::Start(
			&controller->session(),
			data.vinvoice_name().v,
			crl::guard(window, [=](auto) { window->activate(); }));
		return true;
	}, [&](const TLDinternalLinkTypeLanguagePack &data) {
		Lang::CurrentCloudManager().switchWithWarning(
			data.vlanguage_pack_id().v);
		if (controller) {
			controller->window().activate();
		}
		return true;
	}, [&](const TLDinternalLinkTypeLanguageSettings &data) {
		ShowLanguagesBox(controller);
		return true;
	}, [&](const TLDinternalLinkTypeMessage &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.clickFromMessageId = my.itemId,
			.messageLink = data.vurl().v,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeMessageDraft &data) {
		if (!controller) {
			return false;
		}
		const auto text = Api::FormattedTextFromTdb(data.vtext());
		const auto link = data.vcontains_link().v;
		const auto chosen = [=](not_null<Data::Thread*> thread) {
			const auto content = controller->content();
			return content->shareUrl(thread, text, link);
		};
		Window::ShowChooseRecipientBox(controller, chosen);
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypePassportDataRequest &data) {
		if (!external) {
			return true;
		} else if (!controller) {
			return false;
		}
		controller->showPassportForm(Passport::FormRequest(
			UserId(data.vbot_user_id().v),
			data.vscope().v,
			data.vcallback_url().v,
			data.vpublic_key().v,
			data.vnonce().v));
		return true;
	}, [&](const TLDinternalLinkTypePhoneNumberConfirmation &data) {
		if (!controller) {
			return false;
		}
		session->api().confirmPhone().resolve(
			controller,
			data.vphone_number().v,
			data.vhash().v);
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypePremiumFeatures &data) {
		const auto refAddition = data.vreferrer().v;
		const auto ref = u"deeplink"_q
			+ (refAddition.isEmpty() ? QString() : '_' + refAddition);
		::Settings::ShowPremium(controller, ref);
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypePrivacyAndSecuritySettings &) {
		return settingsSection(::Settings::PrivacySecurity::Id());
	}, [&](const TLDinternalLinkTypeProxy &data) {
		auto fields = QMap<QString, QString>{
			{ u"server"_q, data.vserver().v },
			{ u"port"_q, QString::number(data.vport().v) },
		};
		const auto type = data.vtype().match([&](
				const TLDproxyTypeHttp &data) {
			fields.insert(u"user"_q, data.vusername().v);
			fields.insert(u"pass"_q, data.vpassword().v);
			return MTP::ProxyData::Type::Http;
		}, [&](const TLDproxyTypeSocks5 &data) {
			fields.insert(u"user"_q, data.vusername().v);
			fields.insert(u"pass"_q, data.vpassword().v);
			return MTP::ProxyData::Type::Socks5;
		}, [&](const TLDproxyTypeMtproto &data) {
			fields.insert(u"secret"_q, data.vsecret().v);
			return MTP::ProxyData::Type::Mtproto;
		});
		ProxiesBoxController::ShowApplyConfirmation(type, fields);
		if (controller) {
			controller->window().activate();
		}
		return true;
	}, [&](const TLDinternalLinkTypePublicChat &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.usernameOrId = data.vchat_username().v,
			.attachBotUsername = (attach ? attach.botUsername : QString()),
			.attachBotToggleCommand = (attach
				? attach.openWebAppUrl
				: std::optional<QString>()),
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeQrCodeAuthentication &data) {
		return false;
	}, [&](const TLDinternalLinkTypeRestorePurchases &data) {
		return false;
	}, [&](const TLDinternalLinkTypeSettings &data) {
		return settingsSection(::Settings::Main::Id());
	}, [&](const TLDinternalLinkTypeStickerSet &data) {
		if (!controller) {
			return false;
		}
		Core::App().hideMediaView();
		controller->show(Box<StickerSetBox>(
			controller->uiShow(),
			StickerSetIdentifier{ .shortName = data.vsticker_set_name().v },
			(data.vexpect_custom_emoji().v
				? Data::StickersType::Emoji
				: Data::StickersType::Stickers)));
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeTheme &data) {
		if (!controller) {
			return false;
		}
		Core::App().hideMediaView();
		controller->session().data().cloudThemes().resolve( // tdlib themes
			&controller->window(),
			data.vtheme_name().v,
			my.itemId);
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeThemeSettings &data) {
		return settingsSection(::Settings::Chat::Id());
	}, [&](const TLDinternalLinkTypeUnknownDeepLink &data) {
		if (!controller) {
			return false;
		}
		const auto callback = crl::guard(controller, [=](
				TextWithEntities message,
				bool updateRequired) {
			if (updateRequired) {
				const auto callback = [=](Fn<void()> &&close) {
					Core::UpdateApplication();
					close();
				};
				controller->show(Ui::MakeConfirmBox({
					.text = message,
					.confirmed = callback,
					.confirmText = tr::lng_menu_update(),
				}));
			} else {
				controller->show(Ui::MakeInformBox(message));
			}
		});
		session->api().requestDeepLinkInfo(data.vlink().v, callback);
		return true;
	}, [&](const TLDinternalLinkTypeUnsupportedProxy &data) {
		Ui::show(Ui::MakeInformBox(tr::lng_proxy_unsupported(tr::now)));
		return true;
	}, [&](const TLDinternalLinkTypeUserPhoneNumber &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.phone = data.vphone_number().v,
			.attachBotUsername = (attach ? attach.botUsername : QString()),
			.attachBotToggleCommand = (attach
				? attach.openWebAppUrl
				: std::optional<QString>()),
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeUserToken &data) {
		if (!controller) {
			return false;
		}
		const auto token = data.vtoken().v;
		sender->request(TLsearchUserByToken(
			tl_string(token)
		)).done(crl::guard(controller, [=](const TLuser &result) {
			controller->showPeerHistory(
				controller->session().data().processUser(result));
		})).fail(crl::guard(controller, [=] {
			controller->show(
				Ui::MakeInformBox(
					tr::lng_username_not_found(tr::now, lt_user, token)),
				Ui::LayerOption::CloseOther);
		})).send();
		return true;
	}, [&](const TLDinternalLinkTypeVideoChat &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.usernameOrId = data.vchat_username().v,
			.voicechatHash = data.vinvite_hash().v,
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeWebApp &data) {
		if (!controller) {
			return false;
		}
		controller->showPeerByLink(Navigation::PeerByLinkInfo{
			.usernameOrId = data.vbot_username().v,
			.resolveType = Window::ResolveType::BotApp,
			.startToken = data.vstart_parameter().v,
			.botAppName = data.vweb_app_short_name().v,
			.botAppForceConfirmation = my.mayShowConfirmation,
			.clickFromMessageId = my.itemId,
		});
		controller->window().activate();
		return true;
	}, [&](const TLDinternalLinkTypeStory &data) {
		// todo
		return true;
	});
}

} // namespace Core
