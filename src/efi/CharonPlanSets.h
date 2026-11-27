#pragma once
// r410: saved answer sets of a PLAN of several systems (pure: no firmware, no files; host-tested by
// tests/charon-r409-plan-tests.cpp, TestPlanSets).
//
// A plan's answers already live in the plan's store as [shared] (asked once) and [system.<id>] (each
// system's own answers on top of the shared ones), and the plan itself as [plan]. A saved set is that
// store written in the answer-set format of the questions specification, section 6.1:
//
//   [charon-answers] format=1        the header
//   [plan]                           systems, default, timeout, on_failure, leftover, ..., unattended
//   [disk.1] serial=...              only in a set for unattended use (the disk it may erase)
//   [shared]                         the shared part: asked once, used by every system
//   [system.<id>]                    the per-system part (kind, name, version, sizes and its answers)
//   [secrets] password=ask           "ask": the password is asked when the set is loaded;
//                                    clear text only when the author confirmed it with the typed word
//
// What never goes into a set: the password hash, anything plan::SecretKey() names (outside
// [secrets]), and what belongs to one run on one disk (the plan's id, partition identities).
// Loading never starts anything: the caller still shows the summary and requires the typed word
// (user decision 2026-11-23: the typed word is ALWAYS required at the machine).
// GPL-2.0-or-later.
#include "CharonPlanModel.h"
namespace hakefi {
namespace plan {

// Keys of one run on one disk: not part of a set.
inline bool RunKey(const char* section, const char* key)
{
	if (Eq(key, "hash"))
		return true;
	if (Eq(section, "plan"))
		return Eq(key, "id");
	static const char* const names[] = {"esp_guid", "seed_lba", "placed"};
	for (const char* name : names)
		if (Eq(key, name))
			return true;
	return false;
}
inline bool SetSection(const char* section)
{
	return Eq(section, "shared") || (section[0] == 's' && section[1] == 'y' && section[2] == 's' && section[3] == 't'
		&& section[4] == 'e' && section[5] == 'm' && section[6] == '.');
}
// The set's text. `scratch` is an empty store the caller lends (it is wiped before and after).
// clearPassword: null or "" = "[secrets] password=ask". diskSerial: null or "" = no [disk.1] section.
// False: something did not fit (nothing usable in `out`).
inline bool SetText(const Plan& plan, const Store& answers, const char* setName, bool detailed, bool unattended,
	const char* diskSerial, const char* clearPassword, Store& scratch, Out& out)
{
	scratch.Wipe();
	bool ok = true;
	Plan copy = plan;
	Copy(copy.name, sizeof(copy.name), setName);
	copy.unattended = unattended;
	for (auto& rule : copy.disks)
		rule = DiskRule{};
	if (unattended && diskSerial != nullptr && diskSerial[0] != 0) {
		copy.disks[0].present = true;
		ok = Copy(copy.disks[0].serial, sizeof(copy.disks[0].serial), diskSerial) && ok;
		copy.disks[0].existing = Existing::erase;
		copy.disks[0].existingGiven = true;
	}
	// The sections in the order of the specification: plan and disk first (PlanToStore), shared, systems.
	ok = PlanToStore(copy, scratch) && ok;
	ok = scratch.Put("plan", "mode", detailed ? "detailed" : "quick") && ok;
	const size_t count = answers.Count();
	for (int pass = 0; pass < 2; pass++)
		for (size_t i = 0; i < count; i++) {
			const char* section = nullptr;
			const char* key = nullptr;
			const char* value = nullptr;
			answers.At(i, section, key, value);
			if (!SetSection(section) || Eq(section, "shared") != (pass == 0))
				continue;
			if (RunKey(section, key) || SecretKey(section, key))
				continue;
			if (pass == 1 && scratch.Has(section, key))
				continue;   // the model's own keys were written from the plan
			ok = scratch.Put(section, key, value) && ok;
		}
	scratch.Remove("plan", "id");
	out.S("[charon-answers]\r\nformat=1\r\nmade=charon-r410\r\n");
	// In the order of the specification: [plan], [disk.1], [shared], then the systems in the plan's order.
	auto section = [&](const char* name) {
		bool head = false;
		const size_t n = scratch.Count();
		for (size_t i = 0; i < n; i++) {
			const char* s2 = nullptr;
			const char* k2 = nullptr;
			const char* v2 = nullptr;
			scratch.At(i, s2, k2, v2);
			if (!Eq(s2, name) || SecretKey(s2, k2))
				continue;
			if (!head)
				out.Ch('[').S(name).S("]\r\n");
			head = true;
			out.S(k2).Ch('=').S(v2).S("\r\n");
		}
	};
	section("plan");
	section("disk.1");
	section("shared");
	for (int i = 0; i < copy.count; i++) {
		char name[40];
		section(SystemSection(copy.systems[i].id, name, sizeof(name)));
	}
	out.S("[secrets]\r\n");
	if (clearPassword != nullptr && clearPassword[0] != 0)
		out.S("; This file holds a password in clear text. Keep it away from other people.\r\npassword=").S(clearPassword).S("\r\n");
	else
		out.S("password=ask\r\n");
	ok = ok && !scratch.overflow && !out.overflow;
	scratch.Wipe();
	return ok;
}

struct SetInfo {
	bool isSet;          // has the header [charon-answers]
	bool isPlan;         // names two or more systems
	bool unattended;
	bool passwordInside; // [secrets] password is there and is not "ask"
	bool detailed;
	int systems;
};
// A look at a set that is already parsed into `store` (nothing is changed).
inline SetInfo Inspect(const Store& store)
{
	SetInfo info{};
	info.isSet = store.HasSection("charon-answers");
	const char* list = store.GetOr("plan", "systems", "");
	for (size_t i = 0; list[i] != 0;) {
		while (list[i] == ' ' || list[i] == ',')
			i++;
		if (list[i] == 0)
			break;
		info.systems++;
		while (list[i] != 0 && list[i] != ',')
			i++;
	}
	info.isPlan = info.systems >= 2;
	info.unattended = Eq(store.GetOr("plan", "unattended", "no"), "yes");
	info.detailed = Eq(store.GetOr("plan", "mode", "quick"), "detailed");
	const char* password = store.Get("secrets", "password");
	info.passwordInside = password != nullptr && password[0] != 0 && !Eq(password, "ask");
	return info;
}
// Reads a set's text into `store` (wiped first) and makes the plan of it. The result is usable only
// when it returns true: a file that is not a set, a syntax error or any refusal of PlanFromStore
// leaves `report` filled for speaking and the caller must not go on.
inline bool LoadSetText(const char* text, size_t size, Store& store, Plan& plan, Refusals& report, SetInfo& info)
{
	store.Wipe();
	const bool parsed = ParseIni(text, size, store, report);
	info = Inspect(store);
	if (!parsed || !info.isSet || store.overflow)
		return false;
	PlanFromStore(store, plan, report);
	return !report.Refused();
}
// After the password is known: the hash goes where the dialogue would have put it ([shared] and every
// system's own section), the secrets and the header leave the store (plan.txt never holds them).
inline bool FinishLoad(Store& store, const Plan& plan, const char* hash)
{
	bool ok = store.Put("shared", "hash", hash);
	for (int i = 0; i < plan.count; i++) {
		char section[40];
		SystemSection(plan.systems[i].id, section, sizeof(section));
		ok = store.Put(section, "hash", hash) && ok;
	}
	store.RemoveSection("secrets");
	store.RemoveSection("charon-answers");
	return ok && !store.overflow;
}

} // namespace plan
} // namespace hakefi
