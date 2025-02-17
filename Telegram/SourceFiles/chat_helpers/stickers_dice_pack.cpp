/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_dice_pack.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/image/image_location_factory.h"
#include "storage/localimageloader.h"
#include "base/unixtime.h"
#include "apiwrap.h"

#include "tdb/tdb_tl_scheme.h"
#include "history/view/media/history_view_slot_machine.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Stickers {

using namespace Tdb;

const QString DicePacks::kDiceString = QString::fromUtf8("\xF0\x9F\x8E\xB2");
const QString DicePacks::kDartString = QString::fromUtf8("\xF0\x9F\x8E\xAF");
const QString DicePacks::kSlotString = QString::fromUtf8("\xF0\x9F\x8E\xB0");
const QString DicePacks::kFballString = QString::fromUtf8("\xE2\x9A\xBD");
const QString DicePacks::kBballString = QString::fromUtf8("\xF0\x9F\x8F\x80");

DicePack::DicePack(not_null<Main::Session*> session, const QString &emoji)
: _session(session)
, _emoji(emoji) {
}

DicePack::~DicePack() = default;

DocumentData *DicePack::lookup(int value) {
	if (!_requestId) {
		load();
	}
	tryGenerateLocalZero();
	const auto i = _map.find(value);
	return (i != end(_map)) ? i->second.get() : nullptr;
}

void DicePack::apply(const Tdb::TLDmessageDice &data) {
	const auto emplace = [&](int index, const TLsticker &sticker) {
		const auto document = _session->data().processDocument(sticker);
		if (document->sticker()) {
			_map.emplace(index, document);
		}
	};
	if (const auto &state = data.vinitial_state()) {
		state->match([&](const TLDdiceStickersRegular &data) {
			emplace(0, data.vsticker());
		}, [&](const TLDdiceStickersSlotMachine &data) {
			HistoryView::EnumerateSlotMachineParts(0, data, emplace);
		});
	}
	if (const auto &state = data.vfinal_state()) {
		const auto value = data.vvalue().v;
		state->match([&](const TLDdiceStickersRegular &data) {
			emplace(value, data.vsticker());
		}, [&](const TLDdiceStickersSlotMachine &data) {
			HistoryView::EnumerateSlotMachineParts(value, data, emplace);
		});
	}
}

void DicePack::load() {
#if 0 // mtp
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetDice(MTP_string(_emoji)),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
#endif
}

#if 0 // mtp
void DicePack::applySet(const MTPDmessages_stickerSet &data) {
	const auto isSlotMachine = DicePacks::IsSlot(_emoji);
	auto index = 0;
	auto documents = base::flat_map<DocumentId, not_null<DocumentData*>>();
	for (const auto &sticker : data.vdocuments().v) {
		const auto document = _session->data().processDocument(sticker);
		if (document->sticker()) {
			if (isSlotMachine) {
				_map.emplace(index++, document);
			} else {
				documents.emplace(document->id, document);
			}
		}
	}
	if (isSlotMachine) {
		return;
	}
	for (const auto &pack : data.vpacks().v) {
		pack.match([&](const MTPDstickerPack &data) {
			const auto emoji = qs(data.vemoticon());
			if (emoji.isEmpty()) {
				return;
			}
			const auto ch = int(emoji[0].unicode());
			const auto index = (ch == '#') ? 0 : (ch + 1 - '1');
			if (index < 0 || index > 6) {
				return;
			}
			for (const auto &id : data.vdocuments().v) {
				if (const auto document = documents.take(id.v)) {
					_map.emplace(index, *document);
				}
			}
		});
	}
}
#endif

void DicePack::tryGenerateLocalZero() {
	if (!_map.empty()) {
		return;
	}

	if (_emoji == DicePacks::kDiceString) {
		generateLocal(0, u"dice_idle"_q);
	} else if (_emoji == DicePacks::kDartString) {
		generateLocal(0, u"dart_idle"_q);
	} else if (_emoji == DicePacks::kBballString) {
		generateLocal(0, u"bball_idle"_q);
	} else if (_emoji == DicePacks::kFballString) {
		generateLocal(0, u"fball_idle"_q);
	} else if (_emoji == DicePacks::kSlotString) {
		generateLocal(0, u"slot_back"_q);
		generateLocal(2, u"slot_pull"_q);
		generateLocal(8, u"slot_0_idle"_q);
		generateLocal(14, u"slot_1_idle"_q);
		generateLocal(20, u"slot_2_idle"_q);
	}
}

void DicePack::generateLocal(int index, const QString &name) {
	const auto path = u":/gui/art/"_q + name + u".tgs"_q;
	auto task = FileLoadTask(
		_session,
		path,
		QByteArray(),
		nullptr,
		SendMediaType::File,
		FileLoadTo(0, {}, {}, 0),
		{},
		false);
	task.process({ .generateGoodThumbnail = false });
	const auto result = task.peekResult();
	Assert(result != nullptr);
#if 0 // mtp
	const auto document = _session->data().processDocument(
		result->document,
		Images::FromImageInMemory(result->thumb, "WEBP", result->thumbbytes));
#endif
	const auto document = _session->data().processDocument(result->document);
	document->setLocation(Core::FileLocation(path));

	_map.emplace(index, document);

	Ensures(document->sticker());
	Ensures(document->sticker()->isLottie());
}

DicePacks::DicePacks(not_null<Main::Session*> session) : _session(session) {
}

DocumentData *DicePacks::lookup(const QString &emoji, int value) {
	const auto key = emoji.endsWith(QChar(0xFE0F))
		? emoji.mid(0, emoji.size() - 1)
		: emoji;
	const auto i = _packs.find(key);
	if (i != end(_packs)) {
		return i->second->lookup(value);
	}
	return _packs.emplace(
		key,
		std::make_unique<DicePack>(_session, key)
	).first->second->lookup(value);
}

void DicePacks::apply(const TLDupdateDiceEmojis &update) {
	const auto &list = update.vemojis().v;
	_cloudDiceEmoticons = list
		| ranges::views::transform(&TLstring::v)
		| ranges::to_vector;
}

void DicePacks::apply(const TLDmessageDice &data) {
	const auto emoji = data.vemoji().v;
	const auto key = emoji.endsWith(QChar(0xFE0F))
		? emoji.mid(0, emoji.size() - 1)
		: emoji;
	const auto i = _packs.find(key);
	if (i != end(_packs)) {
		return i->second->apply(data);
	}
	return _packs.emplace(
		key,
		std::make_unique<DicePack>(_session, key)
	).first->second->apply(data);
}

const std::vector<QString> &DicePacks::cloudDiceEmoticons() const {
	return _cloudDiceEmoticons;
}

} // namespace Stickers
