/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_widget.h"

#include "intro/intro_start.h"
#include "intro/intro_phone.h"
#include "intro/intro_qr.h"
#include "intro/intro_code.h"
#include "intro/intro_signup.h"
#include "intro/intro_password_check.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "storage/localstorage.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_user.h"
#include "countries/countries_instance.h"
#include "ui/boxes/confirm_box.h"
#include "tdb/tdb_format_phone.h" // Tdb::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "boxes/abstract_box.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "mtproto/mtproto_dc_options.h"
#include "window/window_slide_animation.h"
#include "window/window_connecting_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/section_widget.h"
#include "base/platform/base_platform_info.h"
#include "api/api_text_entities.h"
#include "styles/style_layers.h"
#include "styles/style_intro.h"
#include "base/qt/qt_common_adapters.h"

#include "tdb/tdb_account.h"
#include "tdb/tdb_tl_scheme.h"

namespace Intro {
namespace {

using namespace ::Intro::details;
using namespace Tdb;

[[nodiscard]] QString ComputeNewAccountCountry() {
	if (const auto parent
		= Core::App().domain().maybeLastOrSomeAuthedAccount()) {
		if (const auto session = parent->maybeSession()) {
			const auto iso = Countries::Instance().countryISO2ByPhone(
				session->user()->phone());
			if (!iso.isEmpty()) {
				return iso;
			}
		}
	}
	return Platform::SystemCountry();
}

} // namespace

Widget::Widget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<Main::Account*> account,
	EnterPoint point)
: RpWidget(parent)
, _account(account)
, _api(account->sender())
, _data(details::Data{ .controller = controller })
, _nextStyle(&st::introNextButton)
, _back(this, object_ptr<Ui::IconButton>(this, st::introBackButton))
, _settings(
	this,
	object_ptr<Ui::RoundButton>(
		this,
		tr::lng_menu_settings(),
		st::defaultBoxButton))
, _next(
	this,
	object_ptr<Ui::RoundButton>(this, nullptr, *_nextStyle))
, _connecting(std::make_unique<Window::ConnectionState>(
		this,
		account,
		rpl::single(true))) {
	controller->setDefaultFloatPlayerDelegate(floatPlayerDelegate());

	getData()->country = ComputeNewAccountCountry();
#if 0 // mtp
	_account->mtpValue(
	) | rpl::start_with_next([=](not_null<MTP::Instance*> instance) {
		_api.emplace(instance);
		crl::on_main(this, [=] { createLanguageLink(); });
	}, lifetime());
	switch (point) {
	case EnterPoint::Start:
		getNearestDC();
		appendStep(new StartWidget(this, _account, getData()));
		break;
	case EnterPoint::Phone:
		appendStep(new PhoneWidget(this, _account, getData()));
		break;
	case EnterPoint::Qr:
		appendStep(new QrWidget(this, _account, getData()));
		break;
	default: Unexpected("Enter point in Intro::Widget::Widget.");
	}
#endif
	const auto stepType = [&] {
		switch (point) {
		case EnterPoint::Start: getNearestDC(); return StepType::Start;
		case EnterPoint::Phone: return StepType::Phone;
		case EnterPoint::Qr: return StepType::Qr;
		default: Unexpected("Enter point in Intro::Widget::Widget.");
		}
	}();
	const auto went = go(stepType);
	Assert(went);

	fixOrder();

	Lang::CurrentCloudManager().firstLanguageSuggestion(
	) | rpl::start_with_next([=] {
		createLanguageLink();
	}, lifetime());

#if 0 // mtp
	_account->mtpUpdates(
	) | rpl::start_with_next([=](const MTPUpdates &updates) {
		handleUpdates(updates);
	}, lifetime());
#endif

	_account->tdb().updates(
	) | rpl::start_with_next([=](const TLupdate &update) {
		handleUpdate(update);
	}, lifetime());

	_back->entity()->setClickedCallback([=] { backRequested(); });
	_back->hide(anim::type::instant);

	if (_changeLanguage) {
		_changeLanguage->finishAnimating();
	}

	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	show();
	showControls();
	getStep()->showFast();
	setInnerFocus();

	cSetPasswordRecovered(false);

	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker checker;
		checker.start();
		rpl::merge(
			rpl::single(rpl::empty),
			checker.isLatest(),
			checker.failed(),
			checker.ready()
		) | rpl::start_with_next([=] {
			checkUpdateStatus();
		}, lifetime());
	}
}

rpl::producer<> Widget::showSettingsRequested() const {
	return _settings->entity()->clicks() | rpl::to_empty;
}

not_null<Media::Player::FloatDelegate*> Widget::floatPlayerDelegate() {
	return static_cast<Media::Player::FloatDelegate*>(this);
}

auto Widget::floatPlayerSectionDelegate()
-> not_null<Media::Player::FloatSectionDelegate*> {
	return static_cast<Media::Player::FloatSectionDelegate*>(this);
}

not_null<Ui::RpWidget*> Widget::floatPlayerWidget() {
	return this;
}

void Widget::floatPlayerToggleGifsPaused(bool paused) {
}

auto Widget::floatPlayerGetSection(Window::Column column)
-> not_null<Media::Player::FloatSectionDelegate*> {
	return this;
}

void Widget::floatPlayerEnumerateSections(Fn<void(
		not_null<Media::Player::FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) {
	callback(this, Window::Column::Second);
}

bool Widget::floatPlayerIsVisible(not_null<HistoryItem*> item) {
	return false;
}

void Widget::floatPlayerDoubleClickEvent(not_null<const HistoryItem*> item) {
	getData()->controller->invokeForSessionController(
		&item->history()->peer->session().account(),
		item->history()->peer,
		[&](not_null<Window::SessionController*> controller) {
			controller->showMessage(item);
		});
}

QRect Widget::floatPlayerAvailableRect() {
	return mapToGlobal(rect());
}

bool Widget::floatPlayerHandleWheelEvent(QEvent *e) {
	return false;
}

void Widget::refreshLang() {
	_changeLanguage.destroy();
	createLanguageLink();
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

#if 0 // mtp
void Widget::handleUpdates(const MTPUpdates &updates) {
	updates.match([&](const MTPDupdateShort &data) {
		handleUpdate(data.vupdate());
	}, [&](const MTPDupdates &data) {
		for (const auto &update : data.vupdates().v) {
			handleUpdate(update);
		}
	}, [&](const MTPDupdatesCombined &data) {
		for (const auto &update : data.vupdates().v) {
			handleUpdate(update);
		}
	}, [](const auto &) {});
}

void Widget::handleUpdate(const MTPUpdate &update) {
	update.match([&](const MTPDupdateDcOptions &data) {
		_account->mtp().dcOptions().addFromList(data.vdc_options());
	}, [&](const MTPDupdateConfig &data) {
		_account->mtp().requestConfig();
	}, [&](const MTPDupdateServiceNotification &data) {
		const auto text = TextWithEntities{
			qs(data.vmessage()),
			Api::EntitiesFromMTP(nullptr, data.ventities().v)
		};
		Ui::show(Ui::MakeInformBox(text));
	}, [](const auto &) {});
}
#endif

void Widget::handleUpdate(const TLupdate &update) {
	update.match([&](const TLDupdateAuthorizationState &data) {
		handleAuthorizationState(data.vauthorization_state());
	}, [&](const TLDupdateChatFolders &data) {
		if (getData()->waitingForFilters) {
			getData()->filtersUpdate = std::make_shared<TLupdate>(update);
			getStep()->filtersReceived();
		}
	}, [](const auto &) {
	});
}

void Widget::handleAuthorizationState(const TLauthorizationState &state) {
	state.match([&](const TLDauthorizationStateWaitTdlibParameters &) {
		Unexpected("authorizationStateWaitTdlibParameters in client code.");
	}, [&](const TLDauthorizationStateWaitPhoneNumber &) {
	}, [&](const TLDauthorizationStateWaitCode &data) {
		fillCodeInfo(data.vcode_info());
	}, [&](const TLDauthorizationStateWaitOtherDeviceConfirmation &data) {
		getData()->qrLink = data.vlink().v;
	}, [&](const TLDauthorizationStateWaitRegistration &data) {
		fillTerms(data.vterms_of_service());
	}, [&](const TLDauthorizationStateWaitPassword &data) {
		getData()->pwdState.hasPassword = true;
		getData()->pwdState.hasRecovery
			= data.vhas_recovery_email_address().v;
		getData()->pwdState.hint = data.vpassword_hint().v;
		getData()->pwdEmailPattern
			= data.vrecovery_email_address_pattern().v;
		getData()->pwdState.notEmptyPassport = data.vhas_passport_data().v;
	}, [&](const TLDauthorizationStateReady &) {
		getData()->waitingForFilters = true;
	}, [](const TLDauthorizationStateLoggingOut &) {
	}, [](const TLDauthorizationStateClosing &) {
	}, [](const TLDauthorizationStateClosed &) {
	}, [](const TLDauthorizationStateWaitEmailAddress &) {
		LOG(("Tdb Error: Should not StateWaitEmailAddress in TDesktop."));
	}, [](const TLDauthorizationStateWaitEmailCode &) {
		LOG(("Tdb Error: Should not StateWaitEmailCode in TDesktop."));
	});
	if (!getStep()->applyState(state)) {
		getStep()->jumpByState(state);
	}
}

void Widget::fillCodeInfo(const TLauthenticationCodeInfo &info) {
	info.match([&](const TLDauthenticationCodeInfo &data) {
		getData()->phone = data.vphone_number().v;

		auto codeLength = 0;
		auto codeByTelegram = false;
		auto codeByFragmentUrl = QString();
		data.vtype().match([&](
				const TLDauthenticationCodeTypeTelegramMessage &data) {
			codeLength = data.vlength().v;
			codeByTelegram = true;
		}, [&](const TLDauthenticationCodeTypeSms &data) {
			codeLength = data.vlength().v;
		}, [&](const TLDauthenticationCodeTypeCall &data) {
			codeLength = data.vlength().v;
		}, [&](const TLDauthenticationCodeTypeFragment &data) {
			codeLength = data.vlength().v;
			codeByFragmentUrl = data.vurl().v;
		}, [&](const TLDauthenticationCodeTypeMissedCall &) {
			LOG(("Tdb Error: authenticationCodeTypeMissedCall."));
		}, [&](const TLDauthenticationCodeTypeFlashCall &) {
			LOG(("Tdb Error: authenticationCodeTypeFlashCall."));
		}, [&](const TLDauthenticationCodeTypeFirebaseAndroid &data) {
			LOG(("Tdb Error: authenticationCodeTypeFirebaseAndroid."));
		}, [&](const TLDauthenticationCodeTypeFirebaseIos &data) {
			LOG(("Tdb Error: authenticationCodeTypeFirebaseIos."));
		});

		const auto currentType = data.vtype().type();
		getData()->codeLength = codeLength;
		getData()->codeByTelegram = codeByTelegram;
		getData()->codeByFragmentUrl = codeByFragmentUrl;
		if (const auto next = data.vnext_type()) {
			const auto type = next->type();
			getData()->callStatus
				= (type == id_authenticationCodeTypeCall
					? CallStatus::Waiting
					: CallStatus::Disabled);
			getData()->callTimeout
				= (type == id_authenticationCodeTypeCall
					? data.vtimeout().v
					: 0);
		} else {
			getData()->callStatus = CallStatus::Disabled;
			getData()->callTimeout = 0;
		}
	});
}

void Widget::fillTerms(const TLtermsOfService &terms) {
	getData()->termsLock = terms.match([&](const TLDtermsOfService &data) {
		return Window::TermsLock::FromTL(nullptr, data);
	});
}

void Widget::createLanguageLink() {
	if (_changeLanguage
		|| Core::App().domain().maybeLastOrSomeAuthedAccount()) {
		return;
	}

	const auto createLink = [=](
			const QString &text,
			const QString &languageId) {
		_changeLanguage.create(
			this,
			object_ptr<Ui::LinkButton>(this, text));
		_changeLanguage->hide(anim::type::instant);
		_changeLanguage->entity()->setClickedCallback([=] {
			Lang::CurrentCloudManager().switchToLanguage(languageId);
		});
		_changeLanguage->toggle(
			!_resetAccount && !_terms && _nextShown,
			anim::type::normal);
		updateControlsGeometry();
	};

	const auto currentId = Lang::LanguageIdOrDefault(Lang::Id());
	const auto defaultId = Lang::DefaultLanguageId();
	const auto suggested = Lang::CurrentCloudManager().suggestedLanguage();
	if (currentId != defaultId) {
		createLink(
			Lang::GetOriginalValue(tr::lng_switch_to_this.base),
			defaultId);
	} else if (!suggested.isEmpty() && suggested != currentId/* && _api*/) {
		_api.request(TLgetLanguagePackStrings(
			tl_string(suggested),
			tl_vector<TLstring>(1, tl_string("lng_switch_to_this"))
		)).done([=](const TLlanguagePackStrings &result) {
			const auto strings = Lang::Instance::ParseStrings(result);
			const auto i = strings.find(tr::lng_switch_to_this.base);
			if (i != strings.end()) {
				createLink(i->second, suggested);
			}
		}).send();
	}
}

void Widget::checkUpdateStatus() {
	Expects(!Core::UpdaterDisabled());

	if (Core::UpdateChecker().state() == Core::UpdateChecker::State::Ready) {
		if (_update) return;
		_update.create(
			this,
			object_ptr<Ui::RoundButton>(
				this,
				tr::lng_menu_update(),
				st::defaultBoxButton));
		if (!_showAnimation) {
			_update->setVisible(true);
		}
		const auto stepHasCover = getStep()->hasCover();
		_update->toggle(!stepHasCover, anim::type::instant);
		_update->entity()->setClickedCallback([] {
			Core::checkReadyUpdate();
			Core::Restart();
		});
	} else {
		if (!_update) return;
		_update.destroy();
	}
	updateControlsGeometry();
}

void Widget::setInnerFocus() {
	if (getStep()->animating()) {
		setFocus();
	} else {
		getStep()->setInnerFocus();
	}
}

#if 0 // mtp
void Widget::historyMove(StackAction action, Animate animate) {
	Expects(_stepHistory.size() > 1);

	if (getStep()->animating()) {
		return;
	}

	auto wasStep = getStep((action == StackAction::Back) ? 0 : 1);
	if (action == StackAction::Back) {
		_stepHistory.pop_back();
		wasStep->cancelled();
	} else if (action == StackAction::Replace) {
		_stepHistory.erase(_stepHistory.end() - 2);
	}
#endif
void Widget::historyMove(
		Step *wasStep,
		std::vector<std::unique_ptr<Step>>::iterator nowStep) {
	_back->raise();
	_settings->raise();
	if (_update) {
		_update->raise();
	}
	_connecting->raise();

	for (auto i = nowStep + 1; i != end(_stepHistory); ++i) {
		(*i)->cancelled();
	}
	if (!wasStep || wasStep->animating()) {
		_stepHistory.erase(nowStep + 1, end(_stepHistory));
		showControls();
		getStep()->showFast();
		setInnerFocus();
		return;
	}

	const auto wasType = wasStep ? wasStep->type() : StepType::Start;
	const auto nowType = (*nowStep)->type();
	const auto animate = ((nowType >= wasType)
		|| (wasType == StepType::Qr && nowType == StepType::Phone))
		? Animate::Forward
		: Animate::Back;

	if (_resetAccount) {
		hideAndDestroy(std::exchange(_resetAccount, { nullptr }));
	}
	if (_terms) {
		hideAndDestroy(std::exchange(_terms, { nullptr }));
	}
	{
		getStep()->nextButtonStyle(
		) | rpl::start_with_next([=](const style::RoundButton *st) {
			const auto nextStyle = st ? st : &st::introNextButton;
			if (_nextStyle != nextStyle) {
				_nextStyle = nextStyle;
				const auto wasShown = _next->toggled();
				_next.destroy();
				_next.create(
					this,
					object_ptr<Ui::RoundButton>(this, nullptr, *nextStyle));
				showControls();
				updateControlsGeometry();
				_next->toggle(wasShown, anim::type::instant);
			}
		}, _next->lifetime());
	}

#if 0 // mtp
	getStep()->finishInit();
	getStep()->prepareShowAnimated(wasStep);
	if (wasStep->hasCover() != getStep()->hasCover()) {
#endif
	(*nowStep)->finishInit();
	(*nowStep)->prepareShowAnimated(wasStep);
	if (wasStep->hasCover() != (*nowStep)->hasCover()) {
		_nextTopFrom = wasStep->contentTop() + st::introNextTop;
		_controlsTopFrom = wasStep->hasCover() ? st::introCoverHeight : 0;
		_coverShownAnimation.start(
			[this] { updateControlsGeometry(); },
			0.,
			1.,
			st::introCoverDuration,
			wasStep->hasCover() ? anim::linear : anim::easeOutCirc);
	}

	_stepLifetime.destroy();
#if 0 // mtp
	if (action == StackAction::Forward || action == StackAction::Replace) {
		wasStep->finished();
	}
	if (action == StackAction::Back || action == StackAction::Replace) {
		delete base::take(wasStep);
	}
#endif
	if (wasStep) {
		wasStep->finished();
	}
	_stepHistory.erase(nowStep + 1, end(_stepHistory));
	_back->toggle(getStep()->hasBack(), anim::type::normal);

	auto stepHasCover = getStep()->hasCover();
	_settings->toggle(!stepHasCover, anim::type::normal);
	if (_update) {
		_update->toggle(!stepHasCover, anim::type::normal);
	}
	setupNextButton();
	if (_resetAccount) _resetAccount->show(anim::type::normal);
	if (_terms) _terms->show(anim::type::normal);
	getStep()->showAnimated(animate);
	fixOrder();
}

void Widget::hideAndDestroy(object_ptr<Ui::FadeWrap<Ui::RpWidget>> widget) {
	const auto weak = Ui::MakeWeak(widget.data());
	widget->hide(anim::type::normal);
	widget->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (!shown && weak) {
			weak->deleteLater();
		}
	}, widget->lifetime());
}

void Widget::fixOrder() {
	_next->raise();
	if (_update) _update->raise();
	if (_changeLanguage) _changeLanguage->raise();
	_settings->raise();
	_back->raise();
	floatPlayerRaiseAll();
	_connecting->raise();
}

template <typename WidgetType>
std::unique_ptr<details::Step> Widget::makeStep() {
	return std::make_unique<WidgetType>(this, _account, getData());
}

bool Widget::go(StepType type) {
	getData()->madeInitialJumpToStep = true;

	if (type == StepType::Start) {
		if (const auto parent
			= Core::App().domain().maybeLastOrSomeAuthedAccount()) {
			Core::App().domain().activate(parent);
			return false;
		}
	} else if (!_stepHistory.empty() && getStep()->type() == type) {
		return false;
	}
	const auto wasStep = _stepHistory.empty() ? nullptr : getStep().get();
	auto nowStep = begin(_stepHistory);
	for (; nowStep != end(_stepHistory); ++nowStep) {
		if ((*nowStep)->type() == type) {
			break;
		}
	}
	auto saved = std::unique_ptr<Step>();
	if (nowStep == end(_stepHistory)) {
		appendStep([&]() -> std::unique_ptr<Step> {
			switch (type) {
			case StepType::Start: return makeStep<StartWidget>();
			case StepType::Phone: return makeStep<PhoneWidget>();
			case StepType::Qr: return makeStep<QrWidget>();
			case StepType::Code: return makeStep<CodeWidget>();
			case StepType::Password: return makeStep<PasswordCheckWidget>();
			case StepType::SignUp: return makeStep<SignupWidget>();
			}
			Unexpected("Type in Intro::Widget::go.");
		}());
		if (type == StepType::Qr || type == StepType::Phone) {
			while (_stepHistory.size() > 1
				&& (*(_stepHistory.end() - 2))->type() != StepType::Start) {
				if ((_stepHistory.end() - 2)->get() == wasStep) {
					saved = std::move(*(_stepHistory.end() - 2));
				}
				_stepHistory.erase(_stepHistory.end() - 2);
			}
		}
		nowStep = (_stepHistory.end() - 1);
	}
	historyMove(wasStep, nowStep);
	return true;
}

#if 0 // mtp
void Widget::moveToStep(Step *step, StackAction action, Animate animate) {
	appendStep(step);
	_back->raise();
	_settings->raise();
	if (_update) {
		_update->raise();
	}
	_connecting->raise();

	historyMove(action, animate);
}

void Widget::appendStep(Step *step) {
	_stepHistory.push_back(step);
	step->setGeometry(rect());
	step->setGoCallback([=](Step *step, StackAction action, Animate animate) {
		if (action == StackAction::Back) {
			backRequested();
		} else {
			moveToStep(step, action, animate);
		}
	});
	step->setShowResetCallback([=] {
		showResetButton();
	});
	step->setShowTermsCallback([=] {
		showTerms();
	});
	step->setCancelNearestDcCallback([=] {
		_api.request(base::take(_nearestDcRequestId)).cancel();
	});
	step->setAcceptTermsCallback([=](Fn<void()> callback) {
		acceptTerms(callback);
	});
}
#endif

void Widget::appendStep(std::unique_ptr<Step> step) {
	const auto raw = step.get();
	_stepHistory.push_back(std::move(step));
	raw->setGeometry(rect());
	raw->setGoCallback([=](StepType type) {
		go(type);
	});
	raw->setShowResetCallback([=] {
		showResetButton();
	});
	raw->setShowTermsCallback([=] {
		showTerms();
	});
	raw->setCancelNearestDcCallback([=] {
		_api.request(base::take(_nearestDcRequestId)).cancel();
	});
	raw->setAcceptTermsCallback([=](Fn<void()> callback) {
		acceptTerms(callback);
	});
}

void Widget::showResetButton() {
	if (!_resetAccount) {
		auto entity = object_ptr<Ui::RoundButton>(
			this,
			tr::lng_signin_reset_account(),
			st::introResetButton);
		_resetAccount.create(this, std::move(entity));
		_resetAccount->hide(anim::type::instant);
		_resetAccount->entity()->setClickedCallback([this] { resetAccount(); });
		updateControlsGeometry();
	}
	_resetAccount->show(anim::type::normal);
	if (_changeLanguage) {
		_changeLanguage->hide(anim::type::normal);
	}
}

void Widget::showTerms() {
	if (getData()->termsLock.text.text.isEmpty()) {
		_terms.destroy();
	} else if (!_terms) {
		auto entity = object_ptr<Ui::FlatLabel>(
			this,
			tr::lng_terms_signup(
				lt_link,
				tr::lng_terms_signup_link() | Ui::Text::ToLink(),
				Ui::Text::WithEntities),
			st::introTermsLabel);
		_terms.create(this, std::move(entity));
		_terms->entity()->overrideLinkClickHandler([=] {
			showTerms(nullptr);
		});
		updateControlsGeometry();
		_terms->hide(anim::type::instant);
	}
	if (_changeLanguage) {
		_changeLanguage->toggle(
			!_terms && !_resetAccount && _nextShown,
			anim::type::normal);
	}
}

void Widget::acceptTerms(Fn<void()> callback) {
	showTerms(callback);
}

void Widget::resetAccount() {
	if (_resetRequest/* || !_api*/) {
		return;
	}

	const auto callback = crl::guard(this, [this] {
		if (_resetRequest) {
			return;
		}
		_resetRequest = _api.request(TLdeleteAccount(
			tl_string("Forgot password"),
			tl_string() // password
		)).done([=] {
			_resetRequest = 0;

			getData()->controller->hideLayer();
			if (getData()->phone.isEmpty()) {
#if 0 // mtp
				moveToStep(
					new QrWidget(this, _account, getData()),
					StackAction::Replace,
					Animate::Back);
			} else {
				moveToStep(
					new SignupWidget(this, _account, getData()),
					StackAction::Replace,
					Animate::Forward);
#endif
				go(StepType::Qr);
			} else {
				go(StepType::SignUp);
			}
		}).fail([=](const Error &error) {
			_resetRequest = 0;

			const auto &type = error.message;
			if (type.startsWith(u"2FA_CONFIRM_WAIT_"_q)) {
				const auto seconds = base::StringViewMid(
					type,
					qstr("2FA_CONFIRM_WAIT_").size()).toInt();
				const auto days = (seconds + 59) / 86400;
				const auto hours = ((seconds + 59) % 86400) / 3600;
				const auto minutes = ((seconds + 59) % 3600) / 60;
				auto when = tr::lng_minutes(tr::now, lt_count, minutes);
				if (days > 0) {
					const auto daysCount = tr::lng_days(
						tr::now,
						lt_count,
						days);
					const auto hoursCount = tr::lng_hours(
						tr::now,
						lt_count,
						hours);
					when = tr::lng_signin_reset_in_days(
						tr::now,
						lt_days_count,
						daysCount,
						lt_hours_count,
						hoursCount,
						lt_minutes_count,
						when);
				} else if (hours > 0) {
					const auto hoursCount = tr::lng_hours(
						tr::now,
						lt_count,
						hours);
					when = tr::lng_signin_reset_in_hours(
						tr::now,
						lt_hours_count,
						hoursCount,
						lt_minutes_count,
						when);
				}
				Ui::show(Ui::MakeInformBox(tr::lng_signin_reset_wait(
					tr::now,
					lt_phone_number,
					Tdb::FormatPhone(getData()->phone),
					lt_when,
					when)));
			} else if (type == u"2FA_RECENT_CONFIRM"_q) {
				Ui::show(Ui::MakeInformBox(
					tr::lng_signin_reset_cancelled()));
			} else {
				getData()->controller->hideLayer();
				getStep()->showError(rpl::single(Lang::Hard::ServerError()));
			}
		}).send();
	});

	Ui::show(Ui::MakeConfirmBox({
		.text = tr::lng_signin_sure_reset(),
		.confirmed = callback,
		.confirmText = tr::lng_signin_reset(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void Widget::getNearestDC() {
	_nearestDcRequestId = _api.request(TLgetCountryCode(
	)).done([=](const TLtext &result) {
		_nearestDcRequestId = 0;
		result.match([&](const TLDtext &data) {
			const auto nearestCountry = data.vtext().v;
			if (getData()->country != nearestCountry) {
				getData()->country = nearestCountry;
				getData()->updated.fire({});
			}
		});
	}).send();

#if 0 // mtp
	if (!_api) {
		return;
	}
	_nearestDcRequestId = _api->request(MTPhelp_GetNearestDc(
	)).done([=](const MTPNearestDc &result) {
		_nearestDcRequestId = 0;
		const auto &nearest = result.c_nearestDc();
		DEBUG_LOG(("Got nearest dc, country: %1, nearest: %2, this: %3"
			).arg(qs(nearest.vcountry())
			).arg(nearest.vnearest_dc().v
			).arg(nearest.vthis_dc().v));
		_account->suggestMainDcId(nearest.vnearest_dc().v);
		const auto nearestCountry = qs(nearest.vcountry());
		if (getData()->country != nearestCountry) {
			getData()->country = nearestCountry;
			getData()->updated.fire({});
		}
	}).send();
#endif
}

void Widget::showTerms(Fn<void()> callback) {
	if (getData()->termsLock.text.text.isEmpty()) {
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	const auto box = Ui::show(callback
		? Box<Window::TermsBox>(
			getData()->termsLock,
			tr::lng_terms_agree(),
			tr::lng_terms_decline())
		: Box<Window::TermsBox>(
			getData()->termsLock.text,
			tr::lng_box_ok(),
			nullptr));

	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);

	box->agreeClicks(
	) | rpl::start_with_next([=] {
		if (callback) {
			callback();
		}
		if (box) {
			box->closeBox();
		}
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		const auto box = Ui::show(Box<Window::TermsBox>(
			TextWithEntities{ tr::lng_terms_signup_sorry(tr::now) },
			tr::lng_intro_finish(),
			tr::lng_terms_decline()));
		box->agreeClicks(
		) | rpl::start_with_next([=] {
			if (weak) {
				showTerms(callback);
			}
		}, box->lifetime());
		box->cancelClicks(
		) | rpl::start_with_next([=] {
			if (box) {
				box->closeBox();
			}
		}, box->lifetime());
	}, box->lifetime());
}

void Widget::showControls() {
	getStep()->show();
	setupNextButton();
	_next->toggle(_nextShown, anim::type::instant);
	_nextShownAnimation.stop();
	_connecting->setForceHidden(false);
	auto hasCover = getStep()->hasCover();
	_settings->toggle(!hasCover, anim::type::instant);
	if (_update) {
		_update->toggle(!hasCover, anim::type::instant);
	}
	if (_changeLanguage) {
		_changeLanguage->toggle(
			!_resetAccount && !_terms && _nextShown,
			anim::type::instant);
	}
	if (_terms) {
		_terms->show(anim::type::instant);
	}
	_back->toggle(getStep()->hasBack(), anim::type::instant);
}

void Widget::setupNextButton() {
	_next->entity()->setClickedCallback([=] { getStep()->submit(); });
	_next->entity()->setTextTransform(
		Ui::RoundButton::TextTransform::NoTransform);

	_next->entity()->setText(getStep()->nextButtonText(
	) | rpl::filter([](const QString &text) {
		return !text.isEmpty();
	}));
	getStep()->nextButtonText(
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}) | rpl::filter([=](bool visible) {
		return visible != _nextShown;
	}) | rpl::start_with_next([=](bool visible) {
		_next->toggle(visible, anim::type::normal);
		_nextShown = visible;
		if (_changeLanguage) {
			_changeLanguage->toggle(
				!_resetAccount && !_terms && _nextShown,
				anim::type::normal);
		}
		_nextShownAnimation.start(
			[=] { updateControlsGeometry(); },
			_nextShown ? 0. : 1.,
			_nextShown ? 1. : 0.,
			st::slideDuration);
	}, _stepLifetime);
}

void Widget::hideControls() {
	getStep()->hide();
	_next->hide(anim::type::instant);
	_connecting->setForceHidden(true);
	_settings->hide(anim::type::instant);
	if (_update) _update->hide(anim::type::instant);
	if (_changeLanguage) _changeLanguage->hide(anim::type::instant);
	if (_terms) _terms->hide(anim::type::instant);
	_back->hide(anim::type::instant);
}

void Widget::showAnimated(QPixmap oldContentCache, bool back) {
	_showAnimation = nullptr;

	showControls();
	floatPlayerHideAll();
	auto newContentCache = Ui::GrabWidget(this);
	hideControls();
	floatPlayerShowVisible();

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setDirection(back
		? Window::SlideDirection::FromLeft
		: Window::SlideDirection::FromRight);
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] { showFinished(); });
	_showAnimation->setPixmaps(oldContentCache, newContentCache);
	_showAnimation->start();

	show();
}

void Widget::showFinished() {
	_showAnimation = nullptr;

	showControls();
	getStep()->activate();
}

void Widget::paintEvent(QPaintEvent *e) {
	const auto trivial = (rect() == e->rect());
	setMouseTracking(true);

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (_showAnimation) {
		_showAnimation->paintContents(p);
		return;
	}
	p.fillRect(e->rect(), st::windowBg);
}

void Widget::resizeEvent(QResizeEvent *e) {
	if (_stepHistory.empty()) {
		return;
	}
#if 0 // mtp
	for (const auto step : _stepHistory) {
#endif
	for (const auto &step : _stepHistory) {
		step->setGeometry(rect());
	}

	updateControlsGeometry();
	floatPlayerAreaUpdated();
}

void Widget::updateControlsGeometry() {
	const auto skip = st::introSettingsSkip;
	const auto shown = _coverShownAnimation.value(1.);

	const auto controlsTop = anim::interpolate(
		_controlsTopFrom,
		getStep()->hasCover() ? st::introCoverHeight : 0,
		shown);
	_settings->moveToRight(skip, controlsTop + skip);
	if (_update) {
		_update->moveToRight(
			skip + _settings->width() + skip,
			_settings->y());
	}
	_back->moveToLeft(0, controlsTop);

	auto nextTopTo = getStep()->contentTop() + st::introNextTop;
	auto nextTop = anim::interpolate(_nextTopFrom, nextTopTo, shown);
	const auto shownAmount = _nextShownAnimation.value(_nextShown ? 1. : 0.);
	const auto realNextTop = anim::interpolate(
		nextTop + st::introNextSlide,
		nextTop,
		shownAmount);
	_next->moveToLeft((width() - _next->width()) / 2, realNextTop);
	getStep()->setShowAnimationClipping(shownAmount > 0
		? QRect(0, 0, width(), realNextTop)
		: QRect());
	if (_changeLanguage) {
		_changeLanguage->moveToLeft(
			(width() - _changeLanguage->width()) / 2,
			_next->y() + _next->height() + _changeLanguage->height());
	}
	if (_resetAccount) {
		_resetAccount->moveToLeft(
			(width() - _resetAccount->width()) / 2,
			height() - st::introResetBottom - _resetAccount->height());
	}
	if (_terms) {
		_terms->moveToLeft(
			(width() - _terms->width()) / 2,
			height() - st::introTermsBottom - _terms->height());
	}
}

void Widget::keyPressEvent(QKeyEvent *e) {
	if (_showAnimation || getStep()->animating()) return;

	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		if (getStep()->hasBack()) {
			backRequested();
		}
	} else if (e->key() == Qt::Key_Enter
		|| e->key() == Qt::Key_Return
		|| e->key() == Qt::Key_Space) {
		getStep()->submit();
	}
}

void Widget::backRequested() {
	if (_stepHistory.size() > 1) {
#if 0 // mtp
		historyMove(StackAction::Back, Animate::Back);
	} else if (const auto parent
		= Core::App().domain().maybeLastOrSomeAuthedAccount()) {
		Core::App().domain().activate(parent);
	} else {
		moveToStep(
			new StartWidget(this, _account, getData()),
			StackAction::Replace,
			Animate::Back);
#endif
		const auto to = _stepHistory[_stepHistory.size() - 2]->type();
		if (to == StepType::Start
			&& _stepHistory.back()->type() == StepType::Code) {
			go(StepType::Phone);
		} else {
			go(to);
		}
	} else {
		go(StepType::Start);
	}
}

Widget::~Widget() {
#if 0 // mtp
	for (auto step : base::take(_stepHistory)) {
		delete step;
	}
#endif
	base::take(_stepHistory);
}

} // namespace Intro
