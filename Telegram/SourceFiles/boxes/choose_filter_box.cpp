/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/choose_filter_box.h"

#include "apiwrap.h"
#include "boxes/premium_limits_box.h"
#include "core/application.h" // primaryWindow
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h" // Ui::Text::Bold
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck

#include "tdb/tdb_tl_scheme.h"
#include "tdb/tdb_sender.h"

namespace {

using namespace Tdb;

Data::ChatFilter ChangedFilter(
		const Data::ChatFilter &filter,
		not_null<History*> history,
		bool add) {
	auto always = base::duplicate(filter.always());
	auto pinned = base::duplicate(filter.pinned());
	auto never = base::duplicate(filter.never());
	if (add) {
		never.remove(history);
	} else {
		always.remove(history);
		pinned.erase(ranges::remove(pinned, history), end(pinned));
	}
	const auto result = Data::ChatFilter(
		filter.id(),
		filter.title(),
		filter.iconEmoji(),
		filter.flags(),
		std::move(always),
#if 0 // mtp
		filter.pinned(),
#endif
		std::move(pinned),
		std::move(never));
#if 0 // mtp
	const auto in = result.contains(history);
	if (in == add) {
#endif
	if (add == result.computeContains(history)) {
		return result;
	}
	always = base::duplicate(result.always());
	never = base::duplicate(result.never());
	if (add) {
		always.insert(history);
	} else {
		never.insert(history);
	}
	return Data::ChatFilter(
		filter.id(),
		filter.title(),
		filter.iconEmoji(),
		filter.flags(),
		std::move(always),
		filter.pinned(),
		std::move(never));
}

void ChangeFilterById(
		FilterId filterId,
		not_null<History*> history,
		bool add) {
	Expects(filterId != 0);

	const auto list = history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (i != end(list)) {
		const auto hadMyLinks = i->hasMyLinks();
		const auto sender = &history->session().sender();
		sender->request(TLgetChatFolder(
			tl_int32(filterId)
		)).done([=](const TLchatFolder &result) {
			const auto owner = &history->owner();
			auto parsed = Data::ChatFilter::FromTL(
				filterId,
				result,
				owner,
				hadMyLinks);
			const auto chat = history->peer->name();
			const auto guard = base::make_weak(&history->session());
			const auto account = &history->session().account();
			const auto name = parsed.title();
			const auto show = [=](TextWithEntities text) {
				if (!guard) {
					return;
				}
				if (const auto controller = Core::App().windowFor(account)) {
					controller->showToast(std::move(text));
				}
			};
			sender->request(TLeditChatFolder(
				tl_int32(filterId),
				ChangedFilter(std::move(parsed), history, add).tl()
			)).done([=] {
				// Since only the primary window has dialogs list,
				// We can safely show toast there.
				show((add
					? tr::lng_filters_toast_add
					: tr::lng_filters_toast_remove)(
						tr::now,
						lt_chat,
						Ui::Text::Bold(chat),
						lt_folder,
						Ui::Text::Bold(name),
						Ui::Text::WithEntities));
			}).fail([=](const Error &error) {
#if 0 // tdb errors
				const auto &text = error.message;
				if (text == u"The maximum number of excluded chats exceeded"_q) {
				} else if (text == u"The maximum number of included chats exceeded"_q) {

				} else if (text == u"Folder must contain at least 1 chat"_q) {
				} else if (text == u"Folder must be different from the main chat list"_q) {
				}
#endif
				show({ error.message });
			}).send();
		}).send();
#if 0 // mtp
		const auto was = *i;
		const auto filter = ChangedFilter(was, history, add);
		history->owner().chatsFilters().set(filter);
		history->session().api().request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
			MTP_int(filter.id()),
			filter.tl()
		)).done([=, chat = history->peer->name(), name = filter.title()] {
			const auto account = &history->session().account();
			if (const auto controller = Core::App().windowFor(account)) {
				controller->showToast((add
					? tr::lng_filters_toast_add
					: tr::lng_filters_toast_remove)(
						tr::now,
						lt_chat,
						Ui::Text::Bold(chat),
						lt_folder,
						Ui::Text::Bold(name),
						Ui::Text::WithEntities));
			}
		}).fail([=](const MTP::Error &error) {
			// Revert filter on fail.
			history->owner().chatsFilters().set(was);
		}).send();
#endif
	}
}

} // namespace

ChooseFilterValidator::ChooseFilterValidator(not_null<History*> history)
: _history(history) {
}

bool ChooseFilterValidator::canAdd() const {
	return true; // We'll show the error when we try and fail.
#if 0 // mtp
	for (const auto &filter : _history->owner().chatsFilters().list()) {
		if (filter.id() && !filter.contains(_history)) {
			return true;
		}
	}
	return false;
#endif
}

bool ChooseFilterValidator::canRemove(FilterId filterId) const {
	Expects(filterId != 0);

	return true; // We'll show the error when we try and fail.
#if 0 // mtp
	const auto list = _history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (i != end(list)) {
		const auto &filter = *i;
		return filter.contains(_history)
			&& ((filter.always().size() > 1) || filter.flags());
	}
	return false;
#endif
}

ChooseFilterValidator::LimitData ChooseFilterValidator::limitReached(
		FilterId filterId,
		bool always) const {
	Expects(filterId != 0);

	return {}; // We'll show the error when we try and fail.
#if 0 // mtp
	const auto list = _history->owner().chatsFilters().list();
	const auto i = ranges::find(list, filterId, &Data::ChatFilter::id);
	const auto limit = _history->owner().pinnedChatsLimit(filterId);
	const auto &chatsList = always ? i->always() : i->never();
	return {
		.reached = (i != end(list))
			&& !ranges::contains(chatsList, _history)
			&& (chatsList.size() >= limit),
		.count = int(chatsList.size()),
	};
#endif
}

void ChooseFilterValidator::add(FilterId filterId) const {
	ChangeFilterById(filterId, _history, true);
}

void ChooseFilterValidator::remove(FilterId filterId) const {
	ChangeFilterById(filterId, _history, false);
}

void FillChooseFilterMenu(
		not_null<Window::SessionController*> controller,
		not_null<Ui::PopupMenu*> menu,
		not_null<History*> history) {
	const auto weak = base::make_weak(controller);
	const auto validator = ChooseFilterValidator(history);
	for (const auto &filter : history->owner().chatsFilters().list()) {
		const auto id = filter.id();
		if (!id) {
			continue;
		}

		const auto contains = filter.contains(history);
		const auto action = menu->addAction(filter.title(), [=] {
			const auto toAdd = !filter.contains(history);
			const auto r = validator.limitReached(id, toAdd);
			if (r.reached) {
				controller->show(Box(
					FilterChatsLimitBox,
					&controller->session(),
					r.count,
					toAdd));
				return;
			} else if (toAdd ? validator.canAdd() : validator.canRemove(id)) {
				if (toAdd) {
					validator.add(id);
				} else {
					validator.remove(id);
				}
			}
		}, contains ? &st::mediaPlayerMenuCheck : nullptr);
		action->setEnabled(contains
			? validator.canRemove(id)
			: validator.canAdd());
	}
	history->owner().chatsFilters().changed(
	) | rpl::start_with_next([=] {
		menu->hideMenu();
	}, menu->lifetime());
}
