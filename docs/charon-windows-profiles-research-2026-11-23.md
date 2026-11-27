# More Windows profiles for Charon: research, 2026-11-23

Scope: candidates for the "Windows profile" question of Charon's network install of Windows, checked
against live primary sources on 2026-11-23 (GitHub API and raw files, vendor sites, vendor manual).
"UNVERIFIED" marks what was not confirmed from a primary source. Nothing here was run on a Windows
installation; every verdict "ADD" means "worth implementing and then testing", not "known to work".

Rules applied: official Microsoft media plus scripts/playbooks from the project's official repository;
Narrator, sound, NVDA installability, Windows Update (drivers) and the Charon reader must survive;
unattended, or the remaining manual step is named. Rule change relayed by the coordinator during the
run (not seen by me from the user directly): prebuilt third-party images may be offered with an
"unofficial image" warning; they are in a separate group (section 5).

## 1. What exists today (read from the source)

- Dialogue: `windowsProfiles[]` in `src/efi/CharonWsInstall2.h` (line 174): values `none`, `Slim`,
  `ReviOS`, `AtlasOS`, `Script`. Question `"profile"` (line 303, `Kind::choice`) with the help text.
- Answer file: `CharonWsInstall2.h` lines 483-489 add one FirstLogonCommand,
  `powershell.exe ... %ProgramData%\Charon\Charon-WindowsProfile.ps1 -Profile <value> -Language pl|en -Yes`
  (command buffer 400 bytes), after `Install-Charon.ps1` and `Charon-Readers.ps1`.
  Lines 646-652 copy `Charon-WindowsProfile.ps1` from `\EFI\HAK\addon\` to `\charon\` on the media.
- Script: `tools/charon-addon/Charon-WindowsProfile.ps1` (1046 lines), documented in
  `tools/charon-addon/CHARON-WINDOWS-PROFILES.txt`.
  - `param(... [ValidateSet('None','Slim','ReviOS','AtlasOS','Script')]$Profile ...)`, dispatch in
    `Invoke-Main` (`switch ($ProfileName)`).
  - `Invoke-Slim` (Charon's own code: `$SlimPackagePrefixes`, `$SlimKeptFromTiny11`,
    `$ProtectedPackagePatterns`, `$SlimRegistry` ...).
  - `Invoke-Playbook` (AME CLI 0.8.4 + `.apbx`; `$Downloads`, `$PlaybookBuilds`,
    `$PlaybookDefaultOptions`, `Disable-SecurityToggles`, `Install-RestoreTask`).
  - `Invoke-ScriptProfile` (`-ScriptUrl/-ScriptArguments/-ScriptEntry`; default Win11Debloat zip,
    `-RunDefaults -Silent`, checked against the release's `SHA256SUMS`).
  - Shared guard: `Get-SpeechState`, `Protect-Speech`, `Compare-SpeechState`, `$WatchedValues`,
    `$AudioServices`, `Get-Download` (https only, SHA-256), `Start-Tool`, `Step`/`Announce`.
- The dialogue has no way to pass `-ScriptUrl`; "WinUtil" is reachable only by hand today. The
  hardware-check bypass is already a separate question (`"bypass"`, line 290).

A new profile therefore needs: one `Choice` in `windowsProfiles[]`, one name in the `ValidateSet`,
one line in `ProfileLabel`, one branch in `Invoke-Main`, and either a row of data for an existing
`Invoke-*` function or a new `Invoke-*` function; plus the help text and `CHARON-WINDOWS-PROFILES.txt`.

## 2. Findings that affect the EXISTING profiles

1. AtlasOS changed its delivery. Release 0.6.1 (2026-11-22) ships `AtlasManager.exe` (24 MB),
   `Atlas-v0.6.1.atlaspkg` (44 MB) and `SHA256SUMS.txt`; there is no `.apbx` asset. The release notes
   say "Download AtlasManager.exe to install Atlas 0.6.1, update an existing installation or create a
   Windows ISO"; supported: Windows 11 25H2 and 26H2. Charon's pinned 0.5.0-hotfix `.apbx` URL is a
   fixed release asset (still valid as an address), but it supports only 26100/26200, so on the 26H2
   (26300) media Charon fetches the AtlasOS profile refuses. Whether Atlas Manager has a command-line
   or unattended mode is UNVERIFIED (not in the release notes). Atlas docs: Home, LTSC and Server are
   unsupported (subagent, atlasos.net FAQ).
2. ReviOS 26.10 (2026-10-09) has no GitHub release assets (API: empty asset list), as noted on
   2026-11-20. Licence: the GitHub API reports CC-BY-SA-4.0; the Revision site was reported to me as
   CC BY-NC-SA 4.0. CONFLICT, not resolved.
3. Sophia Script's start-up check exits when it finds AtlasOS or the Revision Tool, so those profiles
   cannot be combined with Sophia.
4. WinUtil now has a real headless mode (section 3.2); the old `-Run` switch is gone.

## 3. Script-based candidates (official media + official repository)

### 3.1 Win11Debloat parameter sets (Raphire) — ADD two more variants
- https://github.com/Raphire/Win11Debloat , MIT, release 2026.10.30 (2026-10-31), pushed 2026-11-15.
  Assets: `Win11Debloat.zip` (759 KB), `SHA256SUMS`, `Get.ps1`.
- Unattended: `Win11Debloat.ps1 -Silent <parameters>`. Presets: `-RunDefaults` (default settings +
  default app removal), `-RunDefaultsLite` (default settings, no app removal), `-Config <PATH>`
  (file made by its GUI), `-Sysprep` (apply to the default profile for new accounts), `-User`,
  `-CreateRestorePoint`, `-WhatIf`, `-LogPath`.
- `Config/DefaultSettings.json` today: CreateRestorePoint, DisableTelemetry, DisableSuggestions,
  DisableEdgeAds, DisableLockscreenTips, DisableBing, DisableStoreSearchSuggestions, DisableCopilot,
  DisableRecall, DisableClickToDo, DisableAISvcAutoStart, DisableWidgets, HideChat, ShowKnownFileExt,
  Hide3dObjects, DisableModernStandbyNetworking. Nothing about sound, Narrator, Defender or services.
- Parameters Charon must never pass: `-ForceRemoveEdge` (the only browser at first sign-in),
  `-DisableDriverUpdates` (drivers come from Windows Update), `-DisableNotifications` (toasts are how
  readers announce events). `-DisableStickyKeys` only removes the 5x Shift shortcut; leave it out.
- Narrator/audio/Update/Defender/Store: not touched by the defaults (from the settings file; the
  default app list was not re-read today — UNVERIFIED whether it still omits Sound Recorder and
  Quick Assist, which Charon's Slim keeps on purpose).
- Verdict: keep the current entry and ADD
  - "Win11Debloat, privacy settings only" = `-RunDefaultsLite -Silent` (no app removal at all), and
  - "Win11Debloat, no AI" = `-Silent -DisableCopilot -DisableRecall -DisableClickToDo
    -DisableAISvcAutoStart -DisableEdgeAI -DisableNotepadAI -DisablePaintAI`.
  Both reuse `Invoke-ScriptProfile` unchanged; only the argument string differs.

### 3.2 Chris Titus WinUtil presets — ADD as a profile (Standard), Minimal optional
- https://github.com/ChrisTitusTech/winutil , MIT, release 26.11.13 (2026-11-13), pushed 2026-11-23.
  Single asset `winutil.ps1` (1.3 MB). No checksum asset: the hash can only be logged, or Charon pins
  a release tag URL (`/releases/download/26.11.13/winutil.ps1`) with a hash it computed itself.
- Unattended (official guide https://winutil.christitus.com/guides/automation/ and
  `scripts/start.ps1` / `scripts/main.ps1`): `-Preset Standard|Minimal|Advanced` and/or
  `-Config <file.json>`. `main.ps1`: "Headless runs never build a window"; it loads the selection,
  calls `Invoke-WinUtilAutoRun`, and exits with a code when run as a file. An unknown key rejects the
  whole import.
- `config/preset.json` today:
  - Minimal: ConsumerFeatures, WPBT, Services, Telemetry.
  - Standard: Activity, ConsumerFeatures, DisableExplorerAutoDiscovery, WPBT, Location, Services,
    Telemetry, DeliveryOptimization, DiskCleanup, DeleteTempFiles, EndTaskOnTaskbar, RestorePoint.
  - Advanced adds DisableStoreSearch, RevertStartMenu, Widget, RemoveOneDrive, WindowsAI,
    RightClickMenu.
- `config/tweaks.json` (as summarised by the fetch; the full file was not read line by line):
  Services disables DiagTrack, CscService, SharedAccess and sets StorSvc to Manual; Telemetry
  disables DiagTrack and WerSvc, sets privacy values and Defender sample submission = 2; WPBT sets
  DisableWpbtExecution=1 (irrelevant to Charon, which is an EFI boot entry/driver, not a WPBT
  binary). No audio, speech or Windows Update service was listed. The Updates tab (delay/disable
  updates) and `WPFTweaksRemoveEdge` are not in any preset.
- Location off (Standard) also stops automatic time zone; acceptable.
- MicroWin: no longer the image tool; Chris Titus's "WinUtil in 2026" post presents "Win11 Creator"
  as the replacement for "the old MicroWin" (how it is run: UNVERIFIED). It builds an image on a
  running Windows, so it is not a first-logon profile. EXCLUDE.
- Verdict: ADD "WinUtil, Standard preset" (and optionally Minimal). Advanced: EXCLUDE from the list
  (changes the Start menu and the context menu; the Start menu change was not checked with a reader).
- UNVERIFIED: run time and exit codes; whether SharedAccess=Disabled matters to anyone (it blocks
  Internet Connection Sharing / mobile hotspot).

### 3.3 Sophia Script for Windows (farag2 / Team Sophia) — ADD only as a LATER add-on, not at first logon
- https://github.com/farag2/Sophia-Script-for-Windows , MIT (GitHub API), release 8.1.0
  (2026-10-31), pushed 2026-11-22. Separate zips per system: Windows 11 (`...Windows.11.v8.1.0.zip`),
  11 LTSC 2024, 11 Arm, Windows 10 22H2, 10 LTSC 2019/2021, each also for PowerShell 7. Also winget
  `TeamSophia.SophiaScript`. Each function has an opposite to restore defaults.
- Unattended form: dot-source `Import-TabCompletion.ps1`, then
  `Sophia -Functions "DiagTrackService -Disable", "AdvertisingID -Disable", ...`.
- Why not at first logon (`Module/Private/InitialActions.ps1`): it EXITS when a newer Sophia release
  exists (so a pinned copy stops working; only "latest" can be used and no hash can be pinned), when
  the Windows build is older than the latest supported one (it then opens Windows Update), on a
  pending reboot, on LTSC with the non-LTSC package, when the Store or Defender components are
  missing, when Controlled Folder Access is on, when Windows Terminal is older than 1.24, and when
  it finds other tweakers (AtlasOS, Revision Tool, Ghost Toolbox ...). It shows a Yes/No arrow-key
  menu when the hosts file has entries and when the Secure Boot DB lacks the 2023 CA. A freshly
  installed, not yet updated Windows therefore gets refused.
- The stock preset must NOT be used: `Uninstall-UWPApps`, `WindowsFeatures -Disable` and
  `Set-UserShellFolderLocation -Root` open dialogs; it also sets `AdminApprovalMode -Never` (UAC
  off), `InputMethod -English`, dark mode, `DNSoverHTTPS -Cloudflare`, `Hibernation -Disable`,
  `StickyShift -Disable`. No audio or Narrator function exists in the preset.
- Verdict: ADD AS ADD-ON run by the user later from Windows (a Charon wrapper with a fixed function
  list: telemetry, advertising, suggestions, Bing search, Windows AI), after Windows Update has
  finished. As a first-logon profile: EXCLUDE (refuses on a non-updated system; possible menus).

### 3.4 O&O ShutUp10++ — ADD as optional add-on (privacy settings), low priority
- https://www.oo-software.com/en/shutup10 , freeware, closed source; version 3.7.1143 (2026-11-10);
  Windows 10 and 11; portable single exe. Vendor manual (PDF, read today): "completely free of
  charge for personal and commercial use"; "You can apply a saved profile (.cfg file) silently from
  the command line"; Free `.cfg` files are not interchangeable with Premium `.pcfg`.
- Redistribution is not addressed in the manual: do not ship the exe with Charon; download it from
  the vendor at run time and check the Authenticode signer.
- Command line: `OOSU10.exe <file>.cfg /quiet` and `/nosrp` are named in the vendor's changelog
  (via search result) — exact syntax not printed in the manual: UNVERIFIED. Direct exe URL
  UNVERIFIED (the site's button points to `/en/download/current/ooshutup10`). No published hash.
- Needs a Charon-authored `.cfg` (made once in its GUI) limited to the "recommended" settings; the
  manual itself warns that other settings affect Windows Update delivery and Defender.
- Verdict: ADD AS ADD-ON only if a privacy layer beyond 3.1/3.2 is wanted; otherwise skip (it
  overlaps, is closed source and cannot be hash-pinned).

### 3.5 Microsoft's own app-removal policy — ADD as the mechanism for Slim on Enterprise/Education
- Microsoft Tech Community, "Policy-based removal of pre-installed Microsoft Store apps": policy
  "Remove default Microsoft Store packages from the system", Windows 11 Enterprise and Education
  25H2+, registry `HKLM\SOFTWARE\Policies\Microsoft\Windows\Appx\RemoveDefaultMicrosoftStorePackages`,
  CSP `ApplicationManagement/RemoveDefaultMicrosoftStorePackages`; removal happens during OOBE and at
  sign-in after a policy change; Microsoft: no plan for Pro. 25 apps (Calculator, Camera, Feedback
  Hub, M365 Copilot, Clipchamp, Copilot, News, Photos, Solitaire, Sticky Notes, Teams, To Do,
  Weather, Notepad, Outlook, Paint, Quick Assist, Snipping Tool, Sound Recorder, Media Player,
  Terminal, Xbox x4).
- Fits Charon because its Enterprise evaluation media are exactly this edition, and the policy can
  be written in the answer file's specialize pass, before the first user exists (no race with app
  provisioning, which `Invoke-Slim` documents as a limitation).
- Keep (do not list): Quick Assist, Sound Recorder, Windows Terminal, Notepad, Calculator, Media Player.
- UNVERIFIED: the per-app registry value layout (third parties say one subkey per package family
  name), behaviour on LTSC, and the community report that KB5083631 changed the list format.
- Verdict: ADD as "Slim (Microsoft policy)" path inside Slim when the edition is Enterprise/Education.

### 3.6 Harden System Security (HotCakeX) — ADD as optional add-on "hardened", after a reader test
- https://github.com/HotCakeX/Harden-Windows-Security , MIT, release 1.0.107.0 (2026-11-20). Uses
  only documented Microsoft methods (author's statement). Windows 11 24H2-26H2, Server 2025.
- Delivered through the Microsoft Store (ID 9p7ggfl7dx57):
  `winget install --id 9p7ggfl7dx57 --exact --source msstore` (+ agreement flags).
- Headless: `HSS.exe --cli --preset=0|1|2 --op=Apply|Remove|Verify` (0 Basic, 1 Recommended,
  2 Complete); intents `--intent=Gaming|Development|School|Business|...`; exit codes 0/1/2.
  It is reversible (`--op=Remove`).
- Categories include Microsoft Security Baselines, Defender, ASR, BitLocker, Lock Screen, UAC,
  Windows Update, Optional Features. Effects on Narrator/NVDA (lock screen and UAC rules, ASR
  rules against unsigned helpers, e.g. portable NVDA or add-ons) are UNVERIFIED and are the risk.
- Verdict: ADD AS ADD-ON with preset 0 (Basic) only, after a real test with NVDA and Narrator on
  the logon screen and the UAC prompt. Needs the Store and winget to work at first logon (not on
  LTSC without the Store).

### 3.7 Open-Shell (classic Start menu) — ADD as optional add-on, after a reader test
- https://github.com/Open-Shell/Open-Shell-Menu , MIT, stable 4.4.198 (2026-05-12), pre-release
  4.4.202 (October). Pre-release 4.4.201 notes: "Improve screen reader, keyboard, and High Contrast
  accessibility" — i.e. the stable build lacks those fixes.
- Does not patch Explorer; the Windows Start menu remains available (Shift+Win by default —
  UNVERIFIED today). Silent install `OpenShellSetup_x.exe /qn ADDLOCAL=StartMenu`: from memory,
  UNVERIFIED.
- Verdict: ADD AS ADD-ON once the accessibility work is in a stable release and it has been driven
  with NVDA and Narrator. No effect on audio, Update, Defender, Store.

### 3.8 "Windows Server as a workstation" — ADD as Charon's own small profile (only for Server media)
- Charon already offers the Server 2025 evaluation image. No maintained script in an official
  repository was found (UNVERIFIED that none exists), so this would be Charon's own code like Slim.
- Content (from general knowledge of Windows Server defaults, to be verified on the image): start
  the Windows Audio services (`Protect-Speech` already sets `Audiosrv`/`AudioEndpointBuilder` to
  Automatic and starts them — on Server this is what makes speech audible at all), add the
  `Wireless-Networking` feature, turn off the shutdown reason dialogue, Ctrl+Alt+Del requirement and
  IE/Edge enhanced security prompts, do not open Server Manager at logon.
- Verdict: ADD (own code, `Invoke-ServerDesktop`), offered only when the chosen version is Server.

## 4. Excluded script-based candidates

| Candidate | Facts (source) | Reason |
|---|---|---|
| AME 11 / AME 10 playbooks (Ameliorated; amelabs.net, `download.ameliorated.io/AME%2010%20Beta.apbx`) | docs: "Windows Update and Windows Security ... completely deleted" | No Windows Update for drivers, no Defender; frozen system. |
| Privacy+ playbook (Ameliorated; `download.ameliorated.io/Privacy%2B.apbx`, verified on amelabs.net) | contents not read (docs page 404) | UNVERIFIED; candidate only after reading its `playbook.conf`. Would reuse `Invoke-Playbook` with the same manual Tamper Protection step. |
| Third-party playbooks (JarvisOS, DraganOS, Revi forks) | not on amelabs.net's verified list | Unverified publishers. |
| AtlasOS 0.6.x as a new entry | section 2 | Unattended mode of Atlas Manager unverified; keep the old entry, mark it 24H2/25H2 only. |
| tiny11builder (ntdevlabs) | now a WinUI app 0.2.0 (2026-10-30, zip + `.sha256`); scripts kept; no licence file (NOTICE.md); runs on Windows against official media | Not a first-logon step; Slim already follows its list. Core/Nano variants remove Update, Defender (Nano: audio and accessibility). |
| WinUtil MicroWin / Win11 Creator | section 3.2 | Image builder, needs a running Windows. |
| privacy.sexy | AGPL-3.0, 0.13.8 (2025-03-13), last push 2026-02 | No CLI or ready script in the repository (UNVERIFIED: docs page 404); scripts are generated in its app. A user can still pass one through "external script". |
| Winhance (memstechtips) | PolyForm Shield 1.0.0, GUI, v26.06.12 | No documented CLI; licence forbids competing redistribution. |
| UnattendedWinstall (memstechtips) | MIT, v3.0.0; an `autounattend.xml` | Replaces Charon's answer file; removes Edge, disables automatic updates. |
| Schneegans unattend-generator | MIT, pushed 2026-11-20 | Not a mod; a generator. Useful only as a reference for doing removals in the specialize pass. |
| hellzerg Optimizer | GPL-3.0, 17.0 (2026-11-12), SHA-256 in README, `optimizer.exe /config=template-windows11.json` | Whether the template run is windowless is UNVERIFIED; overlaps 3.1/3.2. Hold. |
| ExplorerPatcher | GPL-2.0, 26100.9711.71.3 (2026-11-20) | Its own FAQ: flagged as malware by Microsoft Defender, needs Defender exclusions, else "explorer.exe may fail to start". A broken shell leaves a blind user stranded. |
| StartAllBack | commercial, closed | Paid licence. |
| Flyoobe / Flyby11 (builtbybel) | MIT, 3.03.100 (2026-09-28); GUI setup assistant; Flyby11 classic deprecated; no CLI documented | Not unattended. Facts on bypass: Charon already has its own `"bypass"` question (TPM, Secure Boot check, RAM); skipping Setup's Secure Boot *check* does not turn Secure Boot off. |
| NTLite | commercial GUI | Licence; not script-based. |
| Talon (Raven) | two-click GUI, removes Edge, flagged by antivirus (press reports) | Not unattended in a documented way; removes the browser. UNVERIFIED repository. |
| EudynOS, KernelOS | nothing found | Cannot be verified as real, script-based projects. |
| Win-Debloat-Tools (LeDragoX), Windows10Debloater (Sycnex), Debloat-Windows-10 (W4RH4WK), privatezilla | archived / last push 2023-2025 | Unmaintained. |
| windows-defender-remover, Remove-MS-Edge | active | Remove Defender / the browser. |
| simeononsecurity Windows-Optimize-Harden-Debloat | MIT, last push 2025-04 | Stale; heavy hardening of unknown effect on readers. |
| WindowsMize (agadiffe) | MIT, pushed 2026-10, 234 stars, config-file driven | Small project, not reviewed; usable through "external script". |
| Accessibility-oriented setup scripts | two searches | None found in any official repository. `Charon-Readers.ps1` already does this job (NVDA `--install-silent`, JAWS, Narrator). |

## 5. Unofficial images (separate group; each needs the warning and a typed confirmation)

Source: a delegated research run on 2026-11-23, primary sources where named. Microsoft's licence
does not permit redistributing modified Windows media (exact retail wording UNVERIFIED); none of the
publishers claims such a right; the user needs their own licence in every case. No report from a
blind user was found for any of these images: Narrator/NVDA behaviour is UNVERIFIED everywhere.

| Image | Publisher's own location | Verifiable | Activation | Verdict |
|---|---|---|---|---|
| Tiny11 25H2 (NTDEV) | https://archive.org/details/tiny11_25H2 , files under `https://archive.org/download/tiny11_25H2/`: `tiny11_25H2_Nov25.iso` 5,730,246,656 B (x64, newest: November 2025, a year old), `tiny11_25H2_Oct25.iso` 5,514,559,488 B | SHA-256 by NTDEV only for Oct25 x64 (`92484F2B7F707E42383294402A9EABBADEAA5EDE80AC633390AE7F3537E36275`); Nov25 has only archive.org's SHA-1 (`51a2cb8e12432fa9fc88103f5a84040936c7088b`, transfer integrity only). No signature. | none stated; embedded answer file has an empty product key | OFFER WITH WARNING, after one test install. Keeps Windows Update, Defender, recovery; removes Edge, OneDrive, Media Player, Sound Recorder, Quick Assist. Embeds its own `autounattend.xml` (collides with Charon's: Charon's must replace it). Language presumed en-US (UNVERIFIED); Polish only as a language pack afterwards (UNVERIFIED). Range support of archive.org not tested. |
| Tiny11 Core (NTDEV) | same item, `tiny11core_25H2_Nov25.iso` 3,304,132,608 B | as above | none stated | EXCLUDE: no Windows Update, no WinRE, Defender disabled, "can't add languages, updates, or features"; needs the legacy installer. |
| Nano11 (NTDEV) | https://archive.org/details/nano11_25h2 , 2,463,565,824 B, SHA-256 `A1E0614372768CBE2D24DE74B78A4A97BC1017EA5080DFED1D2125E4A527EB1A` | yes | none stated | EXCLUDE: README says accessibility features and the audio service are removed. |
| Tiny10 23H2 (NTDEV) | https://archive.org/details/tiny-10-23-h2 | no checksum | none stated | EXCLUDE: 2023, Windows 10 out of support. |
| Windows X-Lite | https://windowsxlite.com (downloads on pixeldrain) | no checksum, anonymous publisher | site: "NOT Pre-Activated and don't include Toolkits" | EXCLUDE: no publisher-controlled URL, nothing to verify; Defender off by default. |
| Ghost Spectre | no working official site found; links in video descriptions to file hosters, password-protected archives | no | secondary sources: Ghost Toolbox has a "Ghost Online Activator" (not confirmed from a primary source) | EXCLUDE: bundled activator (reported), no official stable URL, no verification. |
| ReviOS ISO | revi.cc: distributed as a playbook only | - | - | Not an image any more. |
| AtlasOS ISO | atlasos.net: "doesn't distribute ready-made Windows ISO files" | - | - | Not an image. |
| AME 10/11 ISO | amelabs.net offers AME Beta and playbooks only | - | - | Not an image. |
| KernelOS, Phoenix LiteOS, ggOS, anonymous "Lite" builds | none (Discord, warez/reupload sites) | no | unknown | EXCLUDE. |

Result: at most ONE image qualifies, Tiny11 25H2 (regular). Honest alternative to say in the
dialogue: the same result comes from official media plus the Slim profile.

Proposed warning (content; said before the questions, as the r406 rule for Windows does):
- EN: "Warning: this is an unofficial Windows image. It was not made by Microsoft and Microsoft
  cannot verify it. Charon can only check that the file is the one its author published, not what
  is inside. Parts may be missing, including parts a blind user needs; nobody has confirmed that
  Narrator and NVDA work on it. It is not a licence: you need your own Windows licence or key. This
  image is from <month year> and must be updated after installation. Official Windows with the slim
  profile gives a similar result. Type UNOFFICIAL to go on."
- PL: "Uwaga: to jest nieoficjalny obraz Windows. Nie zrobił go Microsoft i Microsoft nie może go
  zweryfikować. Charon sprawdzi tylko, czy plik jest tym, który opublikował autor, a nie co jest w
  środku. Może brakować części systemu, także tych potrzebnych osobie niewidomej; nikt nie
  potwierdził, że Narrator i NVDA na nim działają. To nie jest licencja: potrzebujesz własnej
  licencji albo klucza Windows. Obraz pochodzi z <miesiąc rok> i po instalacji trzeba go
  zaktualizować. Oficjalny Windows z profilem odchudzonym daje podobny wynik. Aby iść dalej, wpisz
  NIEOFICJALNY."
- Mechanism: a `ConfirmUnofficial(...)` beside `ConfirmSilent(const Entry&)` in
  `src/efi/CharonWsInstall3.h` (line 97), but with a typed word as in `ConfirmPlan`
  (`CharonWsInstall.h` line 130: `Prompt` + `SameFold`), so that Enter alone cannot pass it; a flag
  on the entry in `windowsVersions[]` (`CharonWsInstall2.h` line 183) marks it "unofficial image" in
  the spoken label, the same way the Speech labels are appended in `CharonWsInstall3.h` line 449.
  The pinned SHA-256 goes where the other fetched images keep theirs; the image's own
  `autounattend.xml` must be overwritten by Charon's.

## 6. Proposed profile list for the dialogue

Profiles (one choice, `windowsProfiles[]`):
1. unchanged (`none`)
2. slim, without the preinstalled apps (`Slim`) — on Enterprise/Education also through Microsoft's
   own removal policy (3.5)
3. Win11Debloat, default (`Script`, as today)
4. Win11Debloat, privacy settings only, no app removal (NEW `DebloatLite`)
5. Win11Debloat, AI features off (NEW `NoAI`)
6. WinUtil, Standard preset (NEW `WinUtil`); optionally `WinUtilMinimal`
7. ReviOS (as today; one manual step)
8. AtlasOS (as today; say "Windows 11 24H2 and 25H2 only" until Atlas Manager is examined)
9. Windows Server as a workstation (NEW `ServerDesktop`; shown only for Server media)

Add-ons (a second, multi-choice question `Kind::multi`, like `"wreaders"`; applied after the profile):
- classic Start menu (Open-Shell) — after a reader test
- hardened, Basic preset (Harden System Security) — after a reader test
- O&O ShutUp10++ recommended settings — optional, low priority
- Sophia Script — not at first logon; a script the user runs later

Unofficial images (in the version list, not the profile list): Tiny11 25H2 by NTDEV, with the
warning and the typed word.

## 7. Mapping onto the existing mechanism

| New entry | `CharonWsInstall2.h` | `Charon-WindowsProfile.ps1` |
|---|---|---|
| `DebloatLite`, `NoAI` | two `Choice` rows in `windowsProfiles[]`; help text of `"profile"` | add to `ValidateSet`; a table `$ScriptPresets = @{ DebloatLite = '-RunDefaultsLite -Silent'; NoAI = '-Silent -DisableCopilot ...' }`; `Invoke-Main` routes both to `Invoke-ScriptProfile`, which takes the arguments from the table instead of `$DefaultScriptArguments`; `ProfileLabel` rows. Download and `SHA256SUMS` check unchanged. |
| `WinUtil` (`WinUtilMinimal`) | one or two `Choice` rows | `Invoke-ScriptProfile` with `$url = https://github.com/ChrisTitusTech/winutil/releases/download/<tag>/winutil.ps1`, `$entry = 'winutil.ps1'`, arguments `-Preset Standard`; new `$Downloads.WinUtil` row with a hash Charon pins per tag (`Get-Download` already refuses a mismatch); `Protect-Speech` before and after as now. |
| Slim through policy | none | in `Invoke-Slim`: when `EditionID` is Enterprise/Education and build >= 26200, write the policy list (new `$SlimPolicyApps`) with `Set-RegValue`; better still, emit the same keys from the answer file's specialize pass next to the other `RunCommand` calls (lines 470-489) so removal happens before the first sign-in. |
| `ServerDesktop` | `Choice` row, offered only when the chosen `WindowsVersion` is Server | new `Invoke-ServerDesktop($Before)` built from `Step`, `Set-RegValue`, `Invoke-Change`; audio is already covered by `Protect-Speech`/`$AudioServices`. |
| Add-ons | new `Choice` list `windowsAddons[]` and a `Kind::multi` question `"waddons"`; pass `-AddOns a,b` in the same command (the 400-byte buffer has room) | new `[string]$AddOns` parameter; after the profile's `switch`, loop over `Invoke-AddOnOpenShell`, `Invoke-AddOnHarden` (`winget` + `HSS.exe --cli --preset=0 --op=Apply`), `Invoke-AddOnShutUp` (`Get-Download`, Authenticode check, `Start-Tool`); each wrapped by `Protect-Speech` and `Compare-SpeechState`. |
| Unofficial image | flag on `WindowsVersion`, label suffix, pinned hash | none (it is media, not a profile); `ConfirmUnofficial` in `CharonWsInstall3.h`. |

Also update `tools/charon-addon/CHARON-WINDOWS-PROFILES.txt` and the help text at line 304-305.

## 8. Unverified points (collected)

1. Nothing was executed; all "keeps Narrator/audio" statements come from reading settings lists.
2. Win11Debloat's current default app list (Sound Recorder, Quick Assist).
3. WinUtil: full contents of `WPFTweaksServices` (summary only), exit codes, run time.
4. AtlasOS 0.6.x: command-line mode of Atlas Manager; `.atlaspkg` format.
5. ReviOS licence (CC-BY-SA-4.0 per GitHub API vs CC BY-NC-SA 4.0 per the site).
6. Privacy+ playbook contents and supported builds.
7. O&O ShutUp10++: exact switches (`/quiet`, `/nosrp`), direct download URL, redistribution terms.
8. Microsoft removal policy: registry value layout, LTSC, the KB5083631 format change.
9. Harden System Security: effect of the Basic preset on NVDA/Narrator, logon screen and UAC.
10. Open-Shell: silent switches, the key that opens the Windows Start menu, behaviour with readers.
11. Server-as-workstation defaults (audio service state, feature names) on the 2025 image.
12. Flyoobe's bypass method; Talon's repository; privacy.sexy having no CLI; hellzerg's windowless run.
13. Tiny11: language, exact build, Narrator/voices/Store presence, archive.org Range support,
    answer-file precedence; Ghost Spectre's activator (secondary sources only); Microsoft's exact
    retail licence wording.
14. The rule change about unofficial images reached me through the coordinator, not from the user.
