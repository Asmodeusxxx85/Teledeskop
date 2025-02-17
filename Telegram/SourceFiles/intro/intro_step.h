/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "mtproto/sender.h"
#include "tdb/tdb_sender.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Tdb {
class TLuser;
class TLauthorizationState;
} // namespace Tdb

namespace style {
struct RoundButton;
} // namespace style;

namespace Main {
class Account;
} // namespace Main;

namespace Ui {
class SlideAnimation;
class CrossFadeAnimation;
class FlatLabel;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Intro {
namespace details {

struct Data;
enum class StackAction;
enum class Animate;

enum class StepType {
	Start,
	Phone,
	Qr,
	Code,
	Password,
	SignUp,
};

class Step : public Ui::RpWidget {
public:
	Step(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data,
		bool hasCover = false);
	~Step();

	[[nodiscard]] Main::Account &account() const {
		return *_account;
	}
	[[nodiscard]] virtual StepType type() const = 0;

	// It should not be called in StartWidget, in other steps it should be
	// present and not changing.
	[[nodiscard]] Tdb::Sender &api() const;
	void apiClear();

	virtual void finishInit() {
	}
	virtual void setInnerFocus() {
		setFocus();
	}

#if 0 // mtp
	void setGoCallback(
		Fn<void(Step *step, StackAction action, Animate animate)> callback);
#endif
	void setGoCallback(Fn<void(StepType)> callback);
	void setShowResetCallback(Fn<void()> callback);
	void setShowTermsCallback(Fn<void()> callback);
	void setCancelNearestDcCallback(Fn<void()> callback);
	void setAcceptTermsCallback(
		Fn<void(Fn<void()> callback)> callback);

	void prepareShowAnimated(Step *after);
	void showAnimated(Animate animate);
	void showFast();
	[[nodiscard]] bool animating() const;
	void setShowAnimationClipping(QRect clipping);

	[[nodiscard]] bool hasCover() const;
	[[nodiscard]] virtual bool hasBack() const;
	virtual void activate();
	virtual void cancelled();
	virtual void finished();

	virtual void submit() = 0;
	[[nodiscard]] virtual rpl::producer<QString> nextButtonText() const;
	[[nodiscard]] virtual auto nextButtonStyle() const
		-> rpl::producer<const style::RoundButton*>;

	[[nodiscard]] int contentLeft() const;
	[[nodiscard]] int contentTop() const;

	void setErrorCentered(bool centered);
	void showError(rpl::producer<QString> text);
	void hideError() {
		showError(rpl::single(QString()));
	}

	virtual bool applyState(const Tdb::TLauthorizationState &state) = 0;
	void jumpByState(const Tdb::TLauthorizationState &state);
	void filtersReceived();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setTitleText(rpl::producer<QString> titleText);
	void setDescriptionText(rpl::producer<QString> descriptionText);
	void setDescriptionText(
		rpl::producer<TextWithEntities> richDescriptionText);
	bool paintAnimated(QPainter &p, QRect clip);

#if 0 // mtp
	void fillSentCodeData(const MTPDauth_sentCode &type);
#endif

	void showDescription();
	void hideDescription();

	[[nodiscard]] not_null<Data*> getData() const {
		return _data;
	}

#if 0 // mtp
	void finish(const MTPauth_Authorization &auth, QImage &&photo = {});
	void finish(const MTPUser &user, QImage &&photo = {});
	void createSession(
		const MTPUser &user,
		QImage photo,
		const QVector<MTPDialogFilter> &filters);

	void goBack();

	template <typename StepType>
	void goNext() {
		goNext(new StepType(parentWidget(), _account, _data));
	}

	template <typename StepType>
	void goReplace(Animate animate) {
		goReplace(new StepType(parentWidget(), _account, _data), animate);
	}
#endif

	void go(StepType type) {
		if (_goCallback) {
			_goCallback(type);
		}
	}

	void showResetButton() {
		if (_showResetCallback) _showResetCallback();
	}
	void showTerms() {
		if (_showTermsCallback) _showTermsCallback();
	}
	void acceptTerms(Fn<void()> callback) {
		if (_acceptTermsCallback) {
			_acceptTermsCallback(callback);
		}
	}
	void cancelNearestDcRequest() {
		if (_cancelNearestDcCallback) _cancelNearestDcCallback();
	}

	virtual int errorTop() const;

private:
	struct CoverAnimation {
		CoverAnimation() = default;
		CoverAnimation(CoverAnimation &&other) = default;
		CoverAnimation &operator=(CoverAnimation &&other) = default;
		~CoverAnimation();

		std::unique_ptr<Ui::CrossFadeAnimation> title;
		std::unique_ptr<Ui::CrossFadeAnimation> description;

		// From content top till the next button top.
		QPixmap contentSnapshotWas;
		QPixmap contentSnapshotNow;

		QRect clipping;
	};
	void updateLabelsPosition();
	void paintContentSnapshot(
		QPainter &p,
		const QPixmap &snapshot,
		float64 alpha,
		float64 howMuchHidden);
	void refreshError(const QString &text);

#if 0 // mtp
	void goNext(Step *step);
	void goReplace(Step *step, Animate animate);
#endif

	[[nodiscard]] CoverAnimation prepareCoverAnimation(Step *step);
	[[nodiscard]] QPixmap prepareContentSnapshot();
	[[nodiscard]] QPixmap prepareSlideAnimation();
	void showFinished();

	void prepareCoverMask();
	void paintCover(QPainter &p, int top);

	void finish(const Tdb::TLuser &self);
	void createSession(const Tdb::TLuser &user);

	const not_null<Main::Account*> _account;
	const not_null<Data*> _data;
	mutable std::optional<Tdb::Sender> _api;

	bool _hasCover = false;
#if 0 // mtp
	Fn<void(Step *step, StackAction action, Animate animate)> _goCallback;
#endif
	Fn<void(StepType)> _goCallback;
	Fn<void()> _showResetCallback;
	Fn<void()> _showTermsCallback;
	Fn<void()> _cancelNearestDcCallback;
	Fn<void(Fn<void()> callback)> _acceptTermsCallback;

	rpl::variable<QString> _titleText;
	object_ptr<Ui::FlatLabel> _title;
	rpl::variable<TextWithEntities> _descriptionText;
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _description;

	bool _errorCentered = false;
	rpl::variable<QString> _errorText;
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _error = { nullptr };

	Ui::Animations::Simple _a_show;
	CoverAnimation _coverAnimation;
	std::unique_ptr<Ui::SlideAnimation> _slideAnimation;
	QPixmap _coverMask;

};


} // namespace details
} // namespace Intro
