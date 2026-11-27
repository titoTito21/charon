// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: multi-system plan - the model.
//
//   Store          answers in namespaces: [plan], [disk.N], [shared],
//                  [system.<id>] ... (the sections of an answer-set file,
//                  work/charon-detailed-questions-spec-2026-11-23.md 6.1)
//   ParseIni / WriteIni   that file format
//   Plan, System   the ordered list of 2-6 systems and the plan settings
//   PlanFromStore / PlanToStore   one mapping for the dialogue, the unattended
//                  file and plan.txt
//   ComputeOrder   the step order per combination (spec section 3)
//   ValidatePlan   every refusal of the pre-flight that needs no disk layout
//   Say            refusals and warnings as speakable sentences, EN and PL
//   MatchDisk      pre-authorisation of a disk by serial ([disk.N])
//
// Pure logic (see CharonPlanText.h). Nothing here reads or writes a disk.
#pragma once

#include "CharonPlanText.h"

namespace hakefi::plan {

constexpr int kMaxSystems = 6;
constexpr int kMaxSteps = 16;
constexpr int kMaxDiskRules = 4;

// ---------------------------------------------------------------------------
// Answer store
// ---------------------------------------------------------------------------
// Records "section\0key\0value\0" packed in the caller's buffer. Put replaces;
// a value that does not fit leaves the store unchanged and sets `overflow`.
class Store {
public:
	// constexpr: a static Store over a static buffer needs no run-time constructor (the EFI
	// program has no support code for guarded static initialisation).
	constexpr Store(char* buffer, size_t capacity) : overflow(false), data_(buffer), capacity_(capacity), used_(0) {}
	bool Put(const char* section, const char* key, const char* value)
	{
		if (section == nullptr || key == nullptr || key[0] == 0)
			return false;
		if (value == nullptr)
			value = "";
		const size_t need = Len(section) + Len(key) + Len(value) + 3;
		size_t at = 0, size = 0;
		const bool had = Find(section, key, at, size);
		if (used_ - (had ? size : 0) + need > capacity_) {
			overflow = true;
			return false;
		}
		if (had)
			Cut(at, size);
		char* p = data_ + used_;
		const char* const parts[3] = {section, key, value};
		for (int part = 0; part < 3; part++) {
			for (size_t i = 0; parts[part][i] != 0; i++)
				*p++ = parts[part][i];
			*p++ = 0;
		}
		used_ += need;
		return true;
	}
	bool PutNumber(const char* section, const char* key, uint64_t value)
	{
		char text[24];
		Out out(text, sizeof(text));
		out.N(value);
		return Put(section, key, text);
	}
	// Null when the key is absent.
	const char* Get(const char* section, const char* key) const
	{
		size_t at = 0, size = 0;
		if (!Find(section, key, at, size))
			return nullptr;
		const char* p = data_ + at;
		p += Len(p) + 1;
		p += Len(p) + 1;
		return p;
	}
	const char* GetOr(const char* section, const char* key, const char* fallback) const
	{
		const char* value = Get(section, key);
		return value != nullptr ? value : fallback;
	}
	bool Has(const char* section, const char* key) const { return Get(section, key) != nullptr; }
	void Remove(const char* section, const char* key)
	{
		size_t at = 0, size = 0;
		if (Find(section, key, at, size))
			Cut(at, size);
	}
	void RemoveSection(const char* section)
	{
		for (size_t at = 0; at < used_;) {
			const size_t size = RecordSize(at);
			if (Eq(data_ + at, section))
				Cut(at, size);
			else
				at += size;
		}
	}
	// Zeroes everything (answers hold passwords).
	void Wipe()
	{
		for (size_t i = 0; i < capacity_; i++)
			data_[i] = 0;
		used_ = 0;
	}
	size_t Count() const
	{
		size_t n = 0;
		for (size_t at = 0; at < used_; at += RecordSize(at))
			n++;
		return n;
	}
	bool At(size_t index, const char*& section, const char*& key, const char*& value) const
	{
		size_t at = 0;
		for (size_t n = 0; at < used_; at += RecordSize(at), n++) {
			if (n != index)
				continue;
			section = data_ + at;
			key = section + Len(section) + 1;
			value = key + Len(key) + 1;
			return true;
		}
		return false;
	}
	bool HasSection(const char* section) const
	{
		for (size_t at = 0; at < used_; at += RecordSize(at))
			if (Eq(data_ + at, section))
				return true;
		return false;
	}
	size_t Used() const { return used_; }
	bool overflow;

private:
	size_t RecordSize(size_t at) const
	{
		size_t n = at;
		for (int part = 0; part < 3; part++)
			n += Len(data_ + n) + 1;
		return n - at;
	}
	bool Find(const char* section, const char* key, size_t& at, size_t& size) const
	{
		for (size_t p = 0; p < used_;) {
			const size_t n = RecordSize(p);
			const char* s = data_ + p;
			if (Eq(s, section) && Eq(s + Len(s) + 1, key)) {
				at = p;
				size = n;
				return true;
			}
			p += n;
		}
		return false;
	}
	void Cut(size_t at, size_t size)
	{
		for (size_t i = at; i + size < used_; i++)
			data_[i] = data_[i + size];
		used_ -= size;
		for (size_t i = used_; i < used_ + size; i++)
			data_[i] = 0;
	}
	char* data_;
	size_t capacity_, used_;
};

// "system.<id>" in the caller's buffer.
inline const char* SystemSection(const char* id, char* buffer, size_t capacity)
{
	Out out(buffer, capacity);
	out.S("system.").S(id);
	return buffer;
}
// An answer of one system: its own section first, then [shared].
inline const char* Resolve(const Store& store, const char* systemId, const char* key, const char* fallback = "")
{
	char section[40];
	const char* own = store.Get(SystemSection(systemId, section, sizeof(section)), key);
	if (own != nullptr)
		return own;
	return store.GetOr("shared", key, fallback);
}

// Values that never go into plan.txt, a saved report or a log.
inline bool SecretKey(const char* section, const char* key)
{
	if (Eq(section, "secrets"))
		return true;
	static const char* const names[] = {"password", "password.windows", "wifi_psk", "admin_pw", "root_pw",
		"encrypt_pw", "passphrase", "product_key"};
	for (const char* name : names)
		if (Eq(key, name))
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Refusals and warnings
// ---------------------------------------------------------------------------
enum class Code : uint8_t {
	none,
	// the list
	tooFew, tooMany, unknownKind, duplicateId, twoCharonDos, sameDistroTwice, sameImageTwice, archImage,
	duplicateHost, hostTooLong, badDefault, badRest, dataNtfsNeedsWindows, sizeBelowMinimum,
	// the machine
	windowsManyDisks, ubuntuRam, startMediumIsTarget, secureBootRefused,
	// the disk and the layout
	notGpt, sectorSize, badSectorSize, tooSmall, espTooSmall, existingEspFull, tableFull, noFreeSpace, badDisk,
	// the file
	iniSyntax, iniOverflow, formatNewer, badValue, unknownKey, noSystems, missingKind,
	noDiskSection, noSerial, diskNoMatch, diskManyMatch, diskNotEmpty, shrinkUnsupported,
	// warnings (asked, not refused)
	warnSecureBoot, warnDosSecureBoot, warnNoNetwork, warnSilent, warnSecondEsp, warnClearPassword,
	warnIgnoredKey,
};
struct Refusal {
	Code code;
	int8_t a, b;      // system indexes, -1 = none
	uint64_t n1, n2;
	char text[48];
};
struct Refusals {
	Refusal list[32];
	uint8_t count = 0;
	bool dropped = false; // more than the list holds
	void Add(Code code, int a = -1, int b = -1, uint64_t n1 = 0, uint64_t n2 = 0, const char* text = nullptr)
	{
		if (count >= 32) {
			dropped = true;
			return;
		}
		Refusal& r = list[count++];
		r.code = code;
		r.a = int8_t(a);
		r.b = int8_t(b);
		r.n1 = n1;
		r.n2 = n2;
		Copy(r.text, sizeof(r.text), text);
	}
	bool Has(Code code) const
	{
		for (uint8_t i = 0; i < count; i++)
			if (list[i].code == code)
				return true;
		return false;
	}
	static bool Warning(Code code) { return code >= Code::warnSecureBoot; }
	// True when something refuses the plan (warnings do not).
	bool Refused() const
	{
		for (uint8_t i = 0; i < count; i++)
			if (!Warning(list[i].code))
				return true;
		return dropped;
	}
};

// ---------------------------------------------------------------------------
// The file format
// ---------------------------------------------------------------------------
// UTF-8, CR LF or LF, "key=value", "#" or ";" comment lines, sections in
// brackets. A ";" after white space ends a value (the comments of the
// documented examples), except in secret values, which are taken as written.
// Section names and keys are stored in lower case (a section name only up to
// its first space: "[machine serial=PF2ABCDE]" keeps the serial as written).
// Keys before the first section land in section "". A later duplicate wins.
// Every faulty line is reported (code iniSyntax, n1 = line number); the rest of
// the file is still read, so that all errors can be spoken at once.
inline bool ParseIni(const char* text, size_t size, Store& store, Refusals& report)
{
	size_t at = 0;
	if (size >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF)
		at = 3;
	char section[96] = "";
	char key[64], value[4096];
	uint64_t line = 0;
	bool good = true;
	while (at < size) {
		size_t end = at;
		while (end < size && text[end] != '\n')
			end++;
		size_t first = at, last = end;
		at = end + 1;
		line++;
		while (first < last && (text[first] == ' ' || text[first] == '\t'))
			first++;
		while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' || text[last - 1] == '\r'))
			last--;
		if (first == last || text[first] == '#' || text[first] == ';')
			continue;
		if (text[first] == '[') {
			size_t close = first + 1;
			while (close < last && text[close] != ']')
				close++;
			if (close == last || close - first - 1 >= sizeof(section) || close == first + 1) {
				report.Add(Code::iniSyntax, -1, -1, line);
				good = false;
				continue;
			}
			size_t n = 0;
			bool word = true;
			for (size_t i = first + 1; i < close; i++) {
				if (text[i] == ' ')
					word = false;
				section[n++] = word ? Lower(text[i]) : text[i];
			}
			while (n > 0 && section[n - 1] == ' ')
				n--;
			section[n] = 0;
			continue;
		}
		size_t equals = first;
		while (equals < last && text[equals] != '=')
			equals++;
		size_t keyEnd = equals;
		while (keyEnd > first && (text[keyEnd - 1] == ' ' || text[keyEnd - 1] == '\t'))
			keyEnd--;
		if (equals == last || keyEnd == first || keyEnd - first >= sizeof(key)) {
			report.Add(Code::iniSyntax, -1, -1, line);
			good = false;
			continue;
		}
		for (size_t i = first; i < keyEnd; i++)
			key[i - first] = Lower(text[i]);
		key[keyEnd - first] = 0;
		size_t valueFirst = equals + 1;
		while (valueFirst < last && (text[valueFirst] == ' ' || text[valueFirst] == '\t'))
			valueFirst++;
		size_t valueLast = last;
		if (!SecretKey(section, key)) {
			for (size_t i = valueFirst; i < last; i++)
				if (text[i] == ';' && (i == valueFirst || text[i - 1] == ' ' || text[i - 1] == '\t')) {
					valueLast = i;
					break;
				}
			while (valueLast > valueFirst && (text[valueLast - 1] == ' ' || text[valueLast - 1] == '\t'))
				valueLast--;
		}
		if (valueLast - valueFirst >= sizeof(value)) {
			report.Add(Code::iniOverflow, -1, -1, line, 0, key);
			good = false;
			continue;
		}
		for (size_t i = valueFirst; i < valueLast; i++)
			value[i - valueFirst] = text[i];
		value[valueLast - valueFirst] = 0;
		if (!store.Put(section, key, value)) {
			report.Add(Code::iniOverflow, -1, -1, line, 0, key);
			good = false;
		}
	}
	for (size_t i = 0; i < sizeof(value); i++)
		value[i] = 0; // a password may have passed through
	return good;
}

// Sections in the order of their first record. With `secrets` false the keys
// of SecretKey() are left out.
inline void WriteIni(const Store& store, Out& out, bool secrets)
{
	const size_t count = store.Count();
	for (size_t i = 0; i < count; i++) {
		const char* section = nullptr;
		const char* key = nullptr;
		const char* value = nullptr;
		store.At(i, section, key, value);
		bool seen = false;
		for (size_t k = 0; k < i && !seen; k++) {
			const char* s2 = nullptr;
			const char* k2 = nullptr;
			const char* v2 = nullptr;
			store.At(k, s2, k2, v2);
			seen = Eq(s2, section);
		}
		if (seen)
			continue;
		if (section[0] != 0)
			out.Ch('[').S(section).S("]\r\n");
		for (size_t k = i; k < count; k++) {
			const char* s2 = nullptr;
			const char* k2 = nullptr;
			const char* v2 = nullptr;
			store.At(k, s2, k2, v2);
			if (!Eq(s2, section) || (!secrets && SecretKey(s2, k2)))
				continue;
			out.S(k2).Ch('=').S(v2).S("\r\n");
		}
	}
}

// ---------------------------------------------------------------------------
// Systems and the plan
// ---------------------------------------------------------------------------
enum class Kind : uint8_t {
	none, charondos, windows, debian, ubuntu, imageDebian, imageUbuntu, imageFedora, imageArch,
	hypervisor, // r410 (agent 2's Charon Hypervisor): a partition of its own, written by Charon (CharonWsHypervisor.h)
};
inline const char* KindWord(Kind kind)
{
	switch (kind) {
	case Kind::charondos: return "charondos";
	case Kind::windows: return "windows";
	case Kind::debian: return "debian";
	case Kind::ubuntu: return "ubuntu";
	case Kind::imageDebian: return "image-debian";
	case Kind::imageUbuntu: return "image-ubuntu";
	case Kind::imageFedora: return "image-fedora";
	case Kind::imageArch: return "image-arch";
	case Kind::hypervisor: return "hypervisor";
	default: return "";
	}
}
// Guided installs, ISO-to-stick entries, netboot.xyz and seedless images have
// no kind word: they cannot be in a plan (spec 2.1).
inline Kind KindFromWord(const char* word)
{
	for (uint8_t k = 1; k <= uint8_t(Kind::hypervisor); k++)
		if (EqNoCase(word, KindWord(Kind(k))))
			return Kind(k);
	return Kind::none;
}
inline bool IsWindows(Kind kind) { return kind == Kind::windows; }
inline bool IsImage(Kind kind) { return kind >= Kind::imageDebian && kind <= Kind::imageArch; }
inline bool IsInstallerLinux(Kind kind) { return kind == Kind::debian || kind == Kind::ubuntu; }
inline bool IsLinux(Kind kind) { return IsImage(kind) || IsInstallerLinux(kind); }
inline bool IsHypervisor(Kind kind) { return kind == Kind::hypervisor; }
// The distribution, for the "same loader directory" clash of spec 3.5.
inline const char* Distribution(Kind kind)
{
	switch (kind) {
	case Kind::debian: case Kind::imageDebian: return "debian";
	case Kind::ubuntu: case Kind::imageUbuntu: return "ubuntu";
	case Kind::imageFedora: return "fedora";
	case Kind::imageArch: return "arch";
	default: return "";
	}
}
// Who takes "the biggest free space" (its placeholder is deleted before its
// step) and who is given an existing partition (its placeholder is retyped).
// r410 (helper bd): Ubuntu's installer too - autoinstall storage layout mode use_gap installs into the largest gap
// (subiquity server/controllers/storage.py: gaps.largest_gap; an existing ESP on the disk is reused, boot.py).
inline bool TakesGap(Kind kind) { return IsInstallerLinux(kind) || IsImage(kind); }
// Registry and CharonDOS kind word.
inline const char* LoaderKind(Kind kind)
{
	return kind == Kind::charondos ? "charondos" : IsWindows(kind) ? "windows" : kind == Kind::hypervisor ? "hypervisor" : "linux";
}

struct System {
	char id[24];
	Kind kind;
	char name[48];        // display name: "Windows 11", "Debian"
	char version[16];     // Windows: 11, 10, ent, ltsc, iot, ltsc10, server
	uint8_t disk;         // 1-based number of the [disk.N] rule / target disk
	uint32_t sizeGb;      // 0 = computed
	uint32_t swapGb;
	bool winre;           // Windows: a separate 1 GiB recovery partition
	uint32_t imageMb;     // ready image: size of the image file
	char image[64];       // ready image: identity of the image file
	uint32_t needMb;      // CharonDOS: its own need (0 = 512)
	char host[40];
	bool speaks;          // false: the "does not speak" confirmation applies
};
// Older Windows first (spec 3.1).
inline uint8_t WindowsRank(const System& system)
{
	if (Eq(system.version, "10") || Eq(system.version, "ltsc10"))
		return 10;
	if (Eq(system.version, "server"))
		return 12;
	return 11;
}
// Minimum and default size in MiB (spec 2.4). CharonDOS: its own need only;
// the layout adds the EFI share when its partition is the EFI partition.
inline void SizeLimits(const System& system, uint64_t& minimumMb, uint64_t& defaultMb)
{
	const uint64_t gb = 1024;
	switch (system.kind) {
	case Kind::windows:
		if (system.version[0] == 0 || Eq(system.version, "11") || Eq(system.version, "ent")) {
			minimumMb = 64 * gb;
			defaultMb = 100 * gb;
		} else {
			minimumMb = 40 * gb;
			defaultMb = 80 * gb;
		}
		break;
	case Kind::debian: minimumMb = 12 * gb; defaultMb = 40 * gb; break;
	case Kind::ubuntu: minimumMb = 25 * gb; defaultMb = 40 * gb; break;
	case Kind::hypervisor: minimumMb = 10 * gb; defaultMb = 66 * gb; break; // r410: agent 2's design: boot 2 GiB + data (8 GiB at least, 64 GiB by default)
	case Kind::charondos:
		minimumMb = system.needMb != 0 ? system.needMb : 512;
		if (minimumMb < 512)
			minimumMb = 512;
		defaultMb = minimumMb;
		break;
	default:
		if (IsImage(system.kind)) {
			minimumMb = uint64_t(system.imageMb) + gb;
			defaultMb = 30 * gb;
			if (defaultMb < minimumMb)
				defaultMb = minimumMb;
		} else {
			minimumMb = defaultMb = 0;
		}
	}
}

enum class OnFailure : uint8_t { stop, skip, retryOnceThenStop };
enum class Leftover : uint8_t { keep, free };
enum class DataFs : uint8_t { none, exfat, ntfs };
enum class Existing : uint8_t { erase, keep, refuseIfNotEmpty };

struct DiskRule {
	bool present;
	char serial[40];
	char model[48];       // "*" allowed at the end
	uint32_t minGb, maxGb;
	Existing existing;
	bool existingGiven;
	bool shrinkWindows;
	uint32_t espSizeMb;   // 0 = 1024
};

struct Plan {
	uint32_t id;          // random, names the plan in the registry and the variable
	char name[40];
	System systems[kMaxSystems];
	uint8_t count;
	char defaultId[24];   // a system id, "menu", or "" = first of the list
	uint32_t timeout;     // seconds
	OnFailure onFailure;
	Leftover leftover;
	DataFs data;          // none unless asked (decision D7)
	uint32_t dataSizeGb;  // 0 = the rest
	char rest[24];        // "data", a system id, "free", or "" = default
	bool unattended;
	bool secureBootRefuse; // secure_boot=refuse-if-on
	DiskRule disks[kMaxDiskRules]; // index 0 = [disk.1]
	char bootOrder[kMaxSystems * 24 + 8]; // r410: system ids, comma separated: the boot entries after Charon; "" = not chosen
};

inline void Clear(Plan& plan)
{
	uint8_t* p = reinterpret_cast<uint8_t*>(&plan);
	for (size_t i = 0; i < sizeof(Plan); i++)
		p[i] = 0;
	plan.timeout = 5;
}
inline int FindSystem(const Plan& plan, const char* id)
{
	for (int i = 0; i < plan.count; i++)
		if (EqNoCase(plan.systems[i].id, id))
			return i;
	return -1;
}
inline bool HasKind(const Plan& plan, Kind kind)
{
	for (int i = 0; i < plan.count; i++)
		if (plan.systems[i].kind == kind)
			return true;
	return false;
}
inline int CountWindows(const Plan& plan)
{
	int n = 0;
	for (int i = 0; i < plan.count; i++)
		n += IsWindows(plan.systems[i].kind) ? 1 : 0;
	return n;
}
inline bool HasCharonDos(const Plan& plan) { return HasKind(plan, Kind::charondos); }
// Spec 2.7: the hardware clock is kept in universal time whenever the plan
// holds a non-Windows system or two Windows.
inline bool ClockUtc(const Plan& plan) { return plan.count > CountWindows(plan) || CountWindows(plan) >= 2; }

inline const char* DefaultName(const System& system)
{
	switch (system.kind) {
	case Kind::charondos: return "CharonDOS";
	case Kind::windows:
		if (Eq(system.version, "10")) return "Windows 10";
		if (Eq(system.version, "ltsc10")) return "Windows 10 LTSC";
		if (Eq(system.version, "ltsc")) return "Windows 11 LTSC";
		if (Eq(system.version, "iot")) return "Windows 11 IoT LTSC";
		if (Eq(system.version, "ent")) return "Windows 11 Enterprise";
		if (Eq(system.version, "server")) return "Windows Server 2025";
		return "Windows 11";
	case Kind::debian: return "Debian";
	case Kind::ubuntu: return "Ubuntu";
	case Kind::imageDebian: return "Debian (image)";
	case Kind::imageUbuntu: return "Ubuntu (image)";
	case Kind::imageFedora: return "Fedora (image)";
	case Kind::imageArch: return "Arch Linux (image)";
	case Kind::hypervisor: return "Charon Hypervisor";
	default: return "";
	}
}

// ---------------------------------------------------------------------------
// Plan <-> store
// ---------------------------------------------------------------------------
namespace detail {
inline bool KnownKey(const char* key, const char* const* known, size_t count)
{
	for (size_t i = 0; i < count; i++)
		if (Eq(key, known[i]))
			return true;
	return false;
}
// Reports the keys of a section this version does not know (ignored, never fatal).
inline void UnknownKeys(const Store& store, const char* section, const char* const* known, size_t count,
	Refusals& report)
{
	const size_t n = store.Count();
	for (size_t i = 0; i < n; i++) {
		const char* s = nullptr;
		const char* k = nullptr;
		const char* v = nullptr;
		store.At(i, s, k, v);
		if (Eq(s, section) && !KnownKey(k, known, count))
			report.Add(Code::warnIgnoredKey, -1, -1, 0, 0, k);
	}
}
inline bool Number(const Store& store, const char* section, const char* key, uint32_t& value, uint64_t low,
	uint64_t high, Refusals& report)
{
	const char* text = store.Get(section, key);
	if (text == nullptr || text[0] == 0)
		return true;
	uint64_t v = 0;
	if (!ParseNumber(text, v) || v < low || v > high) {
		report.Add(Code::badValue, -1, -1, 0, 0, key);
		return false;
	}
	value = uint32_t(v);
	return true;
}
inline bool YesNo(const Store& store, const char* section, const char* key, bool& value, Refusals& report)
{
	const char* text = store.Get(section, key);
	if (text == nullptr || text[0] == 0)
		return true;
	if (IsYes(text))
		value = true;
	else if (IsNo(text))
		value = false;
	else {
		report.Add(Code::badValue, -1, -1, 0, 0, key);
		return false;
	}
	return true;
}
} // namespace detail

// Builds the plan from [plan], [disk.N], [shared] and [system.<id>]. Every
// error is collected (the whole file is validated before anything changes);
// the result is usable only when report.Refused() is false afterwards.
// [charon-answers] format newer than 1 is refused, not guessed.
inline void PlanFromStore(const Store& store, Plan& plan, Refusals& report)
{
	using namespace detail;
	const uint32_t keptId = plan.id;
	Clear(plan);
	plan.id = keptId;
	uint32_t number = 1;
	if (Number(store, "charon-answers", "format", number, 0, 1000000, report) && number > 1)
		report.Add(Code::formatNewer, -1, -1, number);
	number = 1;
	// r410: version 2 = the plan carries boot_order (PlanToStore). An older Charon refuses 2 instead of ignoring the order.
	if (Number(store, "plan", "version", number, 0, 1000000, report) && number > 2)
		report.Add(Code::formatNewer, -1, -1, number);
	static const char* const planKeys[] = {"version", "name", "systems", "default", "timeout", "on_failure",
		"leftover", "data", "data_size_gb", "rest", "report", "secure_boot", "unattended", "confirm", "mode",
		"autostart", "id", "dosdefault_winnt", "dosdefault_linux", "boot_order"};
	UnknownKeys(store, "plan", planKeys, sizeof(planKeys) / sizeof(planKeys[0]), report);
	Copy(plan.name, sizeof(plan.name), store.GetOr("plan", "name", ""));
	if (!Copy(plan.defaultId, sizeof(plan.defaultId), store.GetOr("plan", "default", "")))
		report.Add(Code::badDefault);
	Number(store, "plan", "timeout", plan.timeout, 0, 60, report);
	Number(store, "plan", "data_size_gb", plan.dataSizeGb, 0, 1000000, report);
	if (!Copy(plan.rest, sizeof(plan.rest), store.GetOr("plan", "rest", "")))
		report.Add(Code::badRest);
	YesNo(store, "plan", "unattended", plan.unattended, report);
	const char* text = store.GetOr("plan", "on_failure", "stop");
	if (Eq(text, "stop"))
		plan.onFailure = OnFailure::stop;
	else if (Eq(text, "skip"))
		plan.onFailure = OnFailure::skip;
	else if (Eq(text, "retry-once-then-stop"))
		plan.onFailure = OnFailure::retryOnceThenStop;
	else
		report.Add(Code::badValue, -1, -1, 0, 0, "on_failure");
	text = store.GetOr("plan", "leftover", "keep");
	if (Eq(text, "keep"))
		plan.leftover = Leftover::keep;
	else if (Eq(text, "free"))
		plan.leftover = Leftover::free;
	else
		report.Add(Code::badValue, -1, -1, 0, 0, "leftover");
	text = store.GetOr("plan", "data", "none");
	if (Eq(text, "none") || Eq(text, "no"))
		plan.data = DataFs::none;
	else if (Eq(text, "exfat") || Eq(text, "yes"))
		plan.data = DataFs::exfat;
	else if (Eq(text, "ntfs"))
		plan.data = DataFs::ntfs;
	else
		report.Add(Code::badValue, -1, -1, 0, 0, "data");
	// There is no value that bypasses or switches off Secure Boot.
	text = store.GetOr("plan", "secure_boot", "warn");
	if (Eq(text, "refuse-if-on"))
		plan.secureBootRefuse = true;
	else if (!Eq(text, "warn"))
		report.Add(Code::badValue, -1, -1, 0, 0, "secure_boot");
	// User decision 2026-11-23: the typed word is ALWAYS required, with an
	// answer-set file too. There is no value that replaces it; the serial of
	// [disk.N] must match in addition (AuthoriseDisks).
	text = store.GetOr("plan", "confirm", "typed");
	if (!Eq(text, "typed"))
		report.Add(Code::badValue, -1, -1, 0, 0, "confirm");

	static const char* const diskKeys[] = {"serial", "model", "min_size_gb", "max_size_gb", "existing",
		"shrink_windows", "esp_size_mb"};
	for (int d = 0; d < kMaxDiskRules; d++) {
		char section[16];
		Out name(section, sizeof(section));
		name.S("disk.").N(uint64_t(d) + 1);
		DiskRule& rule = plan.disks[d];
		if (!store.HasSection(section))
			continue;
		rule.present = true;
		UnknownKeys(store, section, diskKeys, sizeof(diskKeys) / sizeof(diskKeys[0]), report);
		if (!Copy(rule.serial, sizeof(rule.serial), store.GetOr(section, "serial", ""))
			|| !Copy(rule.model, sizeof(rule.model), store.GetOr(section, "model", "")))
			report.Add(Code::badValue, -1, -1, 0, 0, "serial");
		Number(store, section, "min_size_gb", rule.minGb, 0, 100000000, report);
		Number(store, section, "max_size_gb", rule.maxGb, 0, 100000000, report);
		Number(store, section, "esp_size_mb", rule.espSizeMb, 0, 65536, report);
		if (rule.espSizeMb != 0 && rule.espSizeMb < 550)
			report.Add(Code::espTooSmall, -1, -1, rule.espSizeMb);
		text = store.Get(section, "existing");
		rule.existingGiven = text != nullptr;
		if (text == nullptr || Eq(text, "keep"))
			rule.existing = Existing::keep; // never "erase" by default
		else if (Eq(text, "erase"))
			rule.existing = Existing::erase;
		else if (Eq(text, "refuse-if-not-empty"))
			rule.existing = Existing::refuseIfNotEmpty;
		else
			report.Add(Code::badValue, -1, -1, 0, 0, "existing");
		YesNo(store, section, "shrink_windows", rule.shrinkWindows, report);
		if (rule.shrinkWindows)
			report.Add(Code::shrinkUnsupported);
	}

	// systems=dos,win11,debian
	const char* list = store.Get("plan", "systems");
	if (list == nullptr || list[0] == 0) {
		report.Add(Code::noSystems);
		return;
	}
	for (size_t at = 0; list[at] != 0;) {
		char id[24];
		size_t n = 0;
		bool cut = false;
		while (list[at] != 0 && list[at] != ',') {
			if (list[at] != ' ' && list[at] != '\t') {
				if (n + 1 < sizeof(id))
					id[n++] = Lower(list[at]);
				else
					cut = true;
			}
			at++;
		}
		id[n] = 0;
		if (list[at] == ',')
			at++;
		if (n == 0)
			continue;
		if (cut) {
			report.Add(Code::badValue, -1, -1, 0, 0, "systems");
			continue;
		}
		if (plan.count >= kMaxSystems) {
			report.Add(Code::tooMany);
			break;
		}
		if (FindSystem(plan, id) >= 0) {
			report.Add(Code::duplicateId, -1, -1, 0, 0, id);
			continue;
		}
		const int index = plan.count++;
		System& system = plan.systems[index];
		Copy(system.id, sizeof(system.id), id);
		char section[40];
		SystemSection(id, section, sizeof(section));
		const char* kind = store.Get(section, "kind");
		if (kind == nullptr)
			report.Add(Code::missingKind, index, -1, 0, 0, id);
		else {
			system.kind = KindFromWord(kind);
			if (system.kind == Kind::none)
				report.Add(Code::unknownKind, index, -1, 0, 0, kind);
		}
		Copy(system.version, sizeof(system.version), store.GetOr(section, "version", ""));
		uint32_t disk = 1;
		Number(store, section, "disk", disk, 1, kMaxDiskRules, report);
		system.disk = uint8_t(disk);
		Number(store, section, "size_gb", system.sizeGb, 0, 1000000, report);
		Number(store, section, "swap_gb", system.swapGb, 0, 1024, report);
		YesNo(store, section, "winre", system.winre, report);
		Number(store, section, "image_mb", system.imageMb, 0, 100000000, report);
		Copy(system.image, sizeof(system.image), store.GetOr(section, "image", ""));
		Number(store, section, "need_mb", system.needMb, 0, 1000000, report);
		system.speaks = true;
		YesNo(store, section, "speaks", system.speaks, report);
		Copy(system.name, sizeof(system.name), store.GetOr(section, "name", DefaultName(system)));
		// Computer name: the system's own, else the plan's shared name. User decision 2026-11-24: ONE identical
		// name on all systems of a plan (before r410: the shared name with the system's id as its ending).
		const char* own = store.Get(section, "hostname");
		Out host(system.host, sizeof(system.host));
		if (own != nullptr && own[0] != 0)
			host.S(own);
		else
			host.S(store.GetOr("shared", "hostpattern", "charon-pc"));
		if (host.overflow)
			report.Add(Code::hostTooLong, index);
	}
	// r410: boot_order=debian,win - every id a system of this plan, none twice. Kept as written (lower case, no blanks).
	const char* wanted = store.GetOr("plan", "boot_order", "");
	Out order(plan.bootOrder, sizeof(plan.bootOrder));
	bool bad = false;
	uint8_t seen = 0;
	for (size_t at = 0; wanted[at] != 0;) {
		char id[24];
		size_t n = 0;
		while (wanted[at] != 0 && wanted[at] != ',') {
			if (wanted[at] != ' ' && wanted[at] != '\t') {
				if (n + 1 < sizeof(id))
					id[n++] = Lower(wanted[at]);
				else
					bad = true;
			}
			at++;
		}
		id[n] = 0;
		if (wanted[at] == ',')
			at++;
		if (n == 0)
			continue;
		const int index = FindSystem(plan, id);
		if (index < 0 || (seen & (1u << index)) != 0) {
			bad = true;
			continue;
		}
		seen = uint8_t(seen | (1u << index));
		if (order.length != 0)
			order.Ch(',');
		order.S(id);
	}
	if (bad || order.overflow) {
		plan.bootOrder[0] = 0;
		report.Add(Code::badValue, -1, -1, 0, 0, "boot_order");
	}
}

// Writes the plan's own keys into the store (other keys of those sections,
// the answers, stay as they are).
inline bool PlanToStore(const Plan& plan, Store& store)
{
	bool ok = store.PutNumber("plan", "version", plan.bootOrder[0] != 0 ? 2 : 1);   // r410: see PlanFromStore
	if (plan.bootOrder[0] != 0)
		ok = store.Put("plan", "boot_order", plan.bootOrder) && ok;
	else
		store.Remove("plan", "boot_order");
	if (plan.id != 0) {
		char id[12];
		Out out(id, sizeof(id));
		out.Hex(plan.id, 8);
		ok = store.Put("plan", "id", id) && ok;
	}
	ok = store.Put("plan", "name", plan.name) && ok;
	char list[kMaxSystems * 24 + 8];
	Out systems(list, sizeof(list));
	for (int i = 0; i < plan.count; i++) {
		if (i != 0)
			systems.Ch(',');
		systems.S(plan.systems[i].id);
	}
	ok = store.Put("plan", "systems", list) && ok;
	ok = store.Put("plan", "default", plan.defaultId) && ok;
	ok = store.PutNumber("plan", "timeout", plan.timeout) && ok;
	ok = store.Put("plan", "on_failure", plan.onFailure == OnFailure::stop ? "stop"
		: plan.onFailure == OnFailure::skip ? "skip" : "retry-once-then-stop") && ok;
	ok = store.Put("plan", "leftover", plan.leftover == Leftover::keep ? "keep" : "free") && ok;
	ok = store.Put("plan", "data", plan.data == DataFs::none ? "none" : plan.data == DataFs::exfat ? "exfat" : "ntfs") && ok;
	ok = store.PutNumber("plan", "data_size_gb", plan.dataSizeGb) && ok;
	ok = store.Put("plan", "rest", plan.rest) && ok;
	ok = store.Put("plan", "secure_boot", plan.secureBootRefuse ? "refuse-if-on" : "warn") && ok;
	ok = store.Put("plan", "unattended", plan.unattended ? "yes" : "no") && ok;
	ok = store.Put("plan", "confirm", "typed") && ok;
	for (int d = 0; d < kMaxDiskRules; d++) {
		const DiskRule& rule = plan.disks[d];
		if (!rule.present)
			continue;
		char section[16];
		Out name(section, sizeof(section));
		name.S("disk.").N(uint64_t(d) + 1);
		ok = store.Put(section, "serial", rule.serial) && ok;
		if (rule.model[0] != 0)
			ok = store.Put(section, "model", rule.model) && ok;
		if (rule.minGb != 0)
			ok = store.PutNumber(section, "min_size_gb", rule.minGb) && ok;
		if (rule.maxGb != 0)
			ok = store.PutNumber(section, "max_size_gb", rule.maxGb) && ok;
		ok = store.Put(section, "existing", rule.existing == Existing::erase ? "erase"
			: rule.existing == Existing::keep ? "keep" : "refuse-if-not-empty") && ok;
		if (rule.espSizeMb != 0)
			ok = store.PutNumber(section, "esp_size_mb", rule.espSizeMb) && ok;
	}
	for (int i = 0; i < plan.count; i++) {
		const System& system = plan.systems[i];
		char section[40];
		SystemSection(system.id, section, sizeof(section));
		ok = store.Put(section, "kind", KindWord(system.kind)) && ok;
		ok = store.Put(section, "name", system.name) && ok;
		if (system.version[0] != 0)
			ok = store.Put(section, "version", system.version) && ok;
		ok = store.PutNumber(section, "disk", system.disk != 0 ? system.disk : 1) && ok;
		ok = store.PutNumber(section, "size_gb", system.sizeGb) && ok;
		if (IsLinux(system.kind))
			ok = store.PutNumber(section, "swap_gb", system.swapGb) && ok;
		if (IsWindows(system.kind))
			ok = store.Put(section, "winre", system.winre ? "yes" : "no") && ok;
		if (IsImage(system.kind)) {
			ok = store.PutNumber(section, "image_mb", system.imageMb) && ok;
			ok = store.Put(section, "image", system.image) && ok;
		}
		if (system.kind == Kind::charondos && system.needMb != 0)
			ok = store.PutNumber(section, "need_mb", system.needMb) && ok;
		ok = store.Put(section, "hostname", system.host) && ok;
		ok = store.Put(section, "speaks", system.speaks ? "yes" : "no") && ok;
	}
	return ok;
}

// ---------------------------------------------------------------------------
// Order of steps (spec 3.1)
// ---------------------------------------------------------------------------
enum class StepKind : uint8_t { prepare, writeDos, writeImage, install, firstStart, finish };
struct Step {
	StepKind kind;
	int8_t system; // -1 for prepare and finish
};
struct Order {
	Step steps[kMaxSteps];
	uint8_t count;
};
// 1 prepare; 2 CharonDOS; 3 ready images written, list order; 4 Windows, older
// version first, list order within a version; 5 installer-based Linux, list
// order; 6 first start of each ready image, list order; 7 finish. The list
// order decides only inside a class.
inline void ComputeOrder(const Plan& plan, Order& order)
{
	order.count = 0;
	auto add = [&order](StepKind kind, int system) {
		if (order.count < kMaxSteps) {
			order.steps[order.count].kind = kind;
			order.steps[order.count].system = int8_t(system);
			order.count++;
		}
	};
	add(StepKind::prepare, -1);
	for (int i = 0; i < plan.count; i++)
		if (plan.systems[i].kind == Kind::charondos || plan.systems[i].kind == Kind::hypervisor)
			add(StepKind::writeDos, i); // r410: the hypervisor's partition is written by Charon too
	for (int i = 0; i < plan.count; i++)
		if (IsImage(plan.systems[i].kind))
			add(StepKind::writeImage, i);
	for (int rank = 10; rank <= 12; rank++)
		for (int i = 0; i < plan.count; i++)
			if (IsWindows(plan.systems[i].kind) && WindowsRank(plan.systems[i]) == rank)
				add(StepKind::install, i);
	for (int i = 0; i < plan.count; i++)
		if (IsInstallerLinux(plan.systems[i].kind))
			add(StepKind::install, i);
	for (int i = 0; i < plan.count; i++)
		if (IsImage(plan.systems[i].kind))
			add(StepKind::firstStart, i);
	add(StepKind::finish, -1);
}

// What starts when the user does nothing (spec 3.2, 4.1, 5; decision D1).
struct StartRule {
	bool dosFirst;    // CharonDOS is the machine's default; WINNT and LINUX start the others
	bool menuWaits;   // Charon's menu waits for a key
	int8_t system;    // Charon's "Continue" target, -1 = none
	uint32_t timeout;
};
inline StartRule StartOf(const Plan& plan)
{
	StartRule rule = {false, false, -1, plan.timeout};
	for (int i = 0; i < plan.count; i++)
		if (plan.systems[i].kind == Kind::charondos) {
			rule.dosFirst = true;
			rule.system = int8_t(i);
		}
	if (EqNoCase(plan.defaultId, "menu")) {
		rule.menuWaits = !rule.dosFirst;
		if (!rule.dosFirst)
			rule.system = -1;
		return rule;
	}
	if (!rule.dosFirst) {
		const int chosen = plan.defaultId[0] != 0 ? FindSystem(plan, plan.defaultId) : 0;
		rule.system = int8_t(chosen >= 0 && chosen < plan.count ? chosen : -1);
	}
	return rule;
}

// r410 (user answer 2): the order of the boot entries after Charon's own.
// DefaultBootSequence: what the question offers first - the system that starts by itself, then the others in
// list order. BootSequence: the order the finish step applies - the chosen one ([plan] boot_order; systems it
// does not name follow in list order), or, when none was chosen, the list order (nothing is rearranged then).
// A plan with CharonDOS keeps its own rule (CharonDOS first, WINNT and LINUX): boot_order is not applied to it.
inline bool BootOrderChosen(const Plan& plan) { return plan.bootOrder[0] != 0 && !HasCharonDos(plan); }
inline int DefaultBootSequence(const Plan& plan, int out[kMaxSystems])
{
	int n = 0;
	const StartRule start = StartOf(plan);
	if (start.system >= 0 && start.system < plan.count)
		out[n++] = start.system;
	for (int i = 0; i < plan.count; i++)
		if (n == 0 || out[0] != i)
			out[n++] = i;
	return n;
}
inline int BootSequence(const Plan& plan, int out[kMaxSystems])
{
	int n = 0;
	auto add = [&n, out](int index) {
		for (int k = 0; k < n; k++)
			if (out[k] == index)
				return;
		if (n < kMaxSystems)
			out[n++] = index;
	};
	if (BootOrderChosen(plan))
		for (size_t at = 0; plan.bootOrder[at] != 0;) {
			char id[24];
			size_t k = 0;
			while (plan.bootOrder[at] != 0 && plan.bootOrder[at] != ',') {
				if (k + 1 < sizeof(id))
					id[k++] = plan.bootOrder[at];
				at++;
			}
			id[k] = 0;
			if (plan.bootOrder[at] == ',')
				at++;
			const int index = FindSystem(plan, id);
			if (index >= 0)
				add(index);
		}
	for (int i = 0; i < plan.count; i++)
		add(i);
	return n;
}
// Writes the ids of `sequence` into plan.bootOrder. False: it does not fit (nothing changed).
inline bool SetBootOrder(Plan& plan, const int* sequence, int count)
{
	char text[sizeof(plan.bootOrder)];
	Out out(text, sizeof(text));
	for (int i = 0; i < count; i++) {
		if (sequence[i] < 0 || sequence[i] >= plan.count)
			return false;
		if (i != 0)
			out.Ch(',');
		out.S(plan.systems[sequence[i]].id);
	}
	if (out.overflow)
		return false;
	Copy(plan.bootOrder, sizeof(plan.bootOrder), text);
	return true;
}

// ---------------------------------------------------------------------------
// Pre-flight that needs no layout (spec 2.5, 3.5)
// ---------------------------------------------------------------------------
struct Machine {
	uint64_t ramMb;
	uint8_t internalDisks;     // disks an installer could mistake for the target
	bool secureBoot;
	bool network;
	bool runningFromRam;       // Charon runs from its RAM volume
	bool startMediumIsTarget;  // the disk Charon was started from is a target disk
	bool driverOptions;        // firmware loads Driver#### (the reader arrives without Charon first)
	bool waitNotStored;        // r410: the finish step could not store the plan's wait in Charon's settings: the usual 5 s are in force
};

inline void ValidatePlan(const Plan& plan, const Machine& machine, Refusals& report)
{
	// r410: the Charon Hypervisor alone is a one-system plan (its single installation, agent 2's design)
	if (plan.count < 2 && !(plan.count == 1 && plan.systems[0].kind == Kind::hypervisor))
		report.Add(Code::tooFew, -1, -1, plan.count);
	if (plan.count > kMaxSystems)
		report.Add(Code::tooMany);
	int dos = -1;
	bool windows = false, ubuntu = false;
	for (int i = 0; i < plan.count; i++) {
		const System& s = plan.systems[i];
		if (s.kind == Kind::charondos) {
			if (dos >= 0)
				report.Add(Code::twoCharonDos, dos, i);
			dos = i;
		}
		windows = windows || IsWindows(s.kind);
		ubuntu = ubuntu || s.kind == Kind::ubuntu;
		// The Arch image carries a partition number in its GRUB core image:
		// whole disk only (r402), so never in a plan.
		if (s.kind == Kind::imageArch)
			report.Add(Code::archImage, i);
		uint64_t minimum = 0, standard = 0;
		SizeLimits(s, minimum, standard);
		if (s.sizeGb != 0 && s.kind != Kind::charondos && uint64_t(s.sizeGb) * 1024 < minimum)
			report.Add(Code::sizeBelowMinimum, i, -1, (minimum + 1023) / 1024, s.sizeGb);
		if ((IsWindows(s.kind) || s.kind == Kind::charondos) && Len(s.host) > 15)
			report.Add(Code::hostTooLong, i);
		for (int k = 0; k < i; k++) {
			const System& o = plan.systems[k];
			if (EqNoCase(o.id, s.id))
				report.Add(Code::duplicateId, k, i, 0, 0, s.id);
			if (IsInstallerLinux(s.kind) && IsInstallerLinux(o.kind) && Eq(Distribution(s.kind), Distribution(o.kind)))
				report.Add(Code::sameDistroTwice, k, i);
			if (IsImage(s.kind) && IsImage(o.kind)
				&& (s.kind == o.kind && (s.image[0] == 0 || o.image[0] == 0 || EqNoCase(s.image, o.image))))
				report.Add(Code::sameImageTwice, k, i);
		}
	}
	if (plan.defaultId[0] != 0 && !EqNoCase(plan.defaultId, "menu") && FindSystem(plan, plan.defaultId) < 0)
		report.Add(Code::badDefault, -1, -1, 0, 0, plan.defaultId);
	if (plan.rest[0] != 0 && !Eq(plan.rest, "free")) {
		if (Eq(plan.rest, "data")) {
			if (plan.data == DataFs::none)
				report.Add(Code::badRest, -1, -1, 0, 0, plan.rest);
		} else {
			const int target = FindSystem(plan, plan.rest);
			if (target < 0 || plan.systems[target].kind == Kind::charondos)
				report.Add(Code::badRest, -1, -1, 0, 0, plan.rest);
		}
	}
	if (plan.data == DataFs::ntfs && !windows)
		report.Add(Code::dataNtfsNeedsWindows);
	if (windows && machine.internalDisks > 1)
		report.Add(Code::windowsManyDisks, -1, -1, machine.internalDisks);
	if (ubuntu && machine.ramMb < 8 * 1024 - 256)
		report.Add(Code::ubuntuRam, -1, -1, machine.ramMb);
	if (machine.startMediumIsTarget && !machine.runningFromRam)
		report.Add(Code::startMediumIsTarget);
	if (machine.secureBoot) {
		if (plan.secureBootRefuse)
			report.Add(Code::secureBootRefused);
		else {
			report.Add(Code::warnSecureBoot);
			if (dos >= 0)
				report.Add(Code::warnDosSecureBoot);
		}
	}
	if (!machine.network)
		report.Add(Code::warnNoNetwork);
	for (int i = 0; i < plan.count; i++)
		if (!plan.systems[i].speaks)
			report.Add(Code::warnSilent, i);
	// The Windows answer file holds the password in clear text on the EFI
	// partition until its step is done (decision D4): said once.
	if (windows)
		report.Add(Code::warnClearPassword);
}

// ---------------------------------------------------------------------------
// Disks, and the pre-authorisation of an unattended plan
// ---------------------------------------------------------------------------
enum class PartType : uint8_t { other, efiSystem, microsoftReserved, basicData, linuxData, windowsRecovery, placeholder };
struct DiskPart {
	uint64_t firstLba, lastLba;
	PartType type;
};
struct DiskState {
	uint32_t sectorSize;
	uint64_t sectors;
	bool gpt;                 // a valid GPT is on the disk
	bool blank;               // nothing to lose
	uint64_t firstUsable, lastUsable; // of the existing GPT
	DiskPart parts[64];
	uint8_t count;
	uint32_t entriesFree;     // free entries of the existing GPT
	bool espFreeKnown;
	uint64_t espFreeMb;       // free space on the existing EFI system partition
	char serial[40];
	char model[48];
};

namespace detail {
inline void Trimmed(const char* from, char* to, size_t capacity)
{
	while (*from == ' ' || *from == '\t')
		from++;
	Copy(to, capacity, from);
	size_t n = Len(to);
	while (n > 0 && (to[n - 1] == ' ' || to[n - 1] == '\t'))
		to[--n] = 0;
}
inline bool ModelMatches(const char* pattern, const char* model)
{
	char p[48], m[48];
	Trimmed(pattern, p, sizeof(p));
	Trimmed(model, m, sizeof(m));
	const size_t n = Len(p);
	if (n > 0 && p[n - 1] == '*') {
		p[n - 1] = 0;
		return StartsNoCase(m, p);
	}
	return EqNoCase(p, m);
}
} // namespace detail

inline bool RuleMatches(const DiskRule& rule, const DiskState& disk)
{
	char wanted[40], have[40];
	detail::Trimmed(rule.serial, wanted, sizeof(wanted));
	detail::Trimmed(disk.serial, have, sizeof(have));
	if (wanted[0] == 0 || have[0] == 0 || !EqNoCase(wanted, have))
		return false;
	if (rule.model[0] != 0 && !detail::ModelMatches(rule.model, disk.model))
		return false;
	const uint64_t gb = disk.sectors / (uint64_t(1024) * 1024 * 1024 / disk.sectorSize);
	if (rule.minGb != 0 && gb < rule.minGb)
		return false;
	if (rule.maxGb != 0 && gb > rule.maxGb)
		return false;
	return true;
}

// Which connected disk rule [disk.<number>] authorises. A serial is the only
// accepted authorisation: no serial, no disk with that serial, or two disks
// that match all refuse, and nothing may be touched. Also applies
// existing=refuse-if-not-empty. `index` is written only on success.
inline bool MatchDisk(const Plan& plan, int number, const DiskState* disks, size_t diskCount, int& index,
	Refusals& report)
{
	if (number < 1 || number > kMaxDiskRules || !plan.disks[number - 1].present) {
		report.Add(Code::noDiskSection, -1, -1, uint64_t(number));
		return false;
	}
	const DiskRule& rule = plan.disks[number - 1];
	char serial[40];
	detail::Trimmed(rule.serial, serial, sizeof(serial));
	if (serial[0] == 0) {
		report.Add(Code::noSerial, -1, -1, uint64_t(number));
		return false;
	}
	int found = -1, matches = 0;
	for (size_t i = 0; i < diskCount; i++)
		if (RuleMatches(rule, disks[i])) {
			found = int(i);
			matches++;
		}
	if (matches == 0) {
		report.Add(Code::diskNoMatch, -1, -1, uint64_t(number), 0, serial);
		return false;
	}
	if (matches > 1) {
		report.Add(Code::diskManyMatch, -1, -1, uint64_t(number), uint64_t(matches), serial);
		return false;
	}
	if (rule.existing == Existing::refuseIfNotEmpty && !disks[found].blank) {
		report.Add(Code::diskNotEmpty, -1, -1, uint64_t(number), 0, serial);
		return false;
	}
	index = found;
	return true;
}

// An unattended plan needs a [disk.N] rule with a serial for every disk its
// systems name ("no disk section, no run"). The match never replaces the typed
// word: it is an additional condition (user decision 2026-11-23).
inline bool AuthoriseDisks(const Plan& plan, const DiskState* disks, size_t diskCount, int indexes[kMaxDiskRules],
	Refusals& report)
{
	bool ok = true, needed[kMaxDiskRules] = {false, false, false, false};
	for (int i = 0; i < plan.count; i++) {
		const int number = plan.systems[i].disk != 0 ? plan.systems[i].disk : 1;
		if (number >= 1 && number <= kMaxDiskRules)
			needed[number - 1] = true;
	}
	int found[kMaxDiskRules] = {-1, -1, -1, -1};
	for (int d = 0; d < kMaxDiskRules; d++) {
		if (!needed[d])
			continue;
		if (!MatchDisk(plan, d + 1, disks, diskCount, found[d], report))
			ok = false;
		for (int k = 0; k < d && ok; k++)
			if (found[k] >= 0 && found[k] == found[d]) {
				report.Add(Code::diskManyMatch, -1, -1, uint64_t(d) + 1, 2, plan.disks[d].serial);
				ok = false;
			}
	}
	if (ok)
		for (int d = 0; d < kMaxDiskRules; d++)
			indexes[d] = found[d];
	return ok;
}

// ---------------------------------------------------------------------------
// Speakable sentences
// ---------------------------------------------------------------------------
inline const char* NameOf(const Plan& plan, int index)
{
	return index >= 0 && index < plan.count ? plan.systems[index].name : "";
}

inline void Say(const Refusal& r, const Plan& plan, bool pl, Out& out)
{
	const char* a = NameOf(plan, r.a);
	const char* b = NameOf(plan, r.b);
	switch (r.code) {
	case Code::tooFew:
		out.S(pl ? "Plan potrzebuje co najmniej dwóch systemów." : "A plan needs at least two systems.");
		break;
	case Code::tooMany:
		out.S(pl ? "Plan może mieć najwyżej sześć systemów." : "A plan can hold six systems at most.");
		break;
	case Code::unknownKind:
		if (pl) out.S("Nieznany rodzaj systemu: ").S(r.text).S(". Systemów instalowanych ręcznie i obrazów bez pliku ustawień nie można umieścić w planie.");
		else out.S("Unknown kind of system: ").S(r.text).S(". Systems that are installed by hand, and images without a settings file, cannot be part of a plan.");
		break;
	case Code::missingKind:
		if (pl) out.S("System ").S(r.text).S(" nie ma wpisu kind w swojej sekcji.");
		else out.S("System ").S(r.text).S(" has no kind entry in its section.");
		break;
	case Code::duplicateId:
		if (pl) out.S("Identyfikator systemu ").S(r.text).S(" występuje dwa razy.");
		else out.S("The system id ").S(r.text).S(" is used twice.");
		break;
	case Code::twoCharonDos:
		out.S(pl ? "CharonDOS może być w planie tylko raz." : "CharonDOS can be in a plan only once.");
		break;
	case Code::sameDistroTwice:
		if (pl) out.S(a).S(" i ").S(b).S(" to ta sama dystrybucja instalowana instalatorem: oba zapisałyby ten sam katalog programu rozruchowego. Zainstaluj drugi egzemplarz jako gotowy obraz.");
		else out.S(a).S(" and ").S(b).S(" are the same distribution installed by its installer: both would write the same boot loader folder. Install the second copy as a ready image.");
		break;
	case Code::sameImageTwice:
		if (pl) out.S(a).S(" i ").S(b).S(" to ten sam plik obrazu: oba systemy miałyby identyczne identyfikatory systemów plików i uruchamiałby się zawsze pierwszy.");
		else out.S(a).S(" and ").S(b).S(" are the same image file: both systems would carry identical file system identifiers and the first one would always start.");
		break;
	case Code::archImage:
		out.S(pl ? "Obraz Arch Linuksa działa tylko na całym dysku, więc nie może być częścią planu."
			: "The Arch Linux image works only on a whole disk, so it cannot be part of a plan.");
		break;
	case Code::duplicateHost:
		if (pl) out.S(a).S(" i ").S(b).S(" miałyby tę samą nazwę komputera, ").S(r.text).S(".");
		else out.S(a).S(" and ").S(b).S(" would have the same computer name, ").S(r.text).S(".");
		break;
	case Code::hostTooLong:
		if (pl) out.S("Nazwa komputera dla systemu ").S(a).S(" jest za długa; Windows i CharonDOS przyjmują najwyżej 15 znaków.");
		else out.S("The computer name for ").S(a).S(" is too long; Windows and CharonDOS take 15 characters at most.");
		break;
	case Code::badDefault:
		if (pl) out.S("Domyślny system ").S(r.text).S(" nie jest na liście.");
		else out.S("The default system ").S(r.text).S(" is not in the list.");
		break;
	case Code::badRest:
		if (pl) out.S("Pozostałe miejsce nie może trafić do: ").S(r.text).S(".");
		else out.S("The space that is left over cannot go to: ").S(r.text).S(".");
		break;
	case Code::dataNtfsNeedsWindows:
		out.S(pl ? "Wspólną partycję NTFS formatuje instalator Windows, a w planie nie ma Windows. Wybierz exFAT."
			: "A shared NTFS partition is formatted by Windows Setup, and the plan has no Windows. Choose exFAT.");
		break;
	case Code::sizeBelowMinimum:
		if (pl) { out.S(a).S(" potrzebuje co najmniej "); Gigabytes(out, r.n1, true); out.S(", a podano "); Gigabytes(out, r.n2, true); out.Ch('.'); }
		else { out.S(a).S(" needs at least "); Gigabytes(out, r.n1, false); out.S(", and "); Gigabytes(out, r.n2, false); out.S(" was given."); }
		break;
	case Code::windowsManyDisks:
		if (pl) out.S("W komputerze jest więcej niż jeden dysk wewnętrzny (").N(r.n1).S("). Instalator Windows nie potrafi jeszcze bezpiecznie wskazać dysku, więc plan z Windows wymaga dokładnie jednego.");
		else out.S("This computer has more than one internal disk (").N(r.n1).S("). Windows Setup cannot yet be told safely which disk to use, so a plan with Windows needs exactly one.");
		break;
	case Code::ubuntuRam:
		if (pl) out.S("Instalator Ubuntu potrzebuje 8 gigabajtów pamięci, a komputer ma ").N(r.n1).S(" megabajtów.");
		else out.S("The Ubuntu installer needs 8 gigabytes of memory, and this computer has ").N(r.n1).S(" megabytes.");
		break;
	case Code::startMediumIsTarget:
		out.S(pl ? "Charon został uruchomiony z dysku, który plan ma zmienić, i nie działa z pamięci. Uruchom Charona z pamięci albo z innego nośnika."
			: "Charon was started from the disk the plan is going to change, and it is not running from memory. Start Charon from memory or from another medium.");
		break;
	case Code::secureBootRefused:
		out.S(pl ? "Secure Boot jest włączony, a plik odpowiedzi każe wtedy odmówić. Nic nie zostało zmienione."
			: "Secure Boot is on, and the answer file says to refuse in that case. Nothing has been changed.");
		break;
	case Code::notGpt:
		out.S(pl ? "Ten dysk nie ma tablicy partycji GPT, więc nie można dodać systemów obok tego, co na nim jest. Można tylko wymazać cały dysk."
			: "This disk has no GPT partition table, so systems cannot be added beside what is on it. Only erasing the whole disk is possible.");
		break;
	case Code::sectorSize:
		if (pl) out.S(a).S(" wymaga dysku z sektorami 512 bajtów, a ten dysk ma sektory ").N(r.n1).S(" bajtów.");
		else out.S(a).S(" needs a disk with 512-byte sectors, and this disk has ").N(r.n1).S("-byte sectors.");
		break;
	case Code::badSectorSize:
		if (pl) out.S("Nieobsługiwany rozmiar sektora dysku: ").N(r.n1).S(" bajtów.");
		else out.S("Unsupported disk sector size: ").N(r.n1).S(" bytes.");
		break;
	case Code::tooSmall:
		if (pl) { out.S("Plan się nie mieści. Potrzeba co najmniej "); Size(out, r.n2, true); out.S(", brakuje "); Size(out, r.n1, true); out.Ch('.'); }
		else { out.S("The plan does not fit. It needs at least "); Size(out, r.n2, false); out.S("; "); Size(out, r.n1, false); out.S(" are missing."); }
		break;
	case Code::espTooSmall:
		if (pl) out.S("Partycja EFI planu musi mieć co najmniej 550 megabajtów; podano ").N(r.n1).Ch('.');
		else out.S("The plan's EFI partition must have at least 550 megabytes; ").N(r.n1).S(" was given.");
		break;
	case Code::existingEspFull:
		if (pl) { out.S("Na istniejącej partycji EFI jest za mało miejsca: wolne "); Megabytes(out, r.n1, true); out.S(", potrzeba "); Megabytes(out, r.n2, true); out.S(". Instalator Windows z drugą partycją EFI na dysku nie został sprawdzony, więc plan z Windows jest tu odrzucany."); }
		else { out.S("The existing EFI partition has too little room: "); Megabytes(out, r.n1, false); out.S(" free, "); Megabytes(out, r.n2, false); out.S(" needed. Windows Setup with a second EFI partition on the disk has not been proven, so a plan with Windows is refused here."); }
		break;
	case Code::tableFull:
		out.S(pl ? "W tablicy partycji tego dysku nie ma dość wolnych wpisów na plan."
			: "The partition table of this disk has too few free entries for the plan.");
		break;
	case Code::noFreeSpace:
		out.S(pl ? "Na tym dysku nie ma wolnego miejsca poza istniejącymi partycjami. Charon nie zmniejsza partycji innych systemów w tej wersji."
			: "This disk has no free space outside its partitions. Charon does not shrink other systems' partitions in this version.");
		break;
	case Code::badDisk:
		out.S(pl ? "Nie można odczytać rozmiaru albo układu tego dysku." : "The size or the layout of this disk cannot be read.");
		break;
	case Code::iniSyntax:
		if (pl) out.S("Plik odpowiedzi, wiersz ").N(r.n1).S(": to nie jest sekcja, komentarz ani wpis klucz=wartość.");
		else out.S("Answer file, line ").N(r.n1).S(": not a section, a comment or a key=value entry.");
		break;
	case Code::iniOverflow:
		if (pl) out.S("Plik odpowiedzi, wiersz ").N(r.n1).S(": wartość ").S(r.text).S(" się nie mieści.");
		else out.S("Answer file, line ").N(r.n1).S(": the value of ").S(r.text).S(" does not fit.");
		break;
	case Code::formatNewer:
		if (pl) out.S("Plik odpowiedzi ma format ").N(r.n1).S(", nowszy niż zna ten Charon. Nie zgaduję.");
		else out.S("The answer file has format ").N(r.n1).S(", newer than this Charon knows. It is not guessed.");
		break;
	case Code::badValue:
		if (pl) out.S("Plik odpowiedzi: nieprawidłowa wartość wpisu ").S(r.text).Ch('.');
		else out.S("Answer file: the value of ").S(r.text).S(" is not valid.");
		break;
	case Code::unknownKey: case Code::warnIgnoredKey:
		if (pl) out.S("Plik odpowiedzi: nieznany wpis ").S(r.text).S(" został pominięty.");
		else out.S("Answer file: the unknown entry ").S(r.text).S(" was ignored.");
		break;
	case Code::noSystems:
		out.S(pl ? "Plik odpowiedzi nie ma listy systemów (wpis systems w sekcji plan)."
			: "The answer file has no list of systems (entry systems in section plan).");
		break;
	case Code::noDiskSection:
		if (pl) out.S("Plik odpowiedzi nie ma sekcji disk.").N(r.n1).S(". Bez wskazania dysku po numerze seryjnym plan bez pytań nie ruszy.");
		else out.S("The answer file has no section disk.").N(r.n1).S(". Without a disk named by its serial number an unattended plan does not run.");
		break;
	case Code::noSerial:
		if (pl) out.S("Sekcja disk.").N(r.n1).S(" nie ma numeru seryjnego. Numer seryjny to jedyne przyjmowane wskazanie dysku.");
		else out.S("Section disk.").N(r.n1).S(" has no serial number. A serial number is the only accepted way to name the disk.");
		break;
	case Code::diskNoMatch:
		if (pl) out.S("Żaden podłączony dysk nie ma numeru seryjnego ").S(r.text).S(" albo nie spełnia pozostałych warunków. Nic nie zostało zmienione.");
		else out.S("No connected disk has the serial number ").S(r.text).S(", or it does not meet the other conditions. Nothing has been changed.");
		break;
	case Code::diskManyMatch:
		if (pl) out.S("Więcej niż jeden dysk pasuje do numeru seryjnego ").S(r.text).S(". Nic nie zostało zmienione.");
		else out.S("More than one disk matches the serial number ").S(r.text).S(". Nothing has been changed.");
		break;
	case Code::diskNotEmpty:
		if (pl) out.S("Dysk ").S(r.text).S(" nie jest pusty, a plik odpowiedzi każe wtedy odmówić. Nic nie zostało zmienione.");
		else out.S("The disk ").S(r.text).S(" is not empty, and the answer file says to refuse in that case. Nothing has been changed.");
		break;
	case Code::shrinkUnsupported:
		out.S(pl ? "Zmniejszanie partycji Windows nie jest dostępne w tej wersji. Zwolnij miejsce w Windows programem Charon-MakeRoom."
			: "Shrinking a Windows partition is not available in this version. Make room inside Windows with Charon-MakeRoom.");
		break;
	case Code::warnSecureBoot:
		out.S(pl ? "Secure Boot jest włączony. Firmware uruchomi tylko podpisane programy rozruchowe. Charon niczego nie obchodzi ani nie wyłącza."
			: "Secure Boot is on. The firmware will start only signed boot loaders. Charon bypasses nothing and switches nothing off.");
		break;
	case Code::warnDosSecureBoot:
		out.S(pl ? "CharonDOS nie może być wpisem domyślnym, dopóki Secure Boot jest włączony, chyba że zostanie zarejestrowany."
			: "CharonDOS cannot be the default entry while Secure Boot is on, unless it is enrolled.");
		break;
	case Code::warnNoNetwork:
		out.S(pl ? "Nie ma połączenia z siecią. Instalatory pobierają pakiety z sieci."
			: "There is no network connection. The installers download packages from the network.");
		break;
	case Code::warnSilent:
		if (pl) out.S(a).S(" nie mówi podczas instalacji ani po niej.");
		else out.S(a).S(" does not speak during or after its installation.");
		break;
	case Code::warnSecondEsp:
		out.S(pl ? "Istniejąca partycja EFI jest za mała, więc plan utworzy własną, drugą partycję EFI."
			: "The existing EFI partition is too small, so the plan creates a second EFI partition of its own.");
		break;
	case Code::warnClearPassword:
		out.S(pl ? "Plik odpowiedzi Windows z hasłem czeka na partycji EFI do swojego kroku i jest wymazywany zaraz po nim."
			: "The Windows answer file with the password waits on the EFI partition until its step and is erased right after it.");
		break;
	default:
		break;
	}
}

// Every refusal (or, with `warnings`, every warning) as one text, a sentence each.
inline void SayAll(const Refusals& report, const Plan& plan, bool pl, bool warnings, Out& out)
{
	bool first = true;
	for (uint8_t i = 0; i < report.count; i++) {
		if (Refusals::Warning(report.list[i].code) != warnings)
			continue;
		if (!first)
			out.Ch(' ');
		first = false;
		Say(report.list[i], plan, pl, out);
	}
	if (!warnings && report.dropped) {
		if (!first)
			out.Ch(' ');
		out.S(pl ? "Jest więcej błędów, niż mieści lista." : "There are more errors than the list holds.");
	}
}

} // namespace hakefi::plan
