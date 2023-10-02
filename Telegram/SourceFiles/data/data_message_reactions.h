/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_message_reaction_id.h"
#include "data/stickers/data_custom_emoji.h"

namespace Tdb {
class TLDupdateActiveEmojiReactions;
class TLDupdateDefaultReactionType;
class TLemojiReaction;
class TLunreadReaction;
class TLmessageReaction;
class TLDstickers;
class TLavailableReactions;
} // namespace Tdb

namespace Ui {
class AnimatedIcon;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Data {

class DocumentMedia;
class Session;

struct Reaction {
	ReactionId id;
	QString title;
	//not_null<DocumentData*> staticIcon;
	not_null<DocumentData*> appearAnimation;
	not_null<DocumentData*> selectAnimation;
	//not_null<DocumentData*> activateAnimation;
	//not_null<DocumentData*> activateEffects;
	DocumentData *centerIcon = nullptr;
	DocumentData *aroundAnimation = nullptr;
	bool active = false;
	bool premium = false;
};

struct PossibleItemReactionsRef {
	std::vector<not_null<const Reaction*>> recent;
	bool morePremiumAvailable = false;
	bool customAllowed = false;
};

struct PossibleItemReactions {
	PossibleItemReactions() = default;
	explicit PossibleItemReactions(const PossibleItemReactionsRef &other);

	std::vector<Reaction> recent;
	bool morePremiumAvailable = false;
	bool customAllowed = false;
};

[[nodiscard]] PossibleItemReactionsRef LookupPossibleReactions(
	not_null<HistoryItem*> item);

[[nodiscard]] PossibleItemReactionsRef ParsePossibleReactions(
	not_null<Main::Session*> session,
	const Tdb::TLavailableReactions &available);

class Reactions final : private CustomEmojiManager::Listener {
public:
	explicit Reactions(not_null<Session*> owner);
	~Reactions();

	[[nodiscard]] Session &owner() const {
		return *_owner;
	}
	[[nodiscard]] Main::Session &session() const;

#if 0 // mtp
	void refreshTop();
	void refreshRecent();
	void refreshRecentDelayed();
	void refreshDefault();
#endif
	void refreshActive(const Tdb::TLDupdateActiveEmojiReactions &data);
	void refreshFavorite(const Tdb::TLDupdateDefaultReactionType &data);

	enum class Type {
		Active,
		Recent,
		Top,
		All,
	};
	[[nodiscard]] const std::vector<Reaction> &list(Type type) const;
	[[nodiscard]] ReactionId favoriteId() const;
	[[nodiscard]] const Reaction *favorite() const;
	void setFavorite(const ReactionId &id);
	[[nodiscard]] DocumentData *chooseGenericAnimation(
		not_null<DocumentData*> custom) const;

	[[nodiscard]] rpl::producer<> topUpdates() const;
	[[nodiscard]] rpl::producer<> recentUpdates() const;
	[[nodiscard]] rpl::producer<> defaultUpdates() const;
	[[nodiscard]] rpl::producer<> favoriteUpdates() const;

	enum class ImageSize {
		BottomInfo,
		InlineList,
	};
	void preloadImageFor(const ReactionId &emoji);
	void preloadAnimationsFor(const ReactionId &emoji);
	[[nodiscard]] QImage resolveImageFor(
		const ReactionId &emoji,
		ImageSize size);

#if 0 // mtp
	void send(not_null<HistoryItem*> item, bool addToRecent);
	[[nodiscard]] bool sending(not_null<HistoryItem*> item) const;

	void poll(not_null<HistoryItem*> item, crl::time now);

	void updateAllInHistory(not_null<PeerData*> peer, bool enabled);
#endif

	void clearTemporary();
	[[nodiscard]] Reaction *lookupTemporary(const ReactionId &id);

#if 0 // mtp
	[[nodiscard]] static bool HasUnread(const MTPMessageReactions &data);
	static void CheckUnknownForUnread(
		not_null<Session*> owner,
		const MTPMessage &message);
#endif

private:
	struct ImageSet {
		QImage bottomInfo;
		QImage inlineList;
		std::shared_ptr<DocumentMedia> media;
		std::unique_ptr<Ui::AnimatedIcon> icon;
		bool fromSelectAnimation = false;
	};
	struct EmojiResolved {
		QString emoji;
		mtpRequestId requestId = 0;
		bool resolved = false;
		bool active = false;
	};

	[[nodiscard]] not_null<CustomEmojiManager::Listener*> resolveListener();
	void customEmojiResolveDone(not_null<DocumentData*> document) override;

#if 0 // mtp
	void requestTop();
	void requestRecent();
	void requestDefault();
#endif
	void requestGeneric();

#if 0 // mtp
	void updateTop(const MTPDmessages_reactions &data);
	void updateRecent(const MTPDmessages_reactions &data);
	void updateDefault(const MTPDmessages_availableReactions &data);
	void updateGeneric(const MTPDmessages_stickerSet &data);

	void recentUpdated();
#endif
	void defaultUpdated();

	[[nodiscard]] std::optional<Reaction> resolveById(const ReactionId &id);
	[[nodiscard]] std::vector<Reaction> resolveByIds(
		const std::vector<ReactionId> &ids,
		base::flat_set<ReactionId> &unresolved);
	void resolve(const ReactionId &id);
	void applyFavorite(const ReactionId &id);

#if 0 // mtp
	[[nodiscard]] std::optional<Reaction> parse(
		const MTPAvailableReaction &entry);
#endif
	void updateFromData(const Tdb::TLDupdateActiveEmojiReactions &data);
	[[nodiscard]] std::optional<Reaction> parse(
		const Tdb::TLemojiReaction &entry);
	void updateGeneric(const Tdb::TLDstickers &data);
	void resolveEmojiNext();
	void resolveEmoji(const QString &emoji);
	void resolveEmoji(not_null<EmojiResolved*> entry);
	void checkAllActiveResolved();
	[[nodiscard]] bool allActiveResolved() const;

	void loadImage(
		ImageSet &set,
		not_null<DocumentData*> document,
		bool fromSelectAnimation);
	void setAnimatedIcon(ImageSet &set);
	void resolveImages();
	void downloadTaskFinished();

#if 0 // mtp
	void repaintCollected();
	void pollCollected();
#endif

	const not_null<Session*> _owner;

	std::vector<Reaction> _active;
	std::vector<Reaction> _available;
	std::vector<Reaction> _recent;
	std::vector<ReactionId> _recentIds;
	base::flat_set<ReactionId> _unresolvedRecent;
	std::vector<Reaction> _top;
	std::vector<ReactionId> _topIds;
	base::flat_set<ReactionId> _unresolvedTop;
	std::vector<not_null<DocumentData*>> _genericAnimations;
	ReactionId _favoriteId;
	ReactionId _unresolvedFavoriteId;
	std::optional<Reaction> _favorite;
	base::flat_map<
		not_null<DocumentData*>,
		std::shared_ptr<DocumentMedia>> _iconsCache;
	base::flat_map<
		not_null<DocumentData*>,
		std::shared_ptr<DocumentMedia>> _genericCache;
	rpl::event_stream<> _topUpdated;
	rpl::event_stream<> _recentUpdated;
	rpl::event_stream<> _defaultUpdated;
	rpl::event_stream<> _favoriteUpdated;

	// We need &i->second stay valid while inserting new items.
	// So we use std::map instead of base::flat_map here.
	// Otherwise we could use flat_map<DocumentId, unique_ptr<Reaction>>.
	std::map<DocumentId, Reaction> _temporary;

#if 0 // mtp
	base::Timer _topRefreshTimer;
	mtpRequestId _topRequestId = 0;
	uint64 _topHash = 0;

	mtpRequestId _recentRequestId = 0;
	bool _recentRequestScheduled = false;
	uint64 _recentHash = 0;

	mtpRequestId _defaultRequestId = 0;
	int32 _defaultHash = 0;
#endif
	std::vector<EmojiResolved> _emojiReactions;

	mtpRequestId _genericRequestId = 0;

	base::flat_map<ReactionId, ImageSet> _images;
	rpl::lifetime _imagesLoadLifetime;
	bool _waitingForList = false;

#if 0 // mtp
	base::flat_map<FullMsgId, mtpRequestId> _sentRequests;

	base::flat_map<not_null<HistoryItem*>, crl::time> _repaintItems;
	base::Timer _repaintTimer;
	base::flat_set<not_null<HistoryItem*>> _pollItems;
	base::flat_set<not_null<HistoryItem*>> _pollingItems;
	mtpRequestId _pollRequestId = 0;

	mtpRequestId _saveFaveRequestId = 0;
#endif

	rpl::lifetime _lifetime;

};

struct RecentReaction {
	not_null<PeerData*> peer;
	bool unread = false;
	bool big = false;
	bool my = false;

	friend inline auto operator<=>(
		const RecentReaction &a,
		const RecentReaction &b) = default;
	friend inline bool operator==(
		const RecentReaction &a,
		const RecentReaction &b) = default;
};

class MessageReactions final {
public:
	explicit MessageReactions(not_null<HistoryItem*> item);

	void add(const ReactionId &id, bool addToRecent);
	void remove(const ReactionId &id);

#if 0 // mtp
	bool change(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent,
		bool ignoreChosen);
	[[nodiscard]] bool checkIfChanged(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessagePeerReaction> &recent,
		bool ignoreChosen) const;
#endif
	bool change(const QVector<Tdb::TLmessageReaction> &list);
	bool change(const QVector<Tdb::TLunreadReaction> &list);

	[[nodiscard]] const std::vector<MessageReaction> &list() const;
	[[nodiscard]] auto recent() const
		-> const base::flat_map<ReactionId, std::vector<RecentReaction>> &;
	[[nodiscard]] std::vector<ReactionId> chosen() const;
	[[nodiscard]] bool empty() const;

	[[nodiscard]] bool hasUnread() const;
	void markRead();

private:
	const not_null<HistoryItem*> _item;

	std::vector<MessageReaction> _list;
	base::flat_map<ReactionId, std::vector<RecentReaction>> _recent;

};

} // namespace Data
