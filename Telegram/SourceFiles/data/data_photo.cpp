/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_photo.h"

#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_reply_preview.h"
#include "data/data_photo_media.h"
#include "ui/image/image.h"
#include "ui/image/image_location_factory.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_loader_mtproto.h"
#include "mainwidget.h"
#include "storage/file_download.h"
#include "core/application.h"

#include "tdb/tdb_tl_scheme.h"
#include "media/streaming/media_streaming_loader_tdb.h"

namespace {

constexpr auto kPhotoSideLimit = 2560;

using namespace Tdb;
using Data::PhotoMedia;
using Data::PhotoSize;
using Data::PhotoSizeIndex;
using Data::kPhotoSizeCount;

[[nodiscard]] QImage ValidatePhotoImage(
		QImage image,
		const Data::CloudFile &file) {
	return (v::is<WebFileLocation>(file.location.file().data)
		&& image.format() == QImage::Format_ARGB32)
		? Images::Opaque(std::move(image))
		: image;
}

[[nodiscard]] int VideoStartTime(
		const tl::conditional<TLanimatedChatPhoto> &photo) {
	return int(
		std::clamp(
			std::floor(photo
				? photo->data().vmain_frame_timestamp().v * 1000
				: 0.),
			0.,
			double(std::numeric_limits<int>::max())));
}

struct Sizes {
	const TLphotoSize *small = nullptr;
	const TLphotoSize *thumbnail = nullptr;
	ImageWithLocation thumbnailProgressive;
	const TLphotoSize *large = nullptr;
};
[[nodiscard]] Sizes LookupSizes(const TLvector<TLphotoSize> &data) {
	const auto &sizes = data.v;
	const auto i = ranges::find_if(sizes, [](const TLphotoSize &data) {
		return !data.data().vprogressive_sizes().v.isEmpty();
	});
	const auto progressive = (i != sizes.end());
	const auto find = [&](const QByteArray &levels) {
		const auto kInvalidIndex = int(levels.size());
		const auto level = [&](const TLphotoSize &size) {
			const auto type = size.data().vtype().v;
			const auto letter = type.isEmpty()
				? 0
				: type[0].unicode();
			const auto index = levels.indexOf(char(letter));
			return (index >= 0) ? index : kInvalidIndex;
		};
		const auto result = ranges::max_element(
			sizes,
			ranges::greater(),
			level);
		return (level(*result) == kInvalidIndex) ? sizes.end() : result;
	};
	const auto image = [&](const QByteArray &levels) {
		const auto i = find(levels);
		return (i != sizes.end()) ? &*i : nullptr;
	};
	return {
		.small = (progressive ? nullptr : image("sa"_q)),
		.thumbnail = (progressive ? nullptr : image("mbsa"_q)),
		.thumbnailProgressive = (progressive
			? Images::FromProgressiveSize(*i, 1)
			: ImageWithLocation()),
		.large = (progressive ? &*i : image("ydxncwmbsai"_q)),
	};
}

} // namespace

PhotoData::PhotoData(not_null<Data::Session*> owner, PhotoId id)
: id(id)
, _owner(owner) {
}

PhotoData::~PhotoData() {
	for (auto &image : _images) {
		base::take(image.loader).reset();
	}
	base::take(_videoSizes);
}

PhotoId PhotoData::IdFromTdb(const TLphoto &data) {
	const auto &sizes = data.data().vsizes().v;
	return sizes.isEmpty()
		? 0
		: sizes.front().data().vphoto().data().vid().v;
}

PhotoId PhotoData::IdFromTdb(const TLchatPhoto &data) {
	return data.data().vid().v;
}

PhotoId PhotoData::IdFromTdb(const TLchatPhotoInfo &data) {
	return data.data().vbig().data().vid().v;
}

void PhotoData::setFromTdb(const TLphoto &data) {
	const auto &fields = data.data();
	setHasAttachedStickers(fields.vhas_stickers().v);
	auto sizes = LookupSizes(fields.vsizes());
	if (!sizes.large) {
		return;
	}
	updateImages(
		(fields.vminithumbnail()
			? fields.vminithumbnail()->data().vdata().v
			: QByteArray()),
		Images::FromPhotoSize(sizes.small),
		(sizes.thumbnail
			? Images::FromPhotoSize(sizes.thumbnail)
			: sizes.thumbnailProgressive),
		Images::FromPhotoSize(sizes.large),
		ImageWithLocation(),
		ImageWithLocation(),
		crl::time());
	applyTdbFile(sizes.large->data().vphoto());
}

void PhotoData::setFromTdb(const TLchatPhoto &data) {
	const auto &fields = data.data();

	date = fields.vadded_date().v;

	const auto sizes = LookupSizes(fields.vsizes());
	if (!sizes.large) {
		return;
	}
	updateImages(
		(fields.vminithumbnail()
			? fields.vminithumbnail()->data().vdata().v
			: QByteArray()),
		Images::FromPhotoSize(sizes.small),
		(sizes.thumbnail
			? Images::FromPhotoSize(sizes.thumbnail)
			: sizes.thumbnailProgressive),
		Images::FromPhotoSize(sizes.large),
		(fields.vsmall_animation()
			? Images::FromAnimationSize(*fields.vsmall_animation())
			: ImageWithLocation()),
		(fields.vanimation()
			? Images::FromAnimationSize(*fields.vanimation())
			: ImageWithLocation()),
		VideoStartTime(fields.vanimation()));
	applyTdbFile(sizes.large->data().vphoto());
}

void PhotoData::setFromTdb(const TLchatPhotoInfo &data) {
	constexpr auto kSmallSize = 160;
	constexpr auto kBigSize = 640;
	const auto &fields = data.data();
	updateImages(
		(fields.vminithumbnail()
			? fields.vminithumbnail()->data().vdata().v
			: QByteArray()),
		Images::FromTdbFile(fields.vsmall(), kSmallSize, kSmallSize),
		ImageWithLocation(),
		Images::FromTdbFile(fields.vbig(), kBigSize, kBigSize),
		ImageWithLocation(),
		ImageWithLocation(),
		crl::time());
	applyTdbFile(fields.vbig());
}

void PhotoData::applyTdbFile(const Tdb::TLfile &file) {
	const auto size = file.data().vsize().v;
	const auto &remote = file.data().vremote().data();
	if (!remote.vis_uploading_completed().v) {
		if (!uploadingData) {
			uploadingData = std::make_unique<Data::UploadState>(size);
		}
		uploadingData->offset = remote.vuploaded_size().v;
		_owner->requestPhotoViewRepaint(this);
	} else if (uploadingData) {
		uploadingData = nullptr;
		_owner->requestPhotoViewRepaint(this);
	}
}

void PhotoData::setFromLocal(const Data::PhotoLocalData &data) {
	Expects(id == data.id);

	const auto image = [&](char level) {
		const auto proj = [&](const auto &pair) {
			return pair.first;
		};
		const auto i = ranges::find(data.thumbs, level, proj);
		return (i == data.thumbs.end())
			? ImageWithLocation()
			: Images::FromImageInMemory(
				i->second.image,
				"JPG",
				i->second.bytes);
	};

	date = data.added;
	updateImages({}, {}, image('m'), image('y'), {}, {}, {});
}

uint64 PhotoData::persistentId() const {
	constexpr auto large = PhotoSize::Large;
	const auto tdb = std::get_if<TdbFileLocation>(
		&location(large).file().data);
	return tdb ? tdb->hash : id;
}

Data::Session &PhotoData::owner() const {
	return *_owner;
}

Main::Session &PhotoData::session() const {
	return _owner->session();
}

void PhotoData::automaticLoadSettingsChanged() {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	if (!(_images[index].flags & Data::CloudFile::Flag::Cancelled)) {
		return;
	}
	_images[index].loader = nullptr;
	_images[index].flags &= ~Data::CloudFile::Flag::Cancelled;
}

void PhotoData::load(
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	load(PhotoSize::Large, origin, fromCloud, autoLoading);
}

bool PhotoData::loading() const {
	return loading(PhotoSize::Large);
}

int PhotoData::validSizeIndex(PhotoSize size) const {
	const auto index = PhotoSizeIndex(size);
	for (auto i = index; i != kPhotoSizeCount; ++i) {
		if (_images[i].location.valid()) {
			return i;
		}
	}
	return PhotoSizeIndex(PhotoSize::Large);
}

int PhotoData::existingSizeIndex(PhotoSize size) const {
	const auto index = PhotoSizeIndex(size);
	for (auto i = index; i != kPhotoSizeCount; ++i) {
		if (_images[i].location.valid() || _images[i].progressivePartSize) {
			return i;
		}
	}
	return PhotoSizeIndex(PhotoSize::Large);
}

bool PhotoData::hasExact(PhotoSize size) const {
	return _images[PhotoSizeIndex(size)].location.valid();
}

bool PhotoData::loading(PhotoSize size) const {
	const auto valid = validSizeIndex(size);
	const auto existing = existingSizeIndex(size);
	if (!_images[valid].loader) {
		return false;
	} else if (valid == existing) {
		return true;
	}
	return (_images[valid].loader->loadSize()
		>= _images[existing].progressivePartSize);
}

bool PhotoData::failed(PhotoSize size) const {
	const auto flags = _images[validSizeIndex(size)].flags;
	return (flags & Data::CloudFile::Flag::Failed);
}

void PhotoData::clearFailed(PhotoSize size) {
	_images[validSizeIndex(size)].flags &= ~Data::CloudFile::Flag::Failed;
}

const ImageLocation &PhotoData::location(PhotoSize size) const {
	return _images[validSizeIndex(size)].location;
}

int PhotoData::SideLimit() {
	return kPhotoSideLimit;
}

std::optional<QSize> PhotoData::size(PhotoSize size) const {
	const auto &provided = location(size);
	const auto result = QSize{ provided.width(), provided.height() };
	const auto limit = SideLimit();
	if (result.isEmpty()) {
		return std::nullopt;
	} else if (result.width() <= limit && result.height() <= limit) {
		return result;
	}
	const auto scaled = result.scaled(limit, limit, Qt::KeepAspectRatio);
	return QSize(std::max(scaled.width(), 1), std::max(scaled.height(), 1));
}

int PhotoData::imageByteSize(PhotoSize size) const {
	const auto existing = existingSizeIndex(size);
	if (const auto result = _images[existing].progressivePartSize) {
		return result;
	}
	return _images[validSizeIndex(size)].byteSize;
}

bool PhotoData::displayLoading() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	if (const auto loader = _images[index].loader.get()) {
		return !loader->finished()
			&& (!loader->loadingLocal() || !loader->autoLoading());
	}
	return (uploading() && !waitingForAlbum());
}

void PhotoData::cancel() {
	if (loading()) {
		_images[PhotoSizeIndex(PhotoSize::Large)].loader->cancel();
	}
}

float64 PhotoData::progress() const {
	if (uploading()) {
		if (uploadingData->size > 0) {
			const auto result = float64(uploadingData->offset)
				/ uploadingData->size;
			return std::clamp(result, 0., 1.);
		}
		return 0.;
	}
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return loading() ? _images[index].loader->currentProgress() : 0.;
}

bool PhotoData::cancelled() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return (_images[index].flags & Data::CloudFile::Flag::Cancelled);
}

void PhotoData::setWaitingForAlbum() {
	if (uploading()) {
		uploadingData->waitingForAlbum = true;
	}
}

bool PhotoData::waitingForAlbum() const {
	return uploading() && uploadingData->waitingForAlbum;
}

int32 PhotoData::loadOffset() const {
	const auto index = PhotoSizeIndex(PhotoSize::Large);
	return loading() ? _images[index].loader->currentOffset() : 0;
}

bool PhotoData::uploading() const {
	return (uploadingData != nullptr);
}

Image *PhotoData::getReplyPreview(
		Data::FileOrigin origin,
		not_null<PeerData*> context,
		bool spoiler) {
	if (!_replyPreview) {
		_replyPreview = std::make_unique<Data::ReplyPreview>(this);
	}
	return _replyPreview->image(origin, context, spoiler);
}

Image *PhotoData::getReplyPreview(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto spoiler = media && media->hasSpoiler();
	return getReplyPreview(item->fullId(), item->history()->peer, spoiler);
}

bool PhotoData::replyPreviewLoaded(bool spoiler) const {
	if (!_replyPreview) {
		return false;
	}
	return _replyPreview->loaded(spoiler);
}

#if 0 // mtp
void PhotoData::setRemoteLocation(
		int32 dc,
		uint64 access,
		const QByteArray &fileReference) {
	_fileReference = fileReference;
	if (_dc != dc || _access != access) {
		_dc = dc;
		_access = access;
	}
}

MTPInputPhoto PhotoData::mtpInput() const {
	return MTP_inputPhoto(
		MTP_long(id),
		MTP_long(_access),
		MTP_bytes(_fileReference));
}

QByteArray PhotoData::fileReference() const {
	return _fileReference;
}

void PhotoData::refreshFileReference(const QByteArray &value) {
	_fileReference = value;
	for (auto &image : _images) {
		image.location.refreshFileReference(value);
	}
}

void PhotoData::collectLocalData(not_null<PhotoData*> local) {
	if (local == this) {
		return;
	}

	for (auto i = 0; i != kPhotoSizeCount; ++i) {
		if (const auto from = local->_images[i].location.file().cacheKey()) {
			if (const auto to = _images[i].location.file().cacheKey()) {
				_owner->cache().copyIfEmpty(from, to);
			}
		}
	}
	if (const auto localMedia = local->activeMediaView()) {
		auto media = createMediaView();
		media->collectLocalData(localMedia.get());
		_owner->keepAlive(std::move(media));
	}
}
#endif

bool PhotoData::isNull() const {
	return !_images[PhotoSizeIndex(PhotoSize::Large)].location.valid();
}

void PhotoData::load(
		PhotoSize size,
		Data::FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading) {
	const auto valid = validSizeIndex(size);
	const auto existing = existingSizeIndex(size);

	// Could've changed, if the requested size didn't have a location.
	const auto validSize = static_cast<PhotoSize>(valid);
	const auto finalCheck = [=] {
		if (const auto active = activeMediaView()) {
			return !active->image(size);
		}
		return true;
	};
	const auto done = [=](QImage result, QByteArray bytes) {
		Expects(_images[valid].loader != nullptr);

		// Find out what progressive photo size have we loaded exactly.
		auto goodFor = validSize;
		const auto loadSize = _images[valid].loader->loadSize();
		if (valid > 0 && _images[valid].byteSize > loadSize) {
			for (auto i = valid; i != 0;) {
				--i;
				const auto required = _images[i].progressivePartSize;
				if (required > 0 && required <= loadSize) {
					goodFor = static_cast<PhotoSize>(i);
					break;
				}
			}
		}
		if (const auto active = activeMediaView()) {
			active->set(
				validSize,
				goodFor,
				ValidatePhotoImage(std::move(result), _images[valid]),
				std::move(bytes));
		}
		if (validSize == PhotoSize::Large && goodFor == validSize) {
			_owner->photoLoadDone(this);
		}
	};
	const auto fail = [=](bool started) {
		if (validSize == PhotoSize::Large) {
			_owner->photoLoadFail(this, started);
		}
	};
	const auto progress = [=] {
		if (validSize == PhotoSize::Large) {
			_owner->photoLoadProgress(this);
		}
	};
	Data::LoadCloudFile(
		&session(),
		_images[valid],
		origin,
		fromCloud,
		autoLoading,
		Data::kImageCacheTag,
		finalCheck,
		done,
		fail,
		progress,
		_images[existing].progressivePartSize);

	if (size == PhotoSize::Large) {
		_owner->notifyPhotoLayoutChanged(this);
	}
}

std::shared_ptr<PhotoMedia> PhotoData::createMediaView() {
	if (auto result = activeMediaView()) {
		return result;
	}
	auto result = std::make_shared<PhotoMedia>(this);
	_media = result;
	return result;
}

std::shared_ptr<PhotoMedia> PhotoData::activeMediaView() const {
	return _media.lock();
}

void PhotoData::updateImages(
		const QByteArray &inlineThumbnailBytes,
		const ImageWithLocation &small,
		const ImageWithLocation &thumbnail,
		const ImageWithLocation &large,
		const ImageWithLocation &videoSmall,
		const ImageWithLocation &videoLarge,
		crl::time videoStartTime) {
	_owner->photoFileIdUpdated(
		this,
		_images[PhotoSizeIndex(PhotoSize::Large)].location.tdbFileId(),
		large.location.tdbFileId());

	if (!inlineThumbnailBytes.isEmpty()
		&& _inlineThumbnailBytes.isEmpty()) {
		_inlineThumbnailBytes = inlineThumbnailBytes;
	}
	const auto update = [&](PhotoSize size, const ImageWithLocation &data) {
		const auto index = PhotoSizeIndex(size);
		Data::UpdateCloudFile(
			_images[index],
			data,
			owner().cache(),
			Data::kImageCacheTag,
			[=](Data::FileOrigin origin) { load(size, origin); },
			[=](QImage preloaded, QByteArray bytes) {
				if (const auto media = activeMediaView()) {
					media->set(
						size,
						size,
						ValidatePhotoImage(
							std::move(preloaded),
							_images[index]),
						std::move(bytes));
				}
			});
	};
	update(PhotoSize::Small, small);
	update(PhotoSize::Thumbnail, thumbnail);
	update(PhotoSize::Large, large);

	if (!videoLarge.location.valid()) {
		_videoSizes = nullptr;
	} else {
		if (!_videoSizes) {
			_videoSizes = std::make_unique<VideoSizes>();
		}
		_videoSizes->startTime = videoStartTime;
		constexpr auto large = PhotoSize::Large;
		constexpr auto small = PhotoSize::Small;
		Data::UpdateCloudFile(
			_videoSizes->large,
			videoLarge,
			owner().cache(),
			Data::kAnimationCacheTag,
			[&](Data::FileOrigin origin) { loadVideo(large, origin); });
		Data::UpdateCloudFile(
			_videoSizes->small,
			videoSmall,
			owner().cache(),
			Data::kAnimationCacheTag,
			[&](Data::FileOrigin origin) { loadVideo(small, origin); });
	}
}

[[nodiscard]] bool PhotoData::hasAttachedStickers() const {
	return _hasStickers;
}

void PhotoData::setHasAttachedStickers(bool value) {
	_hasStickers = value;
}

int PhotoData::width() const {
	return _images[PhotoSizeIndex(PhotoSize::Large)].location.width();
}

int PhotoData::height() const {
	return _images[PhotoSizeIndex(PhotoSize::Large)].location.height();
}

Data::CloudFile &PhotoData::videoFile(PhotoSize size) {
	Expects(_videoSizes != nullptr);

	return (size == PhotoSize::Small && hasVideoSmall())
		? _videoSizes->small
		: _videoSizes->large;
}

const Data::CloudFile &PhotoData::videoFile(PhotoSize size) const {
	Expects(_videoSizes != nullptr);

	return (size == PhotoSize::Small && hasVideoSmall())
		? _videoSizes->small
		: _videoSizes->large;
}


bool PhotoData::hasVideo() const {
	return _videoSizes != nullptr;
}

bool PhotoData::hasVideoSmall() const {
	return hasVideo() && _videoSizes->small.location.valid();
}

bool PhotoData::videoLoading(Data::PhotoSize size) const {
	return _videoSizes && videoFile(size).loader != nullptr;
}

bool PhotoData::videoFailed(Data::PhotoSize size) const {
	return _videoSizes
		&& (videoFile(size).flags & Data::CloudFile::Flag::Failed);
}

void PhotoData::loadVideo(Data::PhotoSize size, Data::FileOrigin origin) {
	if (!_videoSizes) {
		return;
	}
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeMediaView()) {
			return active->videoContent(size).isEmpty();
		}
		return true;
	};
	const auto done = [=](QByteArray result) {
		if (const auto active = activeMediaView()) {
			active->setVideo(size, std::move(result));
		}
	};
	Data::LoadCloudFile(
		&session(),
		videoFile(size),
		origin,
		LoadFromCloudOrLocal,
		autoLoading,
		Data::kAnimationCacheTag,
		finalCheck,
		done);
}

const ImageLocation &PhotoData::videoLocation(Data::PhotoSize size) const {
	static const auto empty = ImageLocation();
	return _videoSizes ? videoFile(size).location : empty;
}

int PhotoData::videoByteSize(Data::PhotoSize size) const {
	return _videoSizes ? videoFile(size).byteSize : 0;
}

crl::time PhotoData::videoStartPosition() const {
	return _videoSizes ? _videoSizes->startTime : crl::time(0);
}

void PhotoData::setVideoPlaybackFailed() {
	if (_videoSizes) {
		_videoSizes->playbackFailed = true;
	}
}

bool PhotoData::videoPlaybackFailed() const {
	return _videoSizes && _videoSizes->playbackFailed;
}

bool PhotoData::videoCanBePlayed() const {
	return hasVideo() && !videoPlaybackFailed();
}

auto PhotoData::createStreamingLoader(
	Data::FileOrigin origin,
	bool forceRemoteLoader) const
-> std::unique_ptr<Media::Streaming::Loader> {
	if (!hasVideo()) {
		return nullptr;
	}
	constexpr auto large = PhotoSize::Large;
	if (!forceRemoteLoader) {
		const auto media = activeMediaView();
		const auto bytes = media ? media->videoContent(large) : QByteArray();
		if (media && !bytes.isEmpty()) {
			return Media::Streaming::MakeBytesLoader(bytes);
		}
	}
	const auto tdb = std::get_if<TdbFileLocation>(
		&videoLocation(large).file().data);
	if (tdb) {
		return std::make_unique<Media::Streaming::LoaderTdb>(
			&session().tdb(),
			tdb->fileId,
			TdbFileLocation::BigFileBaseCacheKey(
				tdb->hash,
				kTdbLocationTypePhoto),
			videoByteSize(large));
	}
	return nullptr;
#if 0 // mtp
	return v::is<StorageFileLocation>(videoLocation(large).file().data)
		? std::make_unique<Media::Streaming::LoaderMtproto>(
			&session().downloader(),
			v::get<StorageFileLocation>(videoLocation(large).file().data),
			videoByteSize(large),
			origin)
		: nullptr;
#endif
}
