// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: multi-system plan - the disk layout, computed up front
// (work/charon-multiboot-spec-2026-11-23.md 2.4, 2.10). Nothing is read from
// or written to a disk here: ComputeLayout() takes a description of the disk
// and returns the ranges, or the reasons why the plan is refused.
//
// Rules implemented:
//   - one shared EFI system partition, first in the plan's space, 1 GiB by
//     default (minimum 550 MB); when CharonDOS is in the plan its partition IS
//     that EFI partition (1 GiB + CharonDOS's own need);
//   - a disk whose content is kept: only its largest free range is used, no
//     existing partition is moved or retyped; its EFI partition stays the
//     shared one when Charon and the new loaders fit (CharonDOS is then an
//     ordinary FAT32 partition in the free range);
//   - 16 MiB Microsoft reserved partition when a Windows is installed and the
//     disk has none;
//   - physical order = install order: Windows (older first, each followed by
//     its optional 1 GiB recovery partition), installer Linux, ready images,
//     then the optional shared data partition (only when asked: decision D7);
//   - sizes: all minimums first (refusal says how much is missing), then every
//     system is raised toward its default in proportion, the rest goes to
//     plan.rest (default: the data partition when asked for, else the last
//     system of the list); every boundary on 1 MiB;
//   - no shared swap partition;
//   - every system's range is a placeholder until its own step; when an
//     installer that takes "the biggest free space" is in the plan, every
//     other free range of the disk is covered by a spare placeholder so that
//     exactly one gap exists when that installer runs.
#pragma once

#include "CharonPlanModel.h"

namespace hakefi::plan {

enum class Role : uint8_t {
	efiSystem,         // the shared EFI system partition, created by the plan
	dosEfi,            // CharonDOS's partition, which is the shared EFI partition
	dos,               // CharonDOS as an ordinary FAT32 partition (an EFI partition existed)
	microsoftReserved,
	system,
	windowsRecovery,
	data,
	spare,             // covers free space during the plan; removed by the finish step
};

struct Range {
	Role role;
	int8_t system;        // index in the plan, -1 = none
	uint64_t firstLba, lbaCount;
	bool placeholder;     // created as a "Linux reserved" placeholder in step 0
	uint8_t guid[16];     // unique partition GUID, set by whoever creates it
};

constexpr int kMaxRanges = 32;
constexpr uint64_t kEspDefaultMb = 1024, kEspMinimumMb = 550, kMsrMb = 16, kRecoveryMb = 1024, kDataMinimumMb = 1024;
constexpr uint64_t kSpareMinimumMb = 16;

struct Layout {
	Range ranges[kMaxRanges];
	uint8_t count;
	uint32_t sectorSize;
	bool newTable;        // the disk gets a new GPT (erased or blank)
	bool espExisting;     // the shared EFI partition is one that was already there
	uint64_t espFirstLba; // its start, when espExisting
	bool secondEsp;       // an EFI partition existed but was too small: the plan made its own
	uint64_t spaceFirstLba, spaceLbaCount; // the free range the plan works in
	uint64_t neededMb;    // sum of all minimums
	uint64_t freeMb;      // left free on purpose (rest=free)
};

inline uint64_t RangeMb(const Layout& layout, const Range& range)
{
	return range.lbaCount / (uint64_t(1024) * 1024 / layout.sectorSize);
}
inline int RangeOfSystem(const Layout& layout, int system, Role role = Role::system)
{
	for (int i = 0; i < layout.count; i++) {
		const Range& r = layout.ranges[i];
		if (r.system != system)
			continue;
		if (r.role == role || (role == Role::system && (r.role == Role::dosEfi || r.role == Role::dos)))
			return i;
	}
	return -1;
}

namespace detail {

struct Item {
	Role role;
	int8_t system;
	uint64_t minimumMb, defaultMb, sizeMb;
	bool fixed;
};

inline bool OnDisk(const System& system, int diskNumber)
{
	return (system.disk != 0 ? system.disk : 1) == diskNumber;
}

// The free ranges of an existing table in MiB-aligned LBAs, in disk order.
struct Space {
	uint64_t firstLba, lbaCount;
};
inline int FreeSpaces(const DiskState& disk, Space* spaces, int capacity)
{
	const uint64_t mib = uint64_t(1024) * 1024 / disk.sectorSize;
	int found = 0;
	uint64_t position = disk.firstUsable;
	while (position <= disk.lastUsable) {
		uint64_t next = disk.lastUsable + 1;
		bool covered = false;
		for (int i = 0; i < disk.count; i++) {
			const DiskPart& p = disk.parts[i];
			if (p.firstLba <= position && position <= p.lastLba) {
				position = p.lastLba + 1;
				covered = true;
				break;
			}
			if (p.firstLba > position && p.firstLba < next)
				next = p.firstLba;
		}
		if (covered)
			continue;
		uint64_t first = position + (mib - 1);
		first -= first % mib;
		if (first < next) {
			uint64_t length = next - first;
			length -= length % mib;
			if (length != 0 && found < capacity) {
				spaces[found].firstLba = first;
				spaces[found].lbaCount = length;
				found++;
			}
		}
		position = next;
	}
	return found;
}

} // namespace detail

// Computes the layout of the systems that the plan puts on disk `diskNumber`
// (1-based, System::disk). `existing` says what happens to the disk's content;
// a blank disk is always given a new table. Returns false and adds the reasons
// to `report` when the plan is refused; `layout` is then not usable. Warnings
// (a second EFI partition) are added to `report` as well.
inline bool ComputeLayout(const Plan& plan, int diskNumber, const DiskState& disk, Existing existing,
	Layout& layout, Refusals& report)
{
	using namespace detail;
	{
		uint8_t* p = reinterpret_cast<uint8_t*>(&layout);
		for (size_t i = 0; i < sizeof(Layout); i++)
			p[i] = 0;
	}
	if (disk.sectorSize != 512 && disk.sectorSize != 4096) {
		report.Add(Code::badSectorSize, -1, -1, disk.sectorSize);
		return false;
	}
	layout.sectorSize = disk.sectorSize;
	const uint64_t mib = uint64_t(1024) * 1024 / disk.sectorSize;
	bool good = true;

	// The systems of this disk.
	int dos = -1, windows = 0, linux = 0, last = -1;
	bool takesGap = false;
	for (int i = 0; i < plan.count; i++) {
		const System& s = plan.systems[i];
		if (!OnDisk(s, diskNumber))
			continue;
		if (s.kind == Kind::charondos)
			dos = i;
		else
			last = i;
		windows += IsWindows(s.kind) ? 1 : 0;
		linux += IsLinux(s.kind) ? 1 : 0;
		takesGap = takesGap || IsInstallerLinux(s.kind);   // r410 (bd): Ubuntu's use_gap as well
		// CharonDOS and ready images are written for 512-byte sectors.
		if (disk.sectorSize != 512 && (s.kind == Kind::charondos || IsImage(s.kind))) {
			report.Add(Code::sectorSize, i, -1, disk.sectorSize);
			good = false;
		}
	}
	if (dos < 0 && last < 0)
		return good; // nothing of the plan goes to this disk

	// The space the plan works in.
	const bool fresh = existing == Existing::erase || disk.blank;
	uint64_t spaceFirst = 0, spaceCount = 0;
	Space others[64];
	int otherCount = 0;
	bool hasEsp = false, hasMsr = false;
	if (fresh) {
		const uint64_t totalMb = disk.sectors / mib;
		if (totalMb < 8) {
			report.Add(Code::badDisk);
			return false;
		}
		// First MiB: protective MBR and primary GPT; last MiB: backup GPT.
		spaceFirst = mib;
		spaceCount = (totalMb - 2) * mib;
		layout.newTable = true;
	} else {
		if (!disk.gpt) {
			report.Add(Code::notGpt);
			return false;
		}
		if (disk.firstUsable == 0 || disk.lastUsable < disk.firstUsable || disk.lastUsable >= disk.sectors) {
			report.Add(Code::badDisk);
			return false;
		}
		Space spaces[64];
		const int n = FreeSpaces(disk, spaces, 64);
		int largest = -1;
		for (int i = 0; i < n; i++)
			if (largest < 0 || spaces[i].lbaCount > spaces[largest].lbaCount)
				largest = i;
		if (largest < 0) {
			report.Add(Code::noFreeSpace);
			return false;
		}
		spaceFirst = spaces[largest].firstLba;
		spaceCount = spaces[largest].lbaCount;
		for (int i = 0; i < n; i++)
			if (i != largest && spaces[i].lbaCount >= kSpareMinimumMb * mib)
				others[otherCount++] = spaces[i];
		for (int i = 0; i < disk.count; i++) {
			if (disk.parts[i].type == PartType::efiSystem && !hasEsp) {
				hasEsp = true;
				layout.espFirstLba = disk.parts[i].firstLba;
			}
			hasMsr = hasMsr || disk.parts[i].type == PartType::microsoftReserved;
		}
	}
	layout.spaceFirstLba = spaceFirst;
	layout.spaceLbaCount = spaceCount;
	const uint64_t totalMb = spaceCount / mib;

	// What is put there, in physical order.
	Item items[kMaxRanges];
	int itemCount = 0;
	auto add = [&](Role role, int system, uint64_t minimumMb, uint64_t defaultMb, bool fixed) {
		if (itemCount >= kMaxRanges)
			return;
		items[itemCount].role = role;
		items[itemCount].system = int8_t(system);
		items[itemCount].minimumMb = minimumMb;
		items[itemCount].defaultMb = defaultMb;
		items[itemCount].sizeMb = minimumMb;
		items[itemCount].fixed = fixed;
		itemCount++;
	};
	uint64_t espMb = kEspDefaultMb;
	if (diskNumber >= 1 && diskNumber <= kMaxDiskRules && plan.disks[diskNumber - 1].espSizeMb != 0)
		espMb = plan.disks[diskNumber - 1].espSizeMb;
	if (espMb < kEspMinimumMb) {
		report.Add(Code::espTooSmall, -1, -1, espMb);
		good = false;
	}
	uint64_t dosMinimum = 0, dosDefault = 0;
	if (dos >= 0)
		SizeLimits(plan.systems[dos], dosMinimum, dosDefault);
	bool ownEsp = fresh || !hasEsp;
	if (!ownEsp) {
		// Charon with its synthesizers, the Windows boot files, the loaders.
		const uint64_t need = 96 + 64 * uint64_t(windows) + 16 * uint64_t(linux) + 4;
		if (!disk.espFreeKnown) {
			report.Add(Code::badDisk);
			return false;
		}
		if (disk.espFreeMb < need) {
			if (windows != 0) {
				report.Add(Code::existingEspFull, -1, -1, disk.espFreeMb, need);
				return false;
			}
			report.Add(Code::warnSecondEsp);
			layout.secondEsp = true;
			ownEsp = true;
		}
	}
	layout.espExisting = !ownEsp;
	if (ownEsp) {
		if (dos >= 0)
			add(Role::dosEfi, dos, espMb + dosMinimum, espMb + dosMinimum, true);
		else
			add(Role::efiSystem, -1, espMb, espMb, true);
	} else if (dos >= 0) {
		const uint64_t size = dosMinimum < 1024 ? 1024 : dosMinimum;
		add(Role::dos, dos, size, size, true);
	}
	if (windows != 0 && !hasMsr)
		add(Role::microsoftReserved, -1, kMsrMb, kMsrMb, true);
	auto addSystem = [&](int i) {
		const System& s = plan.systems[i];
		uint64_t minimum = 0, standard = 0;
		SizeLimits(s, minimum, standard);
		if (s.sizeGb != 0) {
			const uint64_t asked = uint64_t(s.sizeGb) * 1024;
			if (asked < minimum) {
				report.Add(Code::sizeBelowMinimum, i, -1, (minimum + 1023) / 1024, s.sizeGb);
				good = false;
			}
			add(Role::system, i, asked, asked, true);
		} else {
			add(Role::system, i, minimum, standard, false);
		}
		if (IsWindows(s.kind) && s.winre)
			add(Role::windowsRecovery, i, kRecoveryMb, kRecoveryMb, true);
	};
	for (int rank = 10; rank <= 12; rank++)
		for (int i = 0; i < plan.count; i++)
			if (OnDisk(plan.systems[i], diskNumber) && IsWindows(plan.systems[i].kind)
				&& WindowsRank(plan.systems[i]) == rank)
				addSystem(i);
	for (int i = 0; i < plan.count; i++)
		if (OnDisk(plan.systems[i], diskNumber) && IsInstallerLinux(plan.systems[i].kind))
			addSystem(i);
	for (int i = 0; i < plan.count; i++)
		if (OnDisk(plan.systems[i], diskNumber) && IsImage(plan.systems[i].kind))
			addSystem(i);
	// r410 (agent 2's run hvxp1: "the range of this system is not in the layout"): the Charon Hypervisor's range
	for (int i = 0; i < plan.count; i++)
		if (OnDisk(plan.systems[i], diskNumber) && plan.systems[i].kind == Kind::hypervisor)
			addSystem(i);
	int dataItem = -1;
	if (plan.data != DataFs::none && diskNumber == 1) {
		dataItem = itemCount;
		if (plan.dataSizeGb != 0)
			add(Role::data, -1, uint64_t(plan.dataSizeGb) * 1024, uint64_t(plan.dataSizeGb) * 1024, true);
		else
			add(Role::data, -1, kDataMinimumMb, kDataMinimumMb, false);
	}

	// Minimums first.
	uint64_t needed = 0, wanted = 0;
	for (int i = 0; i < itemCount; i++) {
		needed += items[i].minimumMb;
		if (!items[i].fixed && items[i].role == Role::system)
			wanted += items[i].defaultMb - items[i].minimumMb;
	}
	layout.neededMb = needed;
	if (needed > totalMb) {
		report.Add(Code::tooSmall, -1, -1, needed - totalMb, needed);
		return false;
	}
	if (!good)
		return false;
	// Toward the defaults, in proportion.
	uint64_t spare = totalMb - needed;
	if (wanted != 0) {
		const uint64_t share = spare >= wanted ? wanted : spare;
		uint64_t given = 0;
		for (int i = 0; i < itemCount; i++) {
			if (items[i].fixed || items[i].role != Role::system)
				continue;
			const uint64_t part = (items[i].defaultMb - items[i].minimumMb) * share / wanted;
			items[i].sizeMb += part;
			given += part;
		}
		spare -= given;
	}
	// The rest.
	int restItem = -1;
	bool restFree = false;
	if (Eq(plan.rest, "free"))
		restFree = true;
	else if (plan.rest[0] != 0 && !Eq(plan.rest, "data")) {
		const int target = FindSystem(plan, plan.rest);
		for (int i = 0; i < itemCount; i++)
			if (items[i].role == Role::system && items[i].system == target)
				restItem = i;
	} else if (dataItem >= 0 && !items[dataItem].fixed) {
		restItem = dataItem;
	}
	// A size the user typed is exact: the rest never goes there.
	if (restItem >= 0 && items[restItem].fixed)
		restItem = -1;
	if (!restFree && restItem < 0) {
		// The last system of the list that is on this disk (never CharonDOS),
		// else the last system whose size was not typed; else it stays free.
		for (int i = 0; i < itemCount; i++)
			if (items[i].role == Role::system && items[i].system == last && !items[i].fixed)
				restItem = i;
		for (int i = 0; i < itemCount && restItem < 0; i++)
			if (items[itemCount - 1 - i].role == Role::system && !items[itemCount - 1 - i].fixed)
				restItem = itemCount - 1 - i;
	}
	if (!restFree && restItem >= 0) {
		items[restItem].sizeMb += spare;
		spare = 0;
	}
	layout.freeMb = spare;

	// Places.
	auto place = [&](Role role, int system, uint64_t firstLba, uint64_t lbaCount) {
		if (layout.count >= kMaxRanges) {
			good = false;
			return;
		}
		Range& r = layout.ranges[layout.count++];
		r.role = role;
		r.system = int8_t(system);
		r.firstLba = firstLba;
		r.lbaCount = lbaCount;
		r.placeholder = role == Role::system || role == Role::windowsRecovery || role == Role::spare;
	};
	uint64_t cursor = spaceFirst;
	for (int i = 0; i < itemCount; i++) {
		place(items[i].role, items[i].system, cursor, items[i].sizeMb * mib);
		cursor += items[i].sizeMb * mib;
	}
	// An installer that takes the biggest free space must find exactly one.
	if (takesGap) {
		if (spare >= kSpareMinimumMb)
			place(Role::spare, -1, cursor, spare * mib);
		for (int i = 0; i < otherCount; i++)
			place(Role::spare, -1, others[i].firstLba, others[i].lbaCount);
	}
	if (!good || (!fresh && layout.count > disk.entriesFree) || (fresh && layout.count > 128)) {
		report.Add(Code::tableFull);
		return false;
	}
	return true;
}

// Bounds for the question "Size for Debian in gigabytes. Smallest 12, proposed
// 40, largest possible 180" (sys.N.size). False when the plan does not fit.
inline bool SizeBounds(const Plan& plan, int diskNumber, const DiskState& disk, Existing existing, int system,
	uint32_t& smallestGb, uint32_t& proposedGb, uint32_t& largestGb)
{
	Plan trial = plan;
	trial.systems[system].sizeGb = 0;
	Layout layout;
	Refusals report;
	if (!ComputeLayout(trial, diskNumber, disk, existing, layout, report))
		return false;
	const int range = RangeOfSystem(layout, system);
	if (range < 0 || layout.ranges[range].role != Role::system)
		return false;
	uint64_t minimum = 0, standard = 0;
	SizeLimits(plan.systems[system], minimum, standard);
	const uint64_t mib = uint64_t(1024) * 1024 / layout.sectorSize;
	smallestGb = uint32_t((minimum + 1023) / 1024);
	proposedGb = uint32_t(RangeMb(layout, layout.ranges[range]) / 1024);
	largestGb = uint32_t((layout.spaceLbaCount / mib - (layout.neededMb - minimum)) / 1024);
	if (proposedGb < smallestGb)
		proposedGb = smallestGb;
	return true;
}

// What a placeholder becomes just before its step. `remove` true: it is
// deleted, so that its range is the one free gap (Debian's installer and the
// image remapper); otherwise it is retyped to `type` and the installer formats
// it (Windows). r410 (bd): Ubuntu's installer takes the gap too (use_gap).
inline void BeforeStep(Kind kind, Role role, bool& remove, PartType& type)
{
	remove = false;
	type = PartType::placeholder;
	if (role == Role::windowsRecovery) {
		type = PartType::windowsRecovery;
		return;
	}
	if (role == Role::spare) {
		remove = true; // by the finish step
		return;
	}
	if (TakesGap(kind))
		remove = true;
	else if (IsWindows(kind))
		type = PartType::basicData;
	else
		type = PartType::linuxData;
}

// Position of a range among all partitions of the disk by start block, from 1
// (the number an installer that counts by position uses; Windows Setup's
// PartitionID). `countReserved` false leaves Microsoft reserved partitions
// out: whether Setup counts them among existing partitions is an open point of
// the spec (2.6) that the first Windows trial measures.
inline uint32_t PositionNumber(const DiskState& disk, const Layout& layout, int range, bool countReserved)
{
	const uint64_t start = layout.ranges[range].firstLba;
	uint32_t number = 1;
	if (!layout.newTable)
		for (int i = 0; i < disk.count; i++)
			if (disk.parts[i].firstLba < start && (countReserved || disk.parts[i].type != PartType::microsoftReserved))
				number++;
	for (int i = 0; i < layout.count; i++)
		if (layout.ranges[i].firstLba < start
			&& (countReserved || layout.ranges[i].role != Role::microsoftReserved))
			number++;
	return number;
}

// "Charon plan 2: Debian" - at most 36 characters (a GPT name), ASCII.
inline void PlaceholderName(const Plan& plan, const Range& range, int step, Out& out)
{
	char text[96];
	Out all(text, sizeof(text));
	all.S("Charon plan");
	if (range.role == Role::spare)
		all.S(": free");
	else {
		all.Ch(' ').N(uint64_t(step)).S(": ").S(NameOf(plan, range.system));
		if (range.role == Role::windowsRecovery)
			all.S(" recovery");
	}
	for (size_t i = 0; i < 36 && text[i] != 0; i++)
		out.Ch(uint8_t(text[i]) < 0x80 ? text[i] : '?');
}

// The layout as part of the spoken summary: "1, CharonDOS, 2 gigabytes, it is
// also the EFI partition. 2, Windows 11, 100 gigabytes. ..."
inline void SayLayout(const Plan& plan, const Layout& layout, bool pl, Out& out)
{
	int number = 0;
	for (int i = 0; i < layout.count; i++) {
		const Range& r = layout.ranges[i];
		const uint64_t mb = RangeMb(layout, r);
		if (r.role == Role::system || r.role == Role::dosEfi || r.role == Role::dos) {
			out.N(uint64_t(++number)).S(", ").S(NameOf(plan, r.system)).S(", ");
			Size(out, mb, pl);
			if (r.role == Role::dosEfi)
				out.S(pl ? ", to także partycja EFI" : ", it is also the EFI partition");
			out.S(". ");
		} else if (r.role == Role::efiSystem) {
			out.S(pl ? "Partycja EFI, " : "EFI partition, ");
			Size(out, mb, pl);
			out.S(". ");
		} else if (r.role == Role::windowsRecovery) {
			if (pl) { out.S("Partycja odzyskiwania dla systemu ").S(NameOf(plan, r.system)).S(", "); }
			else { out.S("Recovery partition for ").S(NameOf(plan, r.system)).S(", "); }
			Size(out, mb, pl);
			out.S(". ");
		} else if (r.role == Role::data) {
			out.S(pl ? "Wspólna partycja na dane, " : "Shared data partition, ").S(plan.data == DataFs::ntfs ? "NTFS" : "exFAT").S(", ");
			Size(out, mb, pl);
			out.S(". ");
		}
	}
	if (layout.espExisting)
		out.S(pl ? "Istniejąca partycja EFI zostaje i służy wszystkim systemom. " : "The existing EFI partition stays and serves every system. ");
	if (layout.freeMb != 0) {
		out.S(pl ? "Wolne zostaje " : "Left free: ");
		Size(out, layout.freeMb, pl);
		out.S(". ");
	}
}

// ---------------------------------------------------------------------------
// The layout in the plan file: sections [range.N]
// ---------------------------------------------------------------------------
inline const char* RoleWord(Role role)
{
	switch (role) {
	case Role::efiSystem: return "esp";
	case Role::dosEfi: return "dos-esp";
	case Role::dos: return "dos";
	case Role::microsoftReserved: return "msr";
	case Role::system: return "system";
	case Role::windowsRecovery: return "winre";
	case Role::data: return "data";
	default: return "spare";
	}
}
inline bool LayoutToStore(const Plan& plan, const Layout& layout, int diskNumber, Store& store)
{
	bool ok = true;
	char section[24];
	{
		Out name(section, sizeof(section));
		name.S("layout.").N(uint64_t(diskNumber));
	}
	ok = store.PutNumber(section, "ranges", layout.count) && ok;
	ok = store.PutNumber(section, "sector", layout.sectorSize) && ok;
	ok = store.Put(section, "new_table", layout.newTable ? "yes" : "no") && ok;
	ok = store.Put(section, "esp_existing", layout.espExisting ? "yes" : "no") && ok;
	ok = store.PutNumber(section, "esp_first", layout.espFirstLba) && ok;
	ok = store.PutNumber(section, "free_mb", layout.freeMb) && ok;
	for (int i = 0; i < layout.count; i++) {
		const Range& r = layout.ranges[i];
		Out name(section, sizeof(section));
		name.S("range.").N(uint64_t(diskNumber)).Ch('.').N(uint64_t(i));
		ok = store.Put(section, "role", RoleWord(r.role)) && ok;
		ok = store.Put(section, "system", r.system >= 0 ? plan.systems[r.system].id : "") && ok;
		ok = store.PutNumber(section, "first", r.firstLba) && ok;
		ok = store.PutNumber(section, "count", r.lbaCount) && ok;
		ok = store.Put(section, "placeholder", r.placeholder ? "yes" : "no") && ok;
		char guid[40];
		Out text(guid, sizeof(guid));
		GuidText(r.guid, text);
		ok = store.Put(section, "guid", guid) && ok;
	}
	return ok;
}
inline bool LayoutFromStore(const Store& store, const Plan& plan, int diskNumber, Layout& layout)
{
	{
		uint8_t* p = reinterpret_cast<uint8_t*>(&layout);
		for (size_t i = 0; i < sizeof(Layout); i++)
			p[i] = 0;
	}
	char section[24];
	{
		Out name(section, sizeof(section));
		name.S("layout.").N(uint64_t(diskNumber));
	}
	uint64_t n = 0, v = 0;
	if (!ParseNumber(store.GetOr(section, "ranges", ""), n) || n > kMaxRanges)
		return false;
	if (!ParseNumber(store.GetOr(section, "sector", ""), v) || (v != 512 && v != 4096))
		return false;
	layout.sectorSize = uint32_t(v);
	layout.newTable = IsYes(store.GetOr(section, "new_table", ""));
	layout.espExisting = IsYes(store.GetOr(section, "esp_existing", ""));
	ParseNumber(store.GetOr(section, "esp_first", "0"), layout.espFirstLba);
	ParseNumber(store.GetOr(section, "free_mb", "0"), layout.freeMb);
	for (uint64_t i = 0; i < n; i++) {
		Out name(section, sizeof(section));
		name.S("range.").N(uint64_t(diskNumber)).Ch('.').N(i);
		Range& r = layout.ranges[i];
		const char* role = store.GetOr(section, "role", "");
		bool known = false;
		for (uint8_t k = 0; k <= uint8_t(Role::spare); k++)
			if (Eq(role, RoleWord(Role(k)))) {
				r.role = Role(k);
				known = true;
			}
		const char* id = store.GetOr(section, "system", "");
		r.system = int8_t(id[0] != 0 ? FindSystem(plan, id) : -1);
		if (!known || (id[0] != 0 && r.system < 0) || !ParseNumber(store.GetOr(section, "first", ""), r.firstLba)
			|| !ParseNumber(store.GetOr(section, "count", ""), r.lbaCount)
			|| !ParseGuid(store.GetOr(section, "guid", ""), r.guid))
			return false;
		r.placeholder = IsYes(store.GetOr(section, "placeholder", ""));
	}
	layout.count = uint8_t(n);
	return true;
}

} // namespace hakefi::plan
