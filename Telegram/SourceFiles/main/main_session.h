/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include <rpl/filter.h>
#include <rpl/variable.h>
#include "base/timer.h"

class ApiWrap;

namespace Tdb {
class TLuser;
class Account;
class Sender;
class TLDupdateOption;
class TLmessage;
class TLDupdateAccentColors;
} // namespace Tdb

namespace Api {
class Updates;
class SendProgressManager;
} // namespace Api

namespace MTP {
class Instance;
struct ConfigFields;
} // namespace MTP

namespace Support {
class Helper;
class Templates;
} // namespace Support

namespace Data {
class Session;
class Changes;
} // namespace Data

namespace Storage {
class DownloadManagerMtproto;
class Uploader;
class Facade;
class Account;
class Domain;
} // namespace Storage

namespace Window {
class SessionController;
struct TermsLock;
} // namespace Window

namespace Stickers {
class EmojiPack;
class DicePacks;
class GiftBoxPack;
} // namespace Stickers;

namespace InlineBots {
class AttachWebView;
} // namespace InlineBots

namespace Ui {
struct ColorIndicesCompressed;
} // namespace Ui

namespace Main {

class Account;
class Domain;
class SessionSettings;
class SendAsPeers;

class Session final : public base::has_weak_ptr {
public:
#if 0 // mtp
	Session(
		not_null<Account*> account,
		const MTPUser &user,
		std::unique_ptr<SessionSettings> settings);
#endif
	Session(
		not_null<Account*> account,
		const Tdb::TLuser &user,
		std::unique_ptr<SessionSettings> settings);
	~Session();

	Session(const Session &other) = delete;
	Session &operator=(const Session &other) = delete;

	[[nodiscard]] Account &account() const;
	[[nodiscard]] Storage::Account &local() const;
	[[nodiscard]] Domain &domain() const;
	[[nodiscard]] Storage::Domain &domainLocal() const;

	[[nodiscard]] Tdb::Account &tdb() const;
	[[nodiscard]] Tdb::Sender &sender();
	[[nodiscard]] bool loggingOut() const;

	bool apply(const Tdb::TLDupdateOption &option);

	[[nodiscard]] bool premium() const;
	[[nodiscard]] bool premiumPossible() const;
	[[nodiscard]] rpl::producer<bool> premiumPossibleValue() const;
	[[nodiscard]] bool premiumBadgesShown() const;

	[[nodiscard]] bool isTestMode() const;
	[[nodiscard]] uint64 uniqueId() const; // userId() with TestDC shift.
	[[nodiscard]] UserId userId() const;
	[[nodiscard]] PeerId userPeerId() const;
	[[nodiscard]] not_null<UserData*> user() const {
		return _user;
	}
	bool validateSelf(UserId id);

	[[nodiscard]] Data::Changes &changes() const {
		return *_changes;
	}
	[[nodiscard]] Api::Updates &updates() const {
		return *_updates;
	}
	[[nodiscard]] Api::SendProgressManager &sendProgressManager() const {
		return *_sendProgressManager;
	}
#if 0 // mtp
	[[nodiscard]] Storage::DownloadManagerMtproto &downloader() const {
		return *_downloader;
	}
#endif
	[[nodiscard]] Storage::Uploader &uploader() const {
		return *_uploader;
	}
	[[nodiscard]] Storage::Facade &storage() const {
		return *_storage;
	}
	[[nodiscard]] Stickers::EmojiPack &emojiStickersPack() const {
		return *_emojiStickersPack;
	}
	[[nodiscard]] Stickers::DicePacks &diceStickersPacks() const {
		return *_diceStickersPacks;
	}
#if 0 // mtp
	[[nodiscard]] Stickers::GiftBoxPack &giftBoxStickersPacks() const {
		return *_giftBoxStickersPacks;
	}
#endif
	[[nodiscard]] Data::Session &data() const {
		return *_data;
	}
	[[nodiscard]] SessionSettings &settings() const {
		return *_settings;
	}
	[[nodiscard]] SendAsPeers &sendAsPeers() const {
		return *_sendAsPeers;
	}
	[[nodiscard]] InlineBots::AttachWebView &attachWebView() const {
		return *_attachWebView;
	}

	void saveSettings();
	void saveSettingsDelayed(crl::time delay = kDefaultSaveDelay);
	void saveSettingsNowIfNeeded();

	void addWindow(not_null<Window::SessionController*> controller);
	[[nodiscard]] auto windows() const
		-> const base::flat_set<not_null<Window::SessionController*>> &;
	[[nodiscard]] Window::SessionController *tryResolveWindow() const;

	// Shortcuts.
	void notifyDownloaderTaskFinished();
	[[nodiscard]] rpl::producer<> downloaderTaskFinished() const;
#if 0 // mtp
	[[nodiscard]] MTP::DcId mainDcId() const;
#endif
	[[nodiscard]] MTP::Instance &mtp() const;
	[[nodiscard]] const MTP::ConfigFields &serverConfig() const;
	[[nodiscard]] ApiWrap &api() {
		return *_api;
	}

	// Terms lock.
	void lockByTerms(const Window::TermsLock &data);
	void unlockTerms();
	void termsDeleteNow();
	[[nodiscard]] std::optional<Window::TermsLock> termsLocked() const;
	rpl::producer<bool> termsLockChanges() const;
	rpl::producer<bool> termsLockValue() const;

	[[nodiscard]] QString createInternalLink(const QString &query) const;
	[[nodiscard]] QString createInternalLinkFull(const QString &query) const;
	[[nodiscard]] TextWithEntities createInternalLink(
		const TextWithEntities &query) const;
	[[nodiscard]] TextWithEntities createInternalLinkFull(
		TextWithEntities query) const;

#if 0 // goodToRemove
	void setTmpPassword(const QByteArray &password, TimeId validUntil);
	[[nodiscard]] QByteArray validTmpPassword() const;
#endif

	// Can be called only right before ~Session.
	void finishLogout();

	// Uploads cancel with confirmation.
	[[nodiscard]] bool uploadsInProgress() const;
	void uploadsStopWithConfirmation(Fn<void()> done);
	void uploadsStop();

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

	[[nodiscard]] bool supportMode() const;
	[[nodiscard]] Support::Helper &supportHelper() const;
	[[nodiscard]] Support::Templates &supportTemplates() const;

	void applyAccentColors(const Tdb::TLDupdateAccentColors &update);
	[[nodiscard]] auto colorIndicesValue() const
		-> rpl::producer<Ui::ColorIndicesCompressed>;
	[[nodiscard]] auto availableColorIndicesValue() const
		-> rpl::producer<std::vector<uint8>>;

private:
	static constexpr auto kDefaultSaveDelay = crl::time(1000);

	const not_null<Account*> _account;
	const std::unique_ptr<Tdb::Sender> _sender;

	const std::unique_ptr<SessionSettings> _settings;
	const std::unique_ptr<Data::Changes> _changes;
	const std::unique_ptr<ApiWrap> _api;
	const std::unique_ptr<Api::Updates> _updates;
	const std::unique_ptr<Api::SendProgressManager> _sendProgressManager;
#if 0 // mtp
	const std::unique_ptr<Storage::DownloadManagerMtproto> _downloader;
#endif
	rpl::event_stream<> _downloaderTaskFinished;
	const std::unique_ptr<Storage::Uploader> _uploader;
	const std::unique_ptr<Storage::Facade> _storage;

	// _data depends on _downloader / _uploader.
	const std::unique_ptr<Data::Session> _data;
	const UserId _userId;
	const not_null<UserData*> _user;

	// _emojiStickersPack depends on _data.
	const std::unique_ptr<Stickers::EmojiPack> _emojiStickersPack;
	const std::unique_ptr<Stickers::DicePacks> _diceStickersPacks;
#if 0 // mtp
	const std::unique_ptr<Stickers::GiftBoxPack> _giftBoxStickersPacks;
#endif
	const std::unique_ptr<SendAsPeers> _sendAsPeers;
	const std::unique_ptr<InlineBots::AttachWebView> _attachWebView;

	const std::unique_ptr<Support::Helper> _supportHelper;

	std::shared_ptr<QImage> _selfUserpicView;
	rpl::variable<bool> _premiumPossible = false;

	rpl::event_stream<bool> _termsLockChanges;
	std::unique_ptr<Window::TermsLock> _termsLock;

	base::flat_set<not_null<Window::SessionController*>> _windows;
	base::Timer _saveSettingsTimer;

	rpl::event_stream<> _colorIndicesChanged;
	std::unique_ptr<Ui::ColorIndicesCompressed> _colorIndicesCurrent;
	mutable rpl::variable<std::vector<uint8>> _availableColorIndices;
#if 0 // goodToRemove
	QByteArray _tmpPassword;
	TimeId _tmpPasswordValidUntil = 0;

	rpl::event_stream<Ui::ColorIndicesCompressed> _colorIndicesChanges;
#endif

	rpl::lifetime _lifetime;

};

} // namespace Main
