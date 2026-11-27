// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: the hardware clock in universal time, shown as local time.
//
// A multi-system plan keeps the hardware clock in universal time in every
// system (user decision 2026-11-23, also for CharonDOS + Windows alone). The
// finish step writes \EFI\HAK\clock.txt:
//     rtc=utc
//     zone=Europe/Warsaw
// Charon's clock tools (CharonSystemTools.h) and CharonDOS (DATE, TIME, file
// times, programs) read it and show local time; setting the clock converts
// back. Without the file, or with rtc=local, the clock is taken as it is -
// exactly the behaviour before r409.
//
// The zones are the ones the installer offers (CharonWsInstall.h, zones[]):
// a fixed offset and one of two summer-time rules. Pure logic: no library
// call, no allocation, no floating point; shared by the EFI program, CharonDOS
// (src/dos) and the host tests.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hakefi {
namespace clockzone {

struct Stamp {
	int32_t year;
	uint8_t month, day, hour, minute, second;
};
enum : uint8_t { ruleNone = 0, ruleEurope = 1, ruleAmerica = 2 };
struct Zone {
	const char* name;
	int16_t offset; // minutes east of universal time, standard time
	uint8_t rule;
};
inline const Zone* Zones(size_t& count)
{
	static const Zone zones[] = {
		{"Europe/Warsaw", 60, ruleEurope}, {"Europe/London", 0, ruleEurope}, {"Europe/Berlin", 60, ruleEurope},
		{"Europe/Paris", 60, ruleEurope}, {"Europe/Madrid", 60, ruleEurope}, {"Europe/Rome", 60, ruleEurope},
		{"Europe/Kyiv", 120, ruleEurope}, {"Europe/Prague", 60, ruleEurope}, {"America/New_York", -300, ruleAmerica},
		{"America/Chicago", -360, ruleAmerica}, {"America/Denver", -420, ruleAmerica},
		{"America/Los_Angeles", -480, ruleAmerica}, {"UTC", 0, ruleNone},
	};
	count = sizeof(zones) / sizeof(zones[0]);
	return zones;
}
inline bool SameName(const char* a, const char* b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x = char(x + 32);
		if (y >= 'A' && y <= 'Z') y = char(y + 32);
		if (x != y)
			return false;
	}
	return b[n] == 0;
}
inline const Zone* Find(const char* name, size_t length)
{
	size_t count = 0;
	const Zone* zones = Zones(count);
	for (size_t i = 0; i < count; i++)
		if (SameName(name, zones[i].name, length))
			return &zones[i];
	return nullptr;
}

// Days since 1970-01-01 of a calendar date, and back (proleptic Gregorian).
inline int64_t Days(int32_t year, uint32_t month, uint32_t day)
{
	const int32_t y = year - (month <= 2 ? 1 : 0);
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const uint32_t yoe = uint32_t(y - era * 400);
	const uint32_t doy = (153 * (month + (month > 2 ? uint32_t(-3) : 9)) + 2) / 5 + day - 1;
	const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + int64_t(doe) - 719468;
}
inline void Civil(int64_t days, int32_t& year, uint8_t& month, uint8_t& day)
{
	days += 719468;
	const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const uint32_t doe = uint32_t(days - era * 146097);
	const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const uint32_t mp = (5 * doy + 2) / 153;
	day = uint8_t(doy - (153 * mp + 2) / 5 + 1);
	month = uint8_t(mp < 10 ? mp + 3 : mp - 9);
	year = int32_t(int64_t(yoe) + era * 400) + (month <= 2 ? 1 : 0);
}
inline int64_t Minutes(const Stamp& s) { return (Days(s.year, s.month, s.day) * 24 + s.hour) * 60 + s.minute; }
inline Stamp FromMinutes(int64_t minutes, uint8_t second)
{
	Stamp s;
	int64_t days = minutes / 1440;
	int64_t rest = minutes % 1440;
	if (rest < 0) {
		rest += 1440;
		days--;
	}
	Civil(days, s.year, s.month, s.day);
	s.hour = uint8_t(rest / 60);
	s.minute = uint8_t(rest % 60);
	s.second = second;
	return s;
}
// 0 = Sunday.
inline uint32_t Weekday(int64_t days) { return uint32_t(((days % 7) + 11) % 7); }
// The n-th Sunday of a month (n from 1), or the last one with n = 0.
inline int64_t Sunday(int32_t year, uint32_t month, uint32_t n)
{
	if (n == 0) {
		const int64_t last = Days(month == 12 ? year + 1 : year, month == 12 ? 1 : month + 1, 1) - 1;
		return last - Weekday(last);
	}
	const int64_t first = Days(year, month, 1);
	return first + (7 - Weekday(first)) % 7 + 7 * int64_t(n - 1);
}
// True when summer time is in force in the zone at that universal minute.
inline bool Summer(const Zone& zone, int64_t utcMinutes)
{
	if (zone.rule == ruleNone)
		return false;
	int32_t year;
	uint8_t month, day;
	// The year of the universal date: no rule changes within a day of New Year.
	Civil((utcMinutes - (utcMinutes < 0 ? 1439 : 0)) / 1440, year, month, day);
	int64_t start, end;
	if (zone.rule == ruleEurope) {
		// Last Sunday of March to last Sunday of October, 01:00 universal time.
		start = Sunday(year, 3, 0) * 1440 + 60;
		end = Sunday(year, 10, 0) * 1440 + 60;
	} else {
		// Second Sunday of March 02:00 standard time to first Sunday of
		// November 02:00 summer time (01:00 standard time).
		start = Sunday(year, 3, 2) * 1440 + 120 - zone.offset;
		end = Sunday(year, 11, 1) * 1440 + 60 - zone.offset;
	}
	return utcMinutes >= start && utcMinutes < end;
}
inline Stamp Local(const Zone& zone, const Stamp& utc, bool* summer = nullptr)
{
	const int64_t m = Minutes(utc);
	const bool s = Summer(zone, m);
	if (summer != nullptr)
		*summer = s;
	return FromMinutes(m + zone.offset + (s ? 60 : 0), utc.second);
}
// The universal time of a local time. The hour that occurs twice in autumn is
// taken as its first occurrence (summer time); a time inside the hour that
// spring skips comes out one hour later, as clocks show it.
inline Stamp Universal(const Zone& zone, const Stamp& local)
{
	const int64_t standard = Minutes(local) - zone.offset;
	return FromMinutes(Summer(zone, standard - 60) ? standard - 60 : standard, local.second);
}

struct Setting {
	bool universal;   // the hardware clock keeps universal time
	const Zone* zone; // null: no zone known (shown as it is)
};
// Reads clock.txt. Unknown lines are ignored; an unknown zone leaves the
// clock as it is (never a guessed zone).
inline Setting Parse(const char* text, size_t size)
{
	Setting setting = {false, nullptr};
	bool universal = false;
	for (size_t at = 0; text != nullptr && at < size;) {
		size_t end = at;
		while (end < size && text[end] != '\n')
			end++;
		size_t length = end - at;
		while (length > 0 && (text[at + length - 1] == '\r' || text[at + length - 1] == ' '))
			length--;
		const char* line = text + at;
		at = end + 1;
		if (length > 4 && SameName(line, "rtc=", 4))
			universal = SameName(line + 4, "utc", length - 4);
		else if (length > 5 && SameName(line, "zone=", 5))
			setting.zone = Find(line + 5, length - 5);
	}
	setting.universal = universal && setting.zone != nullptr;
	return setting;
}
// The file's text; returns its length, 0 when it does not fit.
inline size_t Text(const char* zoneName, char* out, size_t capacity)
{
	const char* parts[4] = {"rtc=utc\r\nzone=", zoneName, "\r\n", nullptr};
	size_t n = 0;
	for (int p = 0; parts[p] != nullptr; p++)
		for (size_t i = 0; parts[p][i] != 0; i++) {
			if (n + 1 >= capacity)
				return 0;
			out[n++] = parts[p][i];
		}
	out[n] = 0;
	return n;
}
// What a clock reading means: the local time when the setting says the clock
// keeps universal time, the reading itself otherwise.
inline Stamp Shown(const Setting& setting, const Stamp& clock, bool* summer = nullptr)
{
	if (summer != nullptr)
		*summer = false;
	return setting.universal ? Local(*setting.zone, clock, summer) : clock;
}
// What to write into the clock for a time the user gave.
inline Stamp Stored(const Setting& setting, const Stamp& wanted)
{
	return setting.universal ? Universal(*setting.zone, wanted) : wanted;
}

} // namespace clockzone
} // namespace hakefi
