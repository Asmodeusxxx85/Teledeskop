/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_transcribes.h"

#include "history/history_item.h"
#include "history/history.h"
#include "main/main_session.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "apiwrap.h"

#include "tdb/tdb_tl_scheme.h"

namespace Api {
namespace {

using namespace Tdb;

} // namespace

Transcribes::Transcribes(not_null<ApiWrap*> api)
: _session(&api->session())
#if 0 // mtp
, _api(&api->instance()) {
#endif
, _api(&api->sender()) {
}

void Transcribes::toggle(not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	auto i = _map.find(id);
	if (i == _map.end()) {
		load(item);
		//_session->data().requestItemRepaint(item);
		_session->data().requestItemResize(item);
	} else if (!i->second.requestId) {
		i->second.shown = !i->second.shown;
#if 0 // mtp
		if (i->second.roundview) {
#endif
		if (i->second.roundview && !i->second.pending) {
			_session->data().requestItemViewRefresh(item);
		}
		_session->data().requestItemResize(item);
	}
}

const Transcribes::Entry &Transcribes::entry(
		not_null<HistoryItem*> item) const {
	static auto empty = Entry();
	const auto i = _map.find(item->fullId());
	return (i != _map.end()) ? i->second : empty;
}

#if 0 // mtp
void Transcribes::apply(const MTPDupdateTranscribedAudio &update) {
	const auto id = update.vtranscription_id().v;
	const auto i = _ids.find(id);
	if (i == _ids.end()) {
		return;
	}
	const auto j = _map.find(i->second);
	if (j == _map.end()) {
		return;
	}
	const auto text = qs(update.vtext());
	j->second.result = text;
	j->second.pending = update.is_pending();
	if (const auto item = _session->data().message(i->second)) {
		if (j->second.roundview) {
			_session->data().requestItemViewRefresh(item);
		}
		_session->data().requestItemResize(item);
	}
}
#endif

void Transcribes::apply(
		not_null<HistoryItem*> item,
		const TLspeechRecognitionResult &result,
		bool roundview) {
	auto &entry = _map[item->fullId()];
	entry.roundview = roundview;
	result.match([&](const TLDspeechRecognitionResultText &result) {
		entry.requestId = 0;
		entry.result = result.vtext().v;
		entry.pending = false;
	}, [&](const TLDspeechRecognitionResultPending &result) {
		entry.result = result.vpartial_text().v;
		entry.pending = true;
	}, [&](const TLDspeechRecognitionResultError &result) {
		entry.requestId = 0;
		entry.pending = false;
		entry.failed = true;
		if (result.verror().data().vmessage().v == u"MSG_VOICE_TOO_LONG"_q) {
			entry.toolong = true;
		}
	});
	if (entry.roundview && !entry.pending) {
		_session->data().requestItemViewRefresh(item);
	}
	_session->data().requestItemResize(item);
}

void Transcribes::load(not_null<HistoryItem*> item) {
	if (!item->isHistoryEntry() || item->isLocal()) {
		return;
	}
	const auto toggleRound = [](not_null<HistoryItem*> item, Entry &entry) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				if (document->isVideoMessage()) {
					entry.roundview = true;
					document->owner().requestItemViewRefresh(item);
				}
			}
		}
	};
	const auto id = item->fullId();
	const auto requestId = _api.request(TLrecognizeSpeech(
		peerToTdbChat(item->history()->peer->id),
		tl_int53(item->id.bare)
	)).done([=] {
		auto &entry = _map[id];
		if (entry.requestId) {
			entry.requestId = 0;
			if (const auto item = _session->data().message(id)) {
				if (!entry.pending) {
					toggleRound(item, entry);
				}
				_session->data().requestItemResize(item);
			}
		}
	}).fail([=](const Error &error) {
#if 0 // mtp
	const auto requestId = _api.request(MTPmessages_TranscribeAudio(
		item->history()->peer->input,
		MTP_int(item->id)
	)).done([=](const MTPmessages_TranscribedAudio &result) {
		const auto &data = result.data();
		auto &entry = _map[id];
		entry.requestId = 0;
		entry.pending = data.is_pending();
		entry.result = qs(data.vtext());
		_ids.emplace(data.vtranscription_id().v, id);
		if (const auto item = _session->data().message(id)) {
			toggleRound(item, entry);
			_session->data().requestItemResize(item);
		}
	}).fail([=](const MTP::Error &error) {
#endif
		auto &entry = _map[id];
		entry.requestId = 0;
		entry.pending = false;
		entry.failed = true;
#if 0 // mtp
		if (error.type() == u"MSG_VOICE_TOO_LONG"_q) {
#endif
		if (error.message == u"MSG_VOICE_TOO_LONG"_q) {
			entry.toolong = true;
		}
		if (const auto item = _session->data().message(id)) {
			toggleRound(item, entry);
			_session->data().requestItemResize(item);
		}
	}).send();
	auto &entry = _map.emplace(id).first->second;
	entry.requestId = requestId;
	entry.shown = true;
	entry.failed = false;
#if 0 // mtp
	entry.pending = false;
#endif
	entry.pending = true;
}

} // namespace Api
