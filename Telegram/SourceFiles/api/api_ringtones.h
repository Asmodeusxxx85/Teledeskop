/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "tdb/tdb_sender.h"

class ApiWrap;
class PeerData;

namespace Tdb {
class FileGenerator;
} // namespace Tdb

namespace Main {
class Session;
} // namespace Main

namespace Api {

class Ringtones final {
public:
	explicit Ringtones(not_null<ApiWrap*> api);

	using Ids = std::vector<DocumentId>;

	void requestList();
	void applyUpdate();
	void remove(DocumentId id);

	void upload(
		const QString &filename,
		const QString &filemime,
		const QByteArray &content);

	[[nodiscard]] const Ids &list() const;
	[[nodiscard]] rpl::producer<> listUpdates() const;
	[[nodiscard]] rpl::producer<QString> uploadFails() const;
	[[nodiscard]] rpl::producer<DocumentId> uploadDones() const;

	[[nodiscard]] int64 maxSize() const;
	[[nodiscard]] int maxSavedCount() const;
	[[nodiscard]] crl::time maxDuration() const;

private:
#if 0 // goodToRemove
	struct UploadedData {
		QString filename;
		QString filemime;
		QByteArray content;
	};
	void ready(const FullMsgId &msgId, const MTPInputFile &file);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_map<FullMsgId, UploadedData> _uploads;
#endif
	const not_null<Main::Session*> _session;
	Tdb::Sender _tdbApi;
	using UploadToken = QString;
	base::flat_map<UploadToken, std::shared_ptr<Tdb::FileGenerator>> _uploads;

	rpl::event_stream<QString> _uploadFails;
	rpl::event_stream<DocumentId> _uploadDones;

	struct {
		uint64 hash = 0;
		Ids documents;
		rpl::event_stream<> updates;
		mtpRequestId requestId = 0;
	} _list;

};

} // namespace Api
