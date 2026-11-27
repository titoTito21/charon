// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: multi-system plan - the state that survives restarts, and the
// state machine (work/charon-multiboot-spec-2026-11-23.md 2.2, 2.3, 2.9, 5).
//
//   Storage     the plan's files and its firmware variable, behind function
//               pointers: the EFI build binds them to the plan's EFI system
//               partition and to the variable "CharonPlan", a host test to a
//               fake. Paths are UTF-8 with backslashes, from the volume root.
//   Run         the state of every step.
//   Save/Load   \EFI\HAK\plan\plan.txt. The file is the truth, the variable a
//               pointer. Written as plan.new, then plan.txt -> plan.bak, then
//               plan.new -> plan.txt; Load takes the valid file with the
//               highest sequence number, so a torn write leaves a readable
//               older state. The text is built in memory first (Serialize), so
//               a plan made while Charon runs from its RAM volume, or whose
//               target is Charon's own disk, is held in memory while the disk
//               changes and written to the new EFI partition as the first act
//               after the layout exists.
//   Resume      what a start of Charon with an active plan finds: stepN.done,
//               stepN.failed, or an installer in the middle (pass through,
//               within a budget of starts and of time).
//   Next        what to do now.
//   Decide      try again / skip this system / stop here; Policy for
//               unattended plans.
//   Finish...   boot order rule, systems registry, CharonDOS defaults,
//               leftovers, report.
//
// Pure logic (see CharonPlanText.h); every disk or firmware effect is the
// caller's, through Storage or on its own.
#pragma once

#include "CharonPlanLayout.h"
#include "CharonPlanFetch.h"   // r410: everything is downloaded first

namespace hakefi::plan {

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
struct Storage {
	void* context;
	// False: no such file, or it does not fit, or a read error.
	bool (*read)(void* context, const char* path, char* buffer, size_t capacity, size_t* length);
	// Creates or replaces the file (and missing folders) and flushes it.
	bool (*write)(void* context, const char* path, const char* data, size_t length);
	bool (*append)(void* context, const char* path, const char* data, size_t length);
	bool (*rename)(void* context, const char* from, const char* to); // `to` must not exist
	bool (*remove)(void* context, const char* path);                 // true also when it did not exist
	bool (*exists)(void* context, const char* path);
	// The variable CharonPlan. set with length 0 deletes it.
	bool (*getVariable)(void* context, uint8_t* buffer, size_t capacity, size_t* length);
	bool (*setVariable)(void* context, const uint8_t* data, size_t length);
};

constexpr const char* kPlanFile = "\\EFI\\HAK\\plan\\plan.txt";
constexpr const char* kPlanNew = "\\EFI\\HAK\\plan\\plan.new";
constexpr const char* kPlanBackup = "\\EFI\\HAK\\plan\\plan.bak";
constexpr const char* kPlanDone = "\\EFI\\HAK\\plan\\plan-done.txt";
constexpr const char* kPlanLog = "\\EFI\\HAK\\plan\\log.txt";
constexpr const char* kPlanReport = "\\EFI\\HAK\\plan\\report.txt";
constexpr const char* kRegistryFile = "\\EFI\\HAK\\systems.txt";
// The number of the step Charon started last, for the hooks of the installed systems
// (install-charon.sh, Install-Charon.ps1): digits only.
constexpr const char* kPlanCurrent = "\\EFI\\HAK\\plan\\current";

inline void MarkerPath(int step, bool failed, Out& out)
{
	out.S("\\EFI\\HAK\\plan\\step").N(uint64_t(step)).S(failed ? ".failed" : ".done");
}
inline void StepFilePath(int step, const char* name, Out& out)
{
	out.S("\\EFI\\HAK\\plan\\step").N(uint64_t(step)).Ch('\\').S(name);
}

// ---------------------------------------------------------------------------
// Run state
// ---------------------------------------------------------------------------
enum class StepState : uint8_t { pending, prepared, launched, handoff, done, failed, skipped };
enum class Fail : uint8_t {
	none,
	charon,       // Charon's own work returned an error (disk, write, generator)
	download,
	reported,     // the system's first start wrote stepN.failed
	budgetStarts, // no marker after the allowed number of starts
	budgetTime,   // no marker within the allowed time
	interrupted,  // Charon's own step was cut off (power, reset)
	user,         // paused, and the user chose to give this step up
	stopped,      // did not run: the plan was stopped before it
	dependency,   // first start of an image that was not written
};
struct StepRun {
	StepKind kind;
	int8_t system;
	StepState state;
	Fail fail;
	uint16_t starts;      // starts of the installer or pass-throughs so far
	bool retried;
	bool erased;          // its answer files were zeroed and deleted
	uint64_t first;       // seconds: when it was launched
	uint64_t finished;    // seconds: when it ended
	char reason[160];
	char files[120];      // answer files in \EFI\HAK\plan\stepN\, comma separated
};
struct Run {
	uint32_t sequence;    // grows with every Save
	uint8_t count;
	StepRun steps[kMaxSteps];
	bool paused, stopped, finished;
	uint64_t started;
};

constexpr uint16_t kStartBudget = 8;
inline uint64_t TimeBudget(const Plan& plan, const StepRun& step)
{
	const bool windows = step.system >= 0 && step.system < plan.count && IsWindows(plan.systems[step.system].kind);
	return (windows ? 4 : 3) * uint64_t(3600);
}

inline void Begin(const Plan& plan, Run& run, uint64_t now)
{
	uint8_t* p = reinterpret_cast<uint8_t*>(&run);
	for (size_t i = 0; i < sizeof(Run); i++)
		p[i] = 0;
	Order order;
	ComputeOrder(plan, order);
	run.count = order.count;
	for (int i = 0; i < order.count; i++) {
		run.steps[i].kind = order.steps[i].kind;
		run.steps[i].system = order.steps[i].system;
	}
	run.started = now;
}

// Seconds from a calendar date (any fixed origin; only differences are used).
inline uint64_t Seconds(uint32_t year, uint32_t month, uint32_t day, uint32_t hour, uint32_t minute, uint32_t second)
{
	static const uint16_t before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
	if (year < 2000 || month < 1 || month > 12)
		return 0;
	const uint64_t y = year - 2000;
	uint64_t days = y * 365 + (y + 3) / 4 + before[month - 1] + (day - 1);
	if (month > 2 && year % 4 == 0)
		days++;
	return ((days * 24 + hour) * 60 + minute) * 60 + second;
}

inline bool Closed(const Run& run, const StepRun& step)
{
	return step.state == StepState::done || step.state == StepState::skipped
		|| (step.state == StepState::failed && run.stopped && step.kind != StepKind::finish);
}
// The step the plan is at: the first one that is neither done nor given up.
inline int Current(const Run& run)
{
	for (int i = 0; i < run.count; i++)
		if (!Closed(run, run.steps[i]))
			return i;
	return -1;
}

enum class Do : uint8_t {
	nothing,
	prepareDisk,       // step 0: answer files, partitions, plan file
	writeDos,
	writeImage,
	launchInstaller,   // start the system's installer
	launchFirstStart,  // start a written image for the first time
	passThrough,       // an installer is in the middle: start that system's loader
	finish,
	askFailure,        // try again / skip this system / stop here
	askPaused,         // continue / skip this system / stop here
	complete,
};
struct Action {
	Do what;
	int8_t step, system;
};
inline Action Next(const Run& run)
{
	Action action = {Do::nothing, -1, -1};
	if (run.finished) {
		action.what = Do::complete;
		return action;
	}
	const int at = Current(run);
	if (at < 0) {
		action.what = Do::complete;
		return action;
	}
	const StepRun& step = run.steps[at];
	action.step = int8_t(at);
	action.system = step.system;
	if (step.state == StepState::failed) {
		action.what = Do::askFailure;
		return action;
	}
	if (run.paused) {
		action.what = Do::askPaused;
		return action;
	}
	const bool waiting = step.state == StepState::launched || step.state == StepState::handoff;
	switch (step.kind) {
	case StepKind::prepare: action.what = Do::prepareDisk; break;
	case StepKind::writeDos: action.what = Do::writeDos; break;
	case StepKind::writeImage: action.what = Do::writeImage; break;
	case StepKind::install: action.what = waiting ? Do::passThrough : Do::launchInstaller; break;
	case StepKind::firstStart: action.what = waiting ? Do::passThrough : Do::launchFirstStart; break;
	case StepKind::finish: action.what = Do::finish; break;
	}
	return action;
}

inline void MarkPrepared(Run& run, int step)
{
	if (run.steps[step].state == StepState::pending)
		run.steps[step].state = StepState::prepared;
}
// Charon is about to start this step's work (its own, or the installer).
// Saved BEFORE the work begins, so that a cut-off is seen at the next start.
inline void MarkLaunched(Run& run, int step, uint64_t now)
{
	StepRun& s = run.steps[step];
	s.state = StepState::launched;
	s.starts = 1;
	s.first = now;
	s.fail = Fail::none;
	s.reason[0] = 0;
}
inline void MarkDone(Run& run, int step, uint64_t now)
{
	StepRun& s = run.steps[step];
	s.state = StepState::done;
	s.fail = Fail::none;
	s.finished = now;
	if (s.kind == StepKind::finish)
		run.finished = true;
}
inline void MarkFailed(Run& run, int step, Fail fail, const char* reason, uint64_t now)
{
	StepRun& s = run.steps[step];
	s.state = StepState::failed;
	s.fail = fail;
	s.finished = now;
	Copy(s.reason, sizeof(s.reason), reason);
	// The first start of an image that was not written cannot run.
	if (s.kind == StepKind::writeImage)
		for (int i = 0; i < run.count; i++)
			if (run.steps[i].kind == StepKind::firstStart && run.steps[i].system == s.system
				&& run.steps[i].state == StepState::pending)
				run.steps[i].fail = Fail::dependency;
}

enum class Decision : uint8_t { retry, skip, stop };
// Applies the choice to the current step. False when the choice is not
// possible (disk preparation and the finish step cannot be skipped). After
// `retry` the caller must put the step's partition back into its placeholder
// state before anything else.
inline bool Decide(Run& run, Decision decision, uint64_t now)
{
	const int at = Current(run);
	if (at < 0)
		return false;
	StepRun& s = run.steps[at];
	run.paused = false;
	if (decision == Decision::retry) {
		auto reset = [](StepRun& step) {
			step.state = StepState::pending;
			step.fail = Fail::none;
			step.starts = 0;
			step.first = step.finished = 0;
			step.reason[0] = 0;
			step.retried = true;
		};
		// A ready image is written again before its first start is tried again.
		if (s.kind == StepKind::firstStart)
			for (int i = 0; i < run.count; i++)
				if (run.steps[i].kind == StepKind::writeImage && run.steps[i].system == s.system)
					reset(run.steps[i]);
		reset(s);
		return true;
	}
	if (decision == Decision::skip) {
		if (s.kind == StepKind::prepare || s.kind == StepKind::finish)
			return false;
		if (s.state != StepState::failed)
			s.fail = Fail::user;
		s.state = StepState::skipped;
		s.finished = now;
		for (int i = 0; i < run.count; i++)
			if (i != at && run.steps[i].system == s.system && run.steps[i].system >= 0
				&& run.steps[i].state != StepState::done) {
				run.steps[i].state = StepState::skipped;
				run.steps[i].fail = Fail::dependency;
				run.steps[i].finished = now;
			}
		return true;
	}
	// Stop here: nothing more is installed; the finish step still runs.
	run.stopped = true;
	for (int i = 0; i < run.count; i++) {
		StepRun& step = run.steps[i];
		if (step.kind == StepKind::finish || step.state == StepState::done || step.state == StepState::skipped
			|| step.state == StepState::failed)
			continue;
		step.fail = i == at ? Fail::user : Fail::stopped;
		step.state = StepState::skipped;
		step.finished = now;
	}
	if (s.kind == StepKind::finish && s.state == StepState::failed) {
		s.state = StepState::pending; // the finish step is idempotent: run it again
		s.fail = Fail::none;
	}
	return true;
}
// What an unattended plan does with a failed step ([plan] on_failure). A
// failed disk preparation always stops.
inline Decision Policy(const Plan& plan, const Run& run)
{
	const int at = Current(run);
	if (at < 0 || run.steps[at].kind == StepKind::prepare || run.steps[at].kind == StepKind::finish)
		return Decision::stop;
	if (plan.onFailure == OnFailure::skip)
		return Decision::skip;
	if (plan.onFailure == OnFailure::retryOnceThenStop && !run.steps[at].retried)
		return Decision::retry;
	return Decision::stop;
}

enum class Result : uint8_t { pending, done, failed, skipped };
inline Result ResultOf(const Run& run, int system)
{
	bool any = false, all = true, failed = false, skipped = false;
	for (int i = 0; i < run.count; i++) {
		if (run.steps[i].system != system)
			continue;
		any = true;
		all = all && run.steps[i].state == StepState::done;
		failed = failed || run.steps[i].state == StepState::failed;
		skipped = skipped || run.steps[i].state == StepState::skipped;
	}
	if (!any)
		return Result::pending;
	return all ? Result::done : failed ? Result::failed : skipped ? Result::skipped : Result::pending;
}

// ---------------------------------------------------------------------------
// The plan file
// ---------------------------------------------------------------------------
inline const char* StepKindWord(StepKind kind)
{
	switch (kind) {
	case StepKind::prepare: return "prepare";
	case StepKind::writeDos: return "write-dos";
	case StepKind::writeImage: return "write-image";
	case StepKind::install: return "install";
	case StepKind::firstStart: return "first-start";
	default: return "finish";
	}
}
inline const char* StepStateWord(StepState state)
{
	static const char* const words[] = {"pending", "prepared", "launched", "handoff", "done", "failed", "skipped"};
	return words[uint8_t(state)];
}
inline const char* FailWord(Fail fail)
{
	static const char* const words[] = {"", "charon", "download", "reported", "starts", "time", "interrupted",
		"user", "stopped", "dependency"};
	return words[uint8_t(fail)];
}

inline bool RunToStore(const Plan& plan, const Run& run, Store& store)
{
	bool ok = store.PutNumber("charon-plan", "format", 1);
	ok = store.PutNumber("charon-plan", "sequence", run.sequence) && ok;
	ok = store.PutNumber("charon-plan", "steps", run.count) && ok;
	ok = store.PutNumber("charon-plan", "started", run.started) && ok;
	ok = store.Put("charon-plan", "paused", run.paused ? "yes" : "no") && ok;
	ok = store.Put("charon-plan", "stopped", run.stopped ? "yes" : "no") && ok;
	ok = store.Put("charon-plan", "finished", run.finished ? "yes" : "no") && ok;
	for (int i = 0; i < run.count; i++) {
		const StepRun& s = run.steps[i];
		char section[16];
		Out name(section, sizeof(section));
		name.S("step.").N(uint64_t(i));
		ok = store.Put(section, "kind", StepKindWord(s.kind)) && ok;
		ok = store.Put(section, "system", s.system >= 0 ? plan.systems[s.system].id : "") && ok;
		ok = store.Put(section, "state", StepStateWord(s.state)) && ok;
		ok = store.Put(section, "fail", FailWord(s.fail)) && ok;
		ok = store.PutNumber(section, "starts", s.starts) && ok;
		ok = store.Put(section, "retried", s.retried ? "yes" : "no") && ok;
		ok = store.Put(section, "erased", s.erased ? "yes" : "no") && ok;
		ok = store.PutNumber(section, "first", s.first) && ok;
		ok = store.PutNumber(section, "finished", s.finished) && ok;
		ok = store.Put(section, "reason", s.reason) && ok;
		ok = store.Put(section, "files", s.files) && ok;
	}
	return ok;
}
inline bool RunFromStore(const Store& store, const Plan& plan, Run& run)
{
	uint8_t* p = reinterpret_cast<uint8_t*>(&run);
	for (size_t i = 0; i < sizeof(Run); i++)
		p[i] = 0;
	uint64_t v = 0;
	if (!ParseNumber(store.GetOr("charon-plan", "format", ""), v) || v != 1)
		return false; // another format is refused, not read as this one
	if (!ParseNumber(store.GetOr("charon-plan", "sequence", ""), v))
		return false;
	run.sequence = uint32_t(v);
	if (!ParseNumber(store.GetOr("charon-plan", "steps", ""), v) || v < 2 || v > kMaxSteps)
		return false;
	run.count = uint8_t(v);
	ParseNumber(store.GetOr("charon-plan", "started", "0"), run.started);
	run.paused = IsYes(store.GetOr("charon-plan", "paused", ""));
	run.stopped = IsYes(store.GetOr("charon-plan", "stopped", ""));
	run.finished = IsYes(store.GetOr("charon-plan", "finished", ""));
	for (int i = 0; i < run.count; i++) {
		StepRun& s = run.steps[i];
		char section[16];
		Out name(section, sizeof(section));
		name.S("step.").N(uint64_t(i));
		bool known = false;
		const char* word = store.GetOr(section, "kind", "");
		for (uint8_t k = 0; k <= uint8_t(StepKind::finish); k++)
			if (Eq(word, StepKindWord(StepKind(k)))) {
				s.kind = StepKind(k);
				known = true;
			}
		if (!known)
			return false;
		known = false;
		word = store.GetOr(section, "state", "");
		for (uint8_t k = 0; k <= uint8_t(StepState::skipped); k++)
			if (Eq(word, StepStateWord(StepState(k)))) {
				s.state = StepState(k);
				known = true;
			}
		if (!known)
			return false;
		word = store.GetOr(section, "fail", "");
		for (uint8_t k = 0; k <= uint8_t(Fail::dependency); k++)
			if (Eq(word, FailWord(Fail(k))))
				s.fail = Fail(k);
		const char* id = store.GetOr(section, "system", "");
		s.system = int8_t(id[0] != 0 ? FindSystem(plan, id) : -1);
		if (id[0] != 0 && s.system < 0)
			return false;
		if (ParseNumber(store.GetOr(section, "starts", "0"), v))
			s.starts = uint16_t(v);
		s.retried = IsYes(store.GetOr(section, "retried", ""));
		s.erased = IsYes(store.GetOr(section, "erased", ""));
		ParseNumber(store.GetOr(section, "first", "0"), s.first);
		ParseNumber(store.GetOr(section, "finished", "0"), s.finished);
		Copy(s.reason, sizeof(s.reason), store.GetOr(section, "reason", ""));
		Copy(s.files, sizeof(s.files), store.GetOr(section, "files", ""));
	}
	return true;
}

// The whole plan as text: the store without secrets, then a checksum line.
inline bool Serialize(const Store& store, Out& out)
{
	WriteIni(store, out, false);
	const uint32_t crc = Crc32(out.text, out.length);
	out.S("checksum=").Hex(crc, 8).S("\r\n");
	return !out.overflow;
}
// True when the text ends with a checksum line that matches; `body` is the
// length of what the checksum covers.
inline bool Checked(const char* text, size_t length, size_t& body)
{
	const size_t tail = 9 + 8 + 2; // "checksum=" + 8 digits + CR LF
	if (length < tail)
		return false;
	const char* line = text + length - tail;
	if (!StartsNoCase(line, "checksum=") || line[17] != '\r' || line[18] != '\n')
		return false;
	uint64_t stored = 0;
	if (!ParseHex(line + 9, 8, stored))
		return false;
	body = length - tail;
	return Crc32(text, body) == uint32_t(stored);
}

// The firmware variable: a pointer, never answers or secrets (32 bytes).
struct Pointer {
	uint8_t state;       // 1 active, 2 paused, 3 waits for a decision, 4 preparing the disk
	uint8_t step;
	uint32_t planId;
	uint32_t sequence;
	uint8_t espGuid[16]; // partition GUID of the EFI partition that holds the plan
};
constexpr size_t kPointerBytes = 32;
inline void PackPointer(const Pointer& pointer, uint8_t out[kPointerBytes])
{
	out[0] = 'C';
	out[1] = 'P';
	out[2] = 1;
	out[3] = pointer.state;
	out[4] = pointer.step;
	out[5] = out[6] = out[7] = 0;
	for (int i = 0; i < 4; i++) {
		out[8 + i] = uint8_t(pointer.planId >> (8 * i));
		out[12 + i] = uint8_t(pointer.sequence >> (8 * i));
	}
	for (int i = 0; i < 16; i++)
		out[16 + i] = pointer.espGuid[i];
}
inline bool UnpackPointer(const uint8_t* data, size_t length, Pointer& pointer)
{
	if (data == nullptr || length != kPointerBytes || data[0] != 'C' || data[1] != 'P' || data[2] != 1)
		return false;
	pointer.state = data[3];
	pointer.step = data[4];
	pointer.planId = pointer.sequence = 0;
	for (int i = 0; i < 4; i++) {
		pointer.planId |= uint32_t(data[8 + i]) << (8 * i);
		pointer.sequence |= uint32_t(data[12 + i]) << (8 * i);
	}
	for (int i = 0; i < 16; i++)
		pointer.espGuid[i] = data[16 + i];
	return true;
}
inline bool WritePointer(const Storage& storage, const Plan& plan, const Run& run, const uint8_t espGuid[16])
{
	Pointer pointer;
	const int at = Current(run);
	pointer.step = uint8_t(at < 0 ? 0 : at);
	pointer.state = at >= 0 && run.steps[at].state == StepState::failed ? 3
		: run.paused ? 2 : at == 0 ? 4 : 1;
	pointer.planId = plan.id;
	pointer.sequence = run.sequence;
	for (int i = 0; i < 16; i++)
		pointer.espGuid[i] = espGuid != nullptr ? espGuid[i] : 0;
	uint8_t bytes[kPointerBytes];
	PackPointer(pointer, bytes);
	return storage.setVariable(storage.context, bytes, kPointerBytes);
}
inline bool ReadPointer(const Storage& storage, Pointer& pointer)
{
	uint8_t bytes[64];
	size_t length = 0;
	return storage.getVariable(storage.context, bytes, sizeof(bytes), &length) && UnpackPointer(bytes, length, pointer);
}

enum class Io : uint8_t { ok, none, damaged, tooLarge, write };

// Writes the plan (plan.sequence grows first). `store` holds the answers and,
// when the caller put it there, the layout; the plan's and the run's own keys
// are refreshed in it. `work` is the text buffer. The old file survives as
// plan.bak until the next Save.
inline Io Save(const Storage& storage, const Plan& plan, Run& run, Store& store, char* work, size_t capacity,
	const uint8_t espGuid[16] = nullptr)
{
	run.sequence++;
	if (!PlanToStore(plan, store) || !RunToStore(plan, run, store))
		return Io::tooLarge;
	Out out(work, capacity);
	if (!Serialize(store, out))
		return Io::tooLarge;
	storage.remove(storage.context, kPlanNew);
	if (!storage.write(storage.context, kPlanNew, out.text, out.length))
		return Io::write;
	if (storage.exists(storage.context, kPlanFile)) {
		if (!storage.remove(storage.context, kPlanBackup))
			return Io::write;
		if (!storage.rename(storage.context, kPlanFile, kPlanBackup))
			return Io::write;
	}
	if (!storage.rename(storage.context, kPlanNew, kPlanFile))
		return Io::write;
	// The pointer last: it never names a state that is not on the disk.
	if (!WritePointer(storage, plan, run, espGuid))
		return Io::write;
	return Io::ok;
}

// Reads the newest valid plan file into `store`, `plan` and `run`.
// Io::none: no plan file at all (when the variable still names a plan, the
// plan lost its file: PointerLost()). Io::damaged: files exist, none is valid.
inline Io Load(const Storage& storage, char* work, size_t capacity, Store& store, Plan& plan, Run& run,
	Refusals& report)
{
	static const char* const paths[3] = {kPlanFile, kPlanNew, kPlanBackup};
	int best = -1;
	uint64_t bestSequence = 0;
	bool any = false;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < 3; i++) {
			if (pass == 1 && i != best)
				continue;
			if (!storage.exists(storage.context, paths[i]))
				continue;
			any = true;
			size_t length = 0, body = 0;
			if (!storage.read(storage.context, paths[i], work, capacity, &length) || !Checked(work, length, body))
				continue;
			store.Wipe();
			Refusals syntax;
			if (!ParseIni(work, body, store, syntax))
				continue;
			uint64_t sequence = 0, format = 0;
			if (!ParseNumber(store.GetOr("charon-plan", "format", ""), format) || format != 1
				|| !ParseNumber(store.GetOr("charon-plan", "sequence", ""), sequence))
				continue;
			if (pass == 0) {
				if (best < 0 || sequence > bestSequence) {
					best = i;
					bestSequence = sequence;
				}
				continue;
			}
			uint64_t id = 0;
			const char* idText = store.GetOr("plan", "id", "");
			plan.id = 0;
			if (Len(idText) == 8 && ParseHex(idText, 8, id))
				plan.id = uint32_t(id);
			PlanFromStore(store, plan, report);
			if (report.Refused() || !RunFromStore(store, plan, run))
				return Io::damaged;
			return Io::ok;
		}
		if (best < 0)
			break;
	}
	store.Wipe();
	return any ? Io::damaged : Io::none;
}

inline bool Log(const Storage& storage, uint64_t now, const char* text)
{
	char line[400];
	Out out(line, sizeof(line));
	out.N(now).Ch(' ').S(text).S("\r\n");
	return storage.append(storage.context, kPlanLog, out.text, out.length);
}

// The answer files of a step (Autounattend.xml, preseed.cfg, user-data).
inline bool WriteStepFile(const Storage& storage, Run& run, int step, const char* name, const char* data, size_t length)
{
	char path[160];
	Out out(path, sizeof(path));
	StepFilePath(step, name, out);
	if (out.overflow || !storage.write(storage.context, path, data, length))
		return false;
	StepRun& s = run.steps[step];
	// Remember the name, once.
	for (size_t at = 0; s.files[at] != 0;) {
		size_t end = at;
		while (s.files[end] != 0 && s.files[end] != ',')
			end++;
		if (end - at == Len(name) && StartsNoCase(s.files + at, name))
			return true;
		at = s.files[end] == ',' ? end + 1 : end;
	}
	Out files(s.files + Len(s.files), sizeof(s.files) - Len(s.files));
	if (s.files[0] != 0)
		files.Ch(',');
	files.S(name);
	s.erased = false;
	return !files.overflow;
}
// Overwrites every answer file of the step with zeros, then deletes it
// (decision D4: right after the step). `work` must hold the largest file.
// True when nothing of them is left.
inline bool EraseStepFiles(const Storage& storage, Run& run, int step, char* work, size_t capacity)
{
	StepRun& s = run.steps[step];
	bool clean = true;
	for (size_t at = 0; s.files[at] != 0;) {
		size_t end = at;
		while (s.files[end] != 0 && s.files[end] != ',')
			end++;
		char name[64], path[160];
		Out n(name, sizeof(name));
		n.SN(s.files + at, end - at);
		Out p(path, sizeof(path));
		StepFilePath(step, name, p);
		if (storage.exists(storage.context, path)) {
			size_t length = 0;
			bool zeroed = false;
			if (storage.read(storage.context, path, work, capacity, &length)) {
				for (size_t i = 0; i < length; i++)
					work[i] = 0;
				zeroed = storage.write(storage.context, path, work, length);
			}
			// Deleted even when it could not be overwritten: less is left.
			if (!storage.remove(storage.context, path) || !zeroed)
				clean = false;
			if (storage.exists(storage.context, path))
				clean = false;
		}
		at = s.files[end] == ',' ? end + 1 : end;
	}
	if (clean && s.files[0] != 0) {
		s.erased = true;
		s.files[0] = 0;
	}
	return clean;
}

// ---------------------------------------------------------------------------
// A start of Charon with an active plan
// ---------------------------------------------------------------------------
// Looks at the markers the installed system wrote and at the budgets; changes
// the run accordingly (the caller saves). Returns true when the run changed.
// After it, Next() says what to do: passThrough (the installer is in the
// middle), the next step, askFailure, ...
inline bool Resume(const Plan& plan, Run& run, const Storage& storage, uint64_t now, char* work, size_t capacity)
{
	const int at = Current(run);
	if (at < 0)
		return false;
	StepRun& s = run.steps[at];
	const bool waiting = s.state == StepState::launched || s.state == StepState::handoff;
	if (!waiting)
		return false;
	if (s.kind != StepKind::install && s.kind != StepKind::firstStart) {
		// Charon's own step was cut off in the middle.
		if (s.kind == StepKind::finish) {
			s.state = StepState::pending; // safe to run again
			return true;
		}
		MarkFailed(run, at, Fail::interrupted, "", now);
		return true;
	}
	char path[96];
	Out done(path, sizeof(path));
	MarkerPath(at, false, done);
	if (storage.exists(storage.context, path)) {
		MarkDone(run, at, now);
		EraseStepFiles(storage, run, at, work, capacity);
		return true;
	}
	Out failed(path, sizeof(path));
	MarkerPath(at, true, failed);
	if (storage.exists(storage.context, path)) {
		char text[160] = "";
		size_t length = 0;
		if (capacity > 1 && storage.read(storage.context, path, work, capacity - 1, &length)) {
			work[length] = 0;
			size_t n = 0;
			for (size_t i = 0; i < length && work[i] != '\r' && work[i] != '\n' && n + 1 < sizeof(text); i++)
				text[n++] = work[i];
			text[n] = 0;
		}
		MarkFailed(run, at, Fail::reported, text, now);
		return true;
	}
	if (s.starts >= kStartBudget) {
		MarkFailed(run, at, Fail::budgetStarts, "", now);
		return true;
	}
	// A clock that went backwards (a system switched the hardware clock to
	// universal time in between) counts as no time passed; the start budget
	// still bounds the loop.
	if (now > s.first && now - s.first > TimeBudget(plan, s)) {
		MarkFailed(run, at, Fail::budgetTime, "", now);
		return true;
	}
	s.state = StepState::handoff;
	s.starts++;
	return true;
}

// The loaders a system is passed through to, best first (shim before GRUB:
// it is what the distribution registers and what starts under Secure Boot).
// Ready images keep their own EFI partition inside their range.
inline int LoaderCandidates(Kind kind, const char* out[4])
{
	int n = 0;
	switch (kind) {
	case Kind::windows:
		out[n++] = "\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
		break;
	case Kind::debian: case Kind::imageDebian:
		out[n++] = "\\EFI\\debian\\shimx64.efi";
		out[n++] = "\\EFI\\debian\\grubx64.efi";
		break;
	case Kind::ubuntu: case Kind::imageUbuntu:
		out[n++] = "\\EFI\\ubuntu\\shimx64.efi";
		out[n++] = "\\EFI\\ubuntu\\grubx64.efi";
		break;
	case Kind::imageFedora:
		out[n++] = "\\EFI\\fedora\\shimx64.efi";
		out[n++] = "\\EFI\\fedora\\grubx64.efi";
		break;
	case Kind::imageArch:
		out[n++] = "\\EFI\\arch\\grubx64.efi";
		out[n++] = "\\EFI\\GRUB\\grubx64.efi";
		break;
	case Kind::charondos:
		out[n++] = "\\EFI\\CharonDOS\\CHARONDOS.EFI";
		break;
	case Kind::hypervisor: // r410: on its own partition P1 "Hypervisor" (agent 2); the options come from its device-list entry
		out[n++] = "\\boot\\vmlinuz-lts";
		break;
	default:
		break;
	}
	if (IsImage(kind))
		out[n++] = "\\EFI\\BOOT\\BOOTX64.EFI";
	return n;
}

// ---------------------------------------------------------------------------
// The finish step
// ---------------------------------------------------------------------------
struct FinishRule {
	bool dosFirst;     // BootOrder: CharonDOS, Charon, the others
	bool charonFirst;  // BootOrder: Charon, the others
	int8_t target;     // Charon's "Continue" target, -1 = none
	bool menuWaits;
	uint32_t timeout;
};
// Decision D1 and spec 4.1/5: CharonDOS first when it is installed - unless
// this firmware does not load Driver#### options (the reader would not be
// there): then Charon is first and goes straight on to CharonDOS. Without
// CharonDOS Charon's menu is first, with the default system and the timeout.
// Only systems that are done are ever a target.
inline FinishRule FinishOf(const Plan& plan, const Run& run, const Machine& machine)
{
	FinishRule rule = {false, true, -1, false, plan.timeout};
	const StartRule start = StartOf(plan);
	if (start.dosFirst && ResultOf(run, start.system) == Result::done) {
		rule.target = start.system;
		if (machine.driverOptions) {
			rule.dosFirst = true;
			rule.charonFirst = false;
		} else {
			rule.timeout = 0;
		}
		return rule;
	}
	if (start.menuWaits) {
		rule.menuWaits = true;
		return rule;
	}
	if (!start.dosFirst && start.system >= 0 && ResultOf(run, start.system) == Result::done)
		rule.target = start.system;
	for (int i = 0; i < plan.count && rule.target < 0; i++)
		if (plan.systems[i].kind != Kind::charondos && ResultOf(run, i) == Result::done)
			rule.target = int8_t(i);
	rule.menuWaits = rule.target < 0;
	if (machine.waitNotStored && rule.target >= 0)
		rule.timeout = 5;   // r410: the report names the wait that is really in force
	return rule;
}
// r410: after the report the plan ends in its default system by itself (a restart, heard out first) exactly when
// there is such a system and EVERY system of the plan is installed. Otherwise Charon waits for a key.
inline bool FinishRestarts(const Plan& plan, const Run& run, const Machine& machine)
{
	const FinishRule rule = FinishOf(plan, run, machine);
	if (rule.target < 0 || rule.menuWaits)
		return false;
	for (int i = 0; i < plan.count; i++)
		if (ResultOf(run, i) != Result::done)
			return false;
	return true;
}

// The BootOrder the finish step writes: the entries of `first` in their order
// (CharonDOS, Charon - or Charon alone), then every other entry of the old
// order in its old place relative to the others. Nothing is dropped and
// nothing appears twice. Returns the count, 0 when it does not fit.
inline size_t FinishOrder(const uint16_t* old, size_t oldCount, const uint16_t* first, size_t firstCount, uint16_t* out,
	size_t capacity)
{
	size_t n = 0;
	for (size_t i = 0; i < firstCount; i++) {
		bool seen = false;
		for (size_t k = 0; k < n; k++)
			seen = seen || out[k] == first[i];
		if (seen)
			continue;
		if (n >= capacity)
			return 0;
		out[n++] = first[i];
	}
	for (size_t i = 0; i < oldCount; i++) {
		bool seen = false;
		for (size_t k = 0; k < n; k++)
			seen = seen || out[k] == old[i];
		if (seen)
			continue;
		if (n >= capacity)
			return 0;
		out[n++] = old[i];
	}
	return n;
}

// Where a system's loader is: the caller fills this from the disk.
struct Place {
	char partition[40]; // partition GUID text of the loader's volume
	char path[128];     // loader path on that volume
};
// The registry \EFI\HAK\systems.txt: a header line, then one line per system
// that is done: id|name|kind|partition GUID|loader path|plan id|date.
// The same format is read by CharonDOS (src/dos/launch_select.h).
constexpr const char* kRegistryHeader = "# charon-systems 1";
inline void RegistryLines(const Plan& plan, const Run& run, const Place* places, const char* date, Out& out)
{
	int sequence[kMaxSystems];   // r410: in the order of the boot entries, when one was chosen (else the list order)
	const int count = BootSequence(plan, sequence);
	for (int at = 0; at < count; at++) {
		const int i = sequence[at];
		if (ResultOf(run, i) != Result::done || places[i].path[0] == 0)
			continue;
		const System& s = plan.systems[i];
		out.S(s.id).Ch('|');
		for (size_t k = 0; s.name[k] != 0; k++)
			out.Ch(s.name[k] == '|' ? '/' : s.name[k]);
		out.Ch('|').S(LoaderKind(s.kind)).Ch('|').S(places[i].partition).Ch('|').S(places[i].path).Ch('|');
		out.Hex(plan.id, 8).Ch('|').S(date).S("\r\n");
	}
}
namespace detail {
// Field `index` of a registry line; false when the line has fewer fields.
inline bool Field(const char* line, size_t length, int index, const char*& text, size_t& size)
{
	size_t at = 0;
	for (int i = 0; i < index; i++) {
		while (at < length && line[at] != '|')
			at++;
		if (at == length)
			return false;
		at++;
	}
	size_t end = at;
	while (end < length && line[end] != '|')
		end++;
	text = line + at;
	size = end - at;
	return true;
}
inline bool SameField(const char* a, size_t an, const char* b, size_t bn)
{
	if (an != bn)
		return false;
	for (size_t i = 0; i < an; i++)
		if (Lower(a[i]) != Lower(b[i]))
			return false;
	return true;
}
} // namespace detail
// The new registry: the old lines that the fresh ones do not replace (same id,
// or same partition and path), in their order, then the fresh lines.
inline void MergeRegistry(const char* old, size_t oldLength, const char* fresh, size_t freshLength, Out& out)
{
	using namespace detail;
	out.S(kRegistryHeader).S("\r\n");
	for (size_t at = 0; old != nullptr && at < oldLength;) {
		size_t end = at;
		while (end < oldLength && old[end] != '\n')
			end++;
		size_t length = end - at;
		if (length > 0 && old[at + length - 1] == '\r')
			length--;
		const char* line = old + at;
		at = end + 1;
		const char* id = nullptr;
		const char* part = nullptr;
		const char* path = nullptr;
		size_t idN = 0, partN = 0, pathN = 0;
		if (length == 0 || line[0] == '#' || !Field(line, length, 0, id, idN) || !Field(line, length, 3, part, partN)
			|| !Field(line, length, 4, path, pathN))
			continue;
		bool replaced = false;
		for (size_t f = 0; f < freshLength && !replaced;) {
			size_t fe = f;
			while (fe < freshLength && fresh[fe] != '\n')
				fe++;
			size_t fl = fe - f;
			if (fl > 0 && fresh[f + fl - 1] == '\r')
				fl--;
			const char* a = nullptr;
			const char* b = nullptr;
			const char* c = nullptr;
			size_t aN = 0, bN = 0, cN = 0;
			if (fl != 0 && Field(fresh + f, fl, 0, a, aN) && Field(fresh + f, fl, 3, b, bN) && Field(fresh + f, fl, 4, c, cN))
				replaced = SameField(id, idN, a, aN) || (SameField(part, partN, b, bN) && SameField(path, pathN, c, cN));
			f = fe + 1;
		}
		if (!replaced)
			out.SN(line, length).S("\r\n");
	}
	out.SN(fresh, freshLength);
}

// What WINNT and LINUX start in CharonDOS when no name is given: lines for
// \EFI\CharonDOS\BOOT.CFG ("winnt=<id>", "linux=<id>"). With one system of a
// kind no default is needed; with several it is the answer
// plan.dosdefault.winnt / .linux ([plan] dosdefault_winnt / dosdefault_linux:
// a system id, or "ask"), else the first of the list that is done.
inline void DosDefaults(const Plan& plan, const Run& run, const Store& store, Out& out)
{
	if (!HasCharonDos(plan))
		return;
	for (int pass = 0; pass < 2; pass++) {
		const bool windows = pass == 0;
		int count = 0, first = -1;
		for (int i = 0; i < plan.count; i++) {
			const Kind kind = plan.systems[i].kind;
			if ((windows ? IsWindows(kind) : IsLinux(kind)) && ResultOf(run, i) == Result::done) {
				count++;
				if (first < 0)
					first = i;
			}
		}
		if (count < 2)
			continue;
		const char* asked = store.GetOr("plan", windows ? "dosdefault_winnt" : "dosdefault_linux", "");
		if (EqNoCase(asked, "ask"))
			continue;
		int chosen = asked[0] != 0 ? FindSystem(plan, asked) : first;
		if (chosen < 0 || ResultOf(run, chosen) != Result::done
			|| (windows ? !IsWindows(plan.systems[chosen].kind) : !IsLinux(plan.systems[chosen].kind)))
			chosen = first;
		out.S(windows ? "winnt=" : "linux=").S(plan.systems[chosen].id).S("\r\n");
	}
}

// Reserved ranges that were not used (their system is not done): the user is
// asked about each (free it / keep it reserved); unattended: [plan] leftover.
// Spare ranges are not leftovers: the finish step always removes them.
// r410: a system whose installer ran, but whose first start reported an error or never reported: its range is NOT
// empty - it may hold a complete, working system. Seen in VMware: Windows was installed and worked, its first start
// reported "Charon could not be put first in the boot order", the finish step called the range empty, offered to
// free it, and "free it" removed the installed Windows. Such a range is never offered for freeing, never freed by
// an unattended plan and never called unused; the report says what is there.
inline bool MayHoldSystem(const Run& run, int system)
{
	for (int i = 0; i < run.count; i++) {
		const StepRun& s = run.steps[i];
		// Also after "skip this system and continue": the skipped step keeps its reason (Decide), and the
		// range is exactly as full as it was when the step failed.
		if (s.system == system && (s.state == StepState::failed || s.state == StepState::skipped)
			&& (s.fail == Fail::reported || s.fail == Fail::budgetStarts || s.fail == Fail::budgetTime))
			return true;
	}
	return false;
}
inline int Leftovers(const Run& run, const Layout& layout, int* ranges, int capacity)
{
	int n = 0;
	for (int i = 0; i < layout.count; i++) {
		const Range& r = layout.ranges[i];
		if ((r.role != Role::system && r.role != Role::windowsRecovery) || r.system < 0)
			continue;
		if (MayHoldSystem(run, r.system))
			continue;
		if (ResultOf(run, r.system) != Result::done && n < capacity)
			ranges[n++] = i;
	}
	return n;
}

// Leaves a record without answers: [charon-plan], [plan], [step.N], layout.
inline void StripAnswers(Store& store, const Plan& plan)
{
	store.RemoveSection("shared");
	store.RemoveSection("secrets");
	for (int i = 0; i < plan.count; i++) {
		char section[40];
		SystemSection(plan.systems[i].id, section, sizeof(section));
		const char* kind = store.Get(section, "kind");
		char kept[24], name[48];
		Copy(kept, sizeof(kept), kind);
		Copy(name, sizeof(name), store.GetOr(section, "name", ""));
		store.RemoveSection(section);
		store.Put(section, "kind", kept);
		store.Put(section, "name", name);
	}
}
// The end of the finish step: report.txt, plan-done.txt (no answers), the
// plan files and the variable removed. Every answer file must be gone before
// (EraseStepFiles); a step whose files could not be erased is named by the
// return value (-1 = all clean, files removed).
inline int CloseFiles(const Storage& storage, const Plan& plan, Run& run, Store& store, const char* report,
	char* work, size_t capacity)
{
	for (int i = 0; i < run.count; i++)
		if (run.steps[i].files[0] != 0 && !EraseStepFiles(storage, run, i, work, capacity))
			return i;
	storage.write(storage.context, kPlanReport, report, Len(report));
	StripAnswers(store, plan);
	RunToStore(plan, run, store);
	Out out(work, capacity);
	Serialize(store, out);
	storage.remove(storage.context, kPlanDone);
	storage.write(storage.context, kPlanDone, out.text, out.length);
	storage.remove(storage.context, kPlanFile);
	storage.remove(storage.context, kPlanNew);
	storage.remove(storage.context, kPlanBackup);
	storage.remove(storage.context, kPlanCurrent);
	for (int i = 0; i < run.count; i++)
		for (int failed = 0; failed < 2; failed++) {
			char path[96];
			Out marker(path, sizeof(path));
			MarkerPath(i, failed != 0, marker);
			storage.remove(storage.context, path);
		}
	// The emptied folders of the steps' answer files (seen left behind in the first Windows run).
	for (int i = 0; i < run.count; i++) {
		char path[96];
		Out folder(path, sizeof(path));
		folder.S("\\EFI\\HAK\\plan\\step").N(uint64_t(i));
		if (storage.exists(storage.context, path))
			storage.remove(storage.context, path);
	}
	storage.setVariable(storage.context, nullptr, 0);
	return -1;
}

// ---------------------------------------------------------------------------
// Spoken and written texts
// ---------------------------------------------------------------------------
// r410: what a step runner or the finish step gives as its reason is a fixed English sentence: that is what
// the plan file and the log keep. Spoken and shown, it is looked up here, so that a Polish sentence does not
// carry an English reason. A text that is not in the table (a system's own report) is passed on as it is.
// The table is checked against every reason in the sources by tests/charon-r410-plan-reasons-host.py.
struct ReasonText { const char* en; const char* pl; };
inline const char* Reason(const char* en, bool pl)
{
	static const ReasonText table[] = {
		// r410: what Windows' first-logon command reports (-PlanText, CharonWsPlanWindows.h)
		{"Charon could not be put first in the boot order",
		 "nie udało się ustawić Charona jako pierwszego w kolejności rozruchu"},
		// r410: everything is downloaded first (CharonWsPlanDialogue.h FetchUpfront), and the order of the boot entries
		{"a file could not be downloaded before the disk was changed",
		 "nie udało się pobrać pliku przed zmianą dysku"},
		{"a downloaded file does not have the announced size",
		 "pobrany plik ma inny rozmiar niż zapowiedziany"},
		{"there is not enough memory for the files of the plan",
		 "brakuje pamięci na pliki planu"},
		{"the download before the disk was changed was stopped with Escape",
		 "pobieranie przed zmianą dysku przerwano klawiszem Escape"},
		{"a downloaded file is not the one that was expected",
		 "pobrany plik jest inny niż oczekiwany"},
		{"Debian's installer could not be started from the files on the EFI partition",
		 "nie udało się uruchomić instalatora Debiana z plików na partycji EFI"},
		// r410 (helper bd): Ubuntu's installer in a plan, the shared data partition
		{"Ubuntu's installer can only be pointed at the largest disk, and the plan's disk is not the largest",
		 "instalatorowi Ubuntu można wskazać tylko największy dysk, a dysk planu nie jest największy"},
		{"the disk does not have exactly one free range for Ubuntu's installer", "na dysku nie ma dokładnie jednego wolnego miejsca dla instalatora Ubuntu"},
		{"the firmware does not see the plan's EFI partition", "firmware nie widzi partycji EFI planu"},
		{"the installer's kernel could not be read from the EFI partition", "nie udało się odczytać jądra instalatora z partycji EFI"},
		{"the installer could not be read from the EFI partition", "nie udało się odczytać instalatora z partycji EFI"},
		{"Ubuntu's installer could not be downloaded or started", "nie udało się pobrać albo uruchomić instalatora Ubuntu"},
		{"the address of the Ubuntu system image that matches the installer could not be found",
		 "nie udało się ustalić adresu obrazu systemu Ubuntu pasującego do instalatora"},
		{"the answers do not fit in the installer's answer file", "odpowiedzi nie mieszczą się w pliku odpowiedzi instalatora"},
		{"the answer files could not be written", "nie udało się zapisać plików odpowiedzi"},
		{"the seed partition of an image could not be renamed for Ubuntu's installer",
		 "nie udało się zmienić etykiety partycji startowej obrazu na czas instalatora Ubuntu"},
		{"the plan's EFI partition could not be given the label CIDATA", "nie udało się nadać partycji EFI planu etykiety CIDATA"},
		{"Ubuntu's installer could not be started", "nie udało się uruchomić instalatora Ubuntu"},
		{"the shared data partition needs 512-byte sectors", "wspólna partycja na dane wymaga sektorów 512-bajtowych"},
		{"the shared data partition could not be formatted", "nie udało się sformatować wspólnej partycji na dane"},
		{"the free room of an image could not be covered for the installer", "nie udało się przykryć wolnego miejsca obrazu na czas instalatora"},
		{"this version of Charon has no installer for the Charon Hypervisor", "ta wersja Charona nie ma instalatora Charon Hypervisor"},
		{"the partition of the Charon Hypervisor could not be made in its range", "nie udało się zrobić partycji Charon Hypervisor w jej zakresie"},
		{"the Charon Hypervisor could not be written", "nie udało się zapisać Charon Hypervisor"},
		{"Fedora's bigger EFI partition could not be made", "nie udało się zrobić dla Fedory większej partycji EFI"},
		{"the boot entry of a system was not found, so it keeps its place in the boot order",
		 "nie znaleziono wpisu rozruchowego systemu, więc zostaje on na swoim miejscu w kolejności rozruchu"},
		{"the chosen order of the boot entries could not be written",
		 "nie udało się zapisać wybranej kolejności wpisów rozruchowych"},
		{"the EFI partition of the plan is not in the layout",
		 "partycji EFI planu nie ma w układzie dysku"},
		{"the existing partitions of the disk are not as they were when the plan was made",
		 "istniejące partycje dysku nie są takie jak w chwili tworzenia planu"},
		{"the firmware did not take the variable CharonPlan",
		 "firmware nie przyjęło zmiennej CharonPlan"},
		{"the partition table could not be written",
		 "nie udało się zapisać tablicy partycji"},
		{"a partition of the plan could not be created",
		 "nie udało się utworzyć partycji planu"},
		{"the EFI partition could not be formatted",
		 "nie udało się sformatować partycji EFI"},
		{"the firmware does not see the new EFI partition",
		 "firmware nie widzi nowej partycji EFI"},
		{"Charon could not be copied onto the new EFI partition",
		 "nie udało się skopiować Charona na nową partycję EFI"},
		{"the layout does not fit the plan file",
		 "układ dysku nie mieści się w pliku planu"},
		{"the plan could not be written to the new EFI partition",
		 "nie udało się zapisać planu na nowej partycji EFI"},
		{"the boot entry Charon could not be written",
		 "nie udało się zapisać wpisu rozruchowego Charon"},
		{"the disk of the plan was not found",
		 "nie znaleziono dysku planu"},
		{"the layout is missing from the plan file",
		 "w pliku planu brakuje układu dysku"},
		{"the CharonDOS package was not found on any disk (EFI, HAK, packages, CharonDOS)",
		 "nie znaleziono pakietu CharonDOS na żadnym dysku (EFI, HAK, packages, CharonDOS)"},
		{"CharonDOS could not be copied",
		 "nie udało się skopiować CharonDOS"},
		{"Charon could not be copied into memory",
		 "nie udało się skopiować Charona do pamięci"},
		{"the image or its range is not known",
		 "obraz albo jego zakres nie jest znany"},
		{"the answers of this system could not be read back",
		 "nie udało się odczytać odpowiedzi dla tego systemu"},
		{"there is no network connection",
		 "brak połączenia z siecią"},
		{"the reserved range of this system is not on the disk as the plan left it",
		 "zarezerwowany zakres tego systemu nie jest na dysku taki, jak zostawił go plan"},
		{"the image could not be downloaded or written",
		 "nie udało się pobrać albo zapisać obrazu"},
		{"the configuration of the image could not be written to the disk",
		 "nie udało się zapisać na dysk konfiguracji obrazu"},
		{"Charon's files could not be put on the image's EFI partition",
		 "nie udało się umieścić plików Charona na partycji EFI obrazu"},
		{"this version of Charon has no unattended installer for this system in a plan",
		 "ta wersja Charona nie ma dla tego systemu instalatora bez pytań w planie"},
		{"the range of this system is not in the layout",
		 "zakresu tego systemu nie ma w układzie dysku"},
		{"two disks have exactly the same size: Debian's installer could mix them up",
		 "dwa dyski mają dokładnie ten sam rozmiar: instalator Debiana mógłby je pomylić"},
		{"the range of a system that was not installed could not be reserved again",
		 "nie udało się ponownie zarezerwować zakresu systemu, który nie został zainstalowany"},
		{"the disk does not have exactly one free range for Debian's installer",
		 "dysk nie ma dokładnie jednego wolnego zakresu dla instalatora Debiana"},
		{"the plan could not be saved",
		 "nie udało się zapisać planu"},
		{"Debian's installer could not be downloaded or started",
		 "nie udało się pobrać albo uruchomić instalatora Debiana"},
		{"the password for Windows is no longer on the EFI partition",
		 "hasła do Windows nie ma już na partycji EFI"},
		{"the Windows version of the plan is not known to this version of Charon",
		 "ta wersja Charona nie zna wersji Windows zapisanej w planie"},
		{"the partition table could not be read",
		 "nie udało się odczytać tablicy partycji"},
		{"a partition inside the range of this system could not be removed",
		 "nie udało się usunąć partycji z zakresu tego systemu"},
		{"the Windows partition could not be made in its range",
		 "nie udało się utworzyć partycji Windows w jej zakresie"},
		{"the firmware does not see the Windows partition",
		 "firmware nie widzi partycji Windows"},
		{"the answers of this step could not be kept",
		 "nie udało się zachować odpowiedzi tego kroku"},
		{"Windows Setup could not be prepared or started; the reason was said",
		 "nie udało się przygotować albo uruchomić instalatora Windows; powód został już podany"},
		{"the system's boot loader was not found",
		 "nie znaleziono programu rozruchowego systemu"},
		{"the image's boot loader was not found",
		 "nie znaleziono programu rozruchowego obrazu"},
		{"a spare placeholder could not be removed",
		 "nie udało się usunąć zapasowej partycji zastępczej"},
		{"a placeholder over the plan's free space could not be removed",
		 "nie udało się usunąć partycji zastępczej, która chroniła wolne miejsce planu"},
		{"a reserved range could not be freed",
		 "nie udało się zwolnić zarezerwowanego zakresu"},
		{"the layout or the disk of the plan was not found",
		 "nie znaleziono układu albo dysku planu"},
		{"the boot order could not be written",
		 "nie udało się zapisać kolejności rozruchu"},
		{"the loader of a system was not found",
		 "nie znaleziono programu rozruchowego systemu"},
		{"the system that starts by itself could not be set",
		 "nie udało się ustawić systemu, który uruchamia się sam"},
		{"the wait before the default system starts could not be stored in the settings of Charon on the plan's EFI partition, so it keeps its usual value",
		 "nie udało się zapisać czasu oczekiwania przed startem domyślnego systemu w ustawieniach Charona na partycji EFI planu, więc zachowuje on zwykłą wartość"},
		{"the systems registry could not be written",
		 "nie udało się zapisać rejestru systemów"},
		{"the defaults of WINNT and LINUX could not be written",
		 "nie udało się zapisać ustawień domyślnych poleceń WINNT i LINUX"},
		{"the time zone for Charon's clock could not be stored",
		 "nie udało się zapisać strefy czasowej dla zegara Charona"},
		{"the reader add-on could not be registered",
		 "nie udało się zarejestrować dodatku czytnika"},
		{"Charon could not be put back as the fallback loader",
		 "nie udało się przywrócić Charona jako zapasowego programu rozruchowego"},
		{"the boot entry CharonDOS could not be written",
		 "nie udało się zapisać wpisu rozruchowego CharonDOS"},
	};
	if (!pl || en == nullptr)
		return en;
	for (const ReasonText& r : table) {
		size_t i = 0;
		while (r.en[i] != 0 && r.en[i] == en[i])
			i++;
		if (r.en[i] == 0 && en[i] == 0)
			return r.pl;
	}
	return en;
}
// One note of the finish step: notes are joined by "; " and the report ends them with a full stop.
inline void Trouble(Out& out, const char* en, bool pl, const char* name = nullptr)
{
	if (out.length != 0)
		out.S("; ");
	out.S(Reason(en, pl));
	if (name != nullptr)
		out.S(": ").S(name);
}
namespace detail {
inline void StepWork(const Plan& plan, const StepRun& s, bool pl, Out& out)
{
	const char* name = NameOf(plan, s.system);
	switch (s.kind) {
	case StepKind::prepare: out.S(pl ? "przygotowuję dysk" : "the disk is being prepared"); break;
	case StepKind::writeDos:   // r410: CharonDOS or the Charon Hypervisor, both written by Charon
		if (s.system >= 0 && plan.systems[s.system].kind == Kind::hypervisor) {
			if (pl) out.S("zapisuję ").S(name);
			else out.S(name).S(" is being written");
		} else
			out.S(pl ? "zapisuję CharonDOS" : "CharonDOS is being written");
		break;
	case StepKind::writeImage:
		if (pl) out.S("zapisuję na dysk system ").S(name);
		else out.S(name).S(" is being written to the disk");
		break;
	case StepKind::install:
		if (pl) out.S(name).S(" instaluje się sam");
		else out.S(name).S(" is installing itself");
		break;
	case StepKind::firstStart:
		if (pl) out.S(name).S(" konfiguruje się przy pierwszym uruchomieniu");
		else out.S(name).S(" is setting itself up at its first start");
		break;
	default: out.S(pl ? "kończę plan" : "the plan is being finished"); break;
	}
}
inline void Phase(const Plan& plan, const StepRun& s, bool pl, Out& out)
{
	switch (s.fail) {
	case Fail::charon:
		out.S(pl ? "Charon nie mógł dokończyć własnej pracy" : "Charon could not finish its own work");
		break;
	case Fail::download: out.S(pl ? "pobieranie się nie powiodło" : "the download failed"); break;
	case Fail::reported: out.S(pl ? "pierwsze uruchomienie zgłosiło błąd" : "its first start reported an error"); break;
	case Fail::budgetStarts:
		if (pl) out.S("instalator nie zgłosił zakończenia po ").N(kStartBudget).S(" uruchomieniach");
		else out.S("the installer did not report back after ").N(kStartBudget).S(" starts");
		break;
	case Fail::budgetTime:
		if (pl) out.S("instalator nie zgłosił zakończenia w ciągu ").N(TimeBudget(plan, s) / 3600).S(" godzin");
		else out.S("the installer did not report back within ").N(TimeBudget(plan, s) / 3600).S(" hours");
		break;
	case Fail::interrupted:
		out.S(pl ? "praca Charona została przerwana w połowie" : "Charon's own work was cut off in the middle");
		break;
	case Fail::user: out.S(pl ? "pominięty na twoje polecenie" : "given up at your request"); break;
	case Fail::stopped: out.S(pl ? "nie został wykonany, bo plan zatrzymano wcześniej" : "did not run, because the plan was stopped before it"); break;
	case Fail::dependency: out.S(pl ? "nie został wykonany, bo wcześniejszy krok tego systemu się nie powiódł" : "did not run, because an earlier step of this system failed"); break;
	default: out.S(pl ? "nieznany powód" : "unknown reason"); break;
	}
	if (s.reason[0] != 0)
		out.S(": ").S(Reason(s.reason, pl));
}
// r410 (agent 1, T5 run): a system whose image is written but whose first start has not run yet is not "untouched".
// begun: -1 any, 0 only systems with no step done, 1 only systems with a step done.
inline bool AnyStepDone(const Run& run, int system)
{
	for (int k = 0; k < run.count; k++)
		if (run.steps[k].system == system && run.steps[k].state == StepState::done)
			return true;
	return false;
}
inline void NamesWith(const Plan& plan, const Run& run, Result wanted, bool pl, Out& out, int begun = -1)
{
	bool any = false;
	for (int i = 0; i < plan.count; i++)
		if (ResultOf(run, i) == wanted && (begun < 0 || AnyStepDone(run, i) == (begun == 1))) {
			if (any)
				out.S(", ");
			out.S(plan.systems[i].name);
			any = true;
		}
	if (!any)
		out.S(pl ? "nic" : "nothing");
}
} // namespace detail

// "Plan, step 2 of 4: Windows 11 is installing itself. Next: Debian. Space or
// Escape pauses the plan." Steps are counted without disk preparation.
inline void Announce(const Plan& plan, const Run& run, bool pl, Out& out)
{
	const int at = Current(run);
	if (at < 0) {
		out.S(pl ? "Plan jest zakończony." : "The plan is finished.");
		return;
	}
	const StepRun& s = run.steps[at];
	if (pl) out.S("Plan, krok ").N(uint64_t(at)).S(" z ").N(uint64_t(run.count) - 1).S(": ");
	else out.S("Plan, step ").N(uint64_t(at)).S(" of ").N(uint64_t(run.count) - 1).S(": ");
	detail::StepWork(plan, s, pl, out);
	out.S(". ");
	if (s.starts > 1) {
		if (pl) out.S("To uruchomienie numer ").N(s.starts).S(". ");
		else out.S("This is start number ").N(s.starts).S(". ");
	}
	for (int i = at + 1; i < run.count; i++) {
		if (Closed(run, run.steps[i]))
			continue;
		if (run.steps[i].kind == StepKind::finish)
			out.S(pl ? "Potem koniec planu. " : "After that the plan ends. ");
		else if (run.steps[i].system == s.system)
			out.S(pl ? "Potem jego pierwsze uruchomienie. " : "After that, its first start. ");
		else {
			out.S(pl ? "Następny: " : "Next: ").S(NameOf(plan, run.steps[i].system)).S(". ");
		}
		break;
	}
	out.S(pl ? "Spacja albo Escape wstrzymuje plan." : "Space or Escape pauses the plan.");
}

// What is said, and logged, when the plan stops at a failed step (spec 2.9).
inline void FailureText(const Plan& plan, const Run& run, bool pl, Out& out)
{
	const int at = Current(run);
	if (at < 0)
		return;
	const StepRun& s = run.steps[at];
	if (pl) out.S("Plan zatrzymany. Krok ").N(uint64_t(at));
	else out.S("The plan has stopped. Step ").N(uint64_t(at));
	if (s.system >= 0)
		out.S(", ").S(NameOf(plan, s.system));
	out.S(": ");
	detail::Phase(plan, s, pl, out);
	out.S(". ");
	out.S(pl ? "Gotowe i uruchamialne: " : "Finished and bootable: ");
	detail::NamesWith(plan, run, Result::done, pl, out);
	out.S(". ");
	out.S(pl ? "Nietknięte: " : "Not touched: ");
	detail::NamesWith(plan, run, Result::pending, pl, out, 0);
	out.S(". ");
	{
		bool begun = false;
		for (int i = 0; i < plan.count; i++)
			begun = begun || (ResultOf(run, i) == Result::pending && detail::AnyStepDone(run, i));
		if (begun) {
			out.S(pl ? "Zapisane, jeszcze nie uruchomione: " : "Written, not started yet: ");
			detail::NamesWith(plan, run, Result::pending, pl, out, 1);
			out.S(". ");
		}
	}
	if (s.kind == StepKind::prepare || s.kind == StepKind::finish)
		out.S(pl ? "Możesz spróbować ponownie albo zakończyć tutaj." : "You can try again or stop here.");
	else
		out.S(pl ? "Spróbuj ponownie, pomiń ten system i kontynuuj, albo zakończ tutaj."
			: "Try again, skip this system and continue, or stop here.");
}
inline void PausedText(bool pl, Out& out)
{
	out.S(pl ? "Plan wstrzymany. Kontynuuj, pomiń ten system albo zakończ tutaj."
		: "Plan paused. Continue, skip this system, or stop here.");
}
// "Step 3 did not run. Its 40 gigabytes are reserved and empty. Free them, or
// keep them reserved?"
inline void LeftoverText(const Plan& plan, const Layout& layout, int range, bool pl, Out& out)
{
	const Range& r = layout.ranges[range];
	const uint64_t mb = RangeMb(layout, r);
	if (pl) {
		out.S(NameOf(plan, r.system)).S(": krok się nie wykonał. Zarezerwowane dla niego miejsce, ");
		Size(out, mb, true);
		out.S(", jest puste. Zwolnić je czy zostawić zarezerwowane?");
	} else {
		out.S(NameOf(plan, r.system)).S(": its step did not run. The space reserved for it, ");
		Size(out, mb, false);
		out.S(", is empty. Free it, or keep it reserved?");
	}
}
// Charon found the variable but no plan file (started from another medium
// without the plan's disk, or the plan lost its file).
inline void PointerLostText(const Pointer& pointer, bool pl, Out& out)
{
	if (pl) out.S("Plan instalacji zatrzymał się na kroku ").N(pointer.step).S(", ale nie mogę znaleźć jego pliku. Podłącz dysk z planem albo usuń plan.");
	else out.S("An installation plan stopped at step ").N(pointer.step).S(", but its file cannot be found. Connect the disk that holds the plan, or discard the plan.");
}

// r410: which CharonDOS commands start this plan's systems: WINNT for Windows, LINUX for a Linux; none is named for a
// system the plan does not have (seen in VMware: "WINNT starts Windows" in a plan of CharonDOS and Debian).
inline void DosCommandsText(const Plan& plan, bool pl, Out& out)
{
	bool windows = false, otherLinux = false, hypervisor = false;
	for (int i = 0; i < plan.count; i++) {
		if (plan.systems[i].kind == Kind::windows) windows = true;
		else if (IsHypervisor(plan.systems[i].kind)) hypervisor = true;   // r410 (agent 1, h7a): LINUX does not find it
		else if (plan.systems[i].kind != Kind::charondos && plan.systems[i].kind != Kind::none) otherLinux = true;
	}
	if (hypervisor)
		out.S(pl ? "Charon Hypervisor uruchamia się z listy urządzeń rozruchowych Charona; polecenie EXIT w CharonDOS wraca do Charona. "
		         : "Charon Hypervisor starts from Charon's boot devices list; EXIT in CharonDOS goes back to Charon. ");
	if (windows && otherLinux)
		out.S(pl ? "W nim polecenie WINNT uruchamia Windows, a LINUX uruchamia Linuksa. " : "In it, WINNT starts Windows and LINUX starts Linux. ");
	else if (windows)
		out.S(pl ? "W nim polecenie WINNT uruchamia Windows. " : "In it, WINNT starts Windows. ");
	else if (otherLinux)
		out.S(pl ? "W nim polecenie LINUX uruchamia Linuksa. " : "In it, LINUX starts Linux. ");
}

// The summary that is spoken and shown before the one typed confirmation
// (spec 6.6). `diskGb`, `diskEmpty`, `user` describe the target as the
// dialogue knows it.
inline void Summary(const Plan& plan, const Layout& layout, const Machine& machine, uint64_t diskGb, bool diskEmpty,
	const char* user, bool pl, Out& out, const FetchList* fetch = nullptr)
{
	out.S(pl ? "Plan. Dysk, " : "Plan. Disk, ");
	Gigabytes(out, diskGb, pl);
	if (diskEmpty)
		out.S(pl ? ", pusty. " : ", empty. ");
	else if (layout.newTable)
		out.S(pl ? ". " : ". ");
	else
		out.S(pl ? ", to, co na nim jest, zostaje. " : ", what is on it stays. ");
	if (pl) out.S("Systemów: ").N(plan.count).S(", w tej kolejności: ");
	else out.S("Systems: ").N(plan.count).S(", in this order: ");
	Order order;
	ComputeOrder(plan, order);
	int number = 0;
	for (int i = 0; i < order.count; i++) {
		const Step& step = order.steps[i];
		if (step.kind != StepKind::writeDos && step.kind != StepKind::writeImage && step.kind != StepKind::install)
			continue;
		const int range = RangeOfSystem(layout, step.system);
		out.N(uint64_t(++number)).S(", ").S(NameOf(plan, step.system));
		if (range >= 0) {
			out.S(", ");
			Size(out, RangeMb(layout, layout.ranges[range]), pl);
			if (layout.ranges[range].role == Role::dosEfi)
				out.S(pl ? ", to także partycja EFI" : ", it is also the EFI partition");
		}
		if (!plan.systems[step.system].speaks)
			out.S(pl ? ", nie mówi" : ", does not speak");
		out.S(". ");
	}
	for (int i = 0; i < layout.count; i++) {
		const Range& r = layout.ranges[i];
		if (r.role == Role::data) {
			out.S(pl ? "Wspólna partycja na dane, " : "Shared data partition, ").S(plan.data == DataFs::ntfs ? "NTFS, " : "exFAT, ");
			Size(out, RangeMb(layout, r), pl);
			out.S(". ");
		} else if (r.role == Role::windowsRecovery) {
			if (pl) out.S("Partycja odzyskiwania dla systemu ").S(NameOf(plan, r.system)).S(". ");
			else out.S("A recovery partition for ").S(NameOf(plan, r.system)).S(". ");
		}
	}
	if (layout.espExisting)
		out.S(pl ? "Istniejąca partycja EFI służy wszystkim systemom. " : "The existing EFI partition serves every system. ");
	if (layout.freeMb != 0) {
		out.S(pl ? "Wolne zostaje " : "Left free: ");
		Size(out, layout.freeMb, pl);
		out.S(". ");
	}
	if (user != nullptr && user[0] != 0) {
		if (pl) out.S("Użytkownik ").S(user).S(" w każdym systemie. ");
		else out.S("User ").S(user).S(" in every system. ");
	}
	if (ClockUtc(plan))
		out.S(pl ? "Zegar komputera będzie w czasie uniwersalnym. " : "The computer's clock is kept in universal time. ");
	const StartRule start = StartOf(plan);
	if (start.dosFirst) {
		out.S(pl ? "Jako pierwszy uruchamia się CharonDOS. " : "CharonDOS starts first. ");
		DosCommandsText(plan, pl, out);   // r410: only the commands of the systems this plan has (seen: WINNT named with no Windows)
	} else if (start.menuWaits)
		out.S(pl ? "Jako pierwsze uruchamia się menu Charona i czeka na ciebie. " : "Charon's menu starts first and waits for you. ");
	else if (pl) {
		out.S("Jako pierwsze uruchamia się menu Charona; po ").N(start.timeout).Ch(' ')
			.S(PolishForm(start.timeout, "sekundzie", "sekundach", "sekundach")).S(" uruchamia system ").S(NameOf(plan, start.system)).S(". ");
	} else {
		out.S("Charon's menu starts first; after ").N(start.timeout).S(start.timeout == 1 ? " second" : " seconds")
			.S(" it starts ").S(NameOf(plan, start.system)).S(". ");
	}
	if (BootOrderChosen(plan)) {   // r410
		int sequence[kMaxSystems];
		const int count = BootSequence(plan, sequence);
		out.S(pl ? "Wpisy rozruchowe po Charonie w kolejności: " : "Boot entries after Charon, in this order: ");
		for (int i = 0; i < count; i++)
			out.S(i != 0 ? ", " : "").S(NameOf(plan, sequence[i]));
		out.S(". ");
	}
	if (fetch != nullptr)
		FetchSummary(plan, *fetch, pl, out);
	if (layout.newTable && !diskEmpty)
		out.S(pl ? "Wszystko na tym dysku zostanie wymazane. " : "Everything on this disk will be erased. ");
	out.S(pl ? "Instalacja trwa długo i kilka razy sama uruchomi komputer ponownie; nic nie musisz robić. "
		: "The installation takes a long time and restarts the computer several times by itself; you do not need to do anything. ");
	if (machine.runningFromRam)
		out.S(pl ? "Charon działa z pamięci i sam przeniesie się na ten dysk. " : "Charon runs from memory and will put itself onto this disk. ");
	else
		out.S(pl ? "Zostaw nośnik Charona w gnieździe, dopóki nie powiem, że plan jest zakończony. "
			: "Leave Charon's start medium plugged in until I say the plan is finished. ");
	if (machine.secureBoot)
		out.S(pl ? "Secure Boot jest włączony." : "Secure Boot is on.");
	else
		out.S(pl ? "Secure Boot jest wyłączony." : "Secure Boot is off.");
}

// The final report: spoken, written to \EFI\HAK\plan\report.txt and copied
// into every installed system (spec 2.9). One line per fact.
// freedMask: bit i = range i of the layout was freed by the finish step (its partitions were removed).
// clockShift: seconds that the hardware clock went BACK when the first installed system switched it to universal
// time (the zone's offset; 0 = the clock was not switched). Charon's time stamps before that moment are local
// time, the later ones universal time (seen in VMware: "finished" before "first", and the duration was dropped).
inline int ClockSwitcher(const Plan& plan, const Run& run)
{
	for (int k = 0; k < run.count; k++) {
		const StepRun& step = run.steps[k];
		// A step that failed in Charon's own work (download, write, cut off) never started its system, so that
		// system cannot have switched the clock (seen in VMware: Windows' download failed, Debian was installed,
		// and Debian's duration came out as "not measured").
		// r410 (agent 1, h7c): the Charon Hypervisor is written by Charon and does not start during the plan either;
		// taking it as the switcher gave it the zone's offset ("61 minutes" for a 6 s step) and Debian none.
		if (step.system >= 0 && step.first != 0 && plan.systems[step.system].kind != Kind::charondos
			&& plan.systems[step.system].kind != Kind::hypervisor
			&& step.fail != Fail::charon && step.fail != Fail::download && step.fail != Fail::interrupted)
			return step.system;
	}
	return -1;
}
inline void Report(const Plan& plan, const Run& run, const Layout& layout, const Machine& machine, const char* user,
	bool pl, Out& out, uint32_t freedMask = 0, int64_t clockShift = 0, const Upfront* upfront = nullptr)
{
	// r410 (helper pv, R2a): the zone's offset is added only when the stamps show that the clock really went back
	// during the plan (a step ending before it began, or beginning before an earlier step ended). A system that
	// switches the clock only later (Debian at its own later start) left the stamps in order: nothing is added.
	bool wentBack = false;
	{
		uint64_t latest = 0;
		for (int k = 0; k < run.count; k++) {
			const StepRun& step = run.steps[k];
			if (step.first != 0 && step.first < latest) wentBack = true;
			if (step.first != 0 && step.finished != 0 && step.finished < step.first) wentBack = true;
			if (step.first > latest) latest = step.first;
			if (step.finished > latest) latest = step.finished;
		}
	}
	const int switcher = clockShift != 0 && wentBack ? ClockSwitcher(plan, run) : -1;
	int done = 0;
	for (int i = 0; i < plan.count; i++)
		done += ResultOf(run, i) == Result::done ? 1 : 0;
	if (pl) out.S("Plan zakończony. Zainstalowane systemy: ").N(uint64_t(done)).S(" z ").N(plan.count).S(".\r\n");
	else out.S("The plan is finished. Systems installed: ").N(uint64_t(done)).S(" of ").N(plan.count).S(".\r\n");
	if (upfront != nullptr)   // r410: the phases before the first system
		UpfrontReport(*upfront, pl, out);
	for (int i = 0; i < plan.count; i++) {
		const System& s = plan.systems[i];
		const Result result = ResultOf(run, i);
		out.S(s.name).S(": ");
		const int range = RangeOfSystem(layout, i);
		if (range >= 0) {
			Size(out, RangeMb(layout, layout.ranges[range]), pl);
			out.S(", ");
		}
		if (user != nullptr && user[0] != 0 && s.kind != Kind::charondos)
			out.S(pl ? "użytkownik " : "user ").S(user).S(", ");
		uint64_t first = 0, last = 0;
		const StepRun* cause = nullptr;
		for (int k = 0; k < run.count; k++) {
			const StepRun& step = run.steps[k];
			if (step.system != i)
				continue;
			// r410: the steps run in their order, so the system's time runs from its first step's start to its last
			// step's end; the largest stamp is not the last one when the clock went back in between (seen in VMware:
			// a Debian image whose first start switched the clock was reported with 64 minutes instead of 5).
			if (step.first != 0 && first == 0)
				first = step.first;
			if (step.finished != 0)
				last = step.finished;
			if (step.state != StepState::done && (cause == nullptr || cause->fail == Fail::dependency))
				cause = &step;
		}
		if (result == Result::done) {
			out.S(pl ? "zainstalowany" : "installed");
			int64_t elapsed = first != 0 && last != 0 ? int64_t(last) - int64_t(first) : 0;
			bool unknown = false;
			if (switcher >= 0 && s.kind != Kind::charondos && first != 0 && last != 0) {
				if (i == switcher)
					elapsed += clockShift;
				else if (ResultOf(run, switcher) != Result::done)
					unknown = true;   // nobody knows whether the system that failed had switched the clock already
				unknown = unknown || elapsed <= 0 || elapsed > 24 * 3600;
			}
			if (unknown)
				out.S(pl ? ", czas nie został zmierzony, bo w trakcie planu zegar sprzętowy przeszedł na czas uniwersalny"
					: ", the time was not measured, because the hardware clock was switched to universal time during the plan");
			else if (elapsed > 0) {
				const uint64_t minutes = (uint64_t(elapsed) + 59) / 60;
				if (pl) out.S(", ").N(minutes).Ch(' ').S(PolishForm(minutes, "minuta", "minuty", "minut"));
				else out.S(", ").N(minutes).S(minutes == 1 ? " minute" : " minutes");
			}
		} else {
			out.S(result == Result::failed ? (pl ? "NIE zainstalowany, błąd: " : "NOT installed, failed: ")
				: result == Result::skipped ? (pl ? "NIE zainstalowany, pominięty: " : "NOT installed, skipped: ")
				: (pl ? "NIE zainstalowany: " : "NOT installed: "));
			if (cause != nullptr && cause->fail != Fail::none)
				detail::Phase(plan, *cause, pl, out);
			else
				out.S(pl ? "krok nie został wykonany" : "its step did not run");
		}
		out.S(".\r\n");
	}
	const FinishRule rule = FinishOf(plan, run, machine);
	if (rule.dosFirst) {
		out.S(pl ? "Kolejność rozruchu: CharonDOS, Charon, pozostałe. " : "Boot order: CharonDOS, Charon, the others. ");
		DosCommandsText(plan, pl, out);
		out.S("\r\n");
	} else if (rule.target >= 0 && plan.systems[rule.target].kind == Kind::charondos)
		out.S(pl ? "Kolejność rozruchu: Charon, który od razu uruchamia CharonDOS (to firmware nie ładuje czytnika samo), potem pozostałe.\r\n"
			: "Boot order: Charon, which goes straight on to CharonDOS (this firmware does not load the reader by itself), then the others.\r\n");
	else if (rule.target >= 0) {
		if (pl) out.S("Kolejność rozruchu: menu Charona jako pierwsze; po ").N(rule.timeout).S(" s uruchamia system ").S(NameOf(plan, rule.target)).S(".\r\n");
		else out.S("Boot order: Charon's menu first; after ").N(rule.timeout).S(" s it starts ").S(NameOf(plan, rule.target)).S(".\r\n");
	} else
		out.S(pl ? "Kolejność rozruchu: menu Charona jako pierwsze, czeka na wybór.\r\n" : "Boot order: Charon's menu first, it waits for a choice.\r\n");
	if (BootOrderChosen(plan) && !rule.dosFirst) {   // r410: the chosen order of the entries, of the systems that are there
		int sequence[kMaxSystems];
		const int count = BootSequence(plan, sequence);
		int named = 0;
		for (int i = 0; i < count; i++)
			if (ResultOf(run, sequence[i]) == Result::done)
				out.S(named++ != 0 ? ", " : pl ? "Wpisy rozruchowe po Charonie w wybranej kolejności: " : "Boot entries after Charon, in the chosen order: ")
					.S(NameOf(plan, sequence[i]));
		if (named != 0)
			out.S(".\r\n");
	}
	bool data = false;
	for (int i = 0; i < layout.count; i++)
		if (layout.ranges[i].role == Role::data) {
			data = true;
			out.S(pl ? "Wspólna partycja na dane: " : "Shared data partition: ").S(plan.data == DataFs::ntfs ? "NTFS, " : "exFAT, ");
			Size(out, RangeMb(layout, layout.ranges[i]), pl);
			out.S(".\r\n");
		}
	if (!data)
		out.S(pl ? "Wspólna partycja na dane: brak.\r\n" : "Shared data partition: none.\r\n");
	if (ClockUtc(plan))
		out.S(pl ? "Zegar sprzętowy: czas uniwersalny we wszystkich systemach.\r\n" : "Hardware clock: universal time in every system.\r\n");
	int leftovers[kMaxRanges];
	const int n = Leftovers(run, layout, leftovers, kMaxRanges);
	for (int i = 0; i < n; i++) {
		const Range& r = layout.ranges[leftovers[i]];
		if (pl) out.S("Zarezerwowane i nieużyte miejsce dla systemu ").S(NameOf(plan, r.system)).S(": ");
		else out.S("Reserved and unused space for ").S(NameOf(plan, r.system)).S(": ");
		Size(out, RangeMb(layout, r), pl);
		out.S(".\r\n");
	}
	for (int i = 0; i < layout.count; i++) {   // r410: a freed range is named, with what happened to it
		const Range& r = layout.ranges[i];
		if (i >= 32 || !(freedMask & (uint32_t(1) << i)) || r.system < 0)
			continue;
		if (pl) out.S("Zwolnione miejsce, zarezerwowane wcześniej dla systemu ").S(NameOf(plan, r.system)).S(": ");
		else out.S("Freed space, reserved before for ").S(NameOf(plan, r.system)).S(": ");
		Size(out, RangeMb(layout, r), pl);
		out.S(pl ? "; partycja została usunięta, miejsce jest nieprzydzielone.\r\n" : "; its partition was removed, the space is unallocated.\r\n");
	}
	for (int i = 0; i < layout.count; i++) {   // r410: see MayHoldSystem
		const Range& r = layout.ranges[i];
		if (r.role != Role::system || r.system < 0 || !MayHoldSystem(run, r.system))
			continue;
		if (pl) out.S("Miejsce systemu ").S(NameOf(plan, r.system)).S(", ");
		else out.S("The space of ").S(NameOf(plan, r.system)).S(", ");
		Size(out, RangeMb(layout, r), pl);
		out.S(pl ? ": instalator tam pracował, a pierwsze uruchomienie zgłosiło błąd albo się nie zgłosiło. System może tam być i działać; Charon niczego stamtąd nie usunął.\r\n"
		         : ": the installer worked there, and the first start reported an error or did not report at all. The system may be there and work; Charon removed nothing from it.\r\n");
	}
	bool erased = true, secrets = false;
	for (int i = 0; i < run.count; i++) {
		secrets = secrets || run.steps[i].erased || run.steps[i].files[0] != 0;
		erased = erased && run.steps[i].files[0] == 0;
	}
	if (secrets)
		out.S(erased ? (pl ? "Pliki odpowiedzi z hasłami zostały wymazane z partycji EFI.\r\n" : "The answer files with passwords were erased from the EFI partition.\r\n")
			: (pl ? "UWAGA: pliku odpowiedzi z hasłem nie udało się wymazać z partycji EFI, folder EFI, HAK, plan.\r\n"
				: "WARNING: an answer file with a password could not be erased from the EFI partition, folder EFI, HAK, plan.\r\n"));
}

} // namespace hakefi::plan
