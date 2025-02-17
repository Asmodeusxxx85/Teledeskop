/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "data/stickers/data_stickers_set.h"
#include "settings.h"

namespace Tdb {
class TLanimation;
class TLstickerSet;
class TLstickerSetInfo;
class TLDstickerSet;
class TLDstickerSetInfo;
class TLsticker;
class TLtrendingStickerSets;
class TLstickerType;
class TLDupdateStickerSet;
class TLDupdateInstalledStickerSets;
class TLDupdateTrendingStickerSets;
class TLDupdateRecentStickers;
class TLDupdateFavoriteStickers;
class TLDupdateSavedAnimations;
} // namespace Tdb

class HistoryItem;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {

class Session;
class DocumentMedia;

enum class StickersType : uchar {
	Stickers,
	Masks,
	Emoji,
};

[[nodiscard]] StickersType TypeFromTL(const Tdb::TLstickerType &type);

class Stickers final {
public:
	explicit Stickers(not_null<Session*> owner);

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	// for backward compatibility
	static constexpr auto DefaultSetId = 0;
	static constexpr auto CustomSetId = 0xFFFFFFFFFFFFFFFFULL;

	// For stickers panel, should not appear in Sets.
	static constexpr auto RecentSetId = 0xFFFFFFFFFFFFFFFEULL;
	static constexpr auto NoneSetId = 0xFFFFFFFFFFFFFFFDULL;
	static constexpr auto FeaturedSetId = 0xFFFFFFFFFFFFFFFBULL;

	// For cloud-stored recent stickers.
	static constexpr auto CloudRecentSetId = 0xFFFFFFFFFFFFFFFCULL;
	static constexpr auto CloudRecentAttachedSetId = 0xFFFFFFFFFFFFFFF9ULL;

	// For cloud-stored faved stickers.
	static constexpr auto FavedSetId = 0xFFFFFFFFFFFFFFFAULL;

	// For setting up megagroup sticker set.
	static constexpr auto MegagroupSetId = 0xFFFFFFFFFFFFFFEFULL;

	void notifyUpdated(StickersType type);
	[[nodiscard]] rpl::producer<StickersType> updated() const;
	[[nodiscard]] rpl::producer<> updated(StickersType type) const;
	void notifyRecentUpdated(StickersType type);
	[[nodiscard]] rpl::producer<StickersType> recentUpdated() const;
	[[nodiscard]] rpl::producer<> recentUpdated(StickersType type) const;
	void notifySavedGifsUpdated();
	[[nodiscard]] rpl::producer<> savedGifsUpdated() const;
	void notifyStickerSetInstalled(uint64 setId);
	[[nodiscard]] rpl::producer<uint64> stickerSetInstalled() const;
	void notifyEmojiSetInstalled(uint64 setId);
	[[nodiscard]] rpl::producer<uint64> emojiSetInstalled() const;

	void incrementSticker(not_null<DocumentData*> document);

	bool updateNeeded(crl::time now) const {
		return updateNeeded(_lastUpdate, now);
	}
	void setLastUpdate(crl::time update) {
		_lastUpdate = update;
	}
	bool recentUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastRecentUpdate, now);
	}
	void setLastRecentUpdate(crl::time update) {
		if (update) {
			notifyRecentUpdated(StickersType::Stickers);
		}
		_lastRecentUpdate = update;
	}
	bool masksUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastMasksUpdate, now);
	}
	void setLastMasksUpdate(crl::time update) {
		_lastMasksUpdate = update;
	}
	bool emojiUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastEmojiUpdate, now);
	}
	void setLastEmojiUpdate(crl::time update) {
		_lastEmojiUpdate = update;
	}
	bool recentAttachedUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastRecentAttachedUpdate, now);
	}
	void setLastRecentAttachedUpdate(crl::time update) {
		if (update) {
			notifyRecentUpdated(StickersType::Masks);
		}
		_lastRecentAttachedUpdate = update;
	}
	bool favedUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastFavedUpdate, now);
	}
	void setLastFavedUpdate(crl::time update) {
		_lastFavedUpdate = update;
	}
	bool featuredUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastFeaturedUpdate, now);
	}
	void setLastFeaturedUpdate(crl::time update) {
		_lastFeaturedUpdate = update;
	}
	bool featuredEmojiUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastFeaturedEmojiUpdate, now);
	}
	void setLastFeaturedEmojiUpdate(crl::time update) {
		_lastFeaturedEmojiUpdate = update;
	}
	bool savedGifsUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastSavedGifsUpdate, now);
	}
	void setLastSavedGifsUpdate(crl::time update) {
		_lastSavedGifsUpdate = update;
	}
	int featuredSetsUnreadCount() const {
		return _featuredSetsUnreadCount.current();
	}
	void setFeaturedSetsUnreadCount(int count) {
		_featuredSetsUnreadCount = count;
	}
	[[nodiscard]] rpl::producer<int> featuredSetsUnreadCountValue() const {
		return _featuredSetsUnreadCount.value();
	}
	const StickersSets &sets() const {
		return _sets;
	}
	StickersSets &setsRef() {
		return _sets;
	}
	const StickersSetsOrder &setsOrder() const {
		return _setsOrder;
	}
	StickersSetsOrder &setsOrderRef() {
		return _setsOrder;
	}
	const StickersSetsOrder &maskSetsOrder() const {
		return _maskSetsOrder;
	}
	StickersSetsOrder &maskSetsOrderRef() {
		return _maskSetsOrder;
	}
	const StickersSetsOrder &emojiSetsOrder() const {
		return _emojiSetsOrder;
	}
	StickersSetsOrder &emojiSetsOrderRef() {
		return _emojiSetsOrder;
	}
	const StickersSetsOrder &featuredSetsOrder() const {
		return _featuredSetsOrder;
	}
	StickersSetsOrder &featuredSetsOrderRef() {
		return _featuredSetsOrder;
	}
	const StickersSetsOrder &featuredEmojiSetsOrder() const {
		return _featuredEmojiSetsOrder;
	}
	StickersSetsOrder &featuredEmojiSetsOrderRef() {
		return _featuredEmojiSetsOrder;
	}
	const StickersSetsOrder &archivedSetsOrder() const {
		return _archivedSetsOrder;
	}
	StickersSetsOrder &archivedSetsOrderRef() {
		return _archivedSetsOrder;
	}
	const StickersSetsOrder &archivedMaskSetsOrder() const {
		return _archivedMaskSetsOrder;
	}
	StickersSetsOrder &archivedMaskSetsOrderRef() {
		return _archivedMaskSetsOrder;
	}
	const SavedGifs &savedGifs() const {
		return _savedGifs;
	}
	SavedGifs &savedGifsRef() {
		return _savedGifs;
	}
	void removeFromRecentSet(not_null<DocumentData*> document);

	void addSavedGif(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document);
	void checkSavedGif(not_null<HistoryItem*> item);

	void applyArchivedResult(
		const MTPDmessages_stickerSetInstallResultArchive &d);
	void installLocally(uint64 setId);
	void undoInstallLocally(uint64 setId);
	bool isFaved(not_null<const DocumentData*> document);
	void setFaved(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		bool faved);

	void setsReceived(const QVector<Tdb::TLstickerSetInfo> &data);
	void masksReceived(const QVector<Tdb::TLstickerSetInfo> &data);
	void emojiReceived(const QVector<Tdb::TLstickerSetInfo> &data);

	void apply(const Tdb::TLDupdateStickerSet &data);
	void apply(const Tdb::TLDupdateInstalledStickerSets &data);
	void apply(const Tdb::TLDupdateTrendingStickerSets &data);
	void apply(const Tdb::TLDupdateRecentStickers &data);
	void apply(const Tdb::TLDupdateFavoriteStickers &data);
	void apply(const Tdb::TLDupdateSavedAnimations &data);

#if 0 // mtp
	void setsReceived(const QVector<MTPStickerSet> &data, uint64 hash);
	void masksReceived(const QVector<MTPStickerSet> &data, uint64 hash);
	void emojiReceived(const QVector<MTPStickerSet> &data, uint64 hash);
	void specialSetReceived(
		uint64 setId,
		const QString &setTitle,
		const QVector<MTPDocument> &items,
		uint64 hash,
		const QVector<MTPStickerPack> &packs = QVector<MTPStickerPack>(),
		const QVector<MTPint> &usageDates = QVector<MTPint>());
	void featuredSetsReceived(const MTPmessages_FeaturedStickers &result);
	void featuredEmojiSetsReceived(
		const MTPmessages_FeaturedStickers &result);
	void gifsReceived(const QVector<MTPDocument> &items, uint64 hash);
#endif
	void specialSetReceived(
		uint64 setId,
		const QString &setTitle,
		const QVector<Tdb::TLsticker> &stickers);
	void featuredSetsReceived(const Tdb::TLtrendingStickerSets &data);
	void featuredEmojiSetsReceived(const Tdb::TLtrendingStickerSets &data);
	void gifsReceived(const QVector<Tdb::TLanimation> &items, uint64 hash);

	std::vector<not_null<DocumentData*>> getListByEmoji(
		std::vector<EmojiPtr> emoji,
		uint64 seed,
		bool forceAllResults = false);
	std::optional<std::vector<not_null<EmojiPtr>>> getEmojiListFromSet(
		not_null<DocumentData*> document);

#if 0 // mtp
	not_null<StickersSet*> feedSet(const MTPStickerSet &data);
	not_null<StickersSet*> feedSet(const MTPStickerSetCovered &data);
	not_null<StickersSet*> feedSetFull(const MTPDmessages_stickerSet &data);
	void feedSetStickers(
		not_null<StickersSet*> set,
		const QVector<MTPDocument> &documents,
		const QVector<MTPStickerPack> &packs);
	void feedSetCovers(
		not_null<StickersSet*> set,
		const QVector<MTPDocument> &documents);
	void newSetReceived(const MTPDmessages_stickerSet &set);

	QString getSetTitle(const MTPDstickerSet &s);
#endif

	StickersSet *feedSet(
		const Tdb::TLstickerSetInfo &value,
		StickersSetFlag sectionFlag = StickersSetFlag());
	StickersSet *feedSetFull(const Tdb::TLstickerSet &value);
	[[nodiscard]] QString getSetTitle(
		const Tdb::TLDstickerSetInfo &data) const;
	[[nodiscard]] QString getSetTitle(const Tdb::TLDstickerSet &data) const;

	RecentStickerPack &getRecentPack() const;

private:
	bool updateNeeded(crl::time lastUpdate, crl::time now) const {
		constexpr auto kUpdateTimeout = crl::time(3600'000);
		return (lastUpdate == 0)
			|| (now >= lastUpdate + kUpdateTimeout);
	}
	void checkFavedLimit(
		StickersSet &set,
		std::shared_ptr<ChatHelpers::Show> show);
	void setIsFaved(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		std::optional<std::vector<not_null<EmojiPtr>>> emojiList
			= std::nullopt);
	void setIsNotFaved(not_null<DocumentData*> document);
	void pushFavedToFront(
		StickersSet &set,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		const std::vector<not_null<EmojiPtr>> &emojiList);
	void moveFavedToFront(StickersSet &set, int index);
	void requestSetToPushFaved(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document);
#if 0 // mtp
	void setPackAndEmoji(
		StickersSet &set,
		StickersPack &&pack,
		const std::vector<TimeId> &&dates,
		const QVector<MTPStickerPack> &packs);
	void somethingReceived(
		const QVector<MTPStickerSet> &list,
		uint64 hash,
		StickersType type);
	void featuredReceived(
		const MTPDmessages_featuredStickers &data,
		StickersType type);
#endif
	void somethingReceived(
		const QVector<Tdb::TLstickerSetInfo> &list,
		StickersType type);
	void featuredReceived(
		const Tdb::TLtrendingStickerSets &data,
		StickersType type);

	const not_null<Session*> _owner;
	rpl::event_stream<StickersType> _updated;
	rpl::event_stream<StickersType> _recentUpdated;
	rpl::event_stream<> _savedGifsUpdated;
	rpl::event_stream<uint64> _stickerSetInstalled;
	rpl::event_stream<uint64> _emojiSetInstalled;
	crl::time _lastUpdate = 0;
	crl::time _lastRecentUpdate = 0;
	crl::time _lastFavedUpdate = 0;
	crl::time _lastFeaturedUpdate = 0;
	crl::time _lastSavedGifsUpdate = 0;
	crl::time _lastMasksUpdate = 0;
	crl::time _lastEmojiUpdate = 0;
	crl::time _lastFeaturedEmojiUpdate = 0;
	crl::time _lastRecentAttachedUpdate = 0;
	rpl::variable<int> _featuredSetsUnreadCount = 0;
	StickersSets _sets;
	StickersSetsOrder _setsOrder;
	StickersSetsOrder _maskSetsOrder;
	StickersSetsOrder _emojiSetsOrder;
	StickersSetsOrder _featuredSetsOrder;
	StickersSetsOrder _featuredEmojiSetsOrder;
	StickersSetsOrder _archivedSetsOrder;
	StickersSetsOrder _archivedMaskSetsOrder;
	SavedGifs _savedGifs;

};

} // namespace Data
