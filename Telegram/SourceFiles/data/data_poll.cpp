/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_poll.h"

#include "data/data_user.h"
#include "data/data_session.h"
#include "base/call_delayed.h"
#include "main/main_session.h"
#include "api/api_text_entities.h"
#include "ui/text/text_options.h"
#include "tdb/tdb_tl_scheme.h"

namespace {

constexpr auto kShortPollTimeout = 30 * crl::time(1000);
constexpr auto kReloadAfterAutoCloseDelay = crl::time(1000);

using namespace Tdb;

const PollAnswer *AnswerByOption(
		const std::vector<PollAnswer> &list,
		const QByteArray &option) {
	const auto i = ranges::find(
		list,
		option,
		[](const PollAnswer &a) { return a.option; });
	return (i != end(list)) ? &*i : nullptr;
}

PollAnswer *AnswerByOption(
		std::vector<PollAnswer> &list,
		const QByteArray &option) {
	return const_cast<PollAnswer*>(AnswerByOption(
		std::as_const(list),
		option));
}

std::optional<int> CorrectAnswerIndexFromTL(const TLpoll &poll) {
	return poll.data().vtype().match([&](const TLDpollTypeQuiz &data) {
		return std::make_optional<int>(data.vcorrect_option_id().v);
	}, [](auto &&) -> std::optional<int> {
		return std::nullopt;
	});
}

} // namespace

PollData::PollData(not_null<Data::Session*> owner, PollId id)
: id(id)
, _owner(owner) {
}

PollId PollData::IdFromTdb(const TLpoll &data) {
	return data.data().vid().v;
}

Data::Session &PollData::owner() const {
	return *_owner;
}

Main::Session &PollData::session() const {
	return _owner->session();
}

bool PollData::closeByTimer() {
	if (closed()) {
		return false;
	}
	_flags |= Flag::Closed;
	++version;
	base::call_delayed(kReloadAfterAutoCloseDelay, &_owner->session(), [=] {
		_lastResultsUpdate = -1; // Force reload results.
		++version;
		_owner->notifyPollUpdateDelayed(this);
	});
	return true;
}

bool PollData::applyChanges(const MTPDpoll &poll) {
	Expects(poll.vid().v == id);

	const auto newQuestion = qs(poll.vquestion());
	const auto newFlags = (poll.is_closed() ? Flag::Closed : Flag(0))
		| (poll.is_public_voters() ? Flag::PublicVotes : Flag(0))
		| (poll.is_multiple_choice() ? Flag::MultiChoice : Flag(0))
		| (poll.is_quiz() ? Flag::Quiz : Flag(0));
	const auto newCloseDate = poll.vclose_date().value_or_empty();
	const auto newClosePeriod = poll.vclose_period().value_or_empty();
	auto newAnswers = ranges::views::all(
		poll.vanswers().v
	) | ranges::views::transform([](const MTPPollAnswer &data) {
		return data.match([](const MTPDpollAnswer &answer) {
			auto result = PollAnswer();
			result.option = answer.voption().v;
			result.text = qs(answer.vtext());
			return result;
		});
	}) | ranges::views::take(
		kMaxOptions
	) | ranges::to_vector;

	const auto changed1 = (question != newQuestion)
		|| (closeDate != newCloseDate)
		|| (closePeriod != newClosePeriod)
		|| (_flags != newFlags);
	const auto changed2 = (answers != newAnswers);
	if (!changed1 && !changed2) {
		return false;
	}
	if (changed1) {
		question = newQuestion;
		closeDate = newCloseDate;
		closePeriod = newClosePeriod;
		_flags = newFlags;
	}
	if (changed2) {
		std::swap(answers, newAnswers);
		for (const auto &old : newAnswers) {
			if (const auto current = answerByOption(old.option)) {
				current->votes = old.votes;
				current->chosen = old.chosen;
				current->correct = old.correct;
			}
		}
	}
	++version;
	return true;
}

bool PollData::applyChanges(const TLpoll &poll) {
	Expects(poll.data().vid().v == id);

	const auto &fields = poll.data();
	const auto newQuestion = fields.vquestion().v;
	const auto typeFlag = fields.vtype().match([&](const TLDpollTypeQuiz &) {
		return Flag::Quiz;
	}, [&](const TLDpollTypeRegular &data) {
		return data.vallow_multiple_answers().v
			? Flag::MultiChoice
			: Flag(0);
	});
	const auto newFlags = typeFlag
		| (fields.vis_closed().v ? Flag::Closed : Flag(0))
		| (fields.vis_anonymous().v ? Flag(0) : Flag::PublicVotes);
	const auto newCloseDate = fields.vclose_date().v;
	const auto newClosePeriod = fields.vopen_period().v;
	auto newAnswers = ranges::views::all(
		fields.voptions().v
	) | ranges::views::transform([](const TLpollOption &data) {
		auto result = PollAnswer();
		result.option = data.data().vtext().v.toUtf8();
		result.text = data.data().vtext().v;
		result.votes = data.data().vvoter_count().v;
		result.chosen = data.data().vis_chosen().v
			|| data.data().vis_being_chosen().v;
		return result;
	}) | ranges::views::take(
		kMaxOptions
	) | ranges::to_vector;

	const auto changed1 = (question != newQuestion)
		|| (closeDate != newCloseDate)
		|| (closePeriod != newClosePeriod)
		|| (_flags != newFlags);
	const auto changed2 = (answers != newAnswers);
	const auto changed3 = applyResults(poll);
	if (!changed1 && !changed2 && !changed3) {
		return false;
	}
	if (changed1) {
		question = newQuestion;
		closeDate = newCloseDate;
		closePeriod = newClosePeriod;
		_flags = newFlags;
	}
	if (changed2) {
		std::swap(answers, newAnswers);
	}
	++version;
	return true;
}

bool PollData::applyResults(const TLpoll &poll) {
	return poll.match([&](const TLDpoll &results) {
		_lastResultsUpdate = crl::now();

		const auto newTotalVoters = results.vtotal_voter_count().v;
		auto changed = (newTotalVoters != totalVoters);

		if (const auto correctOption = CorrectAnswerIndexFromTL(poll)) {
			const auto set = [&](int id, bool v) {
				if (id < answers.size()) {
					answers[id].correct = v;
				}
			};
			const auto wasCorrectIt = ranges::find_if(
				answers,
				&PollAnswer::correct);
			if (wasCorrectIt == end(answers)) {
				changed = true;
			} else {
				const auto wasCorrectIndex = std::distance(
					begin(answers),
					wasCorrectIt);
				if (wasCorrectIndex != (*correctOption)) {
					set(wasCorrectIndex, false);
					changed = true;
				}
			}
			set(*correctOption, true);
		}

		for (const auto &result : results.voptions().v) {
			if (applyResultToAnswers(result.data(), false)) {
				changed = true;
			}
		}
		{
			const auto &recent = results.vrecent_voter_ids();
			const auto recentChanged = !ranges::equal(
				recentVoters,
				recent.v,
				ranges::equal_to(),
				&PeerData::id,
				&peerFromSender);
			if (recentChanged) {
				changed = true;
				recentVoters = ranges::views::all(
					recent.v
				) | ranges::views::transform([&](const TLmessageSender &d) {
					const auto peer = _owner->peer(peerFromSender(d));
					return peer->isMinimalLoaded() ? peer.get() : nullptr;
				}) | ranges::views::filter([](PeerData *peer) {
					return peer != nullptr;
				}) | ranges::views::transform([](PeerData *peer) {
					return not_null(peer);
				}) | ranges::to_vector;
			}
		}
		results.vtype().match([&](const TLDpollTypeQuiz &data) {
			auto newSolution = Api::FormattedTextFromTdb(data.vexplanation());
			if (solution != newSolution) {
				solution = std::move(newSolution);
				changed = true;
			}
		}, [](auto &&) {
		});
		if (!changed) {
			return false;
		}
		totalVoters = newTotalVoters;
		++version;
		return changed;
	});
}

#if 0 // mtp
bool PollData::applyResults(const MTPPollResults &results) {
	return results.match([&](const MTPDpollResults &results) {
		_lastResultsUpdate = crl::now();

		const auto newTotalVoters =
			results.vtotal_voters().value_or(totalVoters);
		auto changed = (newTotalVoters != totalVoters);
		if (const auto list = results.vresults()) {
			for (const auto &result : list->v) {
				if (applyResultToAnswers(result, results.is_min())) {
					changed = true;
				}
			}
		}
		if (const auto recent = results.vrecent_voters()) {
			const auto recentChanged = !ranges::equal(
				recentVoters,
				recent->v,
				ranges::equal_to(),
				&PeerData::id,
				peerFromMTP);
			if (recentChanged) {
				changed = true;
				recentVoters = ranges::views::all(
					recent->v
				) | ranges::views::transform([&](MTPPeer peerId) {
					const auto peer = _owner->peer(peerFromMTP(peerId));
					return peer->isMinimalLoaded() ? peer.get() : nullptr;
				}) | ranges::views::filter([](PeerData *peer) {
					return peer != nullptr;
				}) | ranges::views::transform([](PeerData *peer) {
					return not_null(peer);
				}) | ranges::to_vector;
			}
		}
		if (results.vsolution()) {
			auto newSolution = TextWithEntities{
				results.vsolution().value_or_empty(),
				Api::EntitiesFromMTP(
					&_owner->session(),
					results.vsolution_entities().value_or_empty())
			};
			if (solution != newSolution) {
				solution = std::move(newSolution);
				changed = true;
			}
		}
		if (!changed) {
			return false;
		}
		totalVoters = newTotalVoters;
		++version;
		return changed;
	});
}
#endif

bool PollData::checkResultsReload(crl::time now) {
	if (_lastResultsUpdate > 0
		&& _lastResultsUpdate + kShortPollTimeout > now) {
		return false;
	} else if (closed() && _lastResultsUpdate >= 0) {
		return false;
	}
	_lastResultsUpdate = now;
	return true;
}

PollAnswer *PollData::answerByOption(const QByteArray &option) {
	return AnswerByOption(answers, option);
}

const PollAnswer *PollData::answerByOption(const QByteArray &option) const {
	return AnswerByOption(answers, option);
}

int PollData::indexByOption(const QByteArray &option) const {
	const auto it = ranges::find(answers, option, &PollAnswer::option);

	return std::clamp(
		int(std::distance(begin(answers), it)),
		0,
		int(answers.size() - 1));
}

#if 0 // goodToRemove
bool PollData::applyResultToAnswers(
		const MTPPollAnswerVoters &result,
		bool isMinResults) {
	return result.match([&](const MTPDpollAnswerVoters &voters) {
		const auto &option = voters.voption().v;
		const auto answer = answerByOption(option);
		if (!answer) {
			return false;
		}
		auto changed = (answer->votes != voters.vvoters().v);
		if (changed) {
			answer->votes = voters.vvoters().v;
		}
		if (!isMinResults) {
			if (answer->chosen != voters.is_chosen()) {
				answer->chosen = voters.is_chosen();
				changed = true;
			}
		}
		if (voters.is_correct() && !answer->correct) {
			answer->correct = voters.is_correct();
			changed = true;
		}
		return changed;
	});
}
#endif

bool PollData::applyResultToAnswers(
		const TLDpollOption &result,
		bool isMinResults) {
	const auto option = result.vtext().v.toUtf8();
	const auto answer = answerByOption(option);
	if (!answer) {
		return false;
	}
	auto changed = (answer->votes != result.vvoter_count().v);
	if (changed) {
		answer->votes = result.vvoter_count().v;
	}
	if (!isMinResults) {
		if (answer->chosen != result.vis_chosen().v) {
			answer->chosen = result.vis_chosen().v;
			changed = true;
		}
	}
	return changed;
}

void PollData::setFlags(Flags flags) {
	if (_flags != flags) {
		_flags = flags;
		++version;
	}
}

PollData::Flags PollData::flags() const {
	return _flags;
}

bool PollData::voted() const {
	return ranges::contains(answers, true, &PollAnswer::chosen);
}

bool PollData::closed() const {
	return (_flags & Flag::Closed);
}

bool PollData::publicVotes() const {
	return (_flags & Flag::PublicVotes);
}

bool PollData::multiChoice() const {
	return (_flags & Flag::MultiChoice);
}

bool PollData::quiz() const {
	return (_flags & Flag::Quiz);
}

#if 0 // mtp
MTPPoll PollDataToMTP(not_null<const PollData*> poll, bool close) {
	const auto convert = [](const PollAnswer &answer) {
		return MTP_pollAnswer(
			MTP_string(answer.text),
			MTP_bytes(answer.option));
	};
	auto answers = QVector<MTPPollAnswer>();
	answers.reserve(poll->answers.size());
	ranges::transform(
		poll->answers,
		ranges::back_inserter(answers),
		convert);
	using Flag = MTPDpoll::Flag;
	const auto flags = ((poll->closed() || close) ? Flag::f_closed : Flag(0))
		| (poll->multiChoice() ? Flag::f_multiple_choice : Flag(0))
		| (poll->publicVotes() ? Flag::f_public_voters : Flag(0))
		| (poll->quiz() ? Flag::f_quiz : Flag(0))
		| (poll->closePeriod > 0 ? Flag::f_close_period : Flag(0))
		| (poll->closeDate > 0 ? Flag::f_close_date : Flag(0));
	return MTP_poll(
		MTP_long(poll->id),
		MTP_flags(flags),
		MTP_string(poll->question),
		MTP_vector<MTPPollAnswer>(answers),
		MTP_int(poll->closePeriod),
		MTP_int(poll->closeDate));
}

MTPInputMedia PollDataToInputMedia(
		not_null<const PollData*> poll,
		bool close) {
	auto inputFlags = MTPDinputMediaPoll::Flag(0)
		| (poll->quiz()
			? MTPDinputMediaPoll::Flag::f_correct_answers
			: MTPDinputMediaPoll::Flag(0));
	auto correct = QVector<MTPbytes>();
	for (const auto &answer : poll->answers) {
		if (answer.correct) {
			correct.push_back(MTP_bytes(answer.option));
		}
	}

	auto solution = poll->solution;
	const auto prepareFlags = Ui::ItemTextDefaultOptions().flags;
	TextUtilities::PrepareForSending(solution, prepareFlags);
	TextUtilities::Trim(solution);
	const auto sentEntities = Api::EntitiesToMTP(
		&poll->session(),
		solution.entities,
		Api::ConvertOption::SkipLocal);
	if (!solution.text.isEmpty()) {
		inputFlags |= MTPDinputMediaPoll::Flag::f_solution;
	}
	if (!sentEntities.v.isEmpty()) {
		inputFlags |= MTPDinputMediaPoll::Flag::f_solution_entities;
	}
	return MTP_inputMediaPoll(
		MTP_flags(inputFlags),
		PollDataToMTP(poll, close),
		MTP_vector<MTPbytes>(correct),
		MTP_string(solution.text),
		sentEntities);
}
#endif
