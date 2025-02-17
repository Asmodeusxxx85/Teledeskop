/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_session.h"

#include "apiwrap.h"
#include "api/api_updates.h"
#include "api/api_send_progress.h"
#include "api/api_user_privacy.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session_settings.h"
#include "main/main_app_config.h"
#include "main/session/send_as_peers.h"
#include "mtproto/mtproto_config.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "chat_helpers/stickers_dice_pack.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "history/history.h"
#include "history/history_item.h"
#include "inline_bots/bot_attach_web_view.h"
#include "storage/file_download.h"
#include "storage/download_manager_mtproto.h"
#include "storage/file_upload.h"
#include "storage/storage_account.h"
#include "storage/storage_facade.h"
#include "storage/storage_account.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_download_manager.h"
#include "data/stickers/data_stickers.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/window_lock_widgets.h"
#include "base/unixtime.h"
#include "calls/calls_instance.h"
#include "support/support_helper.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "styles/style_layers.h"

#include "tdb/tdb_tl_scheme.h"
#include "tdb/tdb_option.h"
#include "api/api_global_privacy.h"
#include "ui/chat/chat_style.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "chat_helpers/spellchecker_common.h"
#endif // TDESKTOP_DISABLE_SPELLCHECK

namespace Main {
namespace {

using namespace Tdb;

#if 0 // goodToRemove
constexpr auto kTmpPasswordReserveTime = TimeId(10);

[[nodiscard]] QString ValidatedInternalLinksDomain(
		not_null<const Session*> session) {
	// This domain should start with 'http[s]://' and end with '/'.
	// Like 'https://telegram.me/' or 'https://t.me/'.
	const auto &domain = session->serverConfig().internalLinksDomain;
	const auto prefixes = {
		u"https://"_q,
		u"http://"_q,
	};
	for (const auto &prefix : prefixes) {
		if (domain.startsWith(prefix, Qt::CaseInsensitive)) {
			return domain.endsWith('/')
				? domain
				: MTP::ConfigFields().internalLinksDomain;
		}
	}
	return MTP::ConfigFields().internalLinksDomain;
}
#endif

} // namespace

Session::Session(
	not_null<Account*> account,
	const Tdb::TLuser &user,
	std::unique_ptr<SessionSettings> settings)
: _account(account)
, _sender(std::make_unique<Tdb::Sender>(&_account->sender()))
, _settings(std::move(settings))
, _changes(std::make_unique<Data::Changes>(this))
, _api(std::make_unique<ApiWrap>(this))
, _updates(std::make_unique<Api::Updates>(this))
, _sendProgressManager(std::make_unique<Api::SendProgressManager>(this))
#if 0 // mtp
, _downloader(std::make_unique<Storage::DownloadManagerMtproto>(_api.get()))
#endif
, _uploader(std::make_unique<Storage::Uploader>(_api.get()))
, _storage(std::make_unique<Storage::Facade>())
, _data(std::make_unique<Data::Session>(this))
, _userId(user.c_user().vid())
, _user(_data->processUser(user))
, _emojiStickersPack(std::make_unique<Stickers::EmojiPack>(this))
, _diceStickersPacks(std::make_unique<Stickers::DicePacks>(this))
#if 0 // mtp
, _giftBoxStickersPacks(std::make_unique<Stickers::GiftBoxPack>(this))
#endif
, _sendAsPeers(std::make_unique<SendAsPeers>(this))
, _attachWebView(std::make_unique<InlineBots::AttachWebView>(this))
, _supportHelper(Support::Helper::Create(this))
, _saveSettingsTimer([=] { saveSettings(); }) {
	Expects(_settings != nullptr);

#if 0 // mtp
	_api->requestTermsUpdate();
#endif
	_api->requestFullPeer(_user);

#if 0 // mtp
	_api->instance().setUserPhone(_user->phone());
#endif

	// Load current userpic and keep it loaded.
	_user->loadUserpic();
	changes().peerFlagsValue(
		_user,
		Data::PeerUpdate::Flag::Photo
	) | rpl::start_with_next([=] {
		auto view = Ui::PeerUserpicView{ .cloud = _selfUserpicView };
		[[maybe_unused]] const auto image = _user->userpicCloudImage(view);
		_selfUserpicView = view.cloud;
	}, lifetime());

	crl::on_main(this, [=] {
		using Flag = Data::PeerUpdate::Flag;
		changes().peerUpdates(
			_user,
			Flag::Name
			| Flag::Username
			| Flag::Photo
			| Flag::About
			| Flag::PhoneNumber
		) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
			local().writeSelf();

#if 0 // mtp
			if (update.flags & Flag::PhoneNumber) {
				const auto phone = _user->phone();
				_api->instance().setUserPhone(phone);
				if (!phone.isEmpty()) {
					_api->instance().requestConfig();
				}
			}
#endif
		}, _lifetime);

#if 0 // mtp
#ifndef OS_MAC_STORE
		_account->appConfig().value(
		) | rpl::start_with_next([=] {
			_premiumPossible = !_account->appConfig().get<bool>(
				"premium_purchase_blocked",
				true);
		}, _lifetime);
#endif // OS_MAC_STORE
#endif

		if (_settings->hadLegacyCallsPeerToPeerNobody()) {
			api().userPrivacy().save(
				Api::UserPrivacy::Key::CallsPeer2Peer,
				Api::UserPrivacy::Rule{
					.option = Api::UserPrivacy::Option::Nobody
				});
			saveSettingsDelayed();
		}

		// Storage::Account uses Main::Account::session() in those methods.
		// So they can't be called during Main::Session construction.
		local().readInstalledStickers();
		local().readInstalledMasks();
		local().readInstalledCustomEmoji();
		local().readFeaturedStickers();
		local().readFeaturedCustomEmoji();
		local().readRecentStickers();
		local().readRecentMasks();
		local().readFavedStickers();
		local().readSavedGifs();
		data().stickers().notifyUpdated(Data::StickersType::Stickers);
		data().stickers().notifyUpdated(Data::StickersType::Masks);
		data().stickers().notifyUpdated(Data::StickersType::Emoji);
		data().stickers().notifySavedGifsUpdated();
	});

#ifndef TDESKTOP_DISABLE_SPELLCHECK
	Spellchecker::Start(this);
#endif // TDESKTOP_DISABLE_SPELLCHECK

#if 0 // mtp
	_api->requestNotifySettings(MTP_inputNotifyUsers());
	_api->requestNotifySettings(MTP_inputNotifyChats());
	_api->requestNotifySettings(MTP_inputNotifyBroadcasts());
#endif
	_api->requestDefaultNotifySettings();

	constexpr auto kMax = 5; // later Core::App().notifications().maxCount();
	sender().request(TLsetOption(
		tl_string("notification_group_count_max"_q),
		tl_optionValueInteger(tl_int64(kMax))
	)).send();
	sender().request(TLsetOption(
		tl_string("notification_group_size_max"_q),
		tl_optionValueInteger(tl_int64(kMax))
	)).send();

	Core::App().downloadManager().trackSession(this);
}

#if 0 // goodToRemove
void Session::setTmpPassword(const QByteArray &password, TimeId validUntil) {
	if (_tmpPassword.isEmpty() || validUntil > _tmpPasswordValidUntil) {
		_tmpPassword = password;
		_tmpPasswordValidUntil = validUntil;
	}
}

QByteArray Session::validTmpPassword() const {
	return (_tmpPasswordValidUntil
		>= base::unixtime::now() + kTmpPasswordReserveTime)
		? _tmpPassword
		: QByteArray();
}
#endif

// Can be called only right before ~Session.
void Session::finishLogout() {
	unlockTerms();
	data().clear();
	data().clearLocalStorage();
}

Session::~Session() {
	unlockTerms();
	data().clear();
	ClickHandler::clearActive();
	ClickHandler::unpressed();
}

Account &Session::account() const {
	return *_account;
}

Storage::Account &Session::local() const {
	return _account->local();
}

Domain &Session::domain() const {
	return _account->domain();
}

Storage::Domain &Session::domainLocal() const {
	return _account->domainLocal();
}

Tdb::Account &Session::tdb() const {
	return _account->tdb();
}

Tdb::Sender &Session::sender() {
	return *_sender;
}

bool Session::loggingOut() const {
	return _account->loggingOut();
}

bool Session::apply(const TLDupdateOption &update) {
	if (update.vname().v == "is_premium_available") {
		_premiumPossible = OptionValue<bool>(update.vvalue());
		return true;
	}
	return _api->apply(update);
}

void Session::notifyDownloaderTaskFinished() {
#if 0 // mtp
	downloader().notifyTaskFinished();
#endif
	_downloaderTaskFinished.fire({});
}

rpl::producer<> Session::downloaderTaskFinished() const {
#if 0 // mtp
	return downloader().taskFinished();
#endif
	return _downloaderTaskFinished.events();
}

bool Session::premium() const {
	return _user->isPremium();
}

bool Session::premiumPossible() const {
	return premium() || _premiumPossible.current();
}

bool Session::premiumBadgesShown() const {
	return supportMode() || premiumPossible();
}

rpl::producer<bool> Session::premiumPossibleValue() const {
	using namespace rpl::mappers;

	auto premium = _user->flagsValue(
	) | rpl::filter([=](UserData::Flags::Change change) {
		return (change.diff & UserDataFlag::Premium);
	}) | rpl::map([=] {
		return _user->isPremium();
	});
	return rpl::combine(
		std::move(premium),
		_premiumPossible.value(),
		_1 || _2);
}

bool Session::isTestMode() const {
#if 0 // mtp
	return mtp().isTestMode();
#endif
	return _account->testMode();
}

uint64 Session::uniqueId() const {
	// See also Account::willHaveSessionUniqueId.
	return userId().bare
		| (isTestMode() ? 0x0100'0000'0000'0000ULL : 0ULL);
}

UserId Session::userId() const {
	return _userId;
}

PeerId Session::userPeerId() const {
	return _userId;
}

bool Session::validateSelf(UserId id) {
	if (id != userId()) {
		LOG(("Auth Error: wrong self user received."));
		crl::on_main(this, [=] { _account->logOut(); });
		return false;
	}
	return true;
}

void Session::saveSettings() {
	local().writeSessionSettings();
}

void Session::saveSettingsDelayed(crl::time delay) {
	_saveSettingsTimer.callOnce(delay);
}

void Session::saveSettingsNowIfNeeded() {
	if (_saveSettingsTimer.isActive()) {
		_saveSettingsTimer.cancel();
		saveSettings();
	}
}

#if 0 // mtp
MTP::DcId Session::mainDcId() const {
	return _account->mtp().mainDcId();
}
#endif

MTP::Instance &Session::mtp() const {
	return _account->mtp();
}

const MTP::ConfigFields &Session::serverConfig() const {
	return _account->mtp().configValues();
}

void Session::lockByTerms(const Window::TermsLock &data) {
	if (!_termsLock || *_termsLock != data) {
		_termsLock = std::make_unique<Window::TermsLock>(data);
		_termsLockChanges.fire(true);
	}
}

void Session::unlockTerms() {
	if (_termsLock) {
		_termsLock = nullptr;
		_termsLockChanges.fire(false);
	}
}

void Session::termsDeleteNow() {
	sender().request(TLdeleteAccount(
		tl_string("Decline ToS update"),
		tl_string()
	)).send();
#if 0 // mtp
	api().request(MTPaccount_DeleteAccount(
		MTP_flags(0),
		MTP_string("Decline ToS update"),
		MTPInputCheckPasswordSRP()
	)).send();
#endif
}

std::optional<Window::TermsLock> Session::termsLocked() const {
	return _termsLock ? base::make_optional(*_termsLock) : std::nullopt;
}

rpl::producer<bool> Session::termsLockChanges() const {
	return _termsLockChanges.events();
}

rpl::producer<bool> Session::termsLockValue() const {
	return rpl::single(
		_termsLock != nullptr
	) | rpl::then(termsLockChanges());
}

QString Session::createInternalLink(const QString &query) const {
	return createInternalLink(TextWithEntities{ .text = query }).text;
}

QString Session::createInternalLinkFull(const QString &query) const {
	return createInternalLinkFull(TextWithEntities{ .text = query }).text;
}

TextWithEntities Session::createInternalLink(
		const TextWithEntities &query) const {
	const auto result = createInternalLinkFull(query);
	const auto prefixes = {
		u"https://"_q,
		u"http://"_q,
	};
	for (auto &prefix : prefixes) {
		if (result.text.startsWith(prefix, Qt::CaseInsensitive)) {
			return Ui::Text::Mid(result, prefix.size());
		}
	}
	LOG(("Warning: bad internal url '%1'").arg(result.text));
	return result;
}

TextWithEntities Session::createInternalLinkFull(
		TextWithEntities query) const {
#if 0 // mtp
	return TextWithEntities::Simple(ValidatedInternalLinksDomain(this))
#endif
	return TextWithEntities::Simple(_account->internalLinksDomain())
		.append(std::move(query));
}

bool Session::supportMode() const {
	return (_supportHelper != nullptr);
}

Support::Helper &Session::supportHelper() const {
	Expects(supportMode());

	return *_supportHelper;
}

Support::Templates& Session::supportTemplates() const {
	return supportHelper().templates();
}

void Session::addWindow(not_null<Window::SessionController*> controller) {
	_windows.emplace(controller);
	controller->lifetime().add([=] {
		_windows.remove(controller);
	});
	updates().addActiveChat(controller->activeChatChanges(
	) | rpl::map([=](Dialogs::Key chat) {
		return chat.peer();
	}) | rpl::distinct_until_changed());
}

bool Session::uploadsInProgress() const {
	return !!_uploader->currentUploadId();
}

void Session::uploadsStopWithConfirmation(Fn<void()> done) {
	const auto id = _uploader->currentUploadId();
	const auto message = data().message(id);
	const auto exists = (message != nullptr);
	const auto window = message
		? Core::App().windowFor(message->history()->peer)
		: Core::App().activePrimaryWindow();
	auto box = Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				tr::lng_upload_sure_stop(),
				st::boxLabel),
			st::boxPadding + QMargins(0, 0, 0, st::boxPadding.bottom()));
		box->setStyle(st::defaultBox);
		box->addButton(tr::lng_selected_upload_stop(), [=] {
			box->closeBox();

			uploadsStop();
			if (done) {
				done();
			}
		}, st::attentionBoxButton);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (exists) {
			box->addLeftButton(tr::lng_upload_show_file(), [=] {
				box->closeBox();

				if (const auto item = data().message(id)) {
					if (const auto window = tryResolveWindow()) {
						window->showMessage(item);
					}
				}
			});
		}
	});
	window->show(std::move(box));
	window->activate();
}

void Session::uploadsStop() {
	_uploader->cancelAll();
}

auto Session::windows() const
-> const base::flat_set<not_null<Window::SessionController*>> & {
	return _windows;
}

Window::SessionController *Session::tryResolveWindow() const {
	if (_windows.empty()) {
		domain().activate(_account);
		if (_windows.empty()) {
			return nullptr;
		}
	}
	for (const auto &window : _windows) {
		if (window->isPrimary()) {
			return window;
		}
	}
	return _windows.front();
}

void Session::applyAccentColors(const TLDupdateAccentColors &update) {
#if 0
	constexpr auto parseColor = [](const MTPJSONValue &color) {
		if (color.type() != mtpc_jsonString) {
			LOG(("API Error: Bad type for color element."));
			return uint32();
		}
		const auto value = color.c_jsonString().vvalue().v;
		if (value.size() != 6) {
			LOG(("API Error: Bad length for color element: %1"
				).arg(qs(value)));
			return uint32();
		}
		const auto hex = [](char ch) {
			return (ch >= 'a' && ch <= 'f')
				? (ch - 'a' + 10)
				: (ch >= 'A' && ch <= 'F')
				? (ch - 'A' + 10)
				: (ch >= '0' && ch <= '9')
				? (ch - '0')
				: 0;
		};
		auto result = (uint32(1) << 24);
		for (auto i = 0; i != 6; ++i) {
			result |= (uint32(hex(value[i])) << ((5 - i) * 4));
		}
		return result;
	};
#endif
	using ThemeColors = std::array<uint32, Ui::kColorPatternsCount>;
	constexpr auto parseColors = [](const TLvector<TLint32> &values) {
		const auto &list = values.v;
		const auto count = int(list.size());
		if (count > Ui::kColorPatternsCount) {
			LOG(("API Error: Bad accentColor list size: %1").arg(count));
			return ThemeColors();
		}
		auto result = ThemeColors();
		for (auto i = 0; i != count; ++i) {
			result[i] = list[i].v;
		}
		return result;
	};
	const auto parseColorIndex = [&](const TLDaccentColor &data) {
		return Ui::ColorIndexData{
			.light = parseColors(data.vlight_theme_colors()),
			.dark = parseColors(data.vdark_theme_colors()),
		};
	};
	auto colors = std::make_shared<
		std::array<Ui::ColorIndexData, Ui::kColorIndexCount>>();
	for (const auto &color : update.vcolors().v) {
		const auto &data = color.data();
		const auto index = data.vid().v;
		if (index < Ui::kSimpleColorIndexCount
			|| index >= Ui::kColorIndexCount) {
			LOG(("API Error: Bad index for accentColor: %1").arg(index));
			continue;
		}
		(*colors)[index] = parseColorIndex(data);
	}

	if (!_colorIndicesCurrent) {
		_colorIndicesCurrent = std::make_unique<Ui::ColorIndicesCompressed>(
			Ui::ColorIndicesCompressed{ std::move(colors) });
		_colorIndicesChanged.fire({});
	} else if (*_colorIndicesCurrent->colors != *colors) {
		_colorIndicesCurrent->colors = std::move(colors);
		_colorIndicesChanged.fire({});
	}

	_availableColorIndices = update.vavailable_accent_color_ids().v
		| ranges::views::transform([](TLint32 v) { return uint8(v.v); })
		| ranges::to_vector;
}

auto Session::colorIndicesValue() const
-> rpl::producer<Ui::ColorIndicesCompressed> {
#if 0 // mtp
	return _account->appConfig().colorIndicesValue();
#endif
	return rpl::single(_colorIndicesCurrent
		? *_colorIndicesCurrent
		: Ui::ColorIndicesCompressed()
	) | rpl::then(_colorIndicesChanged.events() | rpl::map([=] {
		return *_colorIndicesCurrent;
	}));
}

auto Session::availableColorIndicesValue() const
-> rpl::producer<std::vector<uint8>> {
	if (_availableColorIndices.current().empty()) {
		_availableColorIndices = std::vector<uint8>{ 0, 1, 2, 3, 4, 5, 6 };
	}
	return _availableColorIndices.value();
}

} // namespace Main
