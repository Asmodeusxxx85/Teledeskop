/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tdb/tdb_sender.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h"

struct ChatAdminRightsInfo;
struct ChatRestrictionsInfo;

namespace Window {
class SessionNavigation;
} // namespace Window

class AddParticipantsBoxController : public ContactsBoxController {
public:
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChatData*> chat);
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel);
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn);

	explicit AddParticipantsBoxController(not_null<Main::Session*> session);
	explicit AddParticipantsBoxController(not_null<PeerData*> peer);
	AddParticipantsBoxController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> &&alreadyIn);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}

	void rowClicked(not_null<PeerListRow*> row) override;
	void itemDeselectedHook(not_null<PeerData*> peer) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;
	virtual bool needsInviteLinkButton();

private:
	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn,
		bool justCreated);

	QPointer<Ui::BoxContent> showBox(object_ptr<Ui::BoxContent> box) const;

	void addInviteLinkButton();
	void inviteSelectedUsers(
		not_null<PeerListBox*> box,
		Fn<void()> done) const;
	void subscribeToMigration();
	int alreadyInCount() const;
	bool isAlreadyIn(not_null<UserData*> user) const;
	int fullCount() const;
	void updateTitle();

	PeerData *_peer = nullptr;
	base::flat_set<not_null<UserData*>> _alreadyIn;

};

[[nodiscard]] std::vector<not_null<UserData*>> CollectForbiddenUsers(
	not_null<Main::Session*> session,
	const MTPUpdates &updates);
bool ChatInviteForbidden(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer,
	std::vector<not_null<UserData*>> forbidden);

// Adding an admin, banned or restricted user from channel members
// with search + contacts search + global search.
class AddSpecialBoxController
	: public PeerListController
	, public base::has_weak_ptr {
public:
	using Role = ParticipantsBoxController::Role;

	using AdminDoneCallback = Fn<void(
		not_null<UserData*> user,
		ChatAdminRightsInfo adminRights,
		const QString &rank)>;
	using BannedDoneCallback = Fn<void(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo bannedRights)>;
	AddSpecialBoxController(
		not_null<PeerData*> peer,
		Role role,
		AdminDoneCallback adminDoneCallback,
		BannedDoneCallback bannedDoneCallback);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override;

private:
	template <typename Callback>
	bool checkInfoLoaded(not_null<PeerData*> participant, Callback callback);

	void prepareChatRows(not_null<ChatData*> chat);
	void rebuildChatRows(not_null<ChatData*> chat);

	void showAdmin(not_null<UserData*> user, bool sure = false);
	void editAdminDone(
		not_null<UserData*> user,
		ChatAdminRightsInfo rights,
		const QString &rank);
	void showRestricted(not_null<UserData*> user, bool sure = false);
	void editRestrictedDone(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo rights);
	void kickUser(not_null<PeerData*> participant, bool sure = false);
	bool appendRow(not_null<PeerData*> participant);
	bool prependRow(not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> participant) const;

	void subscribeToMigration();
	void migrate(not_null<ChatData*> chat, not_null<ChannelData*> channel);

	QPointer<Ui::BoxContent> showBox(object_ptr<Ui::BoxContent> box) const;

	not_null<PeerData*> _peer;
#if 0 // goodToRemove
	MTP::Sender _api;
#endif
	Tdb::Sender _api;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	ParticipantsAdditionalData _additional;
	std::unique_ptr<ParticipantsOnlineSorter> _onlineSorter;
	Ui::BoxPointer _editBox;
	QPointer<Ui::BoxContent> _editParticipantBox;
	AdminDoneCallback _adminDoneCallback;
	BannedDoneCallback _bannedDoneCallback;

protected:
	bool _excludeSelf = true;

};

// Finds chat/channel members, then contacts, then global search results.
class AddSpecialBoxSearchController : public PeerListSearchController {
public:
	using Role = ParticipantsBoxController::Role;

	AddSpecialBoxSearchController(
		not_null<PeerData*> peer,
		not_null<ParticipantsAdditionalData*> additional);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

private:
	struct CacheEntry {
#if 0 // goodToRemove
		MTPchannels_ChannelParticipants result;
#endif
		Tdb::TLchatMembers result;
		int requestedCount = 0;
	};
	struct Query {
		QString text;
		int offset = 0;
	};

	void searchOnServer();
	bool searchParticipantsInCache();
	void searchParticipantsDone(
		mtpRequestId requestId,
#if 0 // goodToRemove
		const MTPchannels_ChannelParticipants &result,
#endif
		const Tdb::TLchatMembers &result,
		int requestedCount);
	bool searchGlobalInCache();
	void searchGlobalDone(
		mtpRequestId requestId,
		const Tdb::TLchats &result);
#if 0 // goodToRemove
		const MTPcontacts_Found &result);
#endif
	void requestParticipants();
	void addChatMembers(not_null<ChatData*> chat);
	void addChatsContacts();
	void requestGlobal();

	void subscribeToMigration();

	not_null<PeerData*> _peer;
	not_null<ParticipantsAdditionalData*> _additional;
#if 0 // goodToRemove
	MTP::Sender _api;
#endif
	Tdb::Sender _api;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _participantsLoaded = false;
	bool _chatsContactsAdded = false;
	bool _chatMembersAdded = false;
	bool _globalLoaded = false;
	std::map<QString, CacheEntry> _participantsCache;
	std::map<mtpRequestId, Query> _participantsQueries;
#if 0 // goodToRemove
	std::map<QString, MTPcontacts_Found> _globalCache;
#endif
	std::map<QString, Tdb::TLchats> _globalCache;
	std::map<mtpRequestId, QString> _globalQueries;

};
