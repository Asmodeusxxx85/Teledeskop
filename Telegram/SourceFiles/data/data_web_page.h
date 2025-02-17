/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "data/data_photo.h"
#include "data/data_document.h"

namespace Tdb {
class TLwebPage;
} // namespace Tdb

class ChannelData;

namespace Data {
class Session;
} // namespace Data

enum class WebPageType : uint8 {
	None,

	Message,

	Group,
	GroupWithRequest,
	Channel,
	ChannelWithRequest,
	ChannelBoost,

	Photo,
	Video,
	Document,

	User,
	Bot,
	Profile,
	BotApp,

	WallPaper,
	Theme,
	Story,

	Article,
	ArticleWithIV,

	VoiceChat,
	Livestream,
};
[[nodiscard]] WebPageType ParseWebPageType(const MTPDwebPage &type);

struct WebPageCollage {
	using Item = std::variant<PhotoData*, DocumentData*>;

	WebPageCollage() = default;
#if 0 // mtp
	explicit WebPageCollage(
		not_null<Data::Session*> owner,
		const MTPDwebPage &data);
#endif

	std::vector<Item> items;

};

struct WebPageData {
	WebPageData(not_null<Data::Session*> owner, const WebPageId &id);

	[[nodiscard]] static WebPageId IdFromTdb(const Tdb::TLwebPage &data);
	void setFromTdb(const Tdb::TLwebPage &data);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	bool applyChanges(
		WebPageType newType,
		const QString &newUrl,
		const QString &newDisplayUrl,
		const QString &newSiteName,
		const QString &newTitle,
		const TextWithEntities &newDescription,
		FullStoryId newStoryId,
		PhotoData *newPhoto,
		DocumentData *newDocument,
#if 0 // mtp
		WebPageCollage &&newCollage,
#endif
		int newDuration,
		const QString &newAuthor,
		bool newHasLargeMedia,
		int newPendingTill);

#if 0 // mtp
	static void ApplyChanges(
		not_null<Main::Session*> session,
		ChannelData *channel,
		const MTPmessages_Messages &result);
#endif
	void setCollage(WebPageCollage &&newCollage);

	[[nodiscard]] QString displayedSiteName() const;
	[[nodiscard]] bool computeDefaultSmallMedia() const;
	[[nodiscard]] bool suggestEnlargePhoto() const;

	const WebPageId id = 0;
	WebPageType type = WebPageType::None;
	QString url;
	QString displayUrl;
	QString siteName;
	QString title;
	TextWithEntities description;
	FullStoryId storyId;
	QString author;
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	WebPageCollage collage;
	int duration = 0;
	TimeId pendingTill = 0;
	uint32 version : 30 = 0;
	uint32 hasLargeMedia : 1 = 0;
	uint32 failed : 1 = 0;


private:
	void replaceDocumentGoodThumbnail();

	const not_null<Data::Session*> _owner;
	mtpRequestId _collageRequestId = 0;

};
