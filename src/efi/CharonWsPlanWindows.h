// SPDX-License-Identifier: GPL-2.0-or-later
// Charon r409: a Windows as a step of a multi-system plan - what its answer file needs beyond the
// single-system one. PREPARED FOR PHASE B2b: nothing includes this file in the EFI program yet;
// it is host tested with golden files (tests/golden/charon-r409-plan/).
//
// What a plan needs from WindowsAnswers (CharonWsInstall2.h), and how little of it is new:
//   - the disk: no CreatePartitions, one ModifyPartition (NTFS), InstallTo DiskID 0 / PartitionID.
//     That is exactly the EXISTING branch for the answer diskmode=partition; the plan sets that
//     answer and passes plan::PositionNumber() as the partition number. No new XML.
//   - the clock in universal time and BitLocker prevention: the EXISTING answers clockutc=yes and
//     nobitlocker=yes; the plan sets them. No new XML.
//   - new, here: the first-logon lines of a plan step (FirstLogon) and the time zone element
//     (TimeZoneElement), each to be called from WindowsAnswers by ONE line when the answer "plan"
//     is set, so that a single-system answer file does not change by a byte.
//
// Pure text over any sink with Add(const char*) and Number(uint64_t) (install::Text has both).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hakefi {
namespace planwin {

inline bool Same(const char* a, const char* b)
{
	size_t i = 0;
	for (; a[i] != 0 && a[i] == b[i]; i++) {
	}
	return a[i] == b[i];
}
// The Windows name of a zone the installer offers (tzutil /l names); null for an unknown zone,
// and then no TimeZone element is written (Windows keeps deriving the zone from the locale, as
// in a single-system installation).
inline const char* WindowsZone(const char* iana)
{
	static const char* const table[][2] = {
		{"Europe/Warsaw", "Central European Standard Time"}, {"Europe/London", "GMT Standard Time"},
		{"Europe/Berlin", "W. Europe Standard Time"}, {"Europe/Paris", "Romance Standard Time"},
		{"Europe/Madrid", "Romance Standard Time"}, {"Europe/Rome", "W. Europe Standard Time"},
		{"Europe/Kyiv", "FLE Standard Time"}, {"Europe/Prague", "Central Europe Standard Time"},
		{"America/New_York", "Eastern Standard Time"}, {"America/Chicago", "Central Standard Time"},
		{"America/Denver", "Mountain Standard Time"}, {"America/Los_Angeles", "Pacific Standard Time"},
		{"UTC", "UTC"},
	};
	if (iana == nullptr)
		return nullptr;
	for (const auto& row : table)
		if (Same(iana, row[0]))
			return row[1];
	return nullptr;
}

template <class Text> inline void Escaped(Text& x, const char* value)
{
	for (; *value != 0; value++) {
		const char c = *value;
		if (c == '&') x.Add("&amp;");
		else if (c == '<') x.Add("&lt;");
		else if (c == '>') x.Add("&gt;");
		else if (c == '"') x.Add("&quot;");
		else {
			const char one[2] = {c, 0};
			x.Add(one);
		}
	}
}
// One FirstLogonCommands entry, in the form RunCommand(x, order, command, true) writes.
template <class Text> inline void Command(Text& x, unsigned order, const char* command)
{
	x.Add("        <SynchronousCommand wcm:action=\"add\"><Order>");
	x.Number(order);
	x.Add("</Order><CommandLine>");
	Escaped(x, command);
	x.Add("</CommandLine></SynchronousCommand>\n");
}
// "        <TimeZone>Central European Standard Time</TimeZone>" for the Shell-Setup component of
// the specialize pass; nothing for a zone that is not in the table.
template <class Text> inline void TimeZoneElement(Text& x, const char* iana)
{
	const char* zone = WindowsZone(iana);
	if (zone == nullptr)
		return;
	x.Add("        <TimeZone>");
	Escaped(x, zone);
	x.Add("</TimeZone>\n");
}

struct Logon {
	bool twoWindows;          // the plan holds two Windows on one boot store (decision D2)
	const char* description;  // this Windows' name in Windows' own text menu ("Windows 11 Pro")
	unsigned menuSeconds;     // how long that menu waits
	int planStep;             // the step's number; -1: the script reads \EFI\HAK\plan\current
	const char* menuDefault = nullptr; // r410: the last Windows of a plan: "{current}" or the name of the Windows
	                                   // that Windows' own menu starts by itself; null or "": unchanged
};
// The first-logon lines of a plan step. They come AFTER the existing lines that copy Charon's
// folder to %ProgramData%\Charon and set the readers up, and they are the last ones: the final
// line restarts the computer into Charon, which goes on with the plan.
//   1. hibernation and fast start-up off: a hibernated NTFS volume must not be written by the
//      other systems, and a fast start-up is a hibernation.
//   2. two Windows: the text boot menu (drawn before the kernel starts, so Charon's reader reads
//      it), this Windows' name in it, the wait.
//   3. Charon back in front: Install-Charon.ps1 -Mode Manager -Default.
//   4. the marker of the step, written last: Install-Charon.ps1 -PlanResult done. When step 3
//      failed the marker says "failed" instead (cmd's || ), so a Windows that could not put
//      Charon first is reported, not passed over in silence.
//   5. the restart.
template <class Text> inline void FirstLogon(Text& x, unsigned& order, const Logon& logon)
{
	Command(x, order++, "powercfg.exe /h off");
	Command(x, order++, "reg.exe add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power\" /v HiberbootEnabled /t REG_DWORD /d 0 /f");
	if (logon.twoWindows) {
		Command(x, order++, "bcdedit.exe /set {current} bootmenupolicy legacy");
		if (logon.description != nullptr && logon.description[0] != 0) {
			x.Add("        <SynchronousCommand wcm:action=\"add\"><Order>");
			x.Number(order++);
			x.Add("</Order><CommandLine>");
			Escaped(x, "bcdedit.exe /set {current} description \"");
			// A quotation mark inside the name would end the argument: left out.
			for (const char* c = logon.description; *c != 0; c++)
				if (*c != '"') {
					const char one[2] = {*c, 0};
					Escaped(x, one);
				}
			Escaped(x, "\"");
			x.Add("</CommandLine></SynchronousCommand>\n");
		}
		x.Add("        <SynchronousCommand wcm:action=\"add\"><Order>");
		x.Number(order++);
		x.Add("</Order><CommandLine>bcdedit.exe /timeout ");
		x.Number(logon.menuSeconds);
		x.Add("</CommandLine></SynchronousCommand>\n");
		// r410 (T7): Setup made the newest Windows the default; the last Windows puts the plan's one back.
		// bcdedit /default: https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/bcdedit-command-line-options
		// The other Windows is found by its description line in "bcdedit /enum osloader /v" (blocks split at
		// blank lines; the GUID is the first one of the block; field names are localised, so only the value is matched).
		if (logon.menuDefault != nullptr && logon.menuDefault[0] != 0) {
			const char* d = logon.menuDefault;
			bool current = d[0] == '{';
			x.Add("        <SynchronousCommand wcm:action=\"add\"><Order>");
			x.Number(order++);
			x.Add("</Order><CommandLine>");
			if (current)
				Escaped(x, "bcdedit.exe /default {current}");
			else {
				Escaped(x, "powershell.exe -NoProfile -Command \"$d='");
				for (const char* c = d; *c != 0; c++)
					if (*c != '"' && *c != '\'') {
						const char one[2] = {*c, 0};
						Escaped(x, one);
					}
				Escaped(x, "'; foreach($b in ((bcdedit.exe /enum osloader /v | Out-String) -split '(?:\\r?\\n){2,}')) "
					"{ if($b -match ('(?m)\\s' + [regex]::Escape($d) + '\\s*$')) { $g=[regex]::Match($b,'\\{[0-9a-fA-F-]{36}\\}').Value; "
					"if($g) { bcdedit.exe /default $g; break } } }\"");
			}
			x.Add("</CommandLine></SynchronousCommand>\n");
		}
	}
	const char* script = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%ProgramData%\\Charon\\Install-Charon.ps1\"";
	x.Add("        <SynchronousCommand wcm:action=\"add\"><Order>");
	x.Number(order++);
	x.Add("</Order><CommandLine>");
	Escaped(x, "cmd.exe /c ");
	Escaped(x, script);
	Escaped(x, " -Mode Manager -Default -Yes -Source \"%ProgramData%\\Charon\\EFI\\HAK\" && ");
	Escaped(x, script);
	Escaped(x, " -PlanResult done");
	if (logon.planStep >= 0) {
		x.Add(" -PlanStep ");
		x.Number(uint64_t(logon.planStep));
	}
	Escaped(x, " || ");
	Escaped(x, script);
	Escaped(x, " -PlanResult failed -PlanText \"Charon could not be put first in the boot order\" -PlanLog \"%ProgramData%\\Charon\\install-charon.log\"");
	if (logon.planStep >= 0) {
		x.Add(" -PlanStep ");
		x.Number(uint64_t(logon.planStep));
	}
	x.Add("</CommandLine></SynchronousCommand>\n");
	Command(x, order++, "shutdown.exe /r /t 20 /c \"Charon: this system is ready; the computer restarts and the plan goes on.\"");
}

} // namespace planwin
} // namespace hakefi
