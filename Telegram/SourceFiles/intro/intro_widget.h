/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "window/window_lock_widgets.h"
#include "core/core_cloud_password.h"
#include "media/player/media_player_float.h"
#include "tdb/tdb_sender.h"

namespace Tdb {
class TLauthenticationCodeInfo;
class TLauthorizationState;
class TLtermsOfService;
class TLupdate;
} // namespace Tdb

namespace Main {
class Account;
} // namespace Main

namespace Ui {
class IconButton;
class RoundButton;
class LinkButton;
class FlatLabel;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Window {
class ConnectionState;
class Controller;
class SlideAnimation;
} // namespace Window

namespace Intro {
namespace details {

enum class CallStatus {
	Waiting,
	Calling,
	Called,
	Disabled,
};

struct Data {
	// Required for the UserpicButton.
	const not_null<Window::Controller*> controller;

	QString country;
	QString phone;
#if 0 // mtp
	QByteArray phoneHash;
#endif
	bool madeInitialJumpToStep = false;
	bool waitingForFilters = false;
	std::shared_ptr<Tdb::TLupdate> filtersUpdate;
	QImage chosenPhoto;
	QString pwdEmailPattern;

	CallStatus callStatus = CallStatus::Disabled;
	int callTimeout = 0;

	int codeLength = 5;
	bool codeByTelegram = false;
	QString codeByFragmentUrl;

	Core::CloudPasswordState pwdState;

	Window::TermsLock termsLock;

	rpl::event_stream<> updated;

	QString qrLink;
	bool resettingForPhoneAuth = false;
	bool codeByEmail = false;
};

enum class StackAction {
	Back,
	Forward,
	Replace,
};

enum class Animate {
	Back,
	Forward,
};

class Step;
enum class StepType;

} // namespace details

enum class EnterPoint : uchar {
	Start,
	Phone,
	Qr,
};

class Widget
	: public Ui::RpWidget
	, private Media::Player::FloatDelegate
	, private Media::Player::FloatSectionDelegate {
public:
	Widget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<Main::Account*> account,
		EnterPoint point);
	~Widget();

	void showAnimated(QPixmap oldContentCache, bool back = false);

	void setInnerFocus();

	[[nodiscard]] rpl::producer<> showSettingsRequested() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void refreshLang();
	void showFinished();
	void createLanguageLink();
	void checkUpdateStatus();
	void setupNextButton();
#if 0 // mtp
	void handleUpdates(const MTPUpdates &updates);
	void handleUpdate(const MTPUpdate &update);
#endif
	void backRequested();

	void updateControlsGeometry();
	[[nodiscard]] not_null<details::Data*> getData() {
		return &_data;
	}

	void fixOrder();
	void showControls();
	void hideControls();

	void showResetButton();
	void resetAccount();

	void showTerms();
	void acceptTerms(Fn<void()> callback);
	void hideAndDestroy(object_ptr<Ui::FadeWrap<Ui::RpWidget>> widget);

#if 0 // mtp
	[[nodiscard]] details::Step *getStep(int skip = 0) const {
		Expects(skip >= 0);
		Expects(skip < _stepHistory.size());

		return _stepHistory[_stepHistory.size() - skip - 1];
	}
	void historyMove(details::StackAction action, details::Animate animate);
	void moveToStep(
		details::Step *step,
		details::StackAction action,
		details::Animate animate);
	void appendStep(details::Step *step);
#endif

	[[nodiscard]] not_null<details::Step*> getStep() const {
		Expects(!_stepHistory.empty());

		return _stepHistory.back().get();
	}
	void historyMove(
		details::Step *wasStep,
		std::vector<std::unique_ptr<details::Step>>::iterator nowStep);
	void appendStep(std::unique_ptr<details::Step> step);
	bool go(details::StepType type);

	template <typename WidgetType>
	[[nodiscard]] std::unique_ptr<details::Step> makeStep();

	void getNearestDC();
	void showTerms(Fn<void()> callback);

	void handleUpdate(const Tdb::TLupdate &update);
	void handleAuthorizationState(const Tdb::TLauthorizationState &state);
	void fillCodeInfo(const Tdb::TLauthenticationCodeInfo &info);
	void fillTerms(const Tdb::TLtermsOfService &terms);

	// FloatDelegate
	[[nodiscard]] auto floatPlayerDelegate()
		-> not_null<Media::Player::FloatDelegate*>;
	[[nodiscard]] auto floatPlayerSectionDelegate()
		-> not_null<Media::Player::FloatSectionDelegate*>;
	not_null<Ui::RpWidget*> floatPlayerWidget() override;
	void floatPlayerToggleGifsPaused(bool paused) override;
	not_null<Media::Player::FloatSectionDelegate*> floatPlayerGetSection(
		Window::Column column) override;
	void floatPlayerEnumerateSections(Fn<void(
		not_null<Media::Player::FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) override;
	bool floatPlayerIsVisible(not_null<HistoryItem*> item) override;
	void floatPlayerDoubleClickEvent(
		not_null<const HistoryItem*> item) override;

	// FloatSectionDelegate
	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;

	const not_null<Main::Account*> _account;
	Tdb::Sender _api;
	Tdb::RequestId _nearestDcRequestId = 0;

	details::Data _data;

	std::unique_ptr<Window::SlideAnimation> _showAnimation;

#if 0 // mtp
	std::vector<details::Step*> _stepHistory;
#endif
	std::vector<std::unique_ptr<details::Step>> _stepHistory;
	rpl::lifetime _stepLifetime;

	Ui::Animations::Simple _coverShownAnimation;
	int _nextTopFrom = 0;
	int _controlsTopFrom = 0;

	const style::RoundButton *_nextStyle = nullptr;

	object_ptr<Ui::FadeWrap<Ui::IconButton>> _back;
	object_ptr<Ui::FadeWrap<Ui::RoundButton>> _update = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::RoundButton>> _settings;

	object_ptr<Ui::FadeWrap<Ui::RoundButton>> _next;
	object_ptr<Ui::FadeWrap<Ui::LinkButton>> _changeLanguage = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::RoundButton>> _resetAccount = { nullptr };
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _terms = { nullptr };

	std::unique_ptr<Window::ConnectionState> _connecting;

	bool _nextShown = true;
	Ui::Animations::Simple _nextShownAnimation;

	Tdb::RequestId _resetRequest = 0;

};

} // namespace Intro
