/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_isolated_emoji.h"
#include "ui/image/image.h"
#include "base/timer.h"

#include <crl/crl_object_on_queue.h>

namespace Tdb {
class TLDupdateAnimationSearchParameters;
} // namespace Tdb

class HistoryItem;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Lottie {
class SinglePlayer;
class FrameProvider;
struct ColorReplacements;
} // namespace Lottie

namespace Ui {
namespace Text {
class String;
} // namespace Text
namespace Emoji {
class UniversalImages;
} // namespace Emoji
} // namespace Ui

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Stickers {

using IsolatedEmoji = Ui::Text::IsolatedEmoji;

struct LargeEmojiImage {
	std::optional<Image> image;
	FnMut<void()> load;

	[[nodiscard]] static QSize Size();
};

class EmojiPack final {
public:
	using ViewElement = HistoryView::Element;

	struct Sticker {
		DocumentData *document = nullptr;
		const Lottie::ColorReplacements *replacements = nullptr;

		[[nodiscard]] bool empty() const {
			return (document == nullptr);
		}
		[[nodiscard]] explicit operator bool() const {
			return !empty();
		}
	};

	explicit EmojiPack(not_null<Main::Session*> session);
	~EmojiPack();

	bool add(not_null<ViewElement*> view);
	void remove(not_null<const ViewElement*> view);

#if 0 // mtp
	[[nodiscard]] Sticker stickerForEmoji(EmojiPtr emoji);
	[[nodiscard]] Sticker stickerForEmoji(const IsolatedEmoji &emoji);
#endif
	[[nodiscard]] std::shared_ptr<LargeEmojiImage> image(EmojiPtr emoji);

#if 0 // mtp
	[[nodiscard]] EmojiPtr chooseInteractionEmoji(
		not_null<HistoryItem*> item) const;
	[[nodiscard]] EmojiPtr chooseInteractionEmoji(
		const QString &emoticon) const;
	[[nodiscard]] auto animationsForEmoji(EmojiPtr emoji) const
		-> const base::flat_map<int, not_null<DocumentData*>> &;
	[[nodiscard]] bool hasAnimationsFor(not_null<HistoryItem*> item) const;
	[[nodiscard]] bool hasAnimationsFor(const QString &emoticon) const;
	[[nodiscard]] int animationsVersion() const {
		return _animationsVersion;
	}
	[[nodiscard]] rpl::producer<> refreshed() const {
		return _refreshed.events();
	}
#endif

	[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> effectPlayer(
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool premium);

	struct GifSection {
		EmojiPtr emoji = nullptr;
		DocumentData *document = nullptr;

		friend inline constexpr auto operator<=>(
			GifSection,
			GifSection) = default;
	};
	void gifSectionsRefresh(
		const Tdb::TLDupdateAnimationSearchParameters &data);
	[[nodiscard]] auto gifSectionsValue() const
		-> rpl::producer<std::vector<GifSection>>;

private:
	class ImageLoader;

	void refresh();
	void refreshDelayed();
#if 0 // mtp
	void refreshAnimations();
	void applySet(const MTPDmessages_stickerSet &data);
	void applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map);
	void applyAnimationsSet(const MTPDmessages_stickerSet &data);
	[[nodiscard]] auto collectStickers(const QVector<MTPDocument> &list) const
		-> base::flat_map<uint64, not_null<DocumentData*>>;
	[[nodiscard]] auto collectAnimationsIndices(
		const QVector<MTPStickerPack> &packs) const
		-> base::flat_map<uint64, base::flat_set<int>>;
#endif
	void gifSectionsPush();
	void refreshAll();
	void refreshItems(EmojiPtr emoji);
	void refreshItems(const base::flat_set<not_null<ViewElement*>> &list);
	void refreshItems(const base::flat_set<not_null<HistoryItem*>> &items);

	const not_null<Main::Session*> _session;
#if 0 // mtp
	base::flat_map<EmojiPtr, not_null<DocumentData*>> _map;
#endif
	base::flat_map<
		IsolatedEmoji,
		base::flat_set<not_null<HistoryView::Element*>>> _items;
	base::flat_map<EmojiPtr, std::weak_ptr<LargeEmojiImage>> _images;
	mtpRequestId _requestId = 0;

	base::flat_set<not_null<HistoryView::Element*>> _onlyCustomItems;

	int _animationsVersion = 0;
	base::flat_map<
		EmojiPtr,
		base::flat_map<int, not_null<DocumentData*>>> _animations;
	mtpRequestId _animationsRequestId = 0;

	base::flat_map<
		not_null<DocumentData*>,
		std::weak_ptr<Lottie::FrameProvider>> _sharedProviders;

	rpl::event_stream<> _refreshed;

	rpl::variable<std::vector<GifSection>> _gifSections;
	std::vector<GifSection> _gifSectionsEmojiList;
	base::flat_map<EmojiPtr, mtpRequestId> _gifSectionsResolves;

	rpl::lifetime _lifetime;

};

[[nodiscard]] const Lottie::ColorReplacements *ReplacementsByFitzpatrickType(
	int type);

} // namespace Stickers
