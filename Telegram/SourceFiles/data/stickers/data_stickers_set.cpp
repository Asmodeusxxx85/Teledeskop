/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/stickers/data_stickers_set.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/stickers/data_stickers.h"
#include "storage/file_download.h"
#include "ui/image/image.h"

#include "tdb/tdb_tl_scheme.h"

namespace Data {

using namespace Tdb;

StickersSetThumbnailView::StickersSetThumbnailView(
	not_null<StickersSet*> owner)
: _owner(owner) {
}

not_null<StickersSet*> StickersSetThumbnailView::owner() const {
	return _owner;
}

void StickersSetThumbnailView::set(
		not_null<Main::Session*> session,
		QByteArray content) {
	auto image = Images::Read({ .content = content }).image;
	if (image.isNull()) {
		_content = std::move(content);
	} else {
		_image = std::make_unique<Image>(std::move(image));
	}
	session->notifyDownloaderTaskFinished();
}

Image *StickersSetThumbnailView::image() const {
	return _image.get();
}

QByteArray StickersSetThumbnailView::content() const {
	return _content;
}

#if 0 // mtp
StickersSetFlags ParseStickersSetFlags(const MTPDstickerSet &data) {
	using Flag = StickersSetFlag;
	return (data.is_archived() ? Flag::Archived : Flag())
		| (data.is_official() ? Flag::Official : Flag())
		| (data.is_masks() ? Flag::Masks : Flag())
		| (data.is_emojis() ? Flag::Emoji : Flag())
		| (data.vinstalled_date() ? Flag::Installed : Flag())
		| (data.is_videos() ? Flag::Webm : Flag())
		| (data.is_text_color() ? Flag::TextColor : Flag());
}
#endif

StickersSetFlags ParseStickersSetFlags(const TLDstickerSet &data) {
	using Flag = StickersSetFlag;
	return (data.vis_archived().v ? Flag::Archived : Flag())
		| (data.vis_official().v ? Flag::Official : Flag())
		| (data.vsticker_type().type() == id_stickerTypeMask
			? Flag::Masks
			: Flag())
		| (data.vsticker_type().type() == id_stickerTypeCustomEmoji
			? Flag::Emoji
			: Flag())
		| (data.vis_installed().v ? Flag::Installed : Flag())
		| (data.vsticker_format().type() == id_stickerFormatWebm
			? Flag::Webm
			: Flag())
		| (data.vneeds_repainting().v ? Flag::TextColor : Flag());
}

StickersSetFlags ParseStickersSetFlags(const TLDstickerSetInfo &data) {
	using Flag = StickersSetFlag;
	return (data.vis_archived().v ? Flag::Archived : Flag())
		| (data.vis_official().v ? Flag::Official : Flag())
		| (data.vsticker_type().type() == id_stickerTypeMask
			? Flag::Masks
			: Flag())
		| (data.vsticker_type().type() == id_stickerTypeCustomEmoji
			? Flag::Emoji
			: Flag())
		| (data.vis_installed().v ? Flag::Installed : Flag())
		| (data.vsticker_format().type() == id_stickerFormatWebm
			? Flag::Webm
			: Flag())
		| (data.vneeds_repainting().v ? Flag::TextColor : Flag());
}

StickersSet::StickersSet(
	not_null<Data::Session*> owner,
	uint64 id,
	uint64 accessHash,
	uint64 hash,
	const QString &title,
	const QString &shortName,
	int count,
	StickersSetFlags flags,
	TimeId installDate)
: id(id)
, accessHash(accessHash)
, hash(hash)
, title(title)
, shortName(shortName)
, count(count)
, flags(flags)
, installDate(installDate)
, _owner(owner) {
}

StickersSet::~StickersSet() = default;

Data::Session &StickersSet::owner() const {
	return *_owner;
}

Main::Session &StickersSet::session() const {
	return _owner->session();
}

#if 0 // mtp
MTPInputStickerSet StickersSet::mtpInput() const {
	return (id && accessHash)
		? MTP_inputStickerSetID(MTP_long(id), MTP_long(accessHash))
		: MTP_inputStickerSetShortName(MTP_string(shortName));
}
#endif

StickerSetIdentifier StickersSet::identifier() const {
	return StickerSetIdentifier{
		.id = id,
		.accessHash = accessHash,
	};
}

StickersType StickersSet::type() const {
	return (flags & StickersSetFlag::Emoji)
		? StickersType::Emoji
		: (flags & StickersSetFlag::Masks)
		? StickersType::Masks
		: StickersType::Stickers;
}

bool StickersSet::textColor() const {
	return flags & StickersSetFlag::TextColor;
}

void StickersSet::setThumbnail(const ImageWithLocation &data) {
	Data::UpdateCloudFile(
		_thumbnail,
		data,
		_owner->cache(),
		Data::kImageCacheTag,
		[=](Data::FileOrigin origin) { loadThumbnail(); });
	if (!data.bytes.isEmpty()) {
		if (_thumbnail.loader) {
			_thumbnail.loader->cancel();
		}
		if (const auto view = activeThumbnailView()) {
			view->set(&_owner->session(), data.bytes);
		}
	}
}

bool StickersSet::hasThumbnail() const {
	return _thumbnail.location.valid();
}

bool StickersSet::thumbnailLoading() const {
	return (_thumbnail.loader != nullptr);
}

bool StickersSet::thumbnailFailed() const {
	return (_thumbnail.flags & Data::CloudFile::Flag::Failed);
}

void StickersSet::loadThumbnail() {
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeThumbnailView()) {
			return !active->image() && active->content().isEmpty();
		}
		return true;
	};
	const auto done = [=](QByteArray result) {
		if (const auto active = activeThumbnailView()) {
			active->set(&_owner->session(), std::move(result));
		}
	};
	Data::LoadCloudFile(
		&_owner->session(),
		_thumbnail,
		Data::FileOriginStickerSet(id, accessHash),
		LoadFromCloudOrLocal,
		autoLoading,
		Data::kImageCacheTag,
		finalCheck,
		done);
}

const ImageLocation &StickersSet::thumbnailLocation() const {
	return _thumbnail.location;
}

Storage::Cache::Key StickersSet::thumbnailBigFileBaseCacheKey() const {
	const auto &location = _thumbnail.location.file().data;
	if (const auto storage = std::get_if<StorageFileLocation>(&location)) {
		return storage->bigFileBaseCacheKey();
	} else if (const auto file = std::get_if<TdbFileLocation>(&location)) {
		return TdbFileLocation::BigFileBaseCacheKey(
			file->hash,
			kTdbLocationTypeStickerSetThumb);
	}
	return {};
}

int StickersSet::thumbnailByteSize() const {
	return _thumbnail.byteSize;
}

DocumentData *StickersSet::lookupThumbnailDocument() const {
	if (thumbnailDocumentId) {
		const auto i = ranges::find(
			stickers,
			thumbnailDocumentId,
			&DocumentData::id);
		if (i != stickers.end()) {
			return *i;
		}
	}
	return !stickers.empty()
		? stickers.front()
		: !covers.empty()
		? covers.front()
		: nullptr;
}

std::shared_ptr<StickersSetThumbnailView> StickersSet::createThumbnailView() {
	if (auto active = activeThumbnailView()) {
		return active;
	}
	auto view = std::make_shared<StickersSetThumbnailView>(this);
	_thumbnailView = view;
	return view;
}

std::shared_ptr<StickersSetThumbnailView> StickersSet::activeThumbnailView() {
	return _thumbnailView.lock();
}

#if 0 // mtp
MTPInputStickerSet InputStickerSet(StickerSetIdentifier id) {
	return !id
		? MTP_inputStickerSetEmpty()
		: id.id
		? MTP_inputStickerSetID(MTP_long(id.id), MTP_long(id.accessHash))
		: MTP_inputStickerSetShortName(MTP_string(id.shortName));
}

StickerSetIdentifier FromInputSet(const MTPInputStickerSet &id) {
	return id.match([](const MTPDinputStickerSetID &data) {
		return StickerSetIdentifier{
			.id = data.vid().v,
			.accessHash = data.vaccess_hash().v,
		};
	}, [](const MTPDinputStickerSetShortName &data) {
		return StickerSetIdentifier{ .shortName = qs(data.vshort_name()) };
	}, [](const auto &) {
		return StickerSetIdentifier();
	});
}
#endif

} // namespace Stickers
