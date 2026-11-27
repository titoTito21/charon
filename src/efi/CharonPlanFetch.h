#pragma once
// Charon r410: "a plan downloads everything first" - the pure part (host tested:
// tests/charon-r409-plan-tests.cpp, TestFetch). No firmware, no network here.
//
//   FetchList     every file Charon itself fetches for a plan, with its size
//   AssignHolds   where each file waits before the disk is touched: in memory when it fits
//                 (smallest first, a reserve is kept), on Charon's own medium when that is
//                 allowed, otherwise it is fetched only after the disk is prepared ("later")
//   FetchSummary  the sentences of the plan summary: what is fetched first, what later (by
//                 name and size), and what an installer fetches by itself
//   FetchFileLine / FetchPercent   the ONE combined progress of the up-front download
//   Upfront       the phase times in the plan file ([fetch]) and their lines in the report
//
// Design: work/r410a-evidence/ef/DESIGN.txt. GPL-2.0-or-later.
#include "CharonPlanModel.h"

namespace hakefi::plan {

enum class FetchWhat : uint8_t { kernel, initrd, image, windows, reader, dosPrograms,
	hypervisor }; // r410: agent 2's Charon Hypervisor (Alpine Linux and QEMU; held by hv::FetchUpfront itself)
enum class Hold : uint8_t { ram, medium, later };
struct FetchItem {
	FetchWhat what;
	int8_t system;
	bool sizeKnown;
	Hold hold;
	uint64_t bytes;
};
constexpr int kMaxFetch = kMaxSystems * 3;
struct FetchList {
	FetchItem items[kMaxFetch];
	int count;
};
// What the machine offers. mediumFree is looked at only when mediumWritable.
struct FetchRoom {
	uint64_t freeRam;
	bool mediumWritable;
	uint64_t mediumFree;
};
constexpr uint64_t kFetchPiece = uint64_t(64) << 20;      // memory is taken in pieces of this size
constexpr uint64_t kFetchReserveLeast = uint64_t(512) << 20;
constexpr uint64_t kFatFileLimit = (uint64_t(4) << 30) - 1; // one file on FAT
constexpr uint64_t kMediumSpare = uint64_t(64) << 20;

inline bool AddFetch(FetchList& list, FetchWhat what, int system, uint64_t bytes, bool sizeKnown = true)
{
	if (list.count >= kMaxFetch)
		return false;
	FetchItem& item = list.items[list.count++];
	item.what = what;
	item.system = int8_t(system);
	item.sizeKnown = sizeKnown;
	item.bytes = sizeKnown ? bytes : 0;
	item.hold = Hold::later;
	return true;
}
// Memory a held file really takes.
inline uint64_t FetchCost(uint64_t bytes) { return (bytes + kFetchPiece - 1) / kFetchPiece * kFetchPiece; }
// The reserve that stays free for Charon's own work and the firmware: 512 MiB, or an eighth when that is more.
inline uint64_t FetchReserve(uint64_t freeRam) { return freeRam / 8 > kFetchReserveLeast ? freeRam / 8 : kFetchReserveLeast; }

// Decides `hold` of every item. Smallest files first into memory, so that the one file that does
// not fit is the largest one. A file of unknown or zero size is never promised up front.
inline void AssignHolds(FetchList& list, const FetchRoom& room)
{
	const uint64_t reserve = FetchReserve(room.freeRam);
	uint64_t ram = room.freeRam > reserve ? room.freeRam - reserve : 0;
	uint64_t medium = room.mediumWritable ? room.mediumFree : 0;
	bool taken[kMaxFetch] = {};
	for (int i = 0; i < list.count; i++)
		list.items[i].hold = Hold::later;
	for (int round = 0; round < list.count; round++) {
		int best = -1;
		for (int i = 0; i < list.count; i++)
			if (!taken[i] && list.items[i].sizeKnown && list.items[i].bytes != 0
				&& (best < 0 || list.items[i].bytes < list.items[best].bytes))
				best = i;
		if (best < 0)
			break;
		taken[best] = true;
		FetchItem& item = list.items[best];
		const uint64_t cost = FetchCost(item.bytes);
		if (cost <= ram) {
			item.hold = Hold::ram;
			ram -= cost;
		} else if (item.bytes <= kFatFileLimit && item.bytes + kMediumSpare <= medium) {
			item.hold = Hold::medium;
			medium -= item.bytes;
		}
	}
}
inline int FetchCount(const FetchList& list, Hold hold)
{
	int n = 0;
	for (int i = 0; i < list.count; i++)
		n += list.items[i].hold == hold ? 1 : 0;
	return n;
}
inline uint64_t FetchBytes(const FetchList& list, Hold hold)
{
	uint64_t n = 0;
	for (int i = 0; i < list.count; i++)
		n += list.items[i].hold == hold ? list.items[i].bytes : 0;
	return n;
}
// Up front = everything that is there before the disk is touched (memory or the medium).
inline bool IsUpfront(const FetchItem& item) { return item.hold != Hold::later; }
inline int UpfrontCount(const FetchList& list) { return FetchCount(list, Hold::ram) + FetchCount(list, Hold::medium); }
inline uint64_t UpfrontBytes(const FetchList& list) { return FetchBytes(list, Hold::ram) + FetchBytes(list, Hold::medium); }

inline const char* FetchSystemName(const Plan& plan, const FetchItem& item)
{
	return item.system >= 0 && item.system < plan.count ? plan.systems[item.system].name : "?";
}
inline void FetchName(const Plan& plan, const FetchItem& item, bool pl, Out& out)
{
	const char* name = FetchSystemName(plan, item);
	switch (item.what) {
	case FetchWhat::kernel:
		out.S(pl ? "jądro instalatora systemu " : "the installer's kernel of ").S(name);
		break;
	case FetchWhat::initrd:
		out.S(pl ? "instalator systemu " : "the installer of ").S(name);
		break;
	case FetchWhat::image:
		out.S(pl ? "gotowy obraz systemu " : "the ready image of ").S(name);
		break;
	case FetchWhat::windows:
		out.S(pl ? "obraz instalacyjny systemu " : "the installation image of ").S(name);
		break;
	case FetchWhat::reader:
		out.S(pl ? "czytnik ekranu NVDA dla systemu " : "the screen reader NVDA for ").S(name);
		break;
	case FetchWhat::hypervisor:
		out.S(pl ? "Charon Hypervisor: Alpine Linux 3.24 i QEMU" : "Charon Hypervisor: Alpine Linux 3.24 and QEMU");
		break;
	default:
		out.S(pl ? "programy dla CharonDOS" : "the programs for CharonDOS");
		break;
	}
}
inline void FetchSize(Out& out, uint64_t bytes, bool pl)
{
	const uint64_t mib = (bytes + (uint64_t(1) << 20) - 1) >> 20;
	Size(out, mib != 0 ? mib : 1, pl);
}

// The sentences of the plan summary (before the typed word). Nothing is left unsaid: every file
// that is fetched only after the disk is prepared is named with its size.
inline void FetchSummary(const Plan& plan, const FetchList& list, bool pl, Out& out)
{
	const int upfront = UpfrontCount(list);
	if (upfront != 0) {
		if (pl) out.S("Najpierw, zanim cokolwiek zmienię na dysku, pobiorę wszystkie pliki naraz: plików ").N(uint64_t(upfront)).S(", razem ");
		else out.S("First, before anything on the disk is changed, I download every file in one go: ").N(uint64_t(upfront)).S(upfront == 1 ? " file, " : " files, ");
		FetchSize(out, UpfrontBytes(list), pl);
		out.S(pl ? ". Dopiero potem zmieniam dysk, a instalacje idą jedna po drugiej bez pobierania między nimi. "
			: " together. Only then is the disk changed, and the installations follow one another with no download in between. ");
	}
	for (int i = 0; i < list.count; i++) {
		const FetchItem& item = list.items[i];
		if (item.hold == Hold::medium) {
			out.S(pl ? "Na nośniku Charona, a nie w pamięci, poczeka: " : "Waiting on Charon's medium instead of in memory: ");
			FetchName(plan, item, pl, out);
			out.S(", ");
			FetchSize(out, item.bytes, pl);
			out.S(". ");
		} else if (item.hold == Hold::later) {
			out.S(pl ? "Dopiero po przygotowaniu dysku pobiorę: " : "Only after the disk is prepared will I download: ");
			FetchName(plan, item, pl, out);
			if (item.sizeKnown && item.bytes != 0) {
				out.S(", ");
				FetchSize(out, item.bytes, pl);
				out.S(pl ? ", bo nie mieści się w pamięci. " : ", because it does not fit in memory. ");
			} else
				out.S(pl ? ", rozmiar nie jest znany z góry. " : ", its size is not known in advance. ");
		}
	}
	for (int i = 0; i < plan.count; i++)
		if (plan.systems[i].kind == Kind::ubuntu) {   // r410 (bd): subiquity fetches the system image named by iso-url itself
			if (pl) out.S("Instalator systemu ").S(plan.systems[i].name).S(" sam pobiera w trakcie instalacji obraz systemu, prawie 3 gigabajty, i pakiety; to nie należy do tego pobierania. ");
			else out.S("The installer of ").S(plan.systems[i].name).S(" fetches the system image, almost 3 gigabytes, and packages by itself during the installation; they are not part of this download. ");
		} else if (IsInstallerLinux(plan.systems[i].kind)) {
			if (pl) out.S("Pakiety, które instalator systemu ").S(plan.systems[i].name).S(" sam pobiera w trakcie instalacji, nie należą do tego pobierania. ");
			else out.S("The packages that the installer of ").S(plan.systems[i].name).S(" fetches by itself during the installation are not part of this download. ");
		} else if (plan.systems[i].kind == Kind::hypervisor) {   // r410: agent 2's sentences (design part 9)
			// r410 (agent 1, h7a): it is one of the files fetched first, so "during the installation" contradicted the
			// sentence before it
			out.S(pl ? "Charon Hypervisor pochodzi z oficjalnych serwerów Alpine Linux, a ich podpisy są sprawdzane; bez połączenia z siecią nie da się go zainstalować. "
				: "Charon Hypervisor comes from Alpine Linux's official servers, and their signatures are checked; it cannot be installed without a network connection. ");
			out.S(pl ? "Przy włączonym Secure Boot Charon Hypervisor się nie uruchomi: jądro Alpine nie jest do tego podpisane. "
				: "With Secure Boot on, Charon Hypervisor does not start: Alpine's kernel is not signed for it. ");
		}
}

// The combined progress. `index` is an item of the list that is fetched up front.
// "File 2 of 5: the ready image of Debian 13, 3 GB."
inline void FetchFileLine(const Plan& plan, const FetchList& list, int index, bool pl, Out& out)
{
	int number = 0;
	for (int i = 0; i <= index && i < list.count; i++)
		number += IsUpfront(list.items[i]) ? 1 : 0;
	if (pl) out.S("Plik ").N(uint64_t(number)).S(" z ").N(uint64_t(UpfrontCount(list))).S(": ");
	else out.S("File ").N(uint64_t(number)).S(" of ").N(uint64_t(UpfrontCount(list))).S(": ");
	FetchName(plan, list.items[index], pl, out);
	out.S(", ");
	FetchSize(out, list.items[index].bytes, pl);
	out.Ch('.');
}
// Bytes of everything fetched up front that lie before item `index`.
inline uint64_t FetchBefore(const FetchList& list, int index)
{
	uint64_t n = 0;
	for (int i = 0; i < index && i < list.count; i++)
		n += IsUpfront(list.items[i]) ? list.items[i].bytes : 0;
	return n;
}
// Percent of EVERYTHING when `done` bytes of item `index` have arrived.
inline unsigned FetchPercent(const FetchList& list, int index, uint64_t done)
{
	const uint64_t all = UpfrontBytes(list);
	if (all == 0)
		return 100;
	const uint64_t have = FetchBefore(list, index) + done;
	return have >= all ? 100 : unsigned(have * 100 / all);
}

// ---- the phases in the plan file and in the report ------------------------------------
struct Upfront {
	bool present;      // the plan file has a [fetch] section
	uint64_t began, ended; // seconds: the up-front download
	uint64_t placed;   // seconds: the disk was prepared and the files were in place
	uint32_t files;    // fetched up front
	uint64_t mb;
	uint32_t later;    // fetched after the disk was prepared
};
inline bool UpfrontToStore(const Upfront& u, Store& store)
{
	bool ok = store.PutNumber("fetch", "began", u.began);
	ok = store.PutNumber("fetch", "ended", u.ended) && ok;
	ok = store.PutNumber("fetch", "placed", u.placed) && ok;
	ok = store.PutNumber("fetch", "files", u.files) && ok;
	ok = store.PutNumber("fetch", "mb", u.mb) && ok;
	ok = store.PutNumber("fetch", "later", u.later) && ok;
	return ok;
}
inline Upfront UpfrontFromStore(const Store& store)
{
	Upfront u = {};
	u.present = store.HasSection("fetch");
	uint64_t v = 0;
	ParseNumber(store.GetOr("fetch", "began", "0"), u.began);
	ParseNumber(store.GetOr("fetch", "ended", "0"), u.ended);
	ParseNumber(store.GetOr("fetch", "placed", "0"), u.placed);
	if (ParseNumber(store.GetOr("fetch", "files", "0"), v))
		u.files = uint32_t(v);
	ParseNumber(store.GetOr("fetch", "mb", "0"), u.mb);
	if (ParseNumber(store.GetOr("fetch", "later", "0"), v))
		u.later = uint32_t(v);
	return u;
}
// "45 s", "2 min 5 s", "1 h 3 min".
inline void Duration(Out& out, uint64_t seconds)
{
	if (seconds >= 3600)
		out.N(seconds / 3600).S(" h ").N(seconds % 3600 / 60).S(" min");
	else if (seconds >= 60)
		out.N(seconds / 60).S(" min ").N(seconds % 60).S(" s");
	else
		out.N(seconds).S(" s");
}
// The lines of the final report about the phases before the first system. A plan file without
// [fetch] (made by an older Charon) gives no line.
inline void UpfrontReport(const Upfront& u, bool pl, Out& out)
{
	if (!u.present)
		return;
	if (u.files != 0) {
		if (pl) out.S("Pobieranie z góry, przed zmianą dysku: plików ").N(u.files).S(", ");
		else out.S("Up-front download, before the disk was changed: ").N(u.files).S(u.files == 1 ? " file, " : " files, ");
		Size(out, u.mb != 0 ? u.mb : 1, pl);
		if (u.ended >= u.began && u.began != 0) {
			out.S(pl ? ", czas " : ", time ");
			Duration(out, u.ended - u.began);
		}
		out.S(".\r\n");
	} else
		out.S(pl ? "Pobieranie z góry: żaden plik nie czekał w pamięci.\r\n" : "Up-front download: no file waited in memory.\r\n");
	if (u.later != 0) {
		if (pl) out.S("Po przygotowaniu dysku, przed pierwszą instalacją, pobrano jeszcze plików: ").N(u.later).S(".\r\n");
		else out.S("Files downloaded after the disk was prepared, before the first installation: ").N(u.later).S(".\r\n");
	}
	if (u.placed >= u.ended && u.ended != 0) {
		out.S(pl ? "Przygotowanie dysku i rozłożenie plików na miejsca: " : "Disk preparation and putting the files in place: ");
		Duration(out, u.placed - u.ended);
		out.S(".\r\n");
	}
}

} // namespace hakefi::plan
