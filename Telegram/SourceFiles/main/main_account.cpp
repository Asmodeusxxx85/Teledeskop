/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_account.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h" // Storage::StartResult.
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "window/window_controller.h"
#include "media/audio/media_audio.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtp_instance.h"
#include "ui/image/image.h"
#include "mainwidget.h"
#include "api/api_updates.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "main/main_session_settings.h"
#include "lang/lang_instance.h"

#include "tdb/tdb_account.h"
#include "tdb/tdb_tl_scheme.h"
#include "api/api_text_entities.h"
#include "lang/lang_cloud_manager.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/connection_box.h" // TypeToTL
#include "tdb/tdb_option.h"

namespace Main {
namespace {

constexpr auto kWideIdsTag = ~uint64(0);

[[nodiscard]] QString ComposeDataString(const QString &dataName, int index) {
	auto result = dataName;
	result.replace('#', QString());
	if (index > 0) {
		result += '#' + QString::number(index + 1);
	}
	return result;
}

} // namespace

Account::Account(
	not_null<Domain*> domain,
	const QString &dataName,
	int index)
: _domain(domain)
, _local(std::make_unique<Storage::Account>(
	this,
	ComposeDataString(dataName, index)))
, _internalLinksDomain(u"https://t.me/"_q) {
}

Account::~Account() {
	if (const auto session = maybeSession()) {
		session->saveSettingsNowIfNeeded();
	}
	destroySession(DestroyReason::Quitting);
}

Storage::Domain &Account::domainLocal() const {
	return _domain->local();
}

[[nodiscard]] Storage::StartResult Account::legacyStart(
		const QByteArray &passcode) {
	Expects(!_appConfig);

	return _local->legacyStart(passcode);
}

std::unique_ptr<MTP::Config> Account::prepareToStart(
		std::shared_ptr<MTP::AuthKey> localKey) {
	return _local->start(std::move(localKey));
}

void Account::start(std::unique_ptr<MTP::Config> config) {
	_testMode = config ? config->isTestMode() : false;
	_tdb = createTdb();
	_appConfig = std::make_unique<AppConfig>(this);
	startMtp(config
		? std::move(config)
		: std::make_unique<MTP::Config>(
			Core::App().fallbackProductionConfig()));
	_appConfig->start();
	watchProxyChanges();
	watchSessionChanges();

	style::ShortAnimationPlaying(
	) | rpl::start_with_next([=, raw = _tdb.get()](bool playing) {
		_tdbPaused = playing;
		crl::on_main(this, [=] {
			raw->setPaused(_tdbPaused);
		});
	}, _tdb->lifetime());

	if (_session) {
		sender().request(
			Tdb::TLgetMe()
		).done([=](const Tdb::TLuser &result) {
			if (_session) {
				_session->validateSelf(result.data().vid().v);
			}
		}).fail([=](const Tdb::Error &error) {
		}).send();
	}
}

std::unique_ptr<Tdb::Account> Account::createTdb() {
	const auto key = domain().tdbKey();
	const auto langpackPath = cWorkingDir() + u"tdata/lang"_q;
	QDir().mkpath(langpackPath);
	auto result = std::make_unique<Tdb::Account>(Tdb::AccountConfig{
		.apiId = ApiId,
		.apiHash = ApiHash,
		.systemLanguageCode = Lang::GetInstance().systemLangCode(),
		.deviceModel = Platform::DeviceModelPretty(),
		.systemVersion = Platform::SystemVersionPretty(),
		.applicationVersion = QString::fromLatin1(AppVersionStr),
		.databaseDirectory = _local->libDatabasePath(),
		.filesDirectory = _local->libFilesPath(),
		.langpackDirectory = langpackPath,
		.encryptionKey = QByteArray(
			reinterpret_cast<const char*>(key.data()),
			key.size()),
		.testDc = _testMode,
	});
	using namespace Tdb;
	result->updates(
	) | rpl::start_with_next([=](const TLupdate &update) {
		update.match([&](const TLDupdateAuthorizationState &data) {
			data.vauthorization_state().match([&](
					const TLDauthorizationStateWaitTdlibParameters &) {
			}, [](const TLDauthorizationStateReady &) {
			}, [](const TLDauthorizationStateLoggingOut &) {
			}, [](const TLDauthorizationStateClosing &) {
			}, [&](const TLDauthorizationStateClosed &) {
				if (_session) {
					DEBUG_LOG(("Tdb Info: Got 'Closed', logged out."));
					loggedOut();
				}
				setConnectionState(ConnectionState::WaitingForNetwork);
			}, [&](const auto &) {
				if (_session) {
					LOG(("Tdb Info: Got bad state, logged out."));
					loggedOut();
				}
			});
		}, [&](const TLDupdateConnectionState &data) {
			setConnectionState(data.vstate().match([](
				const TLDconnectionStateWaitingForNetwork &) {
				return ConnectionState::WaitingForNetwork;
			}, [](const TLDconnectionStateConnectingToProxy &) {
				return ConnectionState::ConnectingToProxy;
			}, [](const TLDconnectionStateConnecting &) {
				return ConnectionState::Connecting;
			}, [](const TLDconnectionStateUpdating &) {
				return ConnectionState::Updating;
			}, [](const TLDconnectionStateReady &) {
				return ConnectionState::Ready;
			}));
		}, [&](const TLDupdateServiceNotification &data) {
			const auto text = data.vcontent().match([&](
					const TLDmessageText &data) {
				return Api::FormattedTextFromTdb(data.vtext());
			}, [](const auto &) {
				return TextWithEntities();
			});
			if (data.vtype().v.startsWith("AUTH_KEY_DROP_")) {
				Core::App().forceLogOut(this, text);
			} else {
				Core::App().activeWindow()->show(Ui::MakeInformBox(text));
			}
		}, [&](const TLDupdateOption &data) {
			if (data.vname().v == "t_me_url") {
				_internalLinksDomain = OptionValue<QString>(data.vvalue());
			} else {
				Lang::CurrentCloudManager().apply(data);
			}
		}, [](const auto &) {
		});
	}, _lifetime);
	return result;
}

void Account::prepareToStartAdded(
		std::shared_ptr<MTP::AuthKey> localKey) {
	_local->startAdded(std::move(localKey));
}

void Account::watchProxyChanges() {
	using ProxyChange = Core::Application::ProxyChange;


	const auto apply = [&] {
		const auto proxy = Core::App().settings().proxy().selected();
		if (proxy && Core::App().settings().proxy().isEnabled()) {
			_tdb->setProxy(Tdb::TLaddProxy(
				Tdb::tl_string(proxy.host),
				Tdb::tl_int32(proxy.port),
				Tdb::tl_bool(true),
				TypeToTL(proxy)
			));
		} else {
			_tdb->setProxy(Tdb::TLdisableProxy());
		}
	};
	apply();
	Core::App().proxyChanges(
	) | rpl::start_with_next([=](const ProxyChange &change) {
		apply();
#if 0 // mtp
		const auto key = [&](const MTP::ProxyData &proxy) {
			return (proxy.type == MTP::ProxyData::Type::Mtproto)
				? std::make_pair(proxy.host, proxy.port)
				: std::make_pair(QString(), uint32(0));
		};

		if (_mtp) {
			_mtp->restart();
			if (key(change.was) != key(change.now)) {
				_mtp->reInitConnection(_mtp->mainDcId());
			}
		}
		if (_mtpForKeysDestroy) {
			_mtpForKeysDestroy->restart();
		}
#endif
	}, _lifetime);
}

void Account::watchSessionChanges() {
#if 0 // mtp
	sessionChanges(
	) | rpl::start_with_next([=](Session *session) {
		if (!session && _mtp) {
			_mtp->setUserPhone(QString());
		}
	}, _lifetime);
#endif
}

uint64 Account::willHaveSessionUniqueId(MTP::Config *config) const {
	// See also Session::uniqueId.
	if (!_sessionUserId) {
		return 0;
	}
	return _sessionUserId.bare
		| (config && config->isTestMode() ? 0x0100'0000'0000'0000ULL : 0ULL);
}

#if 0 // mtp
void Account::createSession(
		const MTPUser &user,
		std::unique_ptr<SessionSettings> settings) {
	createSession(
		user,
		QByteArray(),
		0,
		settings ? std::move(settings) : std::make_unique<SessionSettings>());
}
#endif

void Account::createSession(
		UserId id,
		QByteArray serialized,
		int streamVersion,
		std::unique_ptr<SessionSettings> settings) {
	using namespace Tdb;
	DEBUG_LOG(("sessionUserSerialized.size: %1").arg(serialized.size()));
	QDataStream peekStream(serialized);
	const auto phone = Serialize::peekUserPhone(streamVersion, peekStream);

#if 0 // mtp
	const auto flags = MTPDuser::Flag::f_self | (phone.isEmpty()
		? MTPDuser::Flag()
		: MTPDuser::Flag::f_phone);

	createSession(
		MTP_user(
			MTP_flags(flags),
			MTP_long(base::take(_sessionUserId).bare),
			MTPlong(), // access_hash
			MTPstring(), // first_name
			MTPstring(), // last_name
			MTPstring(), // username
			MTP_string(phone),
			MTPUserProfilePhoto(),
			MTPUserStatus(),
			MTPint(), // bot_info_version
			MTPVector<MTPRestrictionReason>(),
			MTPstring(), // bot_inline_placeholder
			MTPstring(), // lang_code
			MTPEmojiStatus(),
			MTPVector<MTPUsername>(),
			MTPint(), // stories_max_id
			MTP_int(0), // color
			MTPlong()), // background_emoji_id
		serialized,
		streamVersion,
		std::move(settings));
#endif

	createSession(
		tl_user(
			tl_int53(base::take(_sessionUserId).bare),
			tl_string(), // first_name
			tl_string(), // last_name
			std::nullopt, // username
			tl_string(phone),
			tl_userStatusEmpty(),
			null, // profile_photo
			tl_int32(Data::DecideColorIndex(peerFromUser(id))),
			tl_int64(0), // background_custom_emoji_id
			null, // emoji_status
			tl_bool(true), // is_contact
			tl_bool(true), // is_mutual_contact
			tl_bool(false), // is_close_friend
			tl_bool(false), // is_verified
			tl_bool(false), // is_premium
			tl_bool(false), // is_support
			tl_string(), // restriction_reason
			tl_bool(false), // is_scam
			tl_bool(false), // is_fake,
			tl_bool(false), // has_active_stories
			tl_bool(false), // has_unread_active_stories
			tl_bool(true), // have_access
			tl_userTypeRegular(),
			tl_string(), // language_code
			tl_bool(false)), // added_to_attachment_menu
		serialized,
		streamVersion,
		std::move(settings));
}

void Account::createSession(
		const Tdb::TLuser &user,
		std::unique_ptr<SessionSettings> settings) {
	createSession(
		user,
		QByteArray(),
		0,
		settings ? std::move(settings) : std::make_unique<SessionSettings>());
}

#if 0 // mtp
void Account::createSession(
		const MTPUser &user,
		QByteArray serialized,
		int streamVersion,
		std::unique_ptr<SessionSettings> settings) {
	Expects(_mtp != nullptr);
	Expects(_session == nullptr);
	Expects(_sessionValue.current() == nullptr);

	_session = std::make_unique<Session>(this, user, std::move(settings));
	if (!serialized.isEmpty()) {
		local().readSelf(_session.get(), serialized, streamVersion);
	}
	_sessionValue = _session.get();

	Ensures(_session != nullptr);
}
#endif

void Account::createSession(
		const Tdb::TLuser &user,
		QByteArray serialized,
		int streamVersion,
		std::unique_ptr<SessionSettings> settings) {
	Expects(_session == nullptr);
	Expects(_sessionValue.current() == nullptr);

	_session = std::make_unique<Session>(this, user, std::move(settings));
	if (!serialized.isEmpty()) {
		local().readSelf(_session.get(), serialized, streamVersion);
	}
	_sessionValue = _session.get();

	Ensures(_session != nullptr);
}

void Account::destroySession(DestroyReason reason) {
	_storedSessionSettings.reset();
	_sessionUserId = 0;
	_sessionUserSerialized = {};
	if (!sessionExists()) {
		return;
	}

	_sessionValue = nullptr;

	if (reason == DestroyReason::LoggedOut) {
		_session->finishLogout();
	}
	_session = nullptr;
}

bool Account::sessionExists() const {
	return (_sessionValue.current() != nullptr);
}

Session &Account::session() const {
	Expects(sessionExists());

	return *_sessionValue.current();
}

Session *Account::maybeSession() const {
	return _sessionValue.current();
}

rpl::producer<Session*> Account::sessionValue() const {
	return _sessionValue.value();
}

rpl::producer<Session*> Account::sessionChanges() const {
	return _sessionValue.changes();
}

Tdb::Sender &Account::sender() const {
	Expects(_tdb != nullptr);

	return _tdb->sender();
}

Tdb::Options &Account::options() const {
	Expects(_tdb != nullptr);

	return _tdb->options();
}

QString Account::internalLinksDomain() const {
	return _internalLinksDomain;
}

bool Account::testMode() const {
	Expects(_tdb != nullptr);

	return _testMode;
}

#if 0 // mtp
rpl::producer<not_null<MTP::Instance*>> Account::mtpValue() const {
	return _mtpValue.value() | rpl::map([](MTP::Instance *instance) {
		return not_null{ instance };
	});
}

rpl::producer<not_null<MTP::Instance*>> Account::mtpMainSessionValue() const {
	return mtpValue() | rpl::map([=](not_null<MTP::Instance*> instance) {
		return instance->mainDcIdValue() | rpl::map_to(instance);
	}) | rpl::flatten_latest();
}

rpl::producer<MTPUpdates> Account::mtpUpdates() const {
	return _mtpUpdates.events();
}

rpl::producer<> Account::mtpNewSessionCreated() const {
	return _mtpNewSessionCreated.events();
}
#endif

void Account::setMtpMainDcId(MTP::DcId mainDcId) {
	Expects(!_mtp);

	_mtpFields.mainDcId = mainDcId;
}

void Account::setLegacyMtpKey(std::shared_ptr<MTP::AuthKey> key) {
	Expects(!_mtp);
	Expects(key != nullptr);

	_mtpFields.keys.push_back(std::move(key));
}

QByteArray Account::serializeMtpAuthorization() const {
#if 0 // mtp
	const auto serialize = [&](
			MTP::DcId mainDcId,
			const MTP::AuthKeysList &keys,
			const MTP::AuthKeysList &keysToDestroy) {
		const auto keysSize = [](auto &list) {
			const auto keyDataSize = MTP::AuthKey::Data().size();
			return sizeof(qint32)
				+ list.size() * (sizeof(qint32) + keyDataSize);
		};
		const auto writeKeys = [](
				QDataStream &stream,
				const MTP::AuthKeysList &keys) {
			stream << qint32(keys.size());
			for (const auto &key : keys) {
				stream << qint32(key->dcId());
				key->write(stream);
			}
		};

		auto result = QByteArray();
		// wide tag + userId + mainDcId
		auto size = 2 * sizeof(quint64) + sizeof(qint32);
		size += keysSize(keys) + keysSize(keysToDestroy);
		result.reserve(size);
		{
			QDataStream stream(&result, QIODevice::WriteOnly);
			stream.setVersion(QDataStream::Qt_5_1);

			const auto currentUserId = sessionExists()
				? session().userId()
				: UserId();
			stream
				<< quint64(kWideIdsTag)
				<< quint64(currentUserId.bare)
				<< qint32(mainDcId);
			writeKeys(stream, keys);
			writeKeys(stream, keysToDestroy);

			DEBUG_LOG(("MTP Info: Keys written, userId: %1, dcId: %2"
				).arg(currentUserId.bare
				).arg(mainDcId));
		}
		return result;
	};
	if (_mtp) {
		const auto keys = _mtp->getKeysForWrite();
		const auto keysToDestroy = _mtpForKeysDestroy
			? _mtpForKeysDestroy->getKeysForWrite()
			: MTP::AuthKeysList();
		return serialize(_mtp->mainDcId(), keys, keysToDestroy);
	}
	const auto &keys = _mtpFields.keys;
	const auto &keysToDestroy = _mtpKeysToDestroy;
	return serialize(_mtpFields.mainDcId, keys, keysToDestroy);
#endif
	const auto serialize = [&] {
		auto result = QByteArray();
		// wide tag + userId + legacyMainDcId + 2 * legacyKeysCount
		auto size = 2 * sizeof(quint64) + sizeof(qint32) * 3;
		result.reserve(size);
		{
			QDataStream stream(&result, QIODevice::WriteOnly);
			stream.setVersion(QDataStream::Qt_5_1);

			const auto currentUserId = sessionExists()
				? session().userId()
				: UserId();
			stream
				<< quint64(kWideIdsTag)
				<< quint64(currentUserId.bare)
				<< qint32(0) // legacyMainDcId
				<< qint32(0) // legacyKeysCount
				<< qint32(0); // legacyKeysToDestroyCount

			DEBUG_LOG(("Auth Info: Written, userId: %1"
				).arg(currentUserId.bare));
		}
		return result;
	};
	return serialize();
}

void Account::setSessionUserId(UserId userId) {
	Expects(!sessionExists());

	_sessionUserId = userId;
}

void Account::setSessionFromStorage(
		std::unique_ptr<SessionSettings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion) {
	Expects(!sessionExists());

	DEBUG_LOG(("sessionUserSerialized set: %1"
		).arg(selfSerialized.size()));

	_storedSessionSettings = std::move(data);
	_sessionUserSerialized = std::move(selfSerialized);
	_sessionUserStreamVersion = selfStreamVersion;
}

SessionSettings *Account::getSessionSettings() {
	if (_sessionUserId) {
		return _storedSessionSettings
			? _storedSessionSettings.get()
			: nullptr;
	} else if (const auto session = maybeSession()) {
		return &session->settings();
	}
	return nullptr;
}

void Account::setMtpAuthorization(const QByteArray &serialized) {
#if 0 // mtp
	Expects(!_mtp);
#endif

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	auto legacyUserId = Serialize::read<qint32>(stream);
	auto legacyMainDcId = Serialize::read<qint32>(stream);
	auto userId = quint64();
	auto mainDcId = qint32();
	if (((uint64(legacyUserId) << 32) | uint64(legacyMainDcId))
		== kWideIdsTag) {
		userId = Serialize::read<quint64>(stream);
		mainDcId = Serialize::read<qint32>(stream);
	} else {
		userId = legacyUserId;
		mainDcId = legacyMainDcId;
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("MTP Error: "
			"Could not read main fields from mtp authorization."));
		return;
	}

	setSessionUserId(userId);
	_mtpFields.mainDcId = mainDcId;

	const auto readKeys = [&](auto &keys) {
		const auto count = Serialize::read<qint32>(stream);
		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: "
				"Could not read keys count from mtp authorization."));
			return;
		}
		keys.reserve(count);
		for (auto i = 0; i != count; ++i) {
			const auto dcId = Serialize::read<qint32>(stream);
			const auto keyData = Serialize::read<MTP::AuthKey::Data>(stream);
			if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: "
					"Could not read key from mtp authorization."));
				return;
			}
			keys.push_back(std::make_shared<MTP::AuthKey>(MTP::AuthKey::Type::ReadFromFile, dcId, keyData));
		}
	};
	readKeys(_mtpFields.keys);
	readKeys(_mtpKeysToDestroy);
	LOG(("MTP Info: "
		"read keys, current: %1, to destroy: %2"
		).arg(_mtpFields.keys.size()
		).arg(_mtpKeysToDestroy.size()));
}

void Account::startMtp(std::unique_ptr<MTP::Config> config) {
	Expects(!_mtp);

	auto fields = base::take(_mtpFields);
	fields.config = std::move(config);
	fields.deviceModel = Platform::DeviceModelPretty();
	fields.systemVersion = Platform::SystemVersionPretty();
	_mtp = std::make_unique<MTP::Instance>(
		MTP::Instance::Mode::Normal,
		std::move(fields));

#if 0 // mtp
	const auto writingKeys = _mtp->lifetime().make_state<bool>(false);
	_mtp->writeKeysRequests(
	) | rpl::filter([=] {
		return !*writingKeys;
	}) | rpl::start_with_next([=] {
		*writingKeys = true;
		Ui::PostponeCall(_mtp.get(), [=] {
			local().writeMtpData();
			*writingKeys = false;
		});
	}, _mtp->lifetime());

	const auto writingConfig = _lifetime.make_state<bool>(false);
	rpl::merge(
		_mtp->config().updates(),
		_mtp->dcOptions().changed() | rpl::to_empty
	) | rpl::filter([=] {
		return !*writingConfig;
	}) | rpl::start_with_next([=] {
		*writingConfig = true;
		Ui::PostponeCall(_mtp.get(), [=] {
			local().writeMtpConfig();
			*writingConfig = false;
		});
	}, _lifetime);

	_mtpFields.mainDcId = _mtp->mainDcId();

	_mtp->setUpdatesHandler([=](const MTP::Response &message) {
		checkForUpdates(message) || checkForNewSession(message);
	});
	_mtp->setGlobalFailHandler([=](const MTP::Error &, const MTP::Response &) {
		if (const auto session = maybeSession()) {
			crl::on_main(session, [=] { logOut(); });
		}
	});
	_mtp->setStateChangedHandler([=](MTP::ShiftedDcId dc, int32 state) {
		if (dc == _mtp->mainDcId()) {
			Core::App().settings().proxy().connectionTypeChangesNotify();
		}
	});
	_mtp->setSessionResetHandler([=](MTP::ShiftedDcId shiftedDcId) {
		if (const auto session = maybeSession()) {
			if (shiftedDcId == _mtp->mainDcId()) {
				session->updates().getDifference();
			}
		}
	});

	if (!_mtpKeysToDestroy.empty()) {
		destroyMtpKeys(base::take(_mtpKeysToDestroy));
	}
#endif

	if (_sessionUserId) {
		createSession(
			_sessionUserId,
			base::take(_sessionUserSerialized),
			base::take(_sessionUserStreamVersion),
			(_storedSessionSettings
				? std::move(_storedSessionSettings)
				: std::make_unique<SessionSettings>()));
	}
	_storedSessionSettings = nullptr;

	if (const auto session = maybeSession()) {
		// Skip all pending self updates so that we won't local().writeSelf.
		session->changes().sendNotifications();
	}

#if 0 // mtp
	_mtpValue = _mtp.get();
#endif
}

#if 0 // mtp
bool Account::checkForUpdates(const MTP::Response &message) {
	auto updates = MTPUpdates();
	auto from = message.reply.constData();
	if (!updates.read(from, from + message.reply.size())) {
		return false;
	}
	_mtpUpdates.fire(std::move(updates));
	return true;
}

bool Account::checkForNewSession(const MTP::Response &message) {
	auto newSession = MTPNewSession();
	auto from = message.reply.constData();
	if (!newSession.read(from, from + message.reply.size())) {
		return false;
	}
	_mtpNewSessionCreated.fire({});
	return true;
}
#endif

void Account::logOut() {
	if (_loggingOut) {
		return;
	}
	_loggingOut = true;
#if 0 // mtp
	if (_mtp) {
		_mtp->logout([=] { loggedOut(); });
	} else {
		// We log out because we've forgotten passcode.
		loggedOut();
	}
#endif
	tdb().logout();
}

bool Account::loggingOut() const {
	return _loggingOut;
}

void Account::forcedLogOut() {
	if (sessionExists()) {
		resetAuthorizationKeys();
		loggedOut();
	}
}

void Account::loggedOut() {
#if 0 // mtp
	_loggingOut = false;
#endif
	Media::Player::mixer()->stopAndClear();
	destroySession(DestroyReason::LoggedOut);
	local().reset();
	cSetOtherOnline(0);
	_loggingOut = false;
}

#if 0 // mtp
void Account::destroyMtpKeys(MTP::AuthKeysList &&keys) {
	Expects(_mtp != nullptr);

	if (keys.empty()) {
		return;
	}
	if (_mtpForKeysDestroy) {
		_mtpForKeysDestroy->addKeysForDestroy(std::move(keys));
		local().writeMtpData();
		return;
	}
	auto destroyFields = MTP::Instance::Fields();

	destroyFields.mainDcId = MTP::Instance::Fields::kNoneMainDc;
	destroyFields.config = std::make_unique<MTP::Config>(_mtp->config());
	destroyFields.keys = std::move(keys);
	destroyFields.deviceModel = Platform::DeviceModelPretty();
	destroyFields.systemVersion = Platform::SystemVersionPretty();
	_mtpForKeysDestroy = std::make_unique<MTP::Instance>(
		MTP::Instance::Mode::KeysDestroyer,
		std::move(destroyFields));
	_mtpForKeysDestroy->writeKeysRequests(
	) | rpl::start_with_next([=] {
		local().writeMtpData();
	}, _mtpForKeysDestroy->lifetime());
	_mtpForKeysDestroy->allKeysDestroyed(
	) | rpl::start_with_next([=] {
		LOG(("MTP Info: all keys scheduled for destroy are destroyed."));
		crl::on_main(this, [=] {
			_mtpForKeysDestroy = nullptr;
			local().writeMtpData();
		});
	}, _mtpForKeysDestroy->lifetime());
}

void Account::suggestMainDcId(MTP::DcId mainDcId) {
	Expects(_mtp != nullptr);

	_mtp->suggestMainDcId(mainDcId);
	if (_mtpFields.mainDcId != MTP::Instance::Fields::kNotSetMainDc) {
		_mtpFields.mainDcId = mainDcId;
	}
}

void Account::destroyStaleAuthorizationKeys() {
	Expects(_mtp != nullptr);

	for (const auto &key : _mtp->getKeysForWrite()) {
		// Disable this for now.
		if (key->type() == MTP::AuthKey::Type::ReadFromFile) {
			_mtpKeysToDestroy = _mtp->getKeysForWrite();
			LOG(("MTP Info: destroying stale keys, count: %1"
				).arg(_mtpKeysToDestroy.size()));
			resetAuthorizationKeys();
			return;
		}
	}
}
#endif

void Account::setHandleLoginCode(Fn<void(QString)> callback) {
	_handleLoginCode = std::move(callback);
}

void Account::handleLoginCode(const QString &code) const {
	if (_handleLoginCode) {
		_handleLoginCode(code);
	}
}

void Account::setConnectionState(ConnectionState state) {
	if (_connectionState == state) {
		return;
	}
	_connectionState = state;
	Core::App().settings().proxy().connectionTypeChangesNotify();
}

Account::ConnectionState Account::connectionState() const {
	return _connectionState;
}

void Account::resetAuthorizationKeys() {
#if 0 // mtp
	Expects(_mtp != nullptr);

	{
		const auto old = base::take(_mtp);
		auto config = std::make_unique<MTP::Config>(old->config());
		startMtp(std::move(config));
	}
	local().writeMtpData();
#endif
	tdb().reset();
}

} // namespace Main
