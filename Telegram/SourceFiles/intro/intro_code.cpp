/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_code.h"

#include "lang/lang_keys.h"
#include "intro/intro_signup.h"
#include "intro/intro_password_check.h"
#include "boxes/abstract_box.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/masked_input_field.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/boxes/confirm_box.h"
#include "main/main_account.h"
#include "mtproto/mtp_instance.h"
#include "styles/style_intro.h"

#include "tdb/tdb_format_phone.h" // Tdb::FormatPhone
#include "tdb/tdb_tl_scheme.h"

namespace Intro {
namespace details {
namespace {

using namespace Tdb;

} // namespace

CodeInput::CodeInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder)
: Ui::MaskedInputField(parent, st, std::move(placeholder)) {
}

void CodeInput::setDigitsCountMax(int digitsCount) {
	_digitsCountMax = digitsCount;
}

void CodeInput::correctValue(const QString &was, int wasCursor, QString &now, int &nowCursor) {
	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (now[i].isDigit()) {
			++digitCount;
		}
	}
	accumulate_min(digitCount, _digitsCountMax);
	auto strict = (digitCount == _digitsCountMax);

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		QChar ch(now[i]);
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			newText += ch;
			if (strict && !digitCount) {
				break;
			}
		} else if (ch == '-') {
			newText += ch;
		}
		if (i == oldPos) {
			newPos = newText.length();
		}
	}
	if (newPos < 0 || newPos > newText.size()) {
		newPos = newText.size();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	if (newPos != nowCursor) {
		nowCursor = newPos;
		setCursorPosition(nowCursor);
	}
}

CodeWidget::CodeWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _noTelegramCode(this, tr::lng_code_no_telegram(tr::now), st::introLink)
, _code(this, st::introCode, tr::lng_code_ph())
, _callTimer([=] { sendCall(); })
, _callStatus(getData()->callStatus)
, _callTimeout(getData()->callTimeout)
, _callLabel(this, st::introDescription) {
#if 0 // mtp
, _checkRequestTimer([=] { checkRequest(); }) {
#endif
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	connect(_code, &CodeInput::changed, [=] { codeChanged(); });
	_noTelegramCode->addClickHandler([=] { noTelegramCode(); });

	_code->setDigitsCountMax(getData()->codeLength);

	updateDescText();
	setTitleText(_isFragment.value(
	) | rpl::map([=](bool isFragment) {
		return !isFragment
			? rpl::single(Tdb::FormatPhone(getData()->phone))
			: tr::lng_intro_fragment_title();
	}) | rpl::flatten_latest());

	account->setHandleLoginCode([=](const QString &code) {
		_code->setText(code);
		submitCode();
	});
}

void CodeWidget::refreshLang() {
	if (_noTelegramCode) {
		_noTelegramCode->setText(tr::lng_code_no_telegram(tr::now));
	}
	updateDescText();
	updateControlsGeometry();
}

int CodeWidget::errorTop() const {
	return contentTop() + st::introErrorBelowLinkTop;
}

void CodeWidget::updateDescText() {
	const auto byTelegram = getData()->codeByTelegram;
	const auto isFragment = !getData()->codeByFragmentUrl.isEmpty();
	_isFragment = isFragment;
	setDescriptionText(
		isFragment
			? tr::lng_intro_fragment_about(
				lt_phone_number,
				rpl::single(TextWithEntities{
#if 0 // mtp
					.text = Ui::FormatPhone(getData()->phone)
#endif
					.text = Tdb::FormatPhone(getData()->phone)
				}),
				Ui::Text::RichLangValue)
			: (byTelegram ? tr::lng_code_from_telegram : tr::lng_code_desc)(
				Ui::Text::RichLangValue));
	if (getData()->codeByTelegram) {
		_noTelegramCode->show();
		_callTimer.cancel();
	} else {
		_noTelegramCode->hide();
		_callStatus = getData()->callStatus;
		_callTimeout = getData()->callTimeout;
		if (_callStatus == CallStatus::Waiting && !_callTimer.isActive()) {
			_callTimer.callEach(1000);
		}
	}
	updateCallText();
}

void CodeWidget::updateCallText() {
	auto text = ([this]() -> QString {
		if (getData()->codeByTelegram) {
			return QString();
		}
		switch (_callStatus) {
		case CallStatus::Waiting: {
			if (_callTimeout >= 3600) {
				return tr::lng_code_call(
					tr::now,
					lt_minutes,
					(u"%1:%2"_q
					).arg(_callTimeout / 3600
					).arg((_callTimeout / 60) % 60, 2, 10, QChar('0')),
					lt_seconds,
					u"%1"_q.arg(_callTimeout % 60, 2, 10, QChar('0')));
			} else {
				return tr::lng_code_call(
					tr::now,
					lt_minutes,
					QString::number(_callTimeout / 60),
					lt_seconds,
					u"%1"_q.arg(_callTimeout % 60, 2, 10, QChar('0')));
			}
		} break;
		case CallStatus::Calling:
			return tr::lng_code_calling(tr::now);
		case CallStatus::Called:
			return tr::lng_code_called(tr::now);
		}
		return QString();
	})();
	_callLabel->setText(text);
	_callLabel->setVisible(!text.isEmpty() && !animating());
}

void CodeWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void CodeWidget::updateControlsGeometry() {
	_code->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto linkTop = _code->y() + _code->height() + st::introLinkTop;
	_noTelegramCode->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
	_callLabel->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
}

void CodeWidget::showCodeError(rpl::producer<QString> text) {
	_code->showError();
	showError(std::move(text));
}

void CodeWidget::setInnerFocus() {
	_code->setFocusFast();
}

void CodeWidget::activate() {
	Step::activate();
	_code->show();
	if (getData()->codeByTelegram) {
		_noTelegramCode->show();
	} else {
		_callLabel->show();
	}
	setInnerFocus();
}

void CodeWidget::finished() {
	Step::finished();
	account().setHandleLoginCode(nullptr);
#if 0 // mtp
	_checkRequestTimer.cancel();
#endif
	_callTimer.cancel();
	apiClear();

	cancelled();
	_sentCode.clear();
	_code->setText(QString());
}

void CodeWidget::cancelled() {
	_sentRequest = false;

#if 0 // mtp
	api().request(base::take(_sentRequest)).cancel();
	api().request(base::take(_callRequestId)).cancel();

	// TDLib doesn't want to make this request.
	api().request(MTPauth_CancelCode(
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash)
	)).send();
#endif
}

#if 0 // mtp
void CodeWidget::stopCheck() {
	_checkRequestTimer.cancel();
}

void CodeWidget::checkRequest() {
	auto status = api().instance().state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			if (_sentRequest) {
				api().request(base::take(_sentRequest)).cancel();
				_sentCode.clear();
			}
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void CodeWidget::codeSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	_sentRequest = 0;
	finish(result);
}

void CodeWidget::codeSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showCodeError(tr::lng_flood_error());
		return;
	}

	stopCheck();
	_sentRequest = 0;
	auto &err = error.type();
	if (err == u"PHONE_NUMBER_INVALID"_q
		|| err == u"PHONE_CODE_EXPIRED"_q
		|| err == u"PHONE_NUMBER_BANNED"_q) { // show error
		goBack();
	} else if (err == u"PHONE_CODE_EMPTY"_q || err == u"PHONE_CODE_INVALID"_q) {
		showCodeError(tr::lng_bad_code());
	} else if (err == u"SESSION_PASSWORD_NEEDED"_q) {
		_checkRequestTimer.callEach(1000);
		_sentRequest = api().request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			gotPassword(result);
		}).fail([=](const MTP::Error &error) {
			codeSubmitFail(error);
		}).handleFloodErrors().send();
	} else if (Logs::DebugEnabled()) { // internal server error
		showCodeError(rpl::single(err + ": " + error.description()));
	} else {
		showCodeError(rpl::single(Lang::Hard::ServerError()));
	}
}
#endif

bool CodeWidget::applyState(const TLauthorizationState &state) {
	return state.match([&](const TLDauthorizationStateWaitCode &data) {
		data.vcode_info().match([&](const TLDauthenticationCodeInfo &data) {
			_code->setDigitsCountMax(getData()->codeLength);
			if (_callStatus == CallStatus::Calling) {
				_callStatus = CallStatus::Called;
				getData()->callStatus = _callStatus;
				getData()->callTimeout = _callTimeout;
				updateCallText();
			} else {
				updateDescText();
			}
		});
		return true;
	}, [&](const auto &) {
		return false;
	});
}

void CodeWidget::checkCodeFail(const Error &error) {
	_sentRequest = false;
	const auto &type = error.message;
	if (type == u"PHONE_NUMBER_INVALID"
		|| type == u"PHONE_CODE_EXPIRED"
		|| type == u"PHONE_NUMBER_BANNED") {
#if 0 // mtp
		goBack();
#endif
		go(StepType::Start);
	} else if (type == u"PHONE_CODE_EMPTY"
		|| type == u"PHONE_CODE_INVALID") {
		showCodeError(tr::lng_bad_code());
	} else {
		showCodeError(rpl::single(type));
	}
}

void CodeWidget::codeChanged() {
	hideError();
	submitCode();
}

void CodeWidget::sendCall() {
	if (_callStatus == CallStatus::Waiting) {
		if (--_callTimeout <= 0) {
			_callStatus = CallStatus::Calling;
			_callTimer.cancel();

			api().request(TLresendAuthenticationCode(
			)).send();

#if 0 // mtp
			_callRequestId = api().request(MTPauth_ResendCode(
				MTP_string(getData()->phone),
				MTP_bytes(getData()->phoneHash)
			)).done([=](const MTPauth_SentCode &result) {
				callDone(result);
			}).send();
#endif
		} else {
			getData()->callStatus = _callStatus;
			getData()->callTimeout = _callTimeout;
		}
		updateCallText();
	}
}

#if 0 // mtp
void CodeWidget::callDone(const MTPauth_SentCode &result) {
	result.match([&](const MTPDauth_sentCode &data) {
		fillSentCodeData(data);
		_code->setDigitsCountMax(getData()->codeLength);
		if (_callStatus == CallStatus::Calling) {
			_callStatus = CallStatus::Called;
			getData()->callStatus = _callStatus;
			getData()->callTimeout = _callTimeout;
			updateCallText();
		}
	}, [&](const MTPDauth_sentCodeSuccess &data) {
		finish(data.vauthorization());
	});
}

void CodeWidget::gotPassword(const MTPaccount_Password &result) {
	Expects(result.type() == mtpc_account_password);

	stopCheck();
	_sentRequest = 0;
	const auto &d = result.c_account_password();
	getData()->pwdState = Core::ParseCloudPasswordState(d);
	if (!d.vcurrent_algo() || !d.vsrp_id() || !d.vsrp_B()) {
		LOG(("API Error: No current password received on login."));
		_code->setFocus();
		return;
	} else if (!getData()->pwdState.hasPassword) {
		const auto callback = [=](Fn<void()> &&close) {
			Core::UpdateApplication();
			close();
		};
		Ui::show(Ui::MakeConfirmBox({
			.text = tr::lng_passport_app_out_of_date(),
			.confirmed = callback,
			.confirmText = tr::lng_menu_update(),
		}));
		return;
	}
	goReplace<PasswordCheckWidget>(Animate::Forward);
}
#endif

void CodeWidget::submit() {
	if (getData()->codeByFragmentUrl.isEmpty()) {
		submitCode();
	} else {
		File::OpenUrl(getData()->codeByFragmentUrl);
	}
}

void CodeWidget::submitCode() {
	const auto text = QString(
		_code->getLastText()
	).remove(
		TextUtilities::RegExpDigitsExclude()
	).mid(0, getData()->codeLength);

	if (_sentRequest
		|| _sentCode == text
		|| text.size() != getData()->codeLength) {
		return;
	}

	hideError();

#if 0 // mtp
	_checkRequestTimer.callEach(1000);
#endif

	_sentCode = text;

	_sentRequest = true;
	if (getData()->codeByEmail) {
		api().request(TLcheckAuthenticationEmailCode(
			tl_emailAddressAuthenticationCode(
				tl_string(_sentCode))
		)).fail([=](const Error &error) {
			checkCodeFail(error);
		}).send();
	} else {
		api().request(TLcheckAuthenticationCode(
			tl_string(_sentCode)
		)).fail([=](const Error &error) {
			checkCodeFail(error);
		}).send();
	}

#if 0 // mtp
	getData()->pwdState = Core::CloudPasswordState();
	_sentRequest = api().request(MTPauth_SignIn(
		MTP_flags(MTPauth_SignIn::Flag::f_phone_code),
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash),
		MTP_string(_sentCode),
		MTPEmailVerification()
	)).done([=](const MTPauth_Authorization &result) {
		codeSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		codeSubmitFail(error);
	}).handleFloodErrors().send();
#endif
}

rpl::producer<QString> CodeWidget::nextButtonText() const {
	return _isFragment.value(
	) | rpl::map([=](bool isFragment) {
		return isFragment
			? tr::lng_intro_fragment_button()
			: Step::nextButtonText();
	}) | rpl::flatten_latest();
}

rpl::producer<const style::RoundButton*> CodeWidget::nextButtonStyle() const {
	return _isFragment.value(
	) | rpl::map([](bool isFragment) {
		return isFragment ? &st::introFragmentButton : nullptr;
	});
}

void CodeWidget::noTelegramCode() {
	if (_noTelegramCodeSent) {
		return;
	}
	_noTelegramCodeSent = true;
	api().request(TLresendAuthenticationCode()).send();

#if 0 // mtp
	if (_noTelegramCodeRequestId) {
		return;
	}
	_noTelegramCodeRequestId = api().request(MTPauth_ResendCode(
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash)
	)).done([=](const MTPauth_SentCode &result) {
		noTelegramCodeDone(result);
	}).fail([=](const MTP::Error &error) {
		noTelegramCodeFail(error);
	}).handleFloodErrors().send();
#endif
}

#if 0 // mtp
void CodeWidget::noTelegramCodeDone(const MTPauth_SentCode &result) {
	_noTelegramCodeRequestId = 0;

	result.match([&](const MTPDauth_sentCode &data) {
		const auto &d = result.c_auth_sentCode();
		fillSentCodeData(data);
		_code->setDigitsCountMax(getData()->codeLength);
		const auto next = data.vnext_type();
		if (next && next->type() == mtpc_auth_codeTypeCall) {
			getData()->callStatus = CallStatus::Waiting;
			getData()->callTimeout = d.vtimeout().value_or(60);
		} else {
			getData()->callStatus = CallStatus::Disabled;
			getData()->callTimeout = 0;
		}
		getData()->codeByTelegram = false;
		updateDescText();
	}, [&](const MTPDauth_sentCodeSuccess &data) {
		finish(data.vauthorization());
	});
}

void CodeWidget::noTelegramCodeFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		_noTelegramCodeRequestId = 0;
		showCodeError(tr::lng_flood_error());
		return;
	} else if (error.type() == u"SEND_CODE_UNAVAILABLE"_q) {
		_noTelegramCodeRequestId = 0;
		return;
	}

	_noTelegramCodeRequestId = 0;
	if (Logs::DebugEnabled()) { // internal server error
		showCodeError(rpl::single(error.type() + ": " + error.description()));
	} else {
		showCodeError(rpl::single(Lang::Hard::ServerError()));
	}
}
#endif

} // namespace details
} // namespace Intro
