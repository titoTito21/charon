// SPDX-License-Identifier: GPL-2.0-or-later
// r409 host test of the multi-system plan core (spec T0):
//   src/efi/CharonPlanText.h, CharonPlanModel.h, CharonPlanLayout.h, CharonPlanState.h,
//   the placeholder functions of src/efi/CharonSeedDisk.h (Part 3),
//   and the registry format shared with CharonDOS (src/dos/launch_select.h).
// Everything runs in memory: a fake disk, a fake file store, a fake firmware variable.
// Run through tests/charon-r409-plan-host.py.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/efi/CharonSeedDisk.h"
#include "../src/efi/CharonPlanState.h"
#include "../src/efi/CharonPlanSets.h"
#include "../src/dos/launch_select.h"
#include "../src/efi/CharonClockZone.h"
#include "../src/efi/CharonDiskIdentity.h"
#include "../src/efi/CharonWsPlanWindows.h"
#include "../src/efi/CharonPlanExfat.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace hakefi::plan;
namespace sd = hakefi::seeddisk;

static int gPassed = 0, gFailed = 0;
static bool gVerbose = false;
static void Check(bool ok, const std::string& what)
{
	(ok ? gPassed : gFailed)++;
	if (!ok || gVerbose)
		printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
}
static bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// ---- a memory disk with a fresh GPT ----------------------------------------
struct MemDisk {
	uint32_t bs = 512;
	uint64_t blocks = 0;
	std::vector<uint8_t> data;
	int failAt = -1, writes = 0;
};
static bool MemRead(void* c, uint64_t lba, uint8_t* out, size_t n)
{
	MemDisk& d = *static_cast<MemDisk*>(c);
	if (lba + n > d.blocks)
		return false;
	memcpy(out, d.data.data() + lba * d.bs, n * d.bs);
	return true;
}
static bool MemWrite(void* c, uint64_t lba, const uint8_t* in, size_t n)
{
	MemDisk& d = *static_cast<MemDisk*>(c);
	if (lba + n > d.blocks || d.writes++ == d.failAt)
		return false;
	memcpy(d.data.data() + lba * d.bs, in, n * d.bs);
	return true;
}
static sd::Disk AsDisk(MemDisk& d) { return sd::Disk{&d, d.bs, d.blocks, MemRead, MemWrite}; }
static uint32_t SlowCrc(const uint8_t* p, size_t n) { return Crc32(reinterpret_cast<const char*>(p), n); }
static void P32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }
static void P64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = uint8_t(v >> (8 * i)); }
static void FreshGpt(MemDisk& d, uint32_t bs, uint64_t megabytes)
{
	d.bs = bs;
	d.blocks = megabytes * 1024 * 1024 / bs;
	d.data.assign(size_t(d.blocks * bs), 0);
	d.failAt = -1;
	d.writes = 0;
	const uint64_t entryBlocks = (128 * 128 + bs - 1) / bs;
	std::vector<uint8_t> entries(size_t(entryBlocks * bs), 0);
	auto header = [&](uint64_t my, uint64_t alt, uint64_t entriesLba) {
		uint8_t* h = d.data.data() + my * bs;
		memset(h, 0, bs);
		memcpy(h, "EFI PART", 8);
		P32(h + 8, 0x00010000);
		P32(h + 12, 92);
		P64(h + 24, my);
		P64(h + 32, alt);
		P64(h + 40, 2 + entryBlocks);
		P64(h + 48, d.blocks - 2 - entryBlocks);
		for (int i = 0; i < 16; i++)
			h[56 + i] = uint8_t(0xD0 + i);
		P64(h + 72, entriesLba);
		P32(h + 80, 128);
		P32(h + 84, 128);
		P32(h + 88, SlowCrc(entries.data(), 128 * 128));
		P32(h + 16, SlowCrc(h, 92));
	};
	header(1, d.blocks - 1, 2);
	header(d.blocks - 1, 1, d.blocks - 1 - entryBlocks);
	uint8_t* mbr = d.data.data();
	mbr[446 + 4] = 0xEE;
	P32(mbr + 446 + 8, 1);
	P32(mbr + 446 + 12, uint32_t(d.blocks - 1));
	mbr[510] = 0x55;
	mbr[511] = 0xAA;
}
static sd::Guid G(uint8_t n)
{
	sd::Guid g;
	for (int i = 0; i < 16; i++)
		g.bytes[i] = uint8_t(n + i);
	return g;
}

static void TestPlaceholders(uint32_t bs)
{
	const std::string tag = " (" + std::to_string(bs) + ")";
	MemDisk d;
	FreshGpt(d, bs, 256);
	sd::Disk disk = AsDisk(d);
	std::vector<uint8_t> work(sd::kWorkBytes);
	const uint64_t mib = 1024 * 1024 / bs;
	sd::AddResult r = {};
	// Three ranges that fill the disk from 1 MiB: 64, 100, 80 MiB.
	Check(sd::ReservePlaceholder(disk, work.data(), work.size(), mib, 64 * mib, G(1), u"Charon plan 1: Windows 11", r) == sd::Error::None
		&& r.firstLba == mib && r.lbaCount == 64 * mib, "reserve first range" + tag);
	Check(sd::ReservePlaceholder(disk, work.data(), work.size(), 65 * mib, 100 * mib, G(2), u"Charon plan 2: Debian", r) == sd::Error::None, "reserve second range" + tag);
	Check(sd::ReservePlaceholder(disk, work.data(), work.size(), 165 * mib, 80 * mib, G(3), u"Charon plan: free", r) == sd::Error::None, "reserve third range" + tag);
	std::vector<uint8_t> before = d.data;
	sd::AddResult untouched = {7, 7, 7};
	Check(sd::ReservePlaceholder(disk, work.data(), work.size(), 60 * mib, 10 * mib, G(4), u"x", untouched) == sd::Error::NoRoom
		&& d.data == before && untouched.firstLba == 7, "overlapping reserve refused, nothing written, output unchanged" + tag);
	Check(sd::ReservePlaceholder(disk, work.data(), work.size(), 250 * mib, mib, G(4), nullptr, untouched) == sd::Error::Argument
		&& sd::ReservePlaceholder(disk, work.data(), work.size(), 250 * mib, mib, G(4), u"", untouched) == sd::Error::Argument
		&& d.data == before, "placeholder without a name refused" + tag);
	Check(sd::ReservePlaceholder(disk, work.data(), work.size(), 250 * mib, mib, G(2), u"dup", untouched) == sd::Error::Argument
		&& d.data == before, "duplicate unique GUID refused" + tag);
	sd::Partition parts[8];
	sd::Info info;
	Check(sd::Inspect(disk, work.data(), work.size(), parts, 8, info) == sd::Error::None && info.usedCount == 3 && info.backupValid
		&& memcmp(parts[0].type.bytes, sd::kTypeLinuxReserved.bytes, 16) == 0 && parts[1].name[12] == u'2', "placeholders are Linux reserved, named, backup valid" + tag);
	size_t gaps = 99;
	sd::Gap gap[4];
	Check(sd::FreeGaps(disk, work.data(), work.size(), 0, 16 * 1024 * 1024, gap, 4, gaps) == sd::Error::None && gaps == 0, "no gap while every range is reserved" + tag);

	// Retype: only the plan's own placeholder, found by GUID.
	sd::Partition after;
	memset(&after, 0x55, sizeof(after));
	Check(sd::RetypePartition(disk, work.data(), work.size(), G(9), sd::kTypeLinuxReserved, sd::kTypeBasicData) == sd::Error::NotFound && d.data == before, "retype of an unknown GUID refused" + tag);
	Check(sd::RetypePartition(disk, work.data(), work.size(), G(1), sd::kTypeBasicData, sd::kTypeLinuxData, nullptr, &after) == sd::Error::Mismatch
		&& d.data == before && after.firstLba == 0x5555555555555555ull, "retype of a partition that is not a placeholder refused" + tag);
	Check(sd::RetypePartition(disk, work.data(), work.size(), G(1), sd::kTypeLinuxReserved, sd::kTypeBasicData, u"Windows", &after) == sd::Error::None
		&& after.firstLba == mib && after.lastLba == 65 * mib - 1 && after.name[0] == u'W' && after.name[7] == 0
		&& memcmp(after.type.bytes, sd::kTypeBasicData.bytes, 16) == 0, "retype to basic data, name replaced, place kept" + tag);
	Check(sd::Inspect(disk, work.data(), work.size(), parts, 8, info) == sd::Error::None && info.backupValid && info.usedCount == 3
		&& memcmp(parts[0].unique.bytes, G(1).bytes, 16) == 0, "table valid after retype, GUID kept" + tag);

	// Remove: exactly one gap afterwards.
	before = d.data;
	Check(sd::RemovePartition(disk, work.data(), work.size(), G(2), sd::kTypeBasicData) == sd::Error::Mismatch
		&& sd::RemovePartition(disk, work.data(), work.size(), G(2), sd::kTypeLinuxReserved, 66 * mib) == sd::Error::Mismatch
		&& sd::RemovePartition(disk, work.data(), work.size(), G(1), sd::kTypeLinuxReserved) == sd::Error::Mismatch
		&& sd::RemovePartition(disk, work.data(), work.size(), G(8), sd::kTypeLinuxReserved) == sd::Error::NotFound
		&& d.data == before, "remove refused for wrong type, wrong place, a retyped partition, unknown GUID" + tag);
	// A write that fails at any point leaves a table that still loads.
	for (int fail = 0; fail < 4; fail++) {
		MemDisk copy = d;
		copy.failAt = fail;
		copy.writes = 0;
		sd::Disk cd = AsDisk(copy);
		sd::AddResult freed = {7, 7, 7};
		const sd::Error e = sd::RemovePartition(cd, work.data(), work.size(), G(2), sd::kTypeLinuxReserved, 65 * mib, &freed);
		copy.failAt = -1;
		const sd::Error primary = sd::Inspect(cd, work.data(), work.size(), parts, 8, info);
		// Until the primary array is written (writes 0-2 failed) the old primary table is valid;
		// between array and header (write 3 failed) the complete new backup is.
		const bool usable = fail == 3 ? (primary != sd::Error::None) : (primary == sd::Error::None);
		bool backupOk = true;
		if (fail >= 2) {
			// the backup copy at the end must be complete and hold 2 partitions
			const uint8_t* h = copy.data.data() + (copy.blocks - 1) * bs;
			const uint64_t entriesLba = h[72] | uint64_t(h[73]) << 8 | uint64_t(h[74]) << 16 | uint64_t(h[75]) << 24 | uint64_t(h[76]) << 32;
			backupOk = SlowCrc(copy.data.data() + entriesLba * bs, 128 * 128) == (h[88] | uint32_t(h[89]) << 8 | uint32_t(h[90]) << 16 | uint32_t(h[91]) << 24);
		}
		Check(e == sd::Error::Write && freed.firstLba == 7 && usable && backupOk, "remove: failed write " + std::to_string(fail) + " leaves a valid copy, output unchanged" + tag);
	}
	sd::AddResult freed = {};
	Check(sd::RemovePartition(disk, work.data(), work.size(), G(2), sd::kTypeLinuxReserved, 65 * mib, &freed) == sd::Error::None
		&& freed.firstLba == 65 * mib && freed.lbaCount == 100 * mib, "placeholder removed, freed range reported" + tag);
	Check(sd::FreeGaps(disk, work.data(), work.size(), 0, 16 * 1024 * 1024, gap, 4, gaps) == sd::Error::None && gaps == 1
		&& gap[0].firstLba == 65 * mib && gap[0].lbaCount == 100 * mib, "exactly one gap, the removed range" + tag);
	Check(sd::Inspect(disk, work.data(), work.size(), parts, 8, info) == sd::Error::None && info.usedCount == 2 && info.backupValid, "table valid after remove" + tag);
	Check(sd::RemovePartition(disk, work.data(), work.size(), G(3), sd::kTypeLinuxReserved) == sd::Error::None
		&& sd::FreeGaps(disk, work.data(), work.size(), 0, 0, gap, 4, gaps) == sd::Error::None && gaps == 1
		&& gap[0].firstLba == 65 * mib, "spare removed: the two free ranges join into one gap" + tag);
	size_t count = 5;
	Check(sd::FreeGaps(disk, work.data(), work.size(), 12345, 0, gap, 4, count) == sd::Error::Argument && count == 5, "FreeGaps: bad alignment refused, output unchanged" + tag);
	// Put the placeholder back (what "try this step again" does).
	Check(sd::ReservePlaceholder(disk, work.data(), work.size(), 65 * mib, 100 * mib, G(2), u"Charon plan 2: Debian", r) == sd::Error::None
		&& sd::FreeGaps(disk, work.data(), work.size(), 0, 16 * 1024 * 1024, gap, 4, gaps) == sd::Error::None && gaps == 1 && gap[0].firstLba == 165 * mib,
		"placeholder restored for a retry" + tag);
}

// ---- plans ----------------------------------------------------------------------
struct S { const char* id; Kind kind; const char* version; };
static Plan MakePlan(std::initializer_list<S> systems)
{
	Plan plan;
	Clear(plan);
	plan.id = 0x1a2b3c4d;
	for (const S& s : systems) {
		System& x = plan.systems[plan.count++];
		Copy(x.id, sizeof(x.id), s.id);
		x.kind = s.kind;
		Copy(x.version, sizeof(x.version), s.version ? s.version : "");
		Copy(x.name, sizeof(x.name), DefaultName(x));
		x.disk = 1;
		x.speaks = true;
		if (IsImage(s.kind)) {
			x.imageMb = 3072;
			Copy(x.image, sizeof(x.image), s.id);
		}
		Out host(x.host, sizeof(x.host));
		host.S("pc-").S(s.id);
	}
	return plan;
}
static const S kDos = {"dos", Kind::charondos, nullptr}, kWin11 = {"win11", Kind::windows, "11"}, kWin10 = {"win10", Kind::windows, "10"},
	kDebian = {"debian", Kind::debian, nullptr}, kUbuntu = {"ubuntu", Kind::ubuntu, nullptr}, kImgDebian = {"idebian", Kind::imageDebian, nullptr},
	kImgUbuntu = {"iubuntu", Kind::imageUbuntu, nullptr}, kImgFedora = {"ifedora", Kind::imageFedora, nullptr},
	kHv = {"hv", Kind::hypervisor, nullptr};
static Machine GoodMachine()
{
	Machine m = {};
	m.ramMb = 16384;
	m.internalDisks = 1;
	m.network = true;
	m.driverOptions = true;
	return m;
}
static std::string OrderText(const Plan& plan)
{
	Order o;
	ComputeOrder(plan, o);
	std::string t;
	for (int i = 0; i < o.count; i++) {
		if (i)
			t += " > ";
		t += StepKindWord(o.steps[i].kind);
		if (o.steps[i].system >= 0)
			t += std::string(":") + plan.systems[o.steps[i].system].id;
	}
	return t;
}
static void TestOrder()
{
	Check(OrderText(MakePlan({kDebian, kWin11})) == "prepare > install:win11 > install:debian > finish", "order: Linux + Windows = Windows first");
	Check(OrderText(MakePlan({kWin11, kWin10})) == "prepare > install:win10 > install:win11 > finish", "order: Windows + Windows = older first");
	Check(OrderText(MakePlan({kImgUbuntu, kDebian})) == "prepare > write-image:iubuntu > install:debian > first-start:iubuntu > finish", "order: Linux + Linux = images written, installers, image first starts");
	Check(OrderText(MakePlan({kWin11, kDos})) == "prepare > write-dos:dos > install:win11 > finish", "order: CharonDOS + Windows");
	Check(OrderText(MakePlan({kHv})) == "prepare > write-dos:hv > finish", "order: Charon Hypervisor alone (one-system plan) -> " + OrderText(MakePlan({kHv})));
	Check(OrderText(MakePlan({kHv, kWin11, kDebian})).find("write-dos:hv") != std::string::npos, "order: hypervisor in a plan is written by Charon -> " + OrderText(MakePlan({kHv, kWin11, kDebian})));
	Check(OrderText(MakePlan({kDebian, kDos})) == "prepare > write-dos:dos > install:debian > finish", "order: CharonDOS + Linux");
	Check(OrderText(MakePlan({kImgDebian, kDos})) == "prepare > write-dos:dos > write-image:idebian > first-start:idebian > finish", "order: CharonDOS + Debian image");
	Check(OrderText(MakePlan({kDebian, kWin11, kDos})) == "prepare > write-dos:dos > install:win11 > install:debian > finish", "order: CharonDOS + Windows + Linux");
	Check(OrderText(MakePlan({kUbuntu, kImgFedora, kWin11, kDebian, kDos, kWin10}))
		== "prepare > write-dos:dos > write-image:ifedora > install:win10 > install:win11 > install:ubuntu > install:debian > first-start:ifedora > finish",
		"order: six mixed systems follow the general rule, list order inside a class");
	Plan server = MakePlan({{"srv", Kind::windows, "server"}, {"ltsc", Kind::windows, "ltsc10"}, kWin11});
	Check(OrderText(server) == "prepare > install:ltsc > install:win11 > install:srv > finish", "order: LTSC 2021, Windows 11, Server 2025");

	Plan p = MakePlan({kWin11, kDebian});
	StartRule r = StartOf(p);
	Check(!r.dosFirst && !r.menuWaits && r.system == 0 && r.timeout == 5, "default entry: first of the list through Charon's menu, 5 seconds");
	Copy(p.defaultId, sizeof(p.defaultId), "debian");
	p.timeout = 0;
	r = StartOf(p);
	Check(r.system == 1 && r.timeout == 0, "default entry: the chosen system");
	Copy(p.defaultId, sizeof(p.defaultId), "menu");
	Check(StartOf(p).menuWaits && StartOf(p).system == -1, "default entry: menu waits");
	p = MakePlan({kWin11, kDebian, kDos});
	Copy(p.defaultId, sizeof(p.defaultId), "debian");
	r = StartOf(p);
	Check(r.dosFirst && r.system == 2 && !r.menuWaits, "default entry: CharonDOS in every combination that contains it");
	Check(ClockUtc(MakePlan({kWin11, kDebian})) && ClockUtc(MakePlan({kWin10, kWin11})) && ClockUtc(MakePlan({kDos, kWin11})), "clock: universal time for every plan of the spec's rule");
}

static std::string SayAllText(const Refusals& r, const Plan& p, bool pl, bool warnings)
{
	std::vector<char> b(8000);
	Out out(b.data(), b.size());
	SayAll(r, p, pl, warnings, out);
	return out.overflow ? "OVERFLOW" : std::string(b.data());
}
static void TestValidate()
{
	Machine m = GoodMachine();
	Refusals r;
	Plan p = MakePlan({kWin11, kDebian});
	ValidatePlan(p, m, r);
	Check(!r.Refused() && r.Has(Code::warnClearPassword), "Windows 11 + Debian accepted; the waiting password file is said");
	auto refused = [&](Plan plan, Machine machine, Code code, const char* what) {
		Refusals rr;
		ValidatePlan(plan, machine, rr);
		const std::string en = SayAllText(rr, plan, false, false), pl = SayAllText(rr, plan, true, false);
		Check(rr.Refused() && rr.Has(code) && !en.empty() && !pl.empty() && en != pl && en != "OVERFLOW", what);
	};
	refused(MakePlan({kDebian}), m, Code::tooFew, "one system is not a plan");
	refused(MakePlan({kDos, kDos, kWin11}), m, Code::twoCharonDos, "two CharonDOS refused");
	refused(MakePlan({kDebian, {"deb2", Kind::debian, nullptr}}), m, Code::sameDistroTwice, "two installer copies of one distribution refused");
	{
		Plan two = MakePlan({kImgDebian, {"idebian2", Kind::imageDebian, nullptr}});
		Copy(two.systems[1].image, sizeof(two.systems[1].image), "idebian");
		refused(two, m, Code::sameImageTwice, "two images of the same file refused");
		Refusals ok;
		ValidatePlan(MakePlan({kImgDebian, kImgUbuntu}), m, ok);
		Check(!ok.Refused(), "two different images accepted");
		Refusals mixed;
		ValidatePlan(MakePlan({kDebian, kImgDebian}), m, mixed);
		Check(!mixed.Refused(), "installer + image of one distribution accepted (the offered way)");
	}
	refused(MakePlan({kWin11, {"arch", Kind::imageArch, nullptr}}), m, Code::archImage, "Arch image refused in a plan");
	{
		Plan h = MakePlan({kWin11, kDebian});
		Copy(h.systems[1].host, sizeof(h.systems[1].host), "PC-WIN11");
		Refusals same;
		ValidatePlan(h, m, same);
		Check(!same.Refused() && !same.Has(Code::duplicateHost), "one identical computer name on two systems of a plan is accepted (user decision 2026-11-24)");
		Copy(h.systems[1].host, sizeof(h.systems[1].host), "pc-debian");
		Copy(h.systems[0].host, sizeof(h.systems[0].host), "a-name-longer-than-15");
		refused(h, m, Code::hostTooLong, "Windows computer name over 15 characters refused");
	}
	{
		Machine two = m;
		two.internalDisks = 2;
		refused(MakePlan({kWin11, kDebian}), two, Code::windowsManyDisks, "Windows with two internal disks refused");
		Refusals ok;
		ValidatePlan(MakePlan({kDebian, kImgUbuntu}), two, ok);
		Check(!ok.Refused(), "two internal disks without Windows accepted");
		Machine small = m;
		small.ramMb = 4096;
		refused(MakePlan({kUbuntu, kWin11}), small, Code::ubuntuRam, "Ubuntu installer under 8 GB refused");
		Machine self = m;
		self.startMediumIsTarget = true;
		refused(MakePlan({kWin11, kDebian}), self, Code::startMediumIsTarget, "start medium is the target and not running from memory: refused");
		self.runningFromRam = true;
		Refusals ram;
		ValidatePlan(MakePlan({kWin11, kDebian}), self, ram);
		Check(!ram.Refused(), "start medium is the target, running from memory: accepted (RAM disk requirement)");
	}
	{
		Plan q = MakePlan({kWin11, kDebian});
		q.systems[0].sizeGb = 50;
		refused(q, m, Code::sizeBelowMinimum, "Windows 11 at 50 GB refused (minimum 64)");
		q = MakePlan({kDebian, kImgUbuntu});
		q.data = DataFs::ntfs;
		refused(q, m, Code::dataNtfsNeedsWindows, "NTFS data partition without Windows refused");
		q = MakePlan({kWin11, kDebian});
		Copy(q.defaultId, sizeof(q.defaultId), "fedora");
		refused(q, m, Code::badDefault, "default system that is not in the list refused");
		Copy(q.defaultId, sizeof(q.defaultId), "");
		Copy(q.rest, sizeof(q.rest), "data");
		refused(q, m, Code::badRest, "rest=data without a data partition refused");
		Plan seven = MakePlan({kWin11, kDebian});
		seven.count = 7;
		Refusals rr;
		ValidatePlan(seven, m, rr);
		Check(rr.Has(Code::tooMany), "more than six systems refused");
	}
	{
		Machine sb = m;
		sb.secureBoot = true;
		sb.network = false;
		Plan q = MakePlan({kDos, kImgFedora});
		q.systems[1].speaks = false;
		Refusals rr;
		ValidatePlan(q, sb, rr);
		const std::string w = SayAllText(rr, q, false, true), wp = SayAllText(rr, q, true, true);
		Check(!rr.Refused() && rr.Has(Code::warnSecureBoot) && rr.Has(Code::warnDosSecureBoot) && rr.Has(Code::warnNoNetwork) && rr.Has(Code::warnSilent)
			&& Has(w, "Charon bypasses nothing") && Has(w, "does not speak") && Has(wp, "Secure Boot jest w"), "Secure Boot, no network, silent system: warned, not refused, nothing bypassed");
		q.secureBootRefuse = true;
		Refusals r2;
		ValidatePlan(q, sb, r2);
		Check(r2.Refused() && r2.Has(Code::secureBootRefused), "secure_boot=refuse-if-on refuses");
	}
}

// ---- layout -----------------------------------------------------------------------
static DiskState EmptyDisk(uint64_t gb, uint32_t ss = 512)
{
	DiskState d = {};
	d.sectorSize = ss;
	d.sectors = gb * 1024 * 1024 * 1024 / ss;
	d.blank = true;
	Copy(d.serial, sizeof(d.serial), "S6XYNX0R123456");
	Copy(d.model, sizeof(d.model), "Samsung SSD 980 PRO");
	return d;
}
// A 256 GB disk as Windows leaves it shrunk: EFI 100 MB, MSR 16 MB, Windows 120 GB, a gap, recovery 1 GB at the end.
static DiskState WindowsDisk(uint64_t espFreeMb)
{
	DiskState d = EmptyDisk(256);
	const uint64_t mib = 2048;
	d.blank = false;
	d.gpt = true;
	d.firstUsable = 34;
	d.lastUsable = d.sectors - 34;
	d.entriesFree = 124;
	d.espFreeKnown = true;
	d.espFreeMb = espFreeMb;
	d.parts[0] = {mib, 101 * mib - 1, PartType::efiSystem};
	d.parts[1] = {101 * mib, 117 * mib - 1, PartType::microsoftReserved};
	d.parts[2] = {117 * mib, (117 + 120 * 1024) * mib - 1, PartType::basicData};
	d.parts[3] = {(255 * 1024) * mib, (256 * 1024 - 1) * mib - 1, PartType::windowsRecovery};
	d.count = 4;
	return d;
}
static std::string LayoutText(const Plan& p, const Layout& l)
{
	std::string t;
	for (int i = 0; i < l.count; i++) {
		const Range& r = l.ranges[i];
		if (i)
			t += " ";
		t += RoleWord(r.role);
		if (r.system >= 0)
			t += std::string(":") + p.systems[r.system].id;
		t += "=" + std::to_string(RangeMb(l, r));
	}
	return t;
}
// Every range on 1 MiB, inside the plan's space, no overlap with each other or with existing partitions.
static bool Sound(const DiskState& d, const Layout& l)
{
	const uint64_t mib = 1024 * 1024 / d.sectorSize;
	for (int i = 0; i < l.count; i++) {
		const Range& r = l.ranges[i];
		if (r.firstLba % mib || r.lbaCount % mib || r.lbaCount == 0 || r.firstLba < mib || r.firstLba + r.lbaCount > d.sectors - mib)
			return false;
		for (int k = 0; k < i; k++)
			if (r.firstLba < l.ranges[k].firstLba + l.ranges[k].lbaCount && l.ranges[k].firstLba < r.firstLba + r.lbaCount)
				return false;
		if (!l.newTable)
			for (int k = 0; k < d.count; k++)
				if (r.firstLba <= d.parts[k].lastLba && d.parts[k].firstLba < r.firstLba + r.lbaCount)
					return false;
	}
	return true;
}
static void TestLayout()
{
	struct Case { const char* name; Plan plan; uint64_t gb; const char* want; };
	const Case cases[] = {
		{"Windows 11 + Debian, empty 200 GB", MakePlan({kWin11, kDebian}), 200, "esp=1024 msr=16 system:win11=102400 system:debian=101358"},
		{"CharonDOS + Windows 11, empty 200 GB", MakePlan({kDos, kWin11}), 200, "dos-esp:dos=1536 msr=16 system:win11=203246"},
		{"CharonDOS + Debian, empty 100 GB", MakePlan({kDos, kDebian}), 100, "dos-esp:dos=1536 system:debian=100862"},
		{"CharonDOS + Debian image, empty 64 GB", MakePlan({kDos, kImgDebian}), 64, "dos-esp:dos=1536 system:idebian=63998"},
		{"CharonDOS + Windows 11 + Debian, empty 512 GB", MakePlan({kDos, kWin11, kDebian}), 512, "dos-esp:dos=1536 msr=16 system:win11=102400 system:debian=420334"},
		{"Debian + Ubuntu, empty 120 GB", MakePlan({kDebian, kUbuntu}), 120, "esp=1024 system:debian=40960 system:ubuntu=80894"},
		{"Windows 10 + Windows 11, empty 256 GB", MakePlan({kWin11, kWin10}), 256, "esp=1024 msr=16 system:win10=158702 system:win11=102400"},
		{"Debian installer + Ubuntu image, empty 100 GB", MakePlan({kImgUbuntu, kDebian}), 100, "esp=1024 system:debian=70654 system:iubuntu=30720"},
		{"six systems, empty 1 TB", MakePlan({kUbuntu, kImgFedora, kWin11, kDebian, kDos, kWin10}), 1024,
			"dos-esp:dos=1536 msr=16 system:win10=831982 system:win11=102400 system:ubuntu=40960 system:debian=40960 system:ifedora=30720"},
		// r410 (agent 2's run hvxp1 stopped: "the range of this system is not in the layout"): the hypervisor has a range.
		{"Charon Hypervisor alone, empty 120 GB", MakePlan({kHv}), 120, "esp=1024 system:hv=121854"},
		{"Windows 11 + Charon Hypervisor, empty 200 GB", MakePlan({kWin11, kHv}), 200, "esp=1024 msr=16 system:win11=102400 system:hv=101358"},
		{"Debian + Charon Hypervisor, empty 120 GB", MakePlan({kDebian, kHv}), 120, "esp=1024 system:debian=40960 system:hv=80894"},
		{"Windows 11 + Debian + Charon Hypervisor, empty 256 GB", MakePlan({kWin11, kDebian, kHv}), 256, "esp=1024 msr=16 system:win11=102400 system:debian=40960 system:hv=117742"},
		{"CharonDOS + Charon Hypervisor, empty 120 GB", MakePlan({kDos, kHv}), 120, "dos-esp:dos=1536 system:hv=121342"},
	};
	for (const Case& c : cases) {
		DiskState d = EmptyDisk(c.gb);
		Layout l;
		Refusals r;
		const bool ok = ComputeLayout(c.plan, 1, d, Existing::erase, l, r);
		const std::string got = ok ? LayoutText(c.plan, l) : SayAllText(r, c.plan, false, false);
		uint64_t sum = 0;
		for (int i = 0; i < l.count; i++)
			sum += RangeMb(l, l.ranges[i]);
		Check(ok && got == c.want && Sound(d, l) && l.newTable && sum == c.gb * 1024 - 2 && l.ranges[0].firstLba == 2048 && l.freeMb == 0,
			std::string("layout: ") + c.name + " -> " + got);
	}
	// (The rest goes to the LAST SYSTEM OF THE LIST, wherever it lies on the disk: Windows 10 above, Debian
	// in "Debian installer + Ubuntu image".)
	// Proportional raise when the defaults do not fit; minimum refusal says how much is missing.
	{
		Plan p = MakePlan({kWin11, kDebian});
		DiskState d = EmptyDisk(100);
		Layout l;
		Refusals r;
		const bool fits = ComputeLayout(p, 1, d, Existing::erase, l, r);
		Check(fits && LayoutText(p, l) == "esp=1024 msr=16 system:win11=78773 system:debian=22585" && Sound(d, l),
			"layout: 100 GB, both raised toward their defaults in proportion, the rounding rest to the last system -> " + LayoutText(p, l));
		d = EmptyDisk(70);
		Refusals small;
		const bool ok = ComputeLayout(p, 1, d, Existing::erase, l, small);
		const std::string text = SayAllText(small, p, false, false), pl = SayAllText(small, p, true, false);
		Check(!ok && small.Has(Code::tooSmall) && small.list[0].n2 == 1024 + 16 + 65536 + 12288 && small.list[0].n1 == 78864 - (70 * 1024 - 2)
			&& Has(text, "7186 megabytes are missing") && Has(pl, "brakuje 7186 megabajt"), "layout: 70 GB refused, missing amount spoken: " + text);
	}
	// 2 TB and 4K sectors.
	{
		Plan p = MakePlan({kWin11, kDebian});
		DiskState d = EmptyDisk(2048, 4096);
		Layout l;
		Refusals r;
		Check(ComputeLayout(p, 1, d, Existing::erase, l, r) && Sound(d, l) && l.ranges[0].firstLba == 256 && RangeMb(l, l.ranges[0]) == 1024, "layout: 4K-native disk, Windows + Debian, 1 MiB boundaries");
		Plan q = MakePlan({kDos, kImgDebian});
		Refusals rr;
		Check(!ComputeLayout(q, 1, d, Existing::erase, l, rr) && rr.count == 2 && rr.Has(Code::sectorSize), "layout: CharonDOS and an image on a 4K-native disk refused, both named");
		d.sectorSize = 520;
		Refusals bad;
		Check(!ComputeLayout(p, 1, d, Existing::erase, l, bad) && bad.Has(Code::badSectorSize), "layout: unsupported sector size refused");
	}
	// Options: data partition only when asked, rest, typed sizes, recovery, ESP size.
	{
		Plan p = MakePlan({kWin11, kDebian});
		DiskState d = EmptyDisk(512);
		Layout l;
		Refusals r;
		ComputeLayout(p, 1, d, Existing::erase, l, r);
		bool data = false;
		for (int i = 0; i < l.count; i++)
			data = data || l.ranges[i].role == Role::data;
		Check(!data, "layout: no shared data partition unless asked (decision D7)");
		p.data = DataFs::exfat;
		Check(ComputeLayout(p, 1, d, Existing::erase, l, r) && LayoutText(p, l) == "esp=1024 msr=16 system:win11=102400 system:debian=40960 data=379886",
			"layout: data partition asked: last, gets the rest -> " + LayoutText(p, l));
		p.dataSizeGb = 50;
		Check(ComputeLayout(p, 1, d, Existing::erase, l, r) && LayoutText(p, l) == "esp=1024 msr=16 system:win11=102400 system:debian=369646 data=51200",
			"layout: data partition of a typed size, rest to the last system");
		Copy(p.rest, sizeof(p.rest), "win11");
		Check(ComputeLayout(p, 1, d, Existing::erase, l, r) && LayoutText(p, l) == "esp=1024 msr=16 system:win11=431086 system:debian=40960 data=51200", "layout: rest to a named system");
		Copy(p.rest, sizeof(p.rest), "free");
		Check(ComputeLayout(p, 1, d, Existing::erase, l, r) && LayoutText(p, l) == "esp=1024 msr=16 system:win11=102400 system:debian=40960 data=51200 spare=328686"
			&& l.freeMb == 328686 && l.ranges[5].placeholder, "layout: rest=free with Debian's installer: the free tail is covered by a spare placeholder (one gap)");
		Plan images = MakePlan({kWin11, kImgDebian});
		Copy(images.rest, sizeof(images.rest), "free");
		Check(ComputeLayout(images, 1, d, Existing::erase, l, r) && LayoutText(images, l) == "esp=1024 msr=16 system:win11=102400 system:idebian=30720" && l.freeMb == 390126,
			"layout: rest=free without a gap-taking installer: simply left free");
		p = MakePlan({kWin11, kDebian});
		p.systems[0].sizeGb = 150;
		p.systems[0].winre = true;
		p.disks[0].present = true;
		p.disks[0].espSizeMb = 600;
		Check(ComputeLayout(p, 1, d, Existing::erase, l, r) && LayoutText(p, l) == "esp=600 msr=16 system:win11=153600 winre:win11=1024 system:debian=369046",
			"layout: typed size, recovery partition right after its Windows, ESP of 600 MB -> " + LayoutText(p, l));
		Check(l.ranges[2].placeholder && l.ranges[3].placeholder && l.ranges[4].placeholder && !l.ranges[0].placeholder && !l.ranges[1].placeholder,
			"layout: system and recovery ranges are placeholders, EFI and MSR are final");
		uint32_t lo = 0, mid = 0, hi = 0;
		p.systems[0].sizeGb = 0;
		Check(SizeBounds(p, 1, d, Existing::erase, 1, lo, mid, hi) && lo == 12 && mid == 410 && hi == 446, "size question bounds for Debian: smallest 12, proposed, largest possible");
		p.disks[0].espSizeMb = 300;
		Refusals esp;
		Check(!ComputeLayout(p, 1, d, Existing::erase, l, esp) && esp.Has(Code::espTooSmall), "layout: EFI partition under 550 MB refused");
		Plan dos = MakePlan({kDos, kWin11});
		dos.systems[0].needMb = 4096 + 1024 + 1024;
		Check(ComputeLayout(dos, 1, d, Existing::erase, l, r) && RangeMb(l, l.ranges[0]) == 1024 + 6144, "layout: CharonDOS with a Windows XP profile: 1 GiB + its own need");
	}
	// A disk whose content is kept.
	{
		Plan p = MakePlan({kDebian, kImgUbuntu});
		DiskState d = WindowsDisk(200);
		Layout l;
		Refusals r;
		const bool ok = ComputeLayout(p, 1, d, Existing::keep, l, r);
		Check(ok && !l.newTable && l.espExisting && l.espFirstLba == 2048 && LayoutText(p, l) == "system:debian=40960 system:iubuntu=97163" && Sound(d, l)
			&& l.ranges[0].firstLba == (117 + 120 * 1024) * 2048ull, "layout: existing Windows kept: free range only, its EFI partition shared -> " + LayoutText(p, l));
		Plan w = MakePlan({kWin11, kDebian});
		Check(ComputeLayout(w, 1, d, Existing::keep, l, r) && LayoutText(w, l) == "system:win11=99454 system:debian=38669" && Sound(d, l),
			"layout: kept disk that already has an MSR gets no second one");
		Check(PositionNumber(d, l, 0, true) == 4 && PositionNumber(d, l, 0, false) == 3 && PositionNumber(d, l, 1, true) == 5,
			"position number of the Windows partition among existing ones, with and without the MSR counted");
		Plan dos = MakePlan({kDos, kDebian});
		Check(ComputeLayout(dos, 1, d, Existing::keep, l, r) && LayoutText(dos, l) == "dos:dos=1024 system:debian=137099" && l.espExisting,
			"layout: CharonDOS beside a kept system is an ordinary partition; the existing EFI partition stays the shared one");
		DiskState full = WindowsDisk(30);
		Refusals noRoom;
		Check(!ComputeLayout(w, 1, full, Existing::keep, l, noRoom) && noRoom.Has(Code::existingEspFull), "layout: existing EFI partition too full and Windows in the plan: refused");
		Refusals second;
		Check(ComputeLayout(p, 1, full, Existing::keep, l, second) && l.secondEsp && !l.espExisting && second.Has(Code::warnSecondEsp) && !second.Refused()
			&& LayoutText(p, l) == "esp=1024 system:debian=40960 system:iubuntu=96139", "layout: existing EFI partition too full, Linux only: own EFI partition and a spoken note");
		Refusals tooSmall;
		Plan big = MakePlan({kWin11, kWin10, kDebian, kUbuntu});
		DiskState roomy = WindowsDisk(400);
		Check(!ComputeLayout(big, 1, roomy, Existing::keep, l, tooSmall) && tooSmall.Has(Code::tooSmall), "layout: kept disk without enough free space refused");
		DiskState mbr = WindowsDisk(200);
		mbr.gpt = false;
		Refusals notGpt;
		Check(!ComputeLayout(p, 1, mbr, Existing::keep, l, notGpt) && notGpt.Has(Code::notGpt), "layout: keeping a disk without GPT refused");
		Check(ComputeLayout(p, 1, mbr, Existing::erase, l, r) && l.newTable, "layout: the same disk erased gets a new table");
		// Two gaps and Debian's installer: the other gap is covered.
		DiskState gaps = WindowsDisk(200);
		gaps.parts[2].firstLba = (117 + 20 * 1024) * 2048ull; // 20 GB free before Windows
		Plan deb = MakePlan({kDebian, kImgUbuntu});
		Check(ComputeLayout(deb, 1, gaps, Existing::keep, l, r) && LayoutText(deb, l) == "system:debian=40960 system:iubuntu=97163 spare=20480" && Sound(gaps, l),
			"layout: a second free range on a kept disk is covered while Debian's installer is in the plan");
		DiskState none = WindowsDisk(200);
		none.parts[2].lastLba = none.parts[3].firstLba - 1;
		Refusals noFree;
		Check(!ComputeLayout(p, 1, none, Existing::keep, l, noFree) && (noFree.Has(Code::tooSmall) || noFree.Has(Code::noFreeSpace)), "layout: kept disk with no free space refused");
		DiskState table = WindowsDisk(200);
		table.entriesFree = 1;
		Refusals tf;
		Check(!ComputeLayout(p, 1, table, Existing::keep, l, tf) && tf.Has(Code::tableFull), "layout: partition table without free entries refused");
	}
	// Two disks, one system each (no Windows).
	{
		Plan p = MakePlan({kDebian, kImgUbuntu});
		p.systems[1].disk = 2;
		DiskState a = EmptyDisk(100), b = EmptyDisk(64);
		Layout la, lb;
		Refusals r;
		Check(ComputeLayout(p, 1, a, Existing::erase, la, r) && ComputeLayout(p, 2, b, Existing::erase, lb, r)
			&& LayoutText(p, la) == "esp=1024 system:debian=101374" && LayoutText(p, lb) == "esp=1024 system:iubuntu=64510", "layout: one system per disk, each disk with its own EFI partition");
	}
	// Store round trip and placeholder names.
	{
		Plan p = MakePlan({kDos, kWin11, kDebian});
		p.systems[1].winre = true;
		DiskState d = EmptyDisk(512);
		Layout l, back;
		Refusals r;
		ComputeLayout(p, 1, d, Existing::erase, l, r);
		for (int i = 0; i < l.count; i++)
			for (int k = 0; k < 16; k++)
				l.ranges[i].guid[k] = uint8_t(i * 16 + k + 1);
		std::vector<char> buf(16384);
		Store store(buf.data(), buf.size());
		Check(LayoutToStore(p, l, 1, store) && LayoutFromStore(store, p, 1, back) && back.count == l.count && LayoutText(p, back) == LayoutText(p, l)
			&& memcmp(back.ranges[3].guid, l.ranges[3].guid, 16) == 0 && back.ranges[2].placeholder && back.newTable, "layout survives the plan file (ranges, GUIDs, flags)");
		char name[64];
		Out n(name, sizeof(name));
		PlaceholderName(p, l.ranges[2], 2, n);
		Check(std::string(name) == "Charon plan 2: Windows 11", std::string("placeholder name: ") + name);
		bool remove = false;
		PartType type = PartType::other;
		BeforeStep(Kind::windows, Role::system, remove, type);
		const bool w = !remove && type == PartType::basicData;
		BeforeStep(Kind::debian, Role::system, remove, type);
		const bool deb = remove;
		BeforeStep(Kind::ubuntu, Role::system, remove, type);
		// r410 (bd): Ubuntu's installer in a plan uses autoinstall layout mode use_gap (subiquity storage.py:
		// gaps.largest_gap), so its placeholder is removed like Debian's - no longer retyped.
		const bool ubu = remove;
		BeforeStep(Kind::imageDebian, Role::system, remove, type);
		Check(w && deb && ubu && remove, "before a step: Windows retyped; Debian's and Ubuntu's installers and images get the single gap");
		std::vector<char> text(2000);
		Out say(text.data(), text.size());
		SayLayout(p, l, false, say);
		Check(Has(text.data(), "1, CharonDOS, 1536 megabytes, it is also the EFI partition. 2, Windows 11, 100 gigabytes."), std::string("layout spoken: ") + text.data());
	}
}

// ---- the answer-set file --------------------------------------------------------------
static const char* kFile =
	"\xEF\xBB\xBF[charon-answers]\r\nformat=1\r\n\r\n"
	"[plan]\r\nversion=1\r\nname=classroom-b\r\nsystems=dos,win11,debian          ; order of the list\r\n"
	"default=dos\r\ntimeout=5\r\non_failure=retry-once-then-stop\r\nleftover=free\r\ndata=exfat\r\ndata_size_gb=0\r\nrest=data\r\n"
	"report=esp,systems\r\nsecure_boot=warn\r\nunattended=yes\r\nconfirm=typed\r\n"
	"# a comment\r\n"
	"[disk.1]                          ; pre-authorisation\r\nSerial = S6XYNX0R123456\r\nmodel=Samsung SSD 980*\r\nmin_size_gb=400\r\nmax_size_gb=600\r\nexisting=erase\r\nesp_size_mb=1024\r\n"
	"[shared]\r\nuser=anna\r\npassword=ta jna ; x\r\nlocale=pl_PL\r\nhostpattern=sala-b\r\n"
	"[system.dos]\r\nkind=charondos\r\nprograms=all\r\n"
	"[system.win11]\r\nkind=windows\r\ndisk=1\r\nsize_gb=100\r\nwinre=no\r\nversion=11\r\nlocale=en_US\r\n"
	"[system.debian]\r\nkind=debian\r\ndisk=1\r\nsize_gb=40\r\nswap_gb=2\r\n"
	"[machine serial=PF2ABCDE]\r\nshared.hostname=lab-07\r\n";
static void TestFile()
{
	std::vector<char> buf(32768);
	Store store(buf.data(), buf.size());
	Refusals r;
	Check(ParseIni(kFile, strlen(kFile), store, r) && r.count == 0, "answer file parsed (BOM, CR LF, comments, inline comments)");
	Check(std::string(store.GetOr("plan", "systems", "")) == "dos,win11,debian" && std::string(store.GetOr("disk.1", "serial", "")) == "S6XYNX0R123456"
		&& std::string(store.GetOr("shared", "password", "")) == "ta jna ; x" && store.Has("machine serial=PF2ABCDE", "shared.hostname"),
		"values: inline comment cut, key case folded, a password taken as written, machine section kept verbatim");
	Check(std::string(Resolve(store, "win11", "locale")) == "en_US" && std::string(Resolve(store, "debian", "locale")) == "pl_PL" && std::string(Resolve(store, "dos", "missing", "d")) == "d",
		"per-system answer overrides the shared one");
	Plan p;
	Clear(p);
	PlanFromStore(store, p, r);
	Check(!r.Refused() && p.count == 3 && p.systems[1].kind == Kind::windows && p.systems[1].sizeGb == 100 && p.systems[2].swapGb == 2 && p.unattended
		&& p.onFailure == OnFailure::retryOnceThenStop && p.leftover == Leftover::free && p.data == DataFs::exfat && std::string(p.rest) == "data"
		&& p.disks[0].present && p.disks[0].existing == Existing::erase && p.disks[0].minGb == 400 && std::string(p.systems[1].host) == "sala-b"
		&& std::string(p.systems[0].name) == "CharonDOS", "plan built from [plan], [disk.1], [shared], [system.id]");
	Check(r.count == 0, "known keys (report, mode ...) are not reported; per-system answers are left to the question catalogue: " + SayAllText(r, p, false, true));
	{
		std::vector<char> b3(8192);
		Store s3(b3.data(), b3.size());
		Refusals pr, cr;
		const char* shortcut = "[plan]\nsystems=a,b\nunattended=yes\nconfirm=disk-match\n[system.a]\nkind=windows\n[system.b]\nkind=debian\n";
		ParseIni(shortcut, strlen(shortcut), s3, pr);
		Plan sp;
		Clear(sp);
		PlanFromStore(s3, sp, cr);
		Check(cr.Refused() && cr.Has(Code::badValue) && Has(SayAllText(cr, sp, false, false), "confirm"),
			"confirm=disk-match is refused: the typed word is always required, the serial must match as well");
	}
	Refusals v;
	ValidatePlan(p, GoodMachine(), v);
	Check(!v.Refused(), "the spec's example plan passes the pre-flight: " + SayAllText(v, p, false, false));

	// Disk pre-authorisation.
	DiskState disks[3] = {EmptyDisk(512), EmptyDisk(512), EmptyDisk(256)};
	Copy(disks[1].serial, sizeof(disks[1].serial), "OTHER0001");
	Copy(disks[2].serial, sizeof(disks[2].serial), "OTHER0002");
	int index[kMaxDiskRules] = {9, 9, 9, 9};
	Refusals m;
	Check(AuthoriseDisks(p, disks, 3, index, m) && index[0] == 0 && m.count == 0, "serial matches exactly one disk: authorised");
	Copy(disks[0].serial, sizeof(disks[0].serial), "  s6xynx0r123456 ");
	Check(AuthoriseDisks(p, disks, 3, index, m) && index[0] == 0, "serial compared without case and outer blanks");
	Copy(disks[0].serial, sizeof(disks[0].serial), "S6XYNX0R999999");
	index[0] = 9;
	Refusals no;
	Check(!AuthoriseDisks(p, disks, 3, index, no) && no.Has(Code::diskNoMatch) && index[0] == 9 && Has(SayAllText(no, p, false, false), "Nothing has been changed")
		&& Has(SayAllText(no, p, true, false), "Nic nie zosta"), "serial mismatch: refused, nothing touched, output unchanged, spoken EN and PL");
	Copy(disks[0].serial, sizeof(disks[0].serial), "S6XYNX0R123456");
	Copy(disks[1].serial, sizeof(disks[1].serial), "S6XYNX0R123456");
	Refusals many;
	Check(!AuthoriseDisks(p, disks, 3, index, many) && many.Has(Code::diskManyMatch), "two disks with the serial: refused");
	Copy(disks[1].serial, sizeof(disks[1].serial), "OTHER0001");
	Copy(disks[0].model, sizeof(disks[0].model), "WDC Blue");
	Refusals model;
	Check(!AuthoriseDisks(p, disks, 3, index, model) && model.Has(Code::diskNoMatch), "serial right, model wrong: refused");
	Copy(disks[0].model, sizeof(disks[0].model), "samsung ssd 980 PRO 1TB");
	disks[0].sectors = EmptyDisk(1024).sectors;
	Refusals size;
	Check(!AuthoriseDisks(p, disks, 3, index, size) && size.Has(Code::diskNoMatch), "serial right, size above max_size_gb: refused");
	disks[0].sectors = EmptyDisk(512).sectors;
	Plan q = p;
	q.disks[0].existing = Existing::refuseIfNotEmpty;
	disks[0].blank = false;
	Refusals ne;
	Check(!AuthoriseDisks(q, disks, 3, index, ne) && ne.Has(Code::diskNotEmpty), "existing=refuse-if-not-empty on a used disk: refused");
	disks[0].blank = true;
	q.disks[0].serial[0] = 0;
	Refusals ns;
	Check(!AuthoriseDisks(q, disks, 3, index, ns) && ns.Has(Code::noSerial), "disk section without a serial: refused (a model or a size alone never authorises)");
	q.disks[0].present = false;
	Refusals nd;
	Check(!AuthoriseDisks(q, disks, 3, index, nd) && nd.Has(Code::noDiskSection), "no disk section, no run");

	// Every error is collected, not the first only.
	const char* bad =
		"[charon-answers]\nformat=2\n[plan]\nsystems=a,b,c\non_failure=explode\nsecure_boot=bypass\ntimeout=900\nfuture_key=1\nthis line is wrong\n"
		"[disk.1]\nserial=X\nshrink_windows=yes\nexisting=burn\nesp_size_mb=100\n[system.a]\nkind=windows\n[system.b]\nkind=guided\n[broken\n";
	std::vector<char> b2(8192);
	Store s2(b2.data(), b2.size());
	Refusals e;
	const bool parsed = ParseIni(bad, strlen(bad), s2, e);
	Plan bp;
	Clear(bp);
	PlanFromStore(s2, bp, e);
	Check(!parsed && e.Refused() && e.Has(Code::iniSyntax) && e.Has(Code::formatNewer) && e.Has(Code::badValue) && e.Has(Code::unknownKind) && e.Has(Code::missingKind)
		&& e.Has(Code::shrinkUnsupported) && e.Has(Code::espTooSmall) && e.Has(Code::warnIgnoredKey) && e.count >= 11,
		"a faulty file: all " + std::to_string(e.count) + " problems collected; there is no value that bypasses Secure Boot");
	const std::string en = SayAllText(e, bp, false, false), pl = SayAllText(e, bp, true, false);
	Check(Has(en, "line 9") && Has(en, "line 19") && Has(en, "format 2") && Has(en, "guided") && Has(pl, "wiersz 9") && en != "OVERFLOW", "every error spoken with its line or its key");
	Refusals none;
	Store s3(b2.data(), b2.size());
	s3.Wipe();
	Plan np;
	Clear(np);
	PlanFromStore(s3, np, none);
	Check(none.Has(Code::noSystems), "a file without a list of systems refused");

	// Store behaviour.
	char tiny[40];
	Store t(tiny, sizeof(tiny));
	Check(t.Put("s", "k", "0123456789") && !t.Put("s", "k2", "0123456789012345678901234567890123456789") && t.overflow && std::string(t.Get("s", "k")) == "0123456789" && t.Count() == 1,
		"store: a value that does not fit is refused, the store unchanged, overflow flagged");
	t.Put("s", "k", "new");
	Check(std::string(t.Get("s", "k")) == "new" && t.Count() == 1, "store: put replaces");
	t.Wipe();
	bool zero = true;
	for (char c : tiny)
		zero = zero && c == 0;
	Check(zero && t.Count() == 0, "store: wipe zeroes every byte");
}

// ---- fake storage -----------------------------------------------------------------------
struct Fake {
	std::map<std::string, std::string> files;
	std::vector<uint8_t> variable;
	int failWrites = 0; // fail the Nth write from now (1 = next)
	static bool Read(void* c, const char* path, char* buffer, size_t capacity, size_t* length)
	{
		Fake& f = *static_cast<Fake*>(c);
		auto it = f.files.find(path);
		if (it == f.files.end() || it->second.size() > capacity)
			return false;
		memcpy(buffer, it->second.data(), it->second.size());
		*length = it->second.size();
		return true;
	}
	static bool Write(void* c, const char* path, const char* data, size_t length)
	{
		Fake& f = *static_cast<Fake*>(c);
		if (f.failWrites && --f.failWrites == 0) {
			f.files[path] = std::string(data, length / 2); // torn
			return false;
		}
		f.files[path] = std::string(data, length);
		return true;
	}
	static bool Append(void* c, const char* path, const char* data, size_t length)
	{
		static_cast<Fake*>(c)->files[path] += std::string(data, length);
		return true;
	}
	static bool Rename(void* c, const char* from, const char* to)
	{
		Fake& f = *static_cast<Fake*>(c);
		if (!f.files.count(from) || f.files.count(to))
			return false;
		f.files[to] = f.files[from];
		f.files.erase(from);
		return true;
	}
	static bool Remove(void* c, const char* path)
	{
		static_cast<Fake*>(c)->files.erase(path);
		return true;
	}
	static bool Exists(void* c, const char* path) { return static_cast<Fake*>(c)->files.count(path) != 0; }
	static bool GetVar(void* c, uint8_t* buffer, size_t capacity, size_t* length)
	{
		Fake& f = *static_cast<Fake*>(c);
		if (f.variable.empty() || f.variable.size() > capacity)
			return false;
		memcpy(buffer, f.variable.data(), f.variable.size());
		*length = f.variable.size();
		return true;
	}
	static bool SetVar(void* c, const uint8_t* data, size_t length)
	{
		static_cast<Fake*>(c)->variable.assign(data, data + length);
		return true;
	}
	Storage Bind() { return Storage{this, Read, Write, Append, Rename, Remove, Exists, GetVar, SetVar}; }
};
static std::string Marker(int step, bool failed)
{
	char path[96];
	Out out(path, sizeof(path));
	MarkerPath(step, failed, out);
	return path;
}

struct World {
	Plan plan;
	Run run;
	Fake fake;
	Storage storage;
	std::vector<char> storeBytes, work;
	Store store;
	uint8_t esp[16];
	World(Plan p) : plan(p), storeBytes(65536), work(131072), store(storeBytes.data(), storeBytes.size())
	{
		storage = fake.Bind();
		for (int i = 0; i < 16; i++)
			esp[i] = uint8_t(0xE0 + i);
		Begin(plan, run, 1000);
		store.Put("shared", "user", "anna");
		store.Put("shared", "password", "tajne-haslo");
	}
	World(const World& o) : plan(o.plan), run(o.run), fake(o.fake), storeBytes(o.storeBytes.size()), work(o.work.size()), store(storeBytes.data(), storeBytes.size())
	{
		storage = fake.Bind();
		memcpy(esp, o.esp, 16);
		for (size_t i = 0; i < o.store.Count(); i++) {
			const char* a = nullptr;
			const char* b = nullptr;
			const char* c = nullptr;
			o.store.At(i, a, b, c);
			store.Put(a, b, c);
		}
	}
	Io Save() { return hakefi::plan::Save(storage, plan, run, store, work.data(), work.size(), esp); }
	// What a restart does: everything in memory is gone, the plan is read from the storage.
	Io Restart()
	{
		Clear(plan);
		memset(&run, 0xEE, sizeof(run));
		store.Wipe();
		Refusals r;
		return Load(storage, work.data(), work.size(), store, plan, run, r);
	}
};
static std::string AnnounceText(const World& w, bool pl = false)
{
	char text[600];
	Out out(text, sizeof(text));
	Announce(w.plan, w.run, pl, out);
	return text;
}

static void TestPlanFile()
{
	World w(MakePlan({kDos, kWin11, kDebian}));
	w.plan.data = DataFs::exfat;
	Copy(w.plan.defaultId, sizeof(w.plan.defaultId), "debian");
	w.plan.disks[0].present = true;
	Copy(w.plan.disks[0].serial, sizeof(w.plan.disks[0].serial), "S6XY");
	w.plan.disks[0].existing = Existing::erase;
	// RAM disk requirement: the plan exists only in memory while the disk changes ...
	std::vector<char> memory(65536);
	PlanToStore(w.plan, w.store);
	RunToStore(w.plan, w.run, w.store);
	Out held(memory.data(), memory.size());
	Check(Serialize(w.store, held) && w.fake.files.empty(), "plan serialised in memory before the disk changes; nothing on any volume yet");
	Check(!Has(memory.data(), "tajne-haslo") && Has(memory.data(), "user=anna"), "the password is not in the plan text; other answers are");
	size_t body = 0;
	Check(Checked(memory.data(), held.length, body) && body == held.length - 19, "checksum line verifies");
	memory[200] ^= 1;
	Check(!Checked(memory.data(), held.length, body), "a changed byte fails the checksum");
	// ... and is written to the NEW EFI partition as the first act after the layout exists.
	MarkLaunched(w.run, 0, 1010);
	Check(w.Save() == Io::ok && w.fake.files.count(kPlanFile) && !w.fake.files.count(kPlanNew) && w.fake.variable.size() == kPointerBytes, "first save: plan.txt and the pointer variable");
	Pointer ptr;
	if (ReadPointer(w.storage, ptr) && gVerbose)
		printf("pointer: id %08x step %u state %u sequence %u guid15 %02x\n", ptr.planId, ptr.step, ptr.state, ptr.sequence, ptr.espGuid[15]);
	Check(ReadPointer(w.storage, ptr) && ptr.planId == 0x1a2b3c4d && ptr.step == 0 && ptr.state == 4 && ptr.sequence == 1 && ptr.espGuid[15] == 0xEF
		&& w.fake.variable.size() < 64, "pointer: plan id, EFI partition GUID, step, state; under 64 bytes, no answers");
	const Plan saved = w.plan;
	const Run savedRun = w.run;
	Check(w.Restart() == Io::ok && w.plan.count == 3 && w.plan.id == saved.id && w.plan.data == DataFs::exfat && std::string(w.plan.defaultId) == "debian"
		&& std::string(w.plan.systems[1].name) == "Windows 11" && std::string(w.plan.systems[1].host) == "pc-win11" && w.plan.disks[0].existing == Existing::erase
		&& w.run.count == savedRun.count && w.run.sequence == 1 && w.run.steps[0].state == StepState::launched && w.run.steps[0].first == 1010 && w.run.started == 1000
		&& w.run.steps[2].system == 1 && std::string(w.store.GetOr("shared", "user", "")) == "anna" && !w.store.Has("shared", "password"),
		"round trip: plan, run and answers come back; the password does not");
	// Second save keeps the previous file as plan.bak.
	MarkDone(w.run, 0, 1020);
	w.Save();
	Check(w.fake.files.count(kPlanBackup) && w.run.sequence == 2, "second save: the older state stays as plan.bak");
	// Torn writes.
	{
		World t = w;
		t.fake.files[kPlanFile] = t.fake.files[kPlanFile].substr(0, 300);
		Check(t.Restart() == Io::ok && t.run.sequence == 1 && t.run.steps[0].state == StepState::launched, "torn plan.txt: the older plan.bak is read");
	}
	{
		World t = w;
		MarkLaunched(t.run, 1, 1030);
		t.fake.failWrites = 1; // plan.new is torn
		Check(t.Save() == Io::write, "a failed write of plan.new is reported");
		Check(t.Restart() == Io::ok && t.run.sequence == 2 && t.run.steps[1].state == StepState::pending, "torn plan.new: plan.txt still the truth");
	}
	{
		World t = w;
		// crash after plan.txt -> plan.bak, before plan.new -> plan.txt
		MarkLaunched(t.run, 1, 1030);
		t.Save();
		t.fake.files[kPlanNew] = t.fake.files[kPlanFile];
		t.fake.files.erase(kPlanFile);
		Check(t.Restart() == Io::ok && t.run.sequence == 3 && t.run.steps[1].state == StepState::launched, "crash between the renames: the complete plan.new is read (highest sequence)");
	}
	{
		World t = w;
		for (auto& f : t.fake.files)
			f.second = "garbage";
		Check(t.Restart() == Io::damaged, "all copies damaged: reported as damaged, not as no plan");
		t.fake.files.clear();
		Pointer lost;
		char text[300];
		Out out(text, sizeof(text));
		const bool pointer = ReadPointer(t.storage, lost);
		PointerLostText(lost, false, out);
		Check(t.Restart() == Io::none && pointer && Has(text, "stopped at step 1"), std::string("no file but the variable: ") + text);
	}
	{
		World t = w;
		std::string& f = t.fake.files[kPlanFile];
		const size_t at = f.find("format=1");
		f[at + 7] = '2';
		size_t b = f.size() - 19;
		char crc[12];
		Out c(crc, sizeof(crc));
		c.Hex(Crc32(f.data(), b), 8);
		f.replace(b + 9, 8, crc);
		t.fake.files.erase(kPlanBackup);
		Check(t.Restart() == Io::damaged, "a plan file of another format version is refused, not read as this one");
	}
	// Answer files of a step: written, remembered, zeroed and deleted.
	{
		const char* xml = "<Password><Value>tajne-haslo</Value></Password>";
		Check(WriteStepFile(w.storage, w.run, 2, "Autounattend.xml", xml, strlen(xml)) && WriteStepFile(w.storage, w.run, 2, "answers.ini", "a=b", 3)
			&& WriteStepFile(w.storage, w.run, 2, "Autounattend.xml", xml, strlen(xml)) && std::string(w.run.steps[2].files) == "Autounattend.xml,answers.ini"
			&& w.fake.files.count("\\EFI\\HAK\\plan\\step2\\Autounattend.xml"), "step answer files written under stepN and remembered once");
		w.Save();
		w.Restart();
		std::string seen;
		struct Spy {
			static bool Write(void* c, const char* path, const char* data, size_t length)
			{
				auto* s = static_cast<std::pair<Fake*, std::string*>*>(c);
				if (Has(path, "Autounattend"))
					*s->second = std::string(data, length);
				return Fake::Write(s->first, path, data, length);
			}
		};
		Check(EraseStepFiles(w.storage, w.run, 2, w.work.data(), w.work.size()) && !w.fake.files.count("\\EFI\\HAK\\plan\\step2\\Autounattend.xml")
			&& !w.fake.files.count("\\EFI\\HAK\\plan\\step2\\answers.ini") && w.run.steps[2].erased && w.run.steps[2].files[0] == 0, "step answer files erased after a restart (names came from the plan file)");
		WriteStepFile(w.storage, w.run, 2, "Autounattend.xml", xml, strlen(xml));
		std::pair<Fake*, std::string*> ctx(&w.fake, &seen);
		Storage spy = w.storage;
		spy.context = &ctx;
		spy.write = Spy::Write;
		spy.read = [](void* c, const char* p, char* b, size_t n, size_t* l) { return Fake::Read(static_cast<std::pair<Fake*, std::string*>*>(c)->first, p, b, n, l); };
		spy.remove = [](void* c, const char* p) { return Fake::Remove(static_cast<std::pair<Fake*, std::string*>*>(c)->first, p); };
		spy.exists = [](void* c, const char* p) { return Fake::Exists(static_cast<std::pair<Fake*, std::string*>*>(c)->first, p); };
		EraseStepFiles(spy, w.run, 2, w.work.data(), w.work.size());
		Check(seen.size() == strlen(xml) && seen == std::string(strlen(xml), '\0'), "the password file is overwritten with zeros of its full length before it is deleted");
	}
	Check(Seconds(2026, 11, 23, 12, 0, 0) - Seconds(2026, 11, 23, 9, 0, 0) == 3 * 3600 && Seconds(2027, 1, 1, 0, 0, 0) - Seconds(2026, 12, 31, 23, 59, 59) == 1
		&& Seconds(2028, 3, 1, 0, 0, 0) - Seconds(2028, 2, 28, 0, 0, 0) == 2 * 86400, "clock to seconds: hours, year end, leap day");
}

// Charon's own step: launched, saved, done.
static void Own(World& w, int step, uint64_t now)
{
	MarkLaunched(w.run, step, now);
	w.Save();
	MarkDone(w.run, step, now + 5);
	w.Save();
}
static void TestStateMachine()
{
	// T4: CharonDOS + Windows 11 + Debian, every restart simulated.
	World w(MakePlan({kDos, kWin11, kDebian}));
	Action a = Next(w.run);
	Check(a.what == Do::prepareDisk && a.step == 0, "start: prepare the disk");
	Own(w, 0, 1000);
	a = Next(w.run);
	Check(a.what == Do::writeDos && a.system == 0, "then CharonDOS is written");
	Own(w, 1, 1100);
	a = Next(w.run);
	Check(a.what == Do::launchInstaller && a.system == 1 && a.step == 2, "then the Windows installer is launched");
	Check(AnnounceText(w) == "Plan, step 2 of 4: Windows 11 is installing itself. Next: Debian. Space or Escape pauses the plan.", "announcement: " + AnnounceText(w));
	Check(AnnounceText(w, true) == "Plan, krok 2 z 4: Windows 11 instaluje si\xC4\x99 sam. Nast\xC4\x99pny: Debian. Spacja albo Escape wstrzymuje plan.", "announcement in Polish");
	MarkLaunched(w.run, 2, 2000);
	w.Save();
	Pointer ptr;
	ReadPointer(w.storage, ptr);
	Check(ptr.step == 2 && ptr.state == 1, "pointer follows the step");
	// Windows Setup restarts three times before its first logon writes the marker.
	for (int restart = 1; restart <= 3; restart++) {
		Check(w.Restart() == Io::ok, "restart " + std::to_string(restart) + ": plan found");
		const bool changed = Resume(w.plan, w.run, w.storage, 2000 + restart * 600, w.work.data(), w.work.size());
		a = Next(w.run);
		Check(changed && a.what == Do::passThrough && a.system == 1 && w.run.steps[2].starts == 1 + restart && w.run.steps[2].state == StepState::handoff,
			"restart " + std::to_string(restart) + ": installer in the middle, pass through to Windows");
		w.Save();
	}
	Check(Has(AnnounceText(w), "This is start number 4."), "announcement counts the starts");
	WriteStepFile(w.storage, w.run, 2, "Autounattend.xml", "secret", 6);
	w.Save();
	w.fake.files[Marker(2, false)] = "ok\r\n";
	w.Restart();
	Resume(w.plan, w.run, w.storage, 5000, w.work.data(), w.work.size());
	a = Next(w.run);
	Check(w.run.steps[2].state == StepState::done && w.run.steps[2].erased && !w.fake.files.count("\\EFI\\HAK\\plan\\step2\\Autounattend.xml")
		&& a.what == Do::launchInstaller && a.system == 2, "step2.done: Windows done, its answer file erased right away, Debian's installer next");
	w.Save();
	MarkLaunched(w.run, 3, 5100);
	w.Save();
	w.Restart();
	Resume(w.plan, w.run, w.storage, 5200, w.work.data(), w.work.size());
	Check(Next(w.run).what == Do::passThrough && Next(w.run).system == 2, "Debian's first start: pass through");
	w.Save();
	w.fake.files[Marker(3, false)] = "ok";
	w.Restart();
	Resume(w.plan, w.run, w.storage, 6000, w.work.data(), w.work.size());
	a = Next(w.run);
	Check(a.what == Do::finish && a.step == 4 && ResultOf(w.run, 0) == Result::done && ResultOf(w.run, 1) == Result::done && ResultOf(w.run, 2) == Result::done, "all systems done: finish");
	Machine m = GoodMachine();
	FinishRule rule = FinishOf(w.plan, w.run, m);
	Check(rule.dosFirst && !rule.charonFirst && rule.target == 0, "finish: boot order CharonDOS, Charon, the others");
	m.driverOptions = false;
	rule = FinishOf(w.plan, w.run, m);
	Check(!rule.dosFirst && rule.charonFirst && rule.target == 0 && rule.timeout == 0, "finish on firmware without driver options: Charon first, straight on to CharonDOS");
	m.driverOptions = true;
	// Registry, read back by CharonDOS's own parser.
	Place places[kMaxSystems] = {};
	const char* part = "0a1b2c3d-1111-2222-3333-444455556666";
	Copy(places[0].partition, 40, part);
	Copy(places[0].path, 128, "\\EFI\\CharonDOS\\CHARONDOS.EFI");
	Copy(places[1].partition, 40, part);
	Copy(places[1].path, 128, "\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
	Copy(places[2].partition, 40, part);
	Copy(places[2].path, 128, "\\EFI\\debian\\shimx64.efi");
	Copy(w.plan.systems[2].name, 48, "Debian 13 \xE2\x80\x94 GNOME|Orca");
	char fresh[1000], merged[2000];
	Out lines(fresh, sizeof(fresh));
	RegistryLines(w.plan, w.run, places, "2026-11-23", lines);
	const char* old = "# charon-systems 1\r\nold|Fedora 45|linux|ffffffff-1111-2222-3333-444455556666|\\EFI\\fedora\\shimx64.efi|00000001|2026-01-01\r\n"
		"debian|Debian 12|linux|0a1b2c3d-1111-2222-3333-444455556666|\\EFI\\debian\\shimx64.efi|00000001|2026-01-01\r\n"
		"stale|Something|linux|0A1B2C3D-1111-2222-3333-444455556666|\\efi\\microsoft\\boot\\BOOTMGFW.EFI|00000001|2026-01-01\r\n";
	Out all(merged, sizeof(merged));
	MergeRegistry(old, strlen(old), fresh, lines.length, all);
	LaunchList list;
	launch_clear(&list);
	const int added = launch_parse_registry(merged, all.length, &list);
	Check(added == 3 && list.count == 3 && std::string(list.e[0].id) == "old" && std::string(list.e[1].id) == "win11" && list.e[1].kind == LAUNCH_WINDOWS
		&& std::string(list.e[2].name) == "Debian 13 \xE2\x80\x94 GNOME/Orca" && list.e[2].kind == LAUNCH_LINUX && std::string(list.e[2].path) == "\\EFI\\debian\\shimx64.efi"
		&& Has(merged, "|1a2b3c4d|2026-11-23\r\n") && std::string(merged).rfind(kRegistryHeader, 0) == 0,
		"registry: written by the plan, merged with the old one (same id and same loader replaced), read by CharonDOS's parser; CharonDOS itself is not a WINNT/LINUX target");
	int index = -1;
	Check(launch_choose(&list, LAUNCH_LINUX, "", "", true, &index) == LAUNCH_ASK && launch_choose(&list, LAUNCH_LINUX, "", "debian", false, &index) == LAUNCH_GO && index == 2
		&& launch_choose(&list, LAUNCH_WINDOWS, "", "", false, &index) == LAUNCH_GO && index == 1 && launch_choose(&list, LAUNCH_LINUX, "", "", false, &index) == LAUNCH_NO_CHOICE,
		"selection over that registry: ask with two Linux, default by id, the only Windows directly, no prompt in a batch file");
	// Report and close.
	Layout layout;
	Refusals lr;
	DiskState disk = EmptyDisk(512);
	ComputeLayout(w.plan, 1, disk, Existing::erase, layout, lr);
	std::vector<char> report(6000);
	Out rep(report.data(), report.size());
	Report(w.plan, w.run, layout, m, "anna", false, rep);
	const std::string en = report.data();
	Out repPl(report.data(), report.size());
	Report(w.plan, w.run, layout, m, "anna", true, repPl);
	const std::string pl = report.data();
	Check(Has(en, "Systems installed: 3 of 3.") && Has(en, "Windows 11: 100 gigabytes, user anna, installed, 50 minutes.") && Has(en, "Boot order: CharonDOS, Charon, the others.")
		&& Has(en, "Shared data partition: none.") && Has(en, "universal time") && Has(en, "answer files with passwords were erased") && !rep.overflow
		&& Has(pl, "Zainstalowane systemy: 3 z 3.") && Has(pl, "50 minut.") && !Has(en, "tajne"), "final report EN and PL:\n" + en);
	{
		// r410: the first installed system switches the hardware clock from local to universal time, so Charon's
		// stamp "finished" of that system is taken on another clock than its "first" (seen in VMware: finished
		// before first, and the duration was dropped). The finish step hands the zone's offset over.
		const int sw = ClockSwitcher(w.plan, w.run);
		Run shifted = w.run;   // the clock went one hour back while Windows installed
		for (int k = 0; k < shifted.count; k++)
			if (shifted.steps[k].system == sw && shifted.steps[k].finished != 0)
				shifted.steps[k].finished -= 3600;
			else if (shifted.steps[k].system != sw && shifted.steps[k].system >= 0 && w.plan.systems[shifted.steps[k].system].kind != Kind::charondos) {
				if (shifted.steps[k].first != 0) shifted.steps[k].first -= 3600;
				if (shifted.steps[k].finished != 0) shifted.steps[k].finished -= 3600;
			}
		std::vector<char> a(6000), b(6000), c(6000);
		Out oa(a.data(), a.size()), ob(b.data(), b.size()), oc(c.data(), c.size());
		Report(w.plan, shifted, layout, m, "anna", false, oa, 0, 3600);
		Report(w.plan, shifted, layout, m, "anna", false, ob, 0, 0);
		Report(w.plan, shifted, layout, m, "anna", false, oc, 0, 300);
		auto line = [](const std::string& text, const char* name) {
			const size_t at = text.find(std::string(name) + ": ");
			return at == std::string::npos ? std::string() : text.substr(at, text.find("\r\n", at) - at);
		};
		Check(sw >= 0 && w.plan.systems[sw].kind == Kind::windows && line(a.data(), "Windows 11") == line(en, "Windows 11") && line(a.data(), "Debian") == line(en, "Debian")
			&& !Has(line(b.data(), "Windows 11").c_str(), "minute") && Has(line(c.data(), "Windows 11").c_str(), "installed, the time was not measured, because the hardware clock was switched"),
			std::string("duration across the switch of the hardware clock: with the offset the report is the same as without a switch; an offset that leaves no positive time is said, not guessed:\n") + a.data() + c.data());
			// r410 (VMware, CharonDOS + Debian image): only the switcher's LAST step ends on the new clock; its earlier
			// step's end stays on the old one and is then the largest stamp. The report must still take the last end.
			Run partly = w.run;
			int lastOfSwitcher = -1;
			for (int k = 0; k < partly.count; k++)
				if (partly.steps[k].system == sw && partly.steps[k].finished != 0) lastOfSwitcher = k;
			for (int k = 0; k < partly.count; k++) {
				const int sys = partly.steps[k].system;
				if (k == lastOfSwitcher) partly.steps[k].finished -= 3600;
				else if (sys != sw && sys >= 0 && w.plan.systems[sys].kind != Kind::charondos) {
					if (partly.steps[k].first != 0) partly.steps[k].first -= 3600;
					if (partly.steps[k].finished != 0) partly.steps[k].finished -= 3600;
				}
			}
			std::vector<char> e(6000);
			Out oe(e.data(), e.size());
			Report(w.plan, partly, layout, m, "anna", false, oe, 0, 3600);
			Check(lastOfSwitcher >= 0 && line(e.data(), "Windows 11") == line(en, "Windows 11") && line(e.data(), "Debian") == line(en, "Debian"),
				std::string("duration when only the switcher's last step ends on the new clock (earlier ends are larger stamps):\n") + e.data());
			// r410 (VMware R2a): the zone's offset is handed over but the clock did not go back during the plan (the
			// stamps are in order): nothing is added (was: "66 minutes" for about 5).
			std::vector<char> g(6000);
			Out og(g.data(), g.size());
			Report(w.plan, w.run, layout, m, "anna", false, og, 0, 3600);
			Check(line(g.data(), "Windows 11") == line(en, "Windows 11") && line(g.data(), "Debian") == line(en, "Debian"),
				std::string("an offset with stamps in order adds nothing:\n") + g.data());
	}
	MarkLaunched(w.run, 4, 6100);
	w.Save();
	MarkDone(w.run, 4, 6200);
	Check(CloseFiles(w.storage, w.plan, w.run, w.store, en.c_str(), w.work.data(), w.work.size()) == -1 && w.fake.variable.empty() && !w.fake.files.count(kPlanFile)
		&& !w.fake.files.count(kPlanBackup) && w.fake.files.count(kPlanDone) && w.fake.files[kPlanReport] == en && !Has(w.fake.files[kPlanDone], "anna")
		&& Has(w.fake.files[kPlanDone], "state=done") && Next(w.run).what == Do::complete,
		"close: variable deleted, plan files removed, plan-done.txt without answers, report.txt written");
	Check(w.Restart() == Io::none, "after the finish no active plan is found");

	// Failures (T6 logic).
	auto atWindows = [](Plan p) {
		World* x = new World(p);
		Own(*x, 0, 1000);
		MarkLaunched(x->run, 1, 2000);
		x->Save();
		return x;
	};
	{
		World& f = *atWindows(MakePlan({kWin11, kDebian}));
		f.fake.files[Marker(1, true)] = "Charon-Readers.ps1 failed: no network\r\nlog tail line\r\n";
		f.Restart();
		Resume(f.plan, f.run, f.storage, 3000, f.work.data(), f.work.size());
		a = Next(f.run);
		char text[900];
		Out out(text, sizeof(text));
		FailureText(f.plan, f.run, false, out);
		Check(a.what == Do::askFailure && f.run.steps[1].fail == Fail::reported
			&& std::string(text) == "The plan has stopped. Step 1, Windows 11: its first start reported an error: Charon-Readers.ps1 failed: no network. Finished and bootable: nothing. "
				"Not touched: Debian. Try again, skip this system and continue, or stop here.", std::string("stepN.failed: ") + text);
		f.Save();
		ReadPointer(f.storage, ptr);
		Check(ptr.state == 3, "pointer says the plan waits for a decision");
		// retry
		World r = f;
		Check(Decide(r.run, Decision::retry, 3100) && Next(r.run).what == Do::launchInstaller && Next(r.run).system == 0 && r.run.steps[1].retried && r.run.steps[1].starts == 0,
			"try again: the step is pending again (its partition goes back to the placeholder state first)");
		// skip
		World s = f;
		Check(Decide(s.run, Decision::skip, 3100) && Next(s.run).what == Do::launchInstaller && Next(s.run).system == 1 && ResultOf(s.run, 0) == Result::skipped,
			"skip this system and continue: Debian is next");
		MarkLaunched(s.run, 2, 3200);
		s.fake.files[Marker(2, false)] = "ok";
		Resume(s.plan, s.run, s.storage, 4000, s.work.data(), s.work.size());
		Layout layout2;
		Refusals l2;
		DiskState d2 = EmptyDisk(200);
		ComputeLayout(s.plan, 1, d2, Existing::erase, layout2, l2);
		int left[kMaxRanges];
		const int n = Leftovers(s.run, layout2, left, kMaxRanges);
		FinishRule fr = FinishOf(s.plan, s.run, GoodMachine());
		// r410 (data loss seen in VMware): the installer of Windows ran and its first start reported an error; the user
		// skipped it. Windows may be there and work: its range is not a leftover, in any state the step is left in.
		Check(Next(s.run).what == Do::finish && n == 0 && MayHoldSystem(s.run, 0) && !MayHoldSystem(s.run, 1)
			&& fr.charonFirst && fr.target == 1, "after the skip of a system whose first start reported an error: its range is NOT offered for freeing; default falls to the system that is done");
		std::vector<char> rb(6000);
		Out ro(rb.data(), rb.size());
		Report(s.plan, s.run, layout2, GoodMachine(), "anna", false, ro);
		Check(Has(rb.data(), "Systems installed: 1 of 2.") && Has(rb.data(), "Windows 11: 100 gigabytes, user anna, NOT installed, skipped: its first start reported an error: Charon-Readers.ps1 failed: no network.")
			&& !Has(rb.data(), "Reserved and unused space") && !Has(rb.data(), "Freed space")
			&& Has(rb.data(), "The space of Windows 11, 100 gigabytes: the installer worked there, and the first start reported an error or did not report at all. The system may be there and work; Charon removed nothing from it.")
			&& Has(rb.data(), "it starts Debian"), std::string("report after a skip:\n") + rb.data());
		{
			// The same holds when the plan is stopped there, and when the installer never reported (both budgets).
			World h = f;
			Decide(h.run, Decision::stop, 3100);
			int l3[kMaxRanges];
			Check(MayHoldSystem(h.run, 0) && Leftovers(h.run, layout2, l3, kMaxRanges) == 1 && layout2.ranges[l3[0]].system == 1,
				"stop here after the reported error: the range of Windows is kept out, the untouched range of Debian is the only leftover");
			for (Fail budget : {Fail::budgetStarts, Fail::budgetTime}) {
				World g = f;
				g.run.steps[1].fail = budget;
				Decide(g.run, Decision::skip, 3100);
				Check(MayHoldSystem(g.run, 0) && Leftovers(g.run, layout2, l3, kMaxRanges) <= 1 && (Leftovers(g.run, layout2, l3, kMaxRanges) == 0 || layout2.ranges[l3[0]].system != 0),
					"an installer that never reported back: its range is never a leftover");
			}
			// A step that Charon itself could not do (download, write) or that the user skipped before it ran leaves
			// an empty range: that one IS a leftover, as before, and freeing it is named in the report.
			World c = f;
			c.run.steps[1].fail = Fail::download;
			Decide(c.run, Decision::skip, 3100);
			MarkLaunched(c.run, 2, 3200);
			c.fake.files[Marker(2, false)] = "ok";
			Resume(c.plan, c.run, c.storage, 4000, c.work.data(), c.work.size());
			const int nc = Leftovers(c.run, layout2, l3, kMaxRanges);
			char q[400];
			Out lq(q, sizeof(q));
			LeftoverText(c.plan, layout2, nc > 0 ? l3[0] : 0, false, lq);
			Check(!MayHoldSystem(c.run, 0) && nc == 1 && layout2.ranges[l3[0]].system == 0
				&& Has(q, "Windows 11: its step did not run. The space reserved for it, 100 gigabytes, is empty."), std::string("a download that failed: the range is empty and asked about: ") + q);
			std::vector<char> kept(6000), freed(6000), freedPl(6000);
			Out ok(kept.data(), kept.size()), of(freed.data(), freed.size()), ofp(freedPl.data(), freedPl.size());
			Report(c.plan, c.run, layout2, GoodMachine(), "anna", false, ok);
			Layout after = layout2;
			after.ranges[l3[0]].role = Role::spare;   // what the finish step does to a range it freed
			Report(c.plan, c.run, after, GoodMachine(), "anna", false, of, uint32_t(1) << l3[0]);
			Report(c.plan, c.run, after, GoodMachine(), "anna", true, ofp, uint32_t(1) << l3[0]);
			Check(Has(kept.data(), "Reserved and unused space for Windows 11: 100 gigabytes.") && !Has(kept.data(), "Freed space") && !Has(kept.data(), "The space of Windows 11")
				&& Has(freed.data(), "Freed space, reserved before for Windows 11: 100 gigabytes; its partition was removed, the space is unallocated.") && !Has(freed.data(), "Reserved and unused")
				&& Has(freedPl.data(), "Zwolnione miejsce, zarezerwowane wcze") && Has(freedPl.data(), "Windows 11: 100 gigabajt"),
				std::string("report: a kept range is reserved and unused, a freed range is named as freed:\n") + freed.data() + freedPl.data());
			// r410 (VMware run 3): Windows never started (its download failed), so DEBIAN switched the clock; its
			// duration is measured with the offset, not called "not measured".
			Run late = c.run;
			for (int k = 0; k < late.count; k++)
				if (late.steps[k].system == 1 && late.steps[k].finished != 0)
					late.steps[k].finished -= 3600;
			std::vector<char> dur(6000);
			Out od(dur.data(), dur.size());
			Report(c.plan, late, after, GoodMachine(), "anna", false, od, 0, 3600);
			Check(ClockSwitcher(c.plan, c.run) == 1 && Has(dur.data(), "Debian: ") && !Has(dur.data(), "was not measured") && Has(dur.data(), "installed, 14 minutes"),
				std::string("the system that switched the clock is the first one that really started:\n") + dur.data());
		}
		Place pl2[kMaxSystems] = {};
		Copy(pl2[0].path, 128, "\\x.efi");
		Copy(pl2[1].path, 128, "\\EFI\\debian\\shimx64.efi");
		char reg[600];
		Out ol(reg, sizeof(reg));
		RegistryLines(s.plan, s.run, pl2, "d", ol);
		Check(!Has(reg, "win11") && Has(reg, "debian|Debian|linux|"), "registry lists only systems that are done");
		// stop
		World t = f;
		Check(Decide(t.run, Decision::stop, 3100) && Next(t.run).what == Do::finish && ResultOf(t.run, 0) == Result::failed && ResultOf(t.run, 1) == Result::skipped
			&& t.run.steps[2].fail == Fail::stopped && FinishOf(t.plan, t.run, GoodMachine()).menuWaits, "stop here: nothing more installed, the finish step still runs, the menu waits (nothing is bootable)");
		delete &f;
	}
	{
		// Budgets.
		World& b = *atWindows(MakePlan({kWin11, kDebian}));
		int passes = 0;
		while (Next(b.run).what != Do::askFailure && passes < 20) {
			b.Restart();
			Resume(b.plan, b.run, b.storage, 2000 + passes, b.work.data(), b.work.size());
			b.Save();
			passes++;
		}
		char text[600];
		Out out(text, sizeof(text));
		FailureText(b.plan, b.run, false, out);
		Check(passes == 8 && b.run.steps[1].fail == Fail::budgetStarts && Has(text, "the installer did not report back after 8 starts"), "start budget: the eighth start without a marker ends the loop");
		delete &b;
		World& t = *atWindows(MakePlan({kWin11, kDebian}));
		Resume(t.plan, t.run, t.storage, 2000 + 4 * 3600 - 1, t.work.data(), t.work.size());
		const bool within = Next(t.run).what == Do::passThrough;
		Resume(t.plan, t.run, t.storage, 2000 + 4 * 3600 + 1, t.work.data(), t.work.size());
		Check(within && t.run.steps[1].fail == Fail::budgetTime, "time budget: Windows 4 hours");
		delete &t;
		World d(MakePlan({kDebian, kImgUbuntu}));
		Own(d, 0, 1000);
		Own(d, 1, 1100);
		MarkLaunched(d.run, 2, 2000);
		Resume(d.plan, d.run, d.storage, 2000 + 3 * 3600 + 1, d.work.data(), d.work.size());
		Check(d.run.steps[2].fail == Fail::budgetTime, "time budget: Linux 3 hours");
		World c(MakePlan({kWin11, kDebian}));
		Own(c, 0, 1000);
		MarkLaunched(c.run, 1, 50000);
		Resume(c.plan, c.run, c.storage, 46400, c.work.data(), c.work.size()); // the clock jumped back an hour
		Check(Next(c.run).what == Do::passThrough && c.run.steps[1].starts == 2, "a clock that went backwards does not fail the step");
	}
	{
		// Unattended policies.
		World& u = *atWindows(MakePlan({kWin11, kDebian}));
		u.fake.files[Marker(1, true)] = "x";
		Resume(u.plan, u.run, u.storage, 3000, u.work.data(), u.work.size());
		u.plan.onFailure = OnFailure::stop;
		const bool stop = Policy(u.plan, u.run) == Decision::stop;
		u.plan.onFailure = OnFailure::skip;
		const bool skip = Policy(u.plan, u.run) == Decision::skip;
		u.plan.onFailure = OnFailure::retryOnceThenStop;
		const bool first = Policy(u.plan, u.run) == Decision::retry;
		Decide(u.run, Decision::retry, 3100);
		u.fake.files.erase(Marker(1, true));
		MarkLaunched(u.run, 1, 3200);
		u.run.steps[1].retried = true;
		u.fake.files[Marker(1, true)] = "again";
		Resume(u.plan, u.run, u.storage, 3300, u.work.data(), u.work.size());
		Check(stop && skip && first && Policy(u.plan, u.run) == Decision::stop, "on_failure: stop, skip, retry once then stop");
		delete &u;
		World p(MakePlan({kWin11, kDebian}));
		MarkLaunched(p.run, 0, 1000);
		MarkFailed(p.run, 0, Fail::charon, "the disk refused a write", 1001);
		p.plan.onFailure = OnFailure::skip;
		char text[600];
		Out out(text, sizeof(text));
		FailureText(p.plan, p.run, true, out);
		Check(Policy(p.plan, p.run) == Decision::stop && !Decide(p.run, Decision::skip, 1002) && Has(text, "Krok 0: Charon nie m\xC3\xB3g\xC5\x82 doko\xC5\x84""czy\xC4\x87 w\xC5\x82""asnej pracy: the disk refused a write"),
			"a failed disk preparation always stops and cannot be skipped");
	}
	{
		// Charon's own step cut off; pause; images.
		World i(MakePlan({kDos, kImgDebian}));
		Own(i, 0, 1000);
		MarkLaunched(i.run, 1, 1100);
		i.Save();
		i.Restart();
		Resume(i.plan, i.run, i.storage, 1200, i.work.data(), i.work.size());
		Check(Next(i.run).what == Do::askFailure && i.run.steps[1].fail == Fail::interrupted, "power cut while CharonDOS was being written: seen at the next start");
		Decide(i.run, Decision::retry, 1300);
		Own(i, 1, 1400);
		Check(Next(i.run).what == Do::writeImage, "T3: image written after CharonDOS");
		Own(i, 2, 1500);
		Check(Next(i.run).what == Do::launchFirstStart && Next(i.run).system == 1, "T3: then the image's first start");
		i.run.paused = true;
		char text[200];
		Out out(text, sizeof(text));
		PausedText(false, out);
		Check(Next(i.run).what == Do::askPaused && Has(text, "Plan paused."), "Space or Escape: the plan is paused before the next step");
		i.Save();
		i.Restart();
		Check(i.run.paused && Next(i.run).what == Do::askPaused, "a paused plan stays paused over a restart");
		i.run.paused = false;
		MarkLaunched(i.run, 3, 1600);
		i.fake.files[Marker(3, true)] = "cloud-init failed";
		Resume(i.plan, i.run, i.storage, 1700, i.work.data(), i.work.size());
		Decide(i.run, Decision::retry, 1800);
		Check(Next(i.run).what == Do::writeImage && i.run.steps[2].state == StepState::pending, "retry of an image's first start writes the image again first");
		Decide(i.run, Decision::skip, 1900);
		Check(Next(i.run).what == Do::finish && i.run.steps[3].state == StepState::skipped && ResultOf(i.run, 1) == Result::skipped, "skipping an image skips both of its steps");
		FinishRule fr = FinishOf(i.plan, i.run, GoodMachine());
		Check(fr.dosFirst && fr.target == 0, "CharonDOS alone finished: it is still the default");
		World wf(MakePlan({kWin11, kImgDebian}));
		Own(wf, 0, 1000);
		MarkLaunched(wf.run, 1, 1100);
		MarkFailed(wf.run, 1, Fail::download, "HTTP 404", 1200);
		Decide(wf.run, Decision::skip, 1300);
		Check(Next(wf.run).what == Do::launchInstaller && wf.run.steps[3].state == StepState::skipped && wf.run.steps[3].fail == Fail::dependency,
			"image download failed and skipped: its first start is skipped too, Windows goes on");
		World fin(MakePlan({kWin11, kDebian}));
		for (int k = 0; k < 3; k++)
			Own(fin, k, 1000 + k);
		MarkLaunched(fin.run, 3, 2000);
		Resume(fin.plan, fin.run, fin.storage, 2100, fin.work.data(), fin.work.size());
		Check(Next(fin.run).what == Do::finish && fin.run.steps[3].state == StepState::pending, "a finish step that was cut off simply runs again");
	}
	{
		// CharonDOS defaults for WINNT / LINUX.
		World d(MakePlan({kDos, kWin10, kWin11, kDebian, kImgUbuntu}));
		for (int k = 0; k < d.run.count - 1; k++)
			d.run.steps[k].state = StepState::done;
		char cfg[200];
		Out a1(cfg, sizeof(cfg));
		DosDefaults(d.plan, d.run, d.store, a1);
		const std::string first = cfg;
		d.store.Put("plan", "dosdefault_winnt", "win11");
		d.store.Put("plan", "dosdefault_linux", "ask");
		Out a2(cfg, sizeof(cfg));
		DosDefaults(d.plan, d.run, d.store, a2);
		char out[100];
		Check(first == "winnt=win10\r\nlinux=debian\r\n" && std::string(cfg) == "winnt=win11\r\n" && launch_cfg_get(cfg, strlen(cfg), "winnt", out, sizeof(out)) && std::string(out) == "win11",
			"WINNT/LINUX defaults for BOOT.CFG: first of the list, a chosen one, or ask; read by CharonDOS's parser");
		World one(MakePlan({kDos, kWin11, kDebian}));
		for (int k = 0; k < one.run.count - 1; k++)
			one.run.steps[k].state = StepState::done;
		Out a3(cfg, sizeof(cfg));
		DosDefaults(one.plan, one.run, one.store, a3);
		Check(cfg[0] == 0, "one Windows and one Linux: no default line is needed");
	}
	{
		// r410 (agent 1, h7a): CharonDOS's LINUX does not find Charon Hypervisor; the text names Charon's list instead.
		std::vector<char> text(1000);
		Out out(text.data(), text.size());
		DosCommandsText(MakePlan({kDos, kHv}), false, out);
		const std::string en = text.data();
		Check(en == "Charon Hypervisor starts from Charon's boot devices list; EXIT in CharonDOS goes back to Charon. ", "DOS commands, CharonDOS + hypervisor: " + en);
		Out out2(text.data(), text.size());
		DosCommandsText(MakePlan({kDos, kWin11, kHv}), false, out2);
		const std::string en2 = text.data();
		Check(Has(en2, "In it, WINNT starts Windows. ") && !Has(en2, "LINUX"), "DOS commands, CharonDOS + Windows + hypervisor: " + en2);
	}
	{
		// r410 (agent 1, VMware h7c): "Debian installer + Charon Hypervisor": the hypervisor is written first (6 s on
		// the local clock), Debian switches the clock to universal time; the report said "61 minutes" for the hypervisor.
		World h(MakePlan({kDebian, kHv}));
		int hvStep = -1, debStep = -1;
		for (int k = 0; k < h.run.count; k++) {
			StepRun& s = h.run.steps[k];
			s.state = StepState::done;
			if (s.system < 0) continue;
			if (h.plan.systems[s.system].kind == Kind::hypervisor) { hvStep = k; s.first = 50000; s.finished = 50006; }
			else { debStep = k; s.first = 50010; s.finished = 50010 + 1200 - 3600; }
		}
		Layout hl;
		Refusals hr;
		DiskState hd = EmptyDisk(120);
		ComputeLayout(h.plan, 1, hd, Existing::erase, hl, hr);
		std::vector<char> text(6000);
		Out out(text.data(), text.size());
		Report(h.plan, h.run, hl, GoodMachine(), "anna", false, out, 0, 3600);
		const std::string en = text.data();
		Check(hvStep >= 0 && debStep >= 0 && ClockSwitcher(h.plan, h.run) != hvStep && Has(en, "installed, 1 minute.") && Has(en, "installed, 20 minutes.") && !Has(en, "61 minutes"),
			"report, Debian installer + hypervisor across the clock switch: the hypervisor is not the switcher:\n" + en);
	}
	{
		// Summary before the typed confirmation.
		Plan p = MakePlan({kDos, kWin11, kDebian});
		p.data = DataFs::exfat;
		Layout l;
		Refusals r;
		DiskState d = EmptyDisk(512);
		ComputeLayout(p, 1, d, Existing::erase, l, r);
		std::vector<char> text(4000);
		Out out(text.data(), text.size());
		Machine m2 = GoodMachine();
		Summary(p, l, m2, 512, true, "anna", false, out);
		const std::string en = text.data();
		Check(Has(en, "Plan. Disk, 512 gigabytes, empty. Systems: 3, in this order: 1, CharonDOS, 1536 megabytes, it is also the EFI partition. 2, Windows 11, 100 gigabytes. 3, Debian, 40 gigabytes. Shared data partition, exFAT, 370 gigabytes. User anna in every system.")
			&& Has(en, "CharonDOS starts first. In it, WINNT starts Windows and LINUX starts Linux.") && Has(en, "Leave Charon's start medium plugged in") && Has(en, "Secure Boot is off.")
			&& !Has(en, "will be erased") && !out.overflow, "summary: " + en);
		m2.runningFromRam = true;
		m2.secureBoot = true;
		Plan q = MakePlan({kWin11, kDebian});
		DiskState used = WindowsDisk(200);
		ComputeLayout(q, 1, used, Existing::erase, l, r);
		Out o2(text.data(), text.size());
		Summary(q, l, m2, 256, false, "anna", true, o2);
		const std::string pl = text.data();
		Check(Has(pl, "Wszystko na tym dysku zostanie wymazane.") && Has(pl, "Charon dzia\xC5\x82""a z pami\xC4\x99""ci") && !Has(pl, "Zostaw no\xC5\x9Bnik") && Has(pl, "po 5 sekundach uruchamia system Windows 11")
			&& Has(pl, "Secure Boot jest w\xC5\x82\xC4\x85""czony."), "summary in Polish, used disk erased, running from memory: " + pl);
	}
	{
		char16_t wide[32];
		const size_t n = Utf16("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 \xE2\x80\x94", wide, 32);
		uint8_t g[16];
		char text[40];
		Out out(text, sizeof(text));
		const bool parsed = ParseGuid("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", g);
		GuidText(g, out);
		Check(n == 8 && wide[2] == 0x017C && wide[7] == 0x2014 && parsed && memcmp(g, sd::kTypeEfiSystem.bytes, 16) == 0 && std::string(text) == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
			&& !ParseGuid("C12A7328-F81F-11D2-BA4B-00A0C93EC93", g), "text tools: UTF-8 to UTF-16, GUID text both ways (matches the on-disk EFI type)");
	}
}

// ---- B1: clock zone, disk identity, boot order --------------------------------------------
static std::string StampText(const hakefi::clockzone::Stamp& s)
{
	char t[40];
	snprintf(t, sizeof(t), "%04d-%02u-%02u %02u:%02u:%02u", s.year, s.month, s.day, s.hour, s.minute, s.second);
	return t;
}
static void TestPlanSets()
{
	// r410: a plan's answers saved as a set and loaded again (src/efi/CharonPlanSets.h).
	Plan p = MakePlan({kWin11, kDebian});
	Copy(p.defaultId, sizeof(p.defaultId), "debian");
	p.timeout = 10;
	std::vector<char> ab(65536), sb(65536), lb(65536);
	Store answers(ab.data(), ab.size()), scratch(sb.data(), sb.size()), loaded(lb.data(), lb.size());
	// what the dialogue leaves in the plan's store: [shared] and every system's own section (with the shared copies)
	answers.Put("shared", "user", "anna");
	answers.Put("shared", "locale", "pl_PL");
	answers.Put("shared", "hash", "$6$salt$HASHHASH");
	answers.Put("shared", "password", "tajne-haslo");
	answers.Put("shared", "hostpattern", "duet");
	answers.Put("shared", "sd_punct", "all");
	answers.Put("shared", "sets", "tools");
	answers.Put("system.win11", "user", "anna");
	answers.Put("system.win11", "hash", "$6$salt$HASHHASH");
	answers.Put("system.win11", "wreaders", "nvda");
	answers.Put("system.win11", "w_layout", "laptop");
	answers.Put("system.win11", "detailed", "yes");
	answers.Put("system.win11", "product_key", "AAAAA-BBBBB");
	answers.Put("system.win11", "esp_guid", "0102");
	answers.Put("system.debian", "user", "anna");
	answers.Put("system.debian", "hash", "$6$salt$HASHHASH");
	answers.Put("system.debian", "desktop", "mate-desktop");
	answers.Put("system.debian", "readers", "orca,fenrir");
	answers.Put("system.debian", "shell", "zsh");
	answers.Put("system.debian", "detailed", "yes");
	answers.Put("plan", "id", "1a2b3c4d");
	answers.Put("layout", "range.0", "junk of one run");
	std::vector<char> text(32768);
	Out out(text.data(), text.size());
	const bool made = SetText(p, answers, "duet", true, false, nullptr, nullptr, scratch, out);
	const std::string set = text.data();
	Check(made && set.rfind("[charon-answers]\r\nformat=1\r\n", 0) == 0 && Has(set, "[plan]\r\n") && Has(set, "name=duet\r\n") && Has(set, "systems=win11,debian\r\n")
		&& Has(set, "default=debian\r\n") && Has(set, "timeout=10\r\n") && Has(set, "mode=detailed\r\n") && Has(set, "unattended=no\r\n")
		&& Has(set, "[shared]\r\n") && Has(set, "sd_punct=all\r\n") && Has(set, "sets=tools\r\n") && Has(set, "[system.win11]\r\n") && Has(set, "w_layout=laptop\r\n")
		&& Has(set, "wreaders=nvda\r\n") && Has(set, "[system.debian]\r\n") && Has(set, "readers=orca,fenrir\r\n") && Has(set, "shell=zsh\r\n")
		&& set.find("[shared]") < set.find("[system.win11]") && set.find("[system.win11]") < set.find("[system.debian]")
		&& Has(set, "[secrets]\r\npassword=ask\r\n"), "plan set: header, [plan], shared part once, per-system parts, password=ask:\n" + set);
	Check(!Has(set, "HASHHASH") && !Has(set, "tajne") && !Has(set, "AAAAA") && !Has(set, "esp_guid") && !Has(set, "1a2b3c4d") && !Has(set, "junk") && !Has(set, "[disk.1]")
		&& scratch.Count() == 0, "plan set: no hash, no password, no product key, nothing of one run (plan id, partition identities, layout), no disk rule; the scratch store is wiped");
	// loading: the plan and the answers come back; the password is still to be asked
	Plan q;
	Clear(q);
	q.id = 77;
	Refusals report;
	SetInfo info{};
	std::vector<char> copy(set.begin(), set.end());
	const bool ok = LoadSetText(copy.data(), copy.size(), loaded, q, report, info);
	char said[2000];
	Out so(said, sizeof(said));
	SayAll(report, q, false, false, so);
	Check(ok && info.isSet && info.isPlan && info.systems == 2 && !info.unattended && !info.passwordInside && info.detailed && q.id == 77 && q.count == 2
		&& q.systems[0].kind == Kind::windows && std::string(q.systems[0].version) == "11" && q.systems[1].kind == Kind::debian && std::string(q.defaultId) == "debian"
		&& q.timeout == 10 && std::string(q.systems[1].host) == "pc-debian" && std::string(Resolve(loaded, "debian", "shell")) == "zsh"
		&& std::string(Resolve(loaded, "win11", "sd_punct")) == "all" && std::string(Resolve(loaded, "win11", "w_layout")) == "laptop",
		std::string("plan set loaded: the same plan, shared and per-system answers: ") + said);
	Check(FinishLoad(loaded, q, "$6$new$NEWHASH") && std::string(loaded.GetOr("shared", "hash", "")) == "$6$new$NEWHASH" && std::string(loaded.GetOr("system.debian", "hash", "")) == "$6$new$NEWHASH"
		&& std::string(loaded.GetOr("system.win11", "hash", "")) == "$6$new$NEWHASH" && !loaded.HasSection("secrets") && !loaded.HasSection("charon-answers"),
		"plan set: after the password was asked the new hash is in [shared] and in every system's section; secrets and header are gone");
	// a set for unattended use: the disk's serial and the clear password only on request
	Out out2(text.data(), text.size());
	const bool made2 = SetText(p, answers, "lab", false, true, "S4EWNX0M123456", "tajne-haslo", scratch, out2);
	const std::string set2 = text.data();
	std::vector<char> copy2(set2.begin(), set2.end());
	Refusals r2;
	SetInfo i2{};
	Plan u;
	Clear(u);
	const bool ok2 = LoadSetText(copy2.data(), copy2.size(), loaded, u, r2, i2);
	DiskState disks[2];
	memset(disks, 0, sizeof(disks));
	disks[0].sectorSize = disks[1].sectorSize = 512;
	disks[0].sectors = disks[1].sectors = uint64_t(200) << 21;
	disks[0].blank = disks[1].blank = true;
	Copy(disks[0].serial, sizeof(disks[0].serial), "OTHER");
	Copy(disks[1].serial, sizeof(disks[1].serial), "S4EWNX0M123456");
	int idx[kMaxDiskRules] = {-1, -1, -1, -1};
	Refusals r3, r4;
	const bool authorised = AuthoriseDisks(u, disks, 2, idx, r3);
	Copy(disks[1].serial, sizeof(disks[1].serial), "SOMETHING-ELSE");
	int idx2[kMaxDiskRules] = {-1, -1, -1, -1};
	const bool refused = !AuthoriseDisks(u, disks, 2, idx2, r4);
	Check(made2 && ok2 && i2.unattended && i2.passwordInside && !i2.detailed && u.unattended && Has(set2, "[disk.1]\r\nserial=S4EWNX0M123456\r\n") && Has(set2, "existing=erase\r\n")
		&& Has(set2, "password=tajne-haslo\r\n") && Has(set2, "clear text") && Has(set2, "confirm=typed\r\n") && authorised && idx[0] == 1 && refused && r4.Has(Code::diskNoMatch),
		"unattended set: [disk.1] with the serial, the password in clear text with its warning, confirm=typed; the disk is authorised by its serial only:\n" + set2);
	// not a set, a newer format, a broken file: refused, nothing usable
	{
		const char* texts[3] = {"[plan]\r\nsystems=a,b\r\n", "[charon-answers]\r\nformat=2\r\n[plan]\r\nsystems=debian\r\n[system.debian]\r\nkind=debian\r\n",
			"[charon-answers]\r\nformat=1\r\n[plan]\r\nsystems=win11,debian\r\n[system.win11]\r\nkind=windows\r\n"};
		bool allRefused = true;
		for (const char* t : texts) {
			std::vector<char> c(t, t + strlen(t));
			Refusals rr;
			SetInfo ii{};
			Plan x;
			Clear(x);
			allRefused = allRefused && !LoadSetText(c.data(), c.size(), loaded, x, rr, ii);
		}
		Check(allRefused, "a file without the header, a newer format and a set that names a system it does not describe are refused");
	}
	// too small a buffer: said, never half a file taken for a whole one
	char tiny[200];
	Out to(tiny, sizeof(tiny));
	Check(!SetText(p, answers, "duet", true, false, nullptr, nullptr, scratch, to), "a set that does not fit is reported as not made");
}

static void TestClockAndIdentity()
{
	namespace cz = hakefi::clockzone;
	const char* file = "rtc=utc\r\nzone=Europe/Warsaw\r\n";
	cz::Setting w = cz::Parse(file, strlen(file));
	Check(w.universal && w.zone && std::string(w.zone->name) == "Europe/Warsaw", "clock.txt: universal time, Warsaw");
	auto local = [&](const cz::Setting& s, int y, int mo, int d, int h, int mi, bool* summer = nullptr) {
		return StampText(cz::Shown(s, cz::Stamp{y, uint8_t(mo), uint8_t(d), uint8_t(h), uint8_t(mi), 7}, summer));
	};
	bool summer = false;
	Check(local(w, 2026, 11, 23, 11, 30, &summer) == "2026-11-23 12:30:07" && !summer, "Warsaw winter: universal 11:30 is 12:30");
	Check(local(w, 2026, 7, 1, 22, 30, &summer) == "2026-07-02 00:30:07" && summer, "Warsaw summer: universal 22:30 is 00:30 the next day");
	Check(local(w, 2026, 3, 29, 0, 59) == "2026-03-29 01:59:07" && local(w, 2026, 3, 29, 1, 0) == "2026-03-29 03:00:07", "Warsaw: clocks go forward on 29 March 2026 at 01:00 universal time");
	Check(local(w, 2026, 10, 25, 0, 59) == "2026-10-25 02:59:07" && local(w, 2026, 10, 25, 1, 0) == "2026-10-25 02:00:07", "Warsaw: clocks go back on 25 October 2026");
	Check(local(w, 2026, 12, 31, 23, 30) == "2027-01-01 00:30:07" && local(w, 2028, 2, 28, 23, 30) == "2028-02-29 00:30:07", "year end and leap day");
	const char* ny = "zone=America/New_York\nrtc=UTC\n";
	cz::Setting n = cz::Parse(ny, strlen(ny));
	Check(local(n, 2026, 3, 8, 6, 59) == "2026-03-08 01:59:07" && local(n, 2026, 3, 8, 7, 0) == "2026-03-08 03:00:07"
		&& local(n, 2026, 11, 1, 5, 59) == "2026-11-01 01:59:07" && local(n, 2026, 11, 1, 6, 0) == "2026-11-01 01:00:07" && local(n, 2026, 1, 1, 3, 0) == "2025-12-31 22:00:07",
		"New York: second Sunday of March, first Sunday of November, New Year");
	const char* kyiv = "rtc=utc\nzone=Europe/Kyiv\n";
	const char* la = "rtc=utc\nzone=America/Los_Angeles\n";
	const char* utc = "rtc=utc\nzone=UTC\n";
	Check(local(cz::Parse(kyiv, strlen(kyiv)), 2026, 7, 1, 10, 0) == "2026-07-01 13:00:07" && local(cz::Parse(la, strlen(la)), 2026, 1, 15, 10, 0) == "2026-01-15 02:00:07"
		&& local(cz::Parse(utc, strlen(utc)), 2026, 7, 1, 10, 0) == "2026-07-01 10:00:07", "Kyiv, Los Angeles, UTC");
	// Setting the clock: local time back to universal time; every minute of two years round-trips at the hour marks.
	bool round = true;
	for (int64_t m = cz::Minutes(cz::Stamp{2026, 1, 1, 0, 0, 0}); m < cz::Minutes(cz::Stamp{2028, 1, 1, 0, 0, 0}) && round; m += 30) {
		const cz::Stamp u = cz::FromMinutes(m, 0);
		for (const cz::Setting* s : {&w, &n}) {
			const cz::Stamp back = cz::Stored(*s, cz::Shown(*s, u));
			// The hour that occurs twice comes back as its first occurrence.
			const int64_t diff = cz::Minutes(back) - m;
			round = round && (diff == 0 || diff == -60);
			if (diff == -60)
				round = round && cz::Summer(*s->zone, m - 60) && !cz::Summer(*s->zone, m);
		}
	}
	Check(round, "local -> universal -> local for every half hour of 2026 and 2027, Warsaw and New York");
	Check(StampText(cz::Stored(w, cz::Stamp{2026, 3, 29, 2, 30, 0})) == "2026-03-29 01:30:00", "a time in the hour that spring skips is stored one hour later, as clocks show it");
	const char* localFile = "rtc=local\nzone=Europe/Warsaw\n";
	const char* unknown = "rtc=utc\nzone=Mars/Olympus\n";
	Check(!cz::Parse(localFile, strlen(localFile)).universal && !cz::Parse(unknown, strlen(unknown)).universal && !cz::Parse(nullptr, 0).universal && !cz::Parse("zone=Europe/Warsaw\n", 19).universal
		&& local(cz::Parse(unknown, strlen(unknown)), 2026, 7, 1, 10, 0) == "2026-07-01 10:00:07",
		"rtc=local, an unknown zone or no file: the clock is taken as it is, no zone is guessed");
	char text[64];
	Check(cz::Text("Europe/Prague", text, sizeof(text)) == 29 && cz::Parse(text, strlen(text)).universal && std::string(cz::Parse(text, strlen(text)).zone->name) == "Europe/Prague"
		&& cz::Text("Europe/Prague", text, 10) == 0, "clock.txt written and read back; a buffer that is too small writes nothing");
	size_t zoneCount = 0;
	cz::Zones(zoneCount);
	Check(zoneCount == 13, "the 13 zones the installer offers");

	namespace di = hakefi::diskidentity;
	uint8_t ata[512] = {};
	auto put = [&](size_t at, const char* t, size_t width) {
		char padded[64];
		memset(padded, ' ', sizeof(padded));
		memcpy(padded, t, strlen(t));
		for (size_t i = 0; i < width; i += 2) {
			ata[at + i] = uint8_t(padded[i + 1]);
			ata[at + i + 1] = uint8_t(padded[i]);
		}
	};
	put(20, "      S6XYNX0R123456", 20);
	put(54, "Samsung  SSD 870 EVO 1TB", 40);
	char serial[40], model[48];
	Check(di::FromAta(ata, 512, serial, sizeof(serial), model, sizeof(model)) && std::string(serial) == "S6XYNX0R123456" && std::string(model) == "Samsung SSD 870 EVO 1TB",
		std::string("ATA identify: serial and model, byte pairs swapped, blanks trimmed: ") + serial + " / " + model);
	uint8_t nvme[4096] = {};
	memcpy(nvme + 4, "S4EWNX0M123456      ", 20);
	memcpy(nvme + 24, "Samsung SSD 980 PRO 1TB                 ", 40);
	Check(di::FromNvme(nvme, 4096, serial, sizeof(serial), model, sizeof(model)) && std::string(serial) == "S4EWNX0M123456" && std::string(model) == "Samsung SSD 980 PRO 1TB", "NVMe identify: serial and model");
	nvme[6] = 0xFF;
	Check(!di::FromNvme(nvme, 4096, serial, sizeof(serial), model, sizeof(model)) && serial[0] == 0 && !di::FromAta(ata, 100, serial, sizeof(serial), model, sizeof(model))
		&& !di::FromNvme(nullptr, 0, serial, sizeof(serial), model, sizeof(model)), "identify data that is not text, too short or absent gives no serial");
	memset(nvme + 4, ' ', 20);
	Check(!di::FromNvme(nvme, 4096, serial, sizeof(serial), model, sizeof(model)), "an empty serial is no serial");
	// The serial read this way authorises exactly as a typed one.
	DiskState d = EmptyDisk(512);
	di::FromAta(ata, 512, d.serial, sizeof(d.serial), d.model, sizeof(d.model));
	Plan p = MakePlan({kWin11, kDebian});
	p.disks[0].present = true;
	Copy(p.disks[0].serial, 40, "s6xynx0r123456");
	Copy(p.disks[0].model, 48, "Samsung SSD 870*");
	int index[kMaxDiskRules];
	Refusals r;
	Check(AuthoriseDisks(p, &d, 1, index, r) && index[0] == 0, "a serial from identify data matches the [disk.1] rule");

	const uint16_t old[] = {4, 0, 1, 7, 2};
	const uint16_t dosFirst[] = {7, 1};
	uint16_t out[8];
	size_t count = FinishOrder(old, 5, dosFirst, 2, out, 8);
	Check(count == 5 && out[0] == 7 && out[1] == 1 && out[2] == 4 && out[3] == 0 && out[4] == 2, "finish BootOrder: CharonDOS, Charon, then the others in their old order");
	const uint16_t charon[] = {1};
	count = FinishOrder(old, 5, charon, 1, out, 8);
	const uint16_t fresh[] = {9, 9};
	Check(count == 5 && out[0] == 1 && out[1] == 4 && out[4] == 2 && FinishOrder(old, 5, fresh, 2, out, 8) == 6 && out[0] == 9 && out[1] == 4 && FinishOrder(old, 5, charon, 1, out, 4) == 0,
		"finish BootOrder: Charon first; an entry not yet in the order is added once; an order that does not fit is refused");
}

// ---- B2b, prepared: the Windows plan-step fragments, written out for the golden files ----------
struct Sink {
	std::string text;
	void Add(const char* s) { text += s; }
	void Number(uint64_t v) { text += std::to_string(v); }
};
static void Put(const std::string& dir, const char* name, const std::string& text)
{
	FILE* f = fopen((dir + "/" + name).c_str(), "wb");
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}
static int EmitWindows(const std::string& dir)
{
	namespace pw = hakefi::planwin;
	{
		Sink x;
		unsigned order = 7;
		pw::FirstLogon(x, order, pw::Logon{false, nullptr, 0, -1});
		Put(dir, "windows-first-logon-one-windows.xml", x.text);
	}
	{
		Sink x;
		unsigned order = 12;
		pw::FirstLogon(x, order, pw::Logon{true, "Windows 11 \"Pro\" & <test>", 10, 3});
		Put(dir, "windows-first-logon-two-windows.xml", x.text);
	}
	{
		Sink x;
		for (const char* zone : {"Europe/Warsaw", "Europe/London", "Europe/Berlin", "Europe/Paris", "Europe/Madrid", "Europe/Rome", "Europe/Kyiv", "Europe/Prague",
				"America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles", "UTC", "Mars/Olympus"}) {
			x.Add(zone);
			x.Add(" ->\n");
			pw::TimeZoneElement(x, zone);
		}
		Put(dir, "windows-time-zones.txt", x.text);
	}
	// The plan's disk needs no new XML: it is WindowsAnswers' existing branch for diskmode=partition
	// with this partition number.
	{
		Plan p = MakePlan({kDos, kWin11, kDebian});
		DiskState d = EmptyDisk(512);
		Layout l;
		Refusals r;
		ComputeLayout(p, 1, d, Existing::erase, l, r);
		const int range = RangeOfSystem(l, 1);
		Sink x;
		x.Add("CharonDOS + Windows 11 + Debian, empty disk: Windows partition number by position ");
		x.Number(PositionNumber(d, l, range, true));
		x.Add(" (Microsoft reserved partition counted), ");
		x.Number(PositionNumber(d, l, range, false));
		x.Add(" (not counted)\n");
		Put(dir, "windows-partition-number.txt", x.text);
	}
	return 0;
}

// r410: "a plan downloads everything first" (CharonPlanFetch.h) and the order of the boot entries.
static void TestFetchAndBootOrder()
{
	const uint64_t mib = uint64_t(1) << 20, gib = uint64_t(1) << 30;
	Plan p = MakePlan({kWin11, kDebian});
	FetchList list = {};
	AddFetch(list, FetchWhat::windows, 0, 8225329152ull);
	AddFetch(list, FetchWhat::reader, 0, 52 * mib);
	AddFetch(list, FetchWhat::kernel, 1, 8 * mib + 5);
	AddFetch(list, FetchWhat::initrd, 1, 70 * mib);
	// Plenty of memory: everything waits in memory.
	FetchRoom room = {16 * gib, false, 0};
	AssignHolds(list, room);
	Check(FetchCount(list, Hold::ram) == 4 && FetchCount(list, Hold::later) == 0 && UpfrontCount(list) == 4, "fetch: 16 GB free: all four files wait in memory");
	Check(FetchCost(1) == 64 * mib && FetchCost(64 * mib) == 64 * mib && FetchCost(64 * mib + 1) == 128 * mib, "fetch: memory is counted in 64 MiB pieces");
	Check(FetchReserve(2 * gib) == 512 * mib && FetchReserve(16 * gib) == 2 * gib, "fetch: the reserve is 512 MiB, or an eighth of the free memory when that is more");
	// Exactly at the limit: reserve 2 GiB of 16; files cost 7872+64+64+128 MiB.
	{
		FetchList l = list;
		const uint64_t cost = FetchCost(8225329152ull) + 64 * mib + 64 * mib + 128 * mib;
		FetchRoom tight = {cost + 2 * gib, false, 0};   // free/8 < 2 GiB here, so the reserve is free/8 or 512 MiB
		AssignHolds(l, tight);
		const uint64_t reserve = FetchReserve(tight.freeRam);
		Check((cost + reserve <= tight.freeRam) == (FetchCount(l, Hold::ram) == 4), "fetch: everything is in memory exactly when cost plus reserve fits");
	}
	// A small machine: the Windows image does not fit; the small files do (smallest first).
	room.freeRam = 3 * gib;
	AssignHolds(list, room);
	Check(list.items[0].hold == Hold::later && list.items[1].hold == Hold::ram && list.items[2].hold == Hold::ram && list.items[3].hold == Hold::ram,
		"fetch: 3 GB free: the Windows image is fetched later, the three small files up front");
	Check(UpfrontBytes(list) == 52 * mib + 8 * mib + 5 + 70 * mib && FetchBytes(list, Hold::later) == 8225329152ull, "fetch: byte totals of the two groups");
	char text[3000];
	{
		Out out(text, sizeof(text));
		FetchSummary(p, list, false, out);
		Check(Has(text, "I download every file in one go: 3 files, 131 megabytes together") && Has(text, "Only then is the disk changed")
			&& Has(text, "Only after the disk is prepared will I download: the installation image of Windows 11, 7845 megabytes, because it does not fit in memory")
			&& Has(text, "The packages that the installer of Debian fetches by itself during the installation are not part of this download") && !out.overflow,
			"fetch summary EN: what comes first, the late file by name and size, and Debian's own packages");
		if (gVerbose)
			printf("%s\n", text);
		Out pl(text, sizeof(text));
		FetchSummary(p, list, true, pl);
		Check(Has(text, "pobiorę wszystkie pliki naraz: plików 3, razem 131 megabajtów") && Has(text, "Dopiero po przygotowaniu dysku pobiorę: obraz instalacyjny systemu Windows 11, 7845 megabajtów, bo nie mieści się w pamięci")
			&& Has(text, "Pakiety, które instalator systemu Debian sam pobiera w trakcie instalacji, nie należą do tego pobierania") && !pl.overflow,
			"fetch summary PL");
		if (gVerbose)
			printf("%s\n", text);
	}
	// Progress: one count over all up-front files; the late file is not counted.
	{
		Out out(text, sizeof(text));
		FetchFileLine(p, list, 2, false, out);
		Check(std::string(text) == "File 2 of 3: the installer's kernel of Debian, 9 megabytes.", std::string("fetch progress line EN: ") + text);
		Out pl(text, sizeof(text));
		FetchFileLine(p, list, 3, true, pl);
		Check(std::string(text) == "Plik 3 z 3: instalator systemu Debian, 70 megabajtów.", std::string("fetch progress line PL: ") + text);
		Check(FetchPercent(list, 1, 0) == 0 && FetchPercent(list, 1, 26 * mib) == 19 && FetchPercent(list, 2, 0) == 39 && FetchPercent(list, 3, 70 * mib) == 100
			&& FetchBefore(list, 3) == 52 * mib + 8 * mib + 5, "fetch progress: percent of everything, across files");
	}
	// Nothing fits: every file is named as late; nothing is promised up front.
	room.freeRam = 400 * mib;
	AssignHolds(list, room);
	{
		Out out(text, sizeof(text));
		FetchSummary(p, list, false, out);
		Check(UpfrontCount(list) == 0 && !Has(text, "in one go") && Has(text, "the screen reader NVDA for Windows 11, 52 megabytes") && Has(text, "the installer of Debian, 70 megabytes"),
			"fetch: 400 MB free: nothing up front, every file named as fetched after the disk is prepared");
	}
	// The medium: a file under 4 GiB may wait there when it is writable and has room; a larger one may not.
	room = {400 * mib, true, 20 * gib};
	AssignHolds(list, room);
	Check(list.items[0].hold == Hold::later && list.items[1].hold == Hold::medium && list.items[3].hold == Hold::medium, "fetch: the medium takes files under 4 GiB only");
	room.mediumFree = 100 * mib;
	AssignHolds(list, room);
	Check(list.items[2].hold == Hold::medium && list.items[1].hold == Hold::later && list.items[3].hold == Hold::later,
		"fetch: a medium with 100 MB free takes the 9 MB kernel (64 MB are left spare) and nothing more");
	// A file of unknown size is never promised.
	{
		Plan d = MakePlan({kDos, kImgDebian});
		FetchList l = {};
		AddFetch(l, FetchWhat::dosPrograms, 0, 0, false);
		AddFetch(l, FetchWhat::image, 1, 3 * gib);
		FetchRoom big = {16 * gib, false, 0};
		AssignHolds(l, big);
		Out out(text, sizeof(text));
		FetchSummary(d, l, true, out);
		Check(l.items[0].hold == Hold::later && l.items[1].hold == Hold::ram && Has(text, "Dopiero po przygotowaniu dysku pobiorę: programy dla CharonDOS, rozmiar nie jest znany z góry")
			&& Has(text, "plików 1, razem 3 gigabajty") && !Has(text, "Pakiety"), "fetch: unknown size = later and said; an image plan has no sentence about packages");
		FetchList full = {};
		bool ok = true;
		for (int i = 0; i < kMaxFetch; i++)
			ok = AddFetch(full, FetchWhat::image, 0, 1) && ok;
		Check(ok && !AddFetch(full, FetchWhat::image, 0, 1) && full.count == kMaxFetch, "fetch: the list refuses a file it has no room for");
	}
	// The phases in the plan file and the report.
	{
		World w(MakePlan({kWin11, kDebian}));
		Upfront u = {};
		u.began = 5000;
		u.ended = 5125;
		u.placed = 5125 + 3700;
		u.files = 3;
		u.mb = 131;
		u.later = 1;
		Check(!UpfrontFromStore(w.store).present && UpfrontToStore(u, w.store), "fetch: a plan without [fetch] has no phases; the phases are stored");
		PlanToStore(w.plan, w.store);
		Check(w.Save() == Io::ok && w.Restart() == Io::ok, "fetch: plan file with [fetch] saved and read again");
		const Upfront back = UpfrontFromStore(w.store);
		Check(back.present && back.began == 5000 && back.ended == 5125 && back.placed == 8825 && back.files == 3 && back.mb == 131 && back.later == 1, "fetch: the phases survive a restart");
		Layout layout = {};
		Out with(text, sizeof(text));
		Report(w.plan, w.run, layout, GoodMachine(), "anna", false, with, 0, 0, &back);
		const std::string en = text;
		Check(Has(en.c_str(), "Up-front download, before the disk was changed: 3 files, 131 megabytes, time 2 min 5 s.\r\n")
			&& Has(en.c_str(), "Files downloaded after the disk was prepared, before the first installation: 1.\r\n")
			&& Has(en.c_str(), "Disk preparation and putting the files in place: 1 h 1 min.\r\n"), "report EN: the phases before the first system: " + en);
		Out withPl(text, sizeof(text));
		Report(w.plan, w.run, layout, GoodMachine(), "anna", true, withPl, 0, 0, &back);
		Check(Has(text, "Pobieranie z góry, przed zmianą dysku: plików 3, 131 megabajtów, czas 2 min 5 s.\r\n") && Has(text, "Przygotowanie dysku i rozłożenie plików na miejsca: 1 h 1 min.\r\n"), "report PL: the phases");
		Out without(text, sizeof(text));
		Report(w.plan, w.run, layout, GoodMachine(), "anna", false, without);
		Check(!Has(text, "Up-front") && !Has(text, "Boot entries after Charon"), "report: a plan without the phases and without a chosen order reads as before");
		char d[40];
		Out a(d, sizeof(d));
		Duration(a, 45);
		a.Ch('|');
		Duration(a, 60);
		a.Ch('|');
		Duration(a, 7260);
		Check(std::string(d) == "45 s|1 min 0 s|2 h 1 min", std::string("durations: ") + d);
		// "placed" is a key of one run: a saved answer set does not carry it.
		Check(RunKey("system.debian", "placed") && !RunKey("system.debian", "speech"), "sets: placed is a run key");
	}
	// r410: the wait that is in force, and whether the plan ends in its default system by itself.
	{
		World w(MakePlan({kWin11, kDebian}));
		w.plan.timeout = 10;
		Machine m = GoodMachine();
		for (int i = 0; i < w.run.count; i++)
			w.run.steps[i].state = StepState::done;
		Check(FinishOf(w.plan, w.run, m).timeout == 10 && FinishRestarts(w.plan, w.run, m), "finish: every system installed and a default system: the plan restarts into it by itself");
		m.waitNotStored = true;
		Layout layout = {};
		Out rep(text, sizeof(text));
		Report(w.plan, w.run, layout, m, "anna", false, rep);
		Check(FinishOf(w.plan, w.run, m).timeout == 5 && Has(text, "after 5 s it starts Windows 11"), "finish: a wait that could not be stored is not named; the report says the 5 s that are in force");
		m.waitNotStored = false;
		w.run.steps[2].state = StepState::skipped;
		Check(!FinishRestarts(w.plan, w.run, m), "finish: a skipped system: Charon waits for a key instead of restarting");
		w.run.steps[2].state = StepState::done;
		Copy(w.plan.defaultId, sizeof(w.plan.defaultId), "menu");
		Check(!FinishRestarts(w.plan, w.run, m), "finish: the menu waits: no restart by itself");
	}
	// ---- the order of the boot entries ----
	{
		Plan q = MakePlan({kWin11, kDebian, kImgUbuntu});
		int seq[kMaxSystems];
		Check(BootSequence(q, seq) == 3 && seq[0] == 0 && seq[1] == 1 && seq[2] == 2 && !BootOrderChosen(q), "boot order: none chosen = the list order, nothing rearranged");
		Copy(q.defaultId, sizeof(q.defaultId), "debian");
		Check(DefaultBootSequence(q, seq) == 3 && seq[0] == 1 && seq[1] == 0 && seq[2] == 2, "boot order: the offered default = the default system first, then the list");
		Copy(q.defaultId, sizeof(q.defaultId), "menu");
		Check(DefaultBootSequence(q, seq) == 3 && seq[0] == 0 && seq[1] == 1 && seq[2] == 2, "boot order: the menu waits = the list order is offered");
		const int chosen[3] = {2, 0, 1};
		Check(SetBootOrder(q, chosen, 3) && std::string(q.bootOrder) == "iubuntu,win11,debian" && BootOrderChosen(q), "boot order: stored as ids");
		Check(BootSequence(q, seq) == 3 && seq[0] == 2 && seq[1] == 0 && seq[2] == 1, "boot order: the chosen order is the sequence");
		const int bad[1] = {7};
		Check(!SetBootOrder(q, bad, 1) && std::string(q.bootOrder) == "iubuntu,win11,debian", "boot order: an index outside the plan is refused, nothing changed");
		// Store round trip, the version rule, known key.
		std::vector<char> sb(65536);
		Store store(sb.data(), sb.size());
		Check(PlanToStore(q, store) && std::string(store.GetOr("plan", "boot_order", "")) == "iubuntu,win11,debian" && std::string(store.GetOr("plan", "version", "")) == "2",
			"boot order: written as [plan] boot_order, and the plan file is version 2");
		Plan back;
		Clear(back);
		Refusals r;
		PlanFromStore(store, back, r);
		bool ignored = false;
		for (int i = 0; i < r.count; i++)
			ignored = ignored || r.list[i].code == Code::warnIgnoredKey;
		Check(!r.Refused() && !ignored && std::string(back.bootOrder) == "iubuntu,win11,debian", "boot order: read back; boot_order is a known key of [plan]");
		Plan plain = MakePlan({kWin11, kDebian});
		store.Wipe();
		PlanToStore(plain, store);
		Check(std::string(store.GetOr("plan", "version", "")) == "1" && store.Get("plan", "boot_order") == nullptr, "boot order: a plan without it stays version 1 (an older Charon reads it)");
		// partial order: the systems it does not name follow in list order
		store.Wipe();
		PlanToStore(q, store);
		store.Put("plan", "boot_order", "debian");
		Refusals r2;
		PlanFromStore(store, back, r2);
		Check(!r2.Refused() && BootSequence(back, seq) == 3 && seq[0] == 1 && seq[1] == 0 && seq[2] == 2, "boot order: a partial order is completed in list order");
		// refused: an unknown system, a system twice, a newer version
		store.Put("plan", "boot_order", "debian,haiku");
		Refusals r3;
		PlanFromStore(store, back, r3);
		Check(r3.Refused() && back.bootOrder[0] == 0, "boot order: an unknown system id is refused");
		store.Put("plan", "boot_order", "debian,win11,debian");
		Refusals r4;
		PlanFromStore(store, back, r4);
		Check(r4.Refused() && back.bootOrder[0] == 0, "boot order: a system named twice is refused");
		store.Put("plan", "boot_order", "debian,win11");
		store.Put("plan", "version", "3");
		Refusals r5;
		PlanFromStore(store, back, r5);
		Check(r5.Refused(), "plan file version 3 is refused (this version reads 1 and 2)");
		// CharonDOS keeps its rule.
		Plan withDos = MakePlan({kDos, kWin11, kDebian});
		const int two[3] = {2, 1, 0};
		SetBootOrder(withDos, two, 3);
		Check(!BootOrderChosen(withDos) && BootSequence(withDos, seq) == 3 && seq[0] == 0 && seq[1] == 1 && seq[2] == 2, "boot order: a plan with CharonDOS keeps its own rule");
		// Registry, summary and report follow the chosen order.
		World w(q);
		for (int i = 0; i < w.run.count; i++)
			w.run.steps[i].state = StepState::done;
		Place places[kMaxSystems] = {};
		for (int i = 0; i < 3; i++) {
			Copy(places[i].partition, sizeof(places[i].partition), "00000000-0000-0000-0000-000000000001");
			Copy(places[i].path, sizeof(places[i].path), "\\EFI\\x\\y.efi");
		}
		Out lines(text, sizeof(text));
		RegistryLines(w.plan, w.run, places, "2026-11-24", lines);
		const std::string reg = text;
		Check(reg.find("iubuntu|") < reg.find("win11|") && reg.find("win11|") < reg.find("debian|") && reg.find("iubuntu|") == 0, "registry: lines in the chosen order of the boot entries");
		Layout layout = {};
		Out rep(text, sizeof(text));
		Report(w.plan, w.run, layout, GoodMachine(), "anna", false, rep);
		Check(Has(text, "Boot entries after Charon, in the chosen order: Ubuntu (image), Windows 11, Debian.\r\n"), std::string("report EN names the order: ") + text);
		Out repPl(text, sizeof(text));
		Report(w.plan, w.run, layout, GoodMachine(), "anna", true, repPl);
		Check(Has(text, "Wpisy rozruchowe po Charonie w wybranej kolejności: Ubuntu (image), Windows 11, Debian.\r\n"), "report PL names the order");
		w.run.steps[1].state = StepState::failed;   // write-image of iubuntu
		w.run.steps[1].fail = Fail::charon;
		Out rep2(text, sizeof(text));
		Report(w.plan, w.run, layout, GoodMachine(), "anna", false, rep2);
		Check(Has(text, "in the chosen order: Windows 11, Debian.\r\n"), "report: a system that is not installed is not in the order line");
		Out sum(text, sizeof(text));
		Summary(w.plan, layout, GoodMachine(), 512, true, "anna", false, sum);
		Check(Has(text, "Boot entries after Charon, in this order: Ubuntu (image), Windows 11, Debian. "), "summary EN names the order");
		Out sumPl(text, sizeof(text));
		FetchList none = {};
		Summary(w.plan, layout, GoodMachine(), 512, true, "anna", true, sumPl, &none);
		Check(Has(text, "Wpisy rozruchowe po Charonie w kolejności: Ubuntu (image), Windows 11, Debian. "), "summary PL names the order");
		// A saved answer set carries the order.
		std::vector<char> ab(65536), sc(65536), lb(65536), tb(65536);
		Store answers(ab.data(), ab.size()), scratch(sc.data(), sc.size()), loaded(lb.data(), lb.size());
		answers.Put("shared", "user", "anna");
		answers.Put("shared", "hash", "$6$x$y");
		PlanToStore(q, answers);
		Out set(tb.data(), tb.size());
		Check(SetText(q, answers, "trio", true, false, "", nullptr, scratch, set) && Has(tb.data(), "boot_order=iubuntu,win11,debian"), "sets: the saved set carries boot_order");
		Plan fromSet;
		Clear(fromSet);
		Refusals r6;
		SetInfo info = {};
		Check(LoadSetText(tb.data(), set.length, loaded, fromSet, r6, info) && std::string(fromSet.bootOrder) == "iubuntu,win11,debian", "sets: the loaded set brings the order back");
	}
}

// r410 (helper bd): the exFAT file system of the shared data partition, checked against the specification's rules.
static bool ExfatWrite(void* c, uint64_t lba, const uint8_t* data, size_t bytes)
{
	auto& m = *static_cast<std::map<uint64_t, std::vector<uint8_t>>*>(c);
	for (size_t at = 0; at < bytes; at += 512)
		m[lba + at / 512].assign(data + at, data + at + 512);
	return true;
}
static void TestExfat()
{
	namespace ex = hakefi::plan::exfat;
	const uint64_t sizes[] = {uint64_t(64) << 11, uint64_t(300) << 11, uint64_t(40) << 21, uint64_t(2000) << 21};
	for (uint64_t sectors : sizes) {
		std::map<uint64_t, std::vector<uint8_t>> disk;
		ex::Sink sink{&disk, ExfatWrite};
		const std::string at = " (" + std::to_string(sectors >> 11) + " MiB)";
		Check(ex::Format(sink, 2048, sectors, u"DATA", 0x12345678), "exfat: formatted" + at);
		auto sec = [&](uint64_t lba) { static std::vector<uint8_t> zero(512, 0); auto f = disk.find(lba); return f == disk.end() ? zero : f->second; };
		auto u32 = [](const std::vector<uint8_t>& s, size_t o) { return uint32_t(s[o]) | uint32_t(s[o + 1]) << 8 | uint32_t(s[o + 2]) << 16 | uint32_t(s[o + 3]) << 24; };
		std::vector<uint8_t> region;
		for (int s = 0; s < 11; s++) { auto v = sec(s); region.insert(region.end(), v.begin(), v.end()); }
		const auto b = sec(0);
		Check(!memcmp(b.data() + 3, "EXFAT   ", 8) && b[510] == 0x55 && b[511] == 0xAA, "exfat: boot sector name and signature" + at);
		bool zero = true;
		for (int i = 11; i < 64; i++) zero = zero && b[i] == 0;
		Check(zero, "exfat: MustBeZero" + at);
		const uint32_t sum = ex::BootChecksum(region.data());
		bool sums = true;
		for (int i = 0; i < 512; i += 4) sums = sums && u32(sec(11), i) == sum;
		Check(sums, "exfat: boot checksum sector" + at);
		bool backup = true;
		for (int s = 0; s < 12; s++) backup = backup && sec(s) == sec(12 + s);
		Check(backup, "exfat: backup boot region identical" + at);
		ex::Geometry g;
		ex::Compute(sectors, g);
		Check(g.heapOffset % (1u << g.spcShift) == 0 && g.fatOffset >= 24 && uint64_t(g.fatLength) * 512 >= (uint64_t(g.clusterCount) + 2) * 4
			&& g.heapOffset >= g.fatOffset + g.fatLength && uint64_t(g.heapOffset) + (uint64_t(g.clusterCount) << g.spcShift) <= sectors,
			"exfat: geometry within the rules" + at);
		Check(u32(sec(g.fatOffset), 0) == 0xFFFFFFF8u && u32(sec(g.fatOffset), 4) == 0xFFFFFFFFu && u32(sec(g.fatOffset), 4 * g.rootCluster) == 0xFFFFFFFFu
			&& u32(sec(g.fatOffset), 4 * g.upcaseCluster) == 0xFFFFFFFFu, "exfat: FAT entries" + at);
		const uint64_t root = g.heapOffset + (uint64_t(g.rootCluster - 2) << g.spcShift);
		const auto r = sec(root);
		Check(r[0] == 0x83 && r[1] == 4 && r[2] == 'D' && r[32] == 0x81 && u32(r, 52) == 2 && u32(r, 56) == g.bitmapBytes && r[64] == 0x82 && u32(r, 84) == g.upcaseCluster && r[96] == 0,
			"exfat: root directory entries" + at);
		const uint64_t tl = g.heapOffset + (uint64_t(g.upcaseCluster - 2) << g.spcShift);
		std::vector<uint8_t> table;
		for (uint32_t s = 0; s < 4; s++) { auto v = sec(tl + s); table.insert(table.end(), v.begin(), v.end()); }
		table.resize(u32(r, 88));
		Check(ex::TableChecksum(table.data(), table.size()) == u32(r, 68), "exfat: up-case table checksum" + at);
		std::vector<uint16_t> map;
		for (size_t i = 0; i + 1 < table.size(); i += 2) {
			const uint16_t v = uint16_t(table[i] | table[i + 1] << 8);
			if (v == 0xFFFF && i + 3 < table.size()) { const uint16_t n = uint16_t(table[i + 2] | table[i + 3] << 8); for (uint16_t k = 0; k < n; k++) map.push_back(uint16_t(map.size())); i += 2; }
			else map.push_back(v);
		}
		Check(map.size() == 65536 && map['a'] == 'A' && map['Z'] == 'Z' && map[0x105] == 0x104 && map[0x142] == 0x141 && map[0x17C] == 0x17B && map[0x3B1] == 0x391,
			"exfat: up-case table expands to 65536 entries, a->A, Polish letters" + at);
		const uint64_t bm = g.heapOffset;
		Check((sec(bm)[0] & 1) && ((sec(bm)[(g.rootCluster - 2) / 8] >> ((g.rootCluster - 2) % 8)) & 1) && !((sec(bm)[(g.rootCluster - 1) / 8] >> ((g.rootCluster - 1) % 8)) & 1),
			"exfat: allocation bitmap marks exactly the clusters in use" + at);
	}
	ex::Geometry g;
	Check(!ex::Compute(uint64_t(32) << 11, g), "exfat: a partition under 64 MiB is refused");
}

int main(int argc, char** argv)
{
	if (argc > 2 && !strcmp(argv[1], "--emit"))
		return EmitWindows(argv[2]);
	gVerbose = argc > 1 && !strcmp(argv[1], "-v");
	TestPlaceholders(512);
	TestPlaceholders(4096);
	TestOrder();
	TestValidate();
	TestLayout();
	TestFile();
	TestPlanFile();
	TestStateMachine();
	TestPlanSets();
	TestClockAndIdentity();
	TestFetchAndBootOrder();
	TestExfat();
	printf("%s charon-r409-plan-tests: %d passed, %d failed\n", gFailed == 0 ? "PASS" : "FAIL", gPassed, gFailed);
	return gFailed == 0 ? 0 : 1;
}
