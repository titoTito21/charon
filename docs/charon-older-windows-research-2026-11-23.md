# Windows 7, 8 and 8.1 in Charon: research and feasibility, 2026-11-23

Scope: can Charon's Windows install offer Windows 7 (main target, per the user), Windows 8 and 8.1
(secondary). The user's decision relayed during the run: Windows 7 is to run NATIVELY on the modern
UEFI machines Charon runs on; the built-in PC emulator is only a last-resort fallback (section 9).

How to read this report:
- "VERIFIED" = checked live today (2026-11-23) against a primary source; the URL is given.
- "UNVERIFIED" = from general technical knowledge or secondary sources, not confirmed today.
- Nothing here was run. No Windows 7/8/8.1 media is on this host and none was downloaded, so every
  statement about Setup, WinPE, drivers and readers on those systems is a prediction to be tested.
- Source tree was read only (r395 tree: `work/charon-src-r395`).

---------------------------------------------------------------------------------------------------

## 0. Short answer

| | Windows 7 SP1 x64 | Windows 8 x64 | Windows 8.1 x64 |
|---|---|---|---|
| Official public download | none | none | none (removed) |
| Media route | user's own media only | user's own media only | user's own media only |
| UEFI | needs CSM or an Int10h shim; Secure Boot off | native UEFI, Secure Boot capable | native UEFI, Secure Boot capable |
| Modern hardware | only with injected drivers; hard limit around Intel 6th-9th gen / early Ryzen without modified system files | better inbox (xHCI yes, NVMe no) | best of the three (xHCI and NVMe inbox) |
| Speech in Setup (WinPE) | expected NO (no audio stack in that WinPE) | expected NO | expected NO |
| Speech after install | YES: NVDA 2023.3.4 + eSpeak NG, if an audio driver loads | YES: NVDA 2023.3.4 | YES: NVDA 2025.3.3 |
| Recommendation | OFFER, own media, "older Windows" group, strong warnings, experimental until proven | DO NOT OFFER as an entry (accept as own media under the 8.1 path only if it costs nothing) | OFFER, own media, "older Windows" group |
| Effort | HARD | (MEDIUM, not worth it) | MEDIUM |

The decisive facts: (1) there is no lawful public download, so everything depends on the user's own
ISO/disc and nothing can be proven here without it; (2) Setup will be silent on all three, so the
install must be 100 % unattended and Charon must say so before it starts; (3) Windows 7 on a
GOP-only machine needs an Int10h handler, which Charon can lawfully provide itself (BSD code exists),
and needs drivers that Charon cannot lawfully ship for the newest platforms.

---------------------------------------------------------------------------------------------------

## 1. Media sources

### 1.1 What Microsoft offers today
- VERIFIED: `https://www.microsoft.com/en-us/software-download/windows8ISO` returns HTTP 404;
  `.../software-download/windows8` and `.../software-download/windows7` return 301 to the generic
  `/software-download/` page; `.../windows10ISO` returns 200. There is no public Windows 7, 8 or 8.1
  download page any more.
- VERIFIED: Fido v1.72 (the script Rufus uses; `https://raw.githubusercontent.com/pbatard/Fido/master/Fido.ps1`)
  lists only "Windows 11", "Windows 10" and "UEFI Shell". Windows 8.1 is gone from its table (the
  Softpedia changelog records Fido 1.47, May 2023: "removes Windows ISOs that Microsoft is no longer
  allowing users to download"). Its endpoints are `microsoft.com/software-download-connector/api/
  getskuinformationbyproductedition` and `.../GetProductDownloadLinksBySku` with a session whitelisted
  at `vlscppe.microsoft.com/tags` — the same ones Charon uses; they serve no 7/8/8.1 product ids.
  Fido also refuses to run ON Windows 7 ("too much of a liability").
- Evaluation Center: the earlier Charon research (`work/charon-os-sources-2026-11-20.md`) found only
  Windows 11/10 LTSC/Server evaluations. Windows 8.1 Enterprise evaluation: UNVERIFIED as removed
  (not re-checked today; no listing was found).
- Visual Studio (MSDN) subscriptions and the Volume Licensing / Microsoft 365 admin centre: reported
  to still carry older Windows images for subscribers/licensees. UNVERIFIED (needs a sign-in). They
  are lawful sources for the user, but not something Charon can fetch.
- OEM recovery media: the lawful source for most Windows 7 owners (recovery partition/discs, vendor
  download with the service tag). OEM recovery images are often not plain Setup media (vendor
  imaging tools) — Charon can only use real Setup media (`\sources\boot.wim` + `install.wim`).

Conclusion (plain): there is no official public download of Windows 7, 8 or 8.1. The only route is
the user's OWN media: an ISO on a volume, a disc/stick, a URL the user types, or the user's list
file — the same rule Charon applies to the Windows 9x/XP profiles. Charon hardcodes no third-party
URL (archive.org etc. give no licence).

### 1.2 Keys and activation
- Windows 7 Setup: the key is optional during Setup (30-day grace). UNVERIFIED today, long-standing.
  Retail/OEM media carry `\sources\ei.cfg`, which fixes the edition; without it Setup shows all
  editions of the image.
- Windows 8 / 8.1 retail media: Setup demands a key unless (a) the firmware carries an OEM key
  (MSDM table; Setup then picks the edition itself), (b) the answer file has a key, or (c)
  `\sources\ei.cfg` / `PID.txt` are present. VERIFIED mechanism (page applies to 10/11 monikers, the
  files date from Vista/8): https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-setup-edition-configuration-and-product-id-files--eicfg-and-pidtxt
  — note the same page says "An answer file takes precedence over these files. If you use an answer
  file ... Windows Setup ignores the EI.cfg and PID.txt files." So with Charon's Autounattend.xml the
  key must go into the answer file: Charon must ASK for the key for 8/8.1 (or read the firmware MSDM
  key — whether Setup does that with an answer file present is UNVERIFIED).
- Generic install keys: Microsoft publishes KMS client (GVLK) keys for VOLUME editions only
  (Pro/Enterprise); for retail Core/Pro 8.1 no Microsoft-published generic key exists as far as I
  know. UNVERIFIED. Charon should not embed keys; ask the user.
- Activation servers: online activation of 7/8.1 with a genuine key is reported to still work;
  UNVERIFIED and not testable here. Charon states "activation is up to you and Microsoft".

---------------------------------------------------------------------------------------------------

## 2. Native Windows 7 on "Charon hardware" (modern UEFI) — the main part

### 2.1 Firmware: the Int10h problem
- Windows 7 x64 boots in UEFI mode but its boot graphics (`bootvid`/`vgapnp.sys`) still call the
  VGA BIOS through INT 10h. On firmware with a CSM this is satisfied by the legacy video ROM. On
  GOP-only ("UEFI class 3") firmware there is nothing at C0000h and Windows 7 hangs at "Starting
  Windows" / the disc hangs after "Loading files". Sources: UefiSeven README ("emulates int10h
  interrupts needed for booting Windows 7 under UEFI Class 3 systems"), VgaShim README ("Windows 7,
  even the x64 version booting in EFI mode, insists on using old VGA/VESA graphics configuration
  routines with the 10h interrupt"). VERIFIED as those projects' statements. Microsoft's own current
  pages no longer document it (the two Learn pages fetched today cover Windows 10 only).
- Existing shims and licences (VERIFIED from the repositories today):
  - OVMF `QemuVideoDxe/VbeShim` (Red Hat/Intel, 2013-2014, BSD) — the origin; this is why Windows 7
    boots under OVMF with the standard VGA device.
  - VgaShim, https://github.com/davidcie/VgaShim (2016, Dawid Ciecierski; EDK2 tree, BSD headers;
    repository has no top-level licence metadata).
  - UefiSeven 1.30, https://github.com/manatails/uefiseven (2021-10-04, Seungjoo Kim; `UefiSevenPkg/
    License.txt` = 2-clause BSD; file headers carry Ciecierski, Red Hat and Intel copyrights).
    Adds a forced-resolution hack for panels without 1024x768.
  - FlashBoot Pro: commercial, closed; not usable.
- Can Charon provide the handler itself? YES in principle, and it is the right place for it:
  the technique is a UEFI application/driver step done before the Windows loader starts —
  (1) unlock the legacy region C0000h-CFFFFh (EFI_LEGACY_REGION2_PROTOCOL, or the chipset PAM
  registers when the protocol is absent), (2) copy a ~1 KB real-mode stub that answers VBE
  functions 4F00h/4F01h/4F02h/4F03h with ONE mode matching the current GOP framebuffer (address,
  pitch, 32 bpp), (3) point IVT vector 10h at it, (4) lock the region and start `bootmgfw.efi`.
  2-clause BSD code can be included in GPL-2.0-or-later Charon with the notices kept. Charon's
  resident reader driver is not needed for it: the stub lives in conventional memory, not in boot
  services.
  Risks / limits: firmware that neither exposes the legacy-region protocol nor lets the PAM
  registers be written (the write silently fails — must be detected by read-back and reported, not
  ignored); GOP modes below 1024x768 or odd strides; multi-GPU machines; the screen stays at the
  one firmware resolution until a real display driver exists (none for recent GPUs) — irrelevant to
  a blind user but must be said; a wrong stub = hang at "Starting Windows", so Charon needs a
  watchdog/"if it does not come back" message before the hand-over. It bypasses nothing in the
  security sense (it is the firmware service Windows 7 expects), so it fits Charon's rules.
  Effort: MEDIUM for the shim, but its proof needs real GOP-only hardware (VMware and OVMF already
  satisfy Windows 7 without it, see 2.6).
- Secure Boot: Windows 7's boot manager is not signed for UEFI Secure Boot. Secure Boot must be
  OFF. Charon never bypasses it: detect (SecureBoot variable = 1) and refuse with instructions.
  Windows 7 also needs a GPT disk in UEFI mode (supported) and has no TPM/Modern Standby needs.
- Booting the media: a Windows 7 x64 ISO is UEFI-bootable only through its El Torito image
  (`efi\microsoft\boot\efisys.bin`); the file tree has no `\EFI\BOOT\BOOTX64.EFI`. UNVERIFIED on
  media (well-known). Charon would take `bootmgfw.efi` from the FAT image `efisys.bin` (small FAT12
  reader) or from the disc's El Torito entry. Windows 8/8.1 media have `\efi\boot\bootx64.efi`.
  `install.wim` of Windows 7 SP1 x64 is under 4 GB (no split needed); 8.1 images can exceed 4 GB
  (Charon's `CharonWimSplit.h` already handles that).

### 2.2 Drivers a clean Windows 7 SP1 lacks
All UNVERIFIED today unless marked; these are the long-known gaps.

| Device class | Windows 7 SP1 inbox | Lawful fix | Notes |
|---|---|---|---|
| NVMe | none | Microsoft hotfix KB2990941 + KB3087873 (stornvme); or a vendor NVMe driver (Samsung, Intel, the BSD-licensed OpenFabrics NVMe driver) | VERIFIED: the Update Catalog search for KB3087873 returns Windows 7 rows; for KB2990941 it returns none (it was a request-only hotfix; no official download found today). Hotfixes are .msu packages, not INF drivers |
| USB 3.x xHCI | none | vendor drivers: Intel USB 3.0 eXtensible HC driver (7/8/9 and 100/200 series), AMD, ASMedia, Renesas, VIA | On a machine with only xHCI ports the keyboard AND the install stick are dead in Setup without it. Community "generic xHCI" drivers (ports of the Windows 8 stack, patched vendor INFs) are modified binaries with broken/test signatures — EXCLUDE |
| AHCI | inbox (msahci) | — | Intel RST RAID / VMD: vendor F6 driver; VMD (11th gen+) has no Windows 7 driver — turn VMD off in firmware |
| Ethernet | old Intel/Realtek/Broadcom only | vendor drivers; Intel I219 (to about -V v7), Realtek 8168 family have Windows 7 packages; I225/I226 2.5 GbE: Windows 7 driver existence UNVERIFIED | no network = no profile downloads; profiles must then come from the media |
| Wi-Fi | none for anything recent | Intel up to about 8265/3168 have Windows 7 drivers; AX2xx/BE2xx have none | plan on Ethernet |
| Audio | generic HD Audio class driver (`hdaudio.sys`) | none needed for classic HD Audio codecs (Realtek ALC etc. on the PCH HDA link) | DECISIVE for speech: machines whose codec sits behind Intel Smart Sound/SoundWire/SOF DSP (many laptops since 2019-2020) or AMD ACP have NO Windows 7 audio driver: no speech at all. USB audio class 1 works inbox (a USB headset is the fallback — if a USB controller driver is loaded) |
| Display | Standard VGA via the Int10h shim | NVIDIA ended Windows 7 drivers in 2021 (up to RTX 30), AMD earlier, Intel iGPU after 6th gen: none | not needed for a blind user |
| Chipset/ACPI | — | — | see 2.3 |

SHA-2 trap: a clean SP1 image cannot verify SHA-2-only driver signatures (it needs KB3033929 or
KB4474419, VERIFIED requirement: https://support.microsoft.com/help/4472027). Drivers injected for
Setup and first boot must therefore be SHA-1 or dual-signed period drivers (2015-2019). Anything
newer fails to load with code 52. Charon does not disable signature enforcement.

### 2.3 Practical CPU/platform limit
UNVERIFIED (community knowledge; press sources confirm the vendor statements):
- Intel: official Windows 7 support ended with Skylake (6th gen, 100 series); Kaby Lake and AMD Ryzen
  were never supported and Microsoft blocked Windows Update on them (press, 2017). Drivers exist in
  practice for 100/200 series and partly 300 series (8th/9th gen).
- From roughly Intel 10th-12th gen and AMD AM5 on, a stock Windows 7 kernel stops with
  `ACPI_BIOS_ERROR` (0xA5) or lacks every chipset driver. The community answer is a patched
  `acpi.sys` (taken from Windows 8 pre-release builds or hex-edited) and modified/"canonkong"
  driver packs with stripped signatures, loaded with enforcement disabled. These are modified
  Microsoft/vendor binaries: Charon must not ship, fetch or recommend them — EXCLUDE.
- Consequence: lawful native Windows 7 is realistic on UEFI machines of about 2011-2018
  (Sandy Bridge to Coffee Lake, AM3+/early AM4), SATA or NVMe with a period driver, HD Audio
  codec, Ethernet. On newer machines Charon should refuse early with the reason (CPU family,
  no Windows 7 driver for the disk/USB/audio found), not start an install that ends silent.
  Charon can detect most of this before starting: CPUID family/model, PCI class codes (NVMe
  01-08-02, xHCI 0C-03-30, HDA 04-03 vs. DSP 04-01/04-80, VMD), Secure Boot state, CSM presence.

### 2.4 How drivers get in: can Charon inject them?
- What Charon does to media today (read from the source): it writes FILES onto the install volume
  (`Autounattend.xml`, `\charon\...`, `\charon\pe\...`, readers) and splits a WIM by streaming
  (`CharonWimSplit.h`, raw resource copy, no recompression). It has no code that modifies the
  contents of `boot.wim`/`install.wim`, and adding one in firmware (LZX/XPRESS, metadata rewrite,
  component store servicing for .msu) is not realistic — offline servicing of packages is a DISM job.
- The supported no-WIM-edit routes (answer-file mechanisms exist since Vista; the Microsoft page
  fetched today, for Windows 10/11, describes them unchanged:
  https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/add-device-drivers-to-windows-during-windows-setup ):
  1. `Microsoft-Windows-PnpCustomizationsWinPE` / `DriverPaths` in the windowsPE pass: "Windows
     loads boot-critical drivers that Windows PE requires to access the local disk and network";
     these drivers are then also placed in the installed image.
  2. `Microsoft-Windows-PnpCustomizationsNonWinPE` / `DriverPaths` in the offlineServicing pass:
     adds drivers to the applied image's driver store before first boot.
  3. `\$WinPEDriver$` folder on the media root (scanned automatically). UNVERIFIED in Microsoft's
     current docs (documented for Vista SP1+ Setup); `DriverPaths` is the documented one — use it.
  All three take INF drivers only. So: vendor NVMe/xHCI/NIC drivers in `\charon\drivers7\` +
  `DriverPaths` = EASY for Charon to write, and it is the whole of what Charon can do itself.
- The chicken-and-egg cases:
  - xHCI-only machine, install stick on USB: WinPE loads from the stick through the firmware
    (ramdisk), then loses the stick until the xHCI driver loads. `DriverPaths` are read from the
    answer file, which Setup must first find on the (now invisible) stick. UNVERIFIED which wins;
    this is the classic "A required CD/DVD drive device driver is missing" failure and usually
    needs the driver INSIDE boot.wim. Charon-side ways around it without editing boot.wim: put the
    Setup files on an internal SATA/NVMe partition (Charon already copies media to a volume), so
    WinPE needs only the storage driver — and for NVMe that again needs the driver before the
    answer file can be read. Realistic statement: SATA-installed machines work with `DriverPaths`;
    NVMe-only + xHCI-only machines need a boot.wim that already contains the drivers.
  - Therefore a second route is needed for the hard cases: the USER prepares the media once in a
    running Windows with DISM (Microsoft's own tool) — a small documented script Charon ships
    (`Prepare-Windows7Media.ps1`: `dism /mount-wim` boot.wim index 1+2 and install.wim, `/add-driver`
    the folder, `/add-package` KB3087873 [+KB2990941 if the user has it], `/unmount /commit`).
    That is legitimate servicing of the user's own media and keeps Charon out of WIM editing.
    Charon then only checks a marker file and proceeds.
- Charon cannot fetch vendor drivers for the user in general (no stable official URLs per device,
  redistribution terms vary). Proposal: `\charon\drivers7\` is filled by the user (or by the
  preparation script from a folder); Charon reports per device class whether a matching INF is
  present (PCI id match against the INFs is feasible in firmware: plain text parsing) and refuses
  when the boot disk has no driver.

### 2.5 Verdict for native Windows 7
Feasible and lawful on roughly 2011-2018 UEFI machines with Secure Boot off; on GOP-only firmware
only with Charon's own Int10h shim; on machines newer than that, not without modified system
binaries, which Charon excludes. Every element is unproven until run on media and real hardware.

### 2.6 Test platforms
- VMware Workstation (the current test platform): Windows 7/8/8.1 x64 are supported guests with
  `firmware = "efi"`; VMware's EFI firmware answers the legacy video calls, so Windows 7 boots
  there WITHOUT the shim. UNVERIFIED today but long-standing. Virtual hardware that Windows 7 has
  inbox drivers for: SATA (AHCI) or LSI Logic SAS disk, e1000 (not e1000e/vmxnet3 without Tools),
  HD Audio, USB 2.0 controller (set `usb.present`/EHCI; the xHCI controller has no Windows 7
  driver). NVMe virtual disk = a good negative/driver-injection test. Secure Boot off.
  VMware Tools for Windows 7 need the SHA-2 update first (UNVERIFIED which Tools version is last).
  VMware cannot prove the shim: for that either a real GOP-only machine or QEMU/OVMF with a
  GOP-only display (`-device virtio-gpu-pci` or `ramfb`, where OVMF installs no VBE shim) —
  the latter is a good automated test of Charon's shim (expected: hang without, boot with).
- QEMU/OVMF: `-vga std`/QXL get OVMF's own VbeShim (Windows 7 boots); ICH9 AHCI, e1000, `intel-hda`
  + `hda-duplex` and `usb-ehci` have inbox drivers; `nvme` and `qemu-xhci` do not.
- 32-bit editions: out (Charon is x64 UEFI; 32-bit Windows 7 has no UEFI boot at all).

---------------------------------------------------------------------------------------------------

## 3. Windows 8 and 8.1 hardware/UEFI
- Native UEFI 2.3.1 + Secure Boot support (their boot managers are Microsoft-signed; whether a 2012
  -2014 `bootmgfw.efi` is still accepted depends on the machine's dbx revocations — UNVERIFIED, and
  the 2023+ certificate rollover makes old media more likely to be refused; Charon reports, never
  bypasses). No Int10h requirement: GOP-only firmware is fine.
- Inbox: xHCI on both; NVMe (stornvme) on 8.1 only — Windows 8.0 has none (UNVERIFIED); neither has
  drivers for Intel SST/SoundWire audio, VMD, Wi-Fi 6. Kaby Lake/Ryzen and later were never
  supported by Microsoft on 8.1 either, but 8.1 generally boots on much newer hardware than 7
  (generic class drivers). Upper limit UNVERIFIED; reports of 8.1 on 12th gen exist.
- VMware: supported guests with EFI and Secure Boot; SATA/NVMe(8.1)/e1000e/HD Audio/xHCI work.

---------------------------------------------------------------------------------------------------

## 4. Unattended Setup differences (answer file)
Charon's generator is `Xml`/`Component`/`RunCommand` in `src/efi/CharonWsInstall2.h` lines 384-560.
UNVERIFIED against the Windows 7/8.1 unattend references today (the reference is linked from the
Microsoft page above: learn.microsoft.com/previous-versions/windows/it-pro/windows-8.1-and-8/ff699038).

| Element Charon writes today | 7 | 8 | 8.1 | Action |
|---|---|---|---|---|
| windowsPE: International-Core-WinPE, Setup (DiskConfiguration, ImageInstall, UserData) | same schema | same | same | keep; `ImageInstall/InstallFrom/MetaData` by index works |
| `UserData/ProductKey` | optional | required | required | ask for the key for 8/8.1 |
| windowsPE RunSynchronous `LabConfig` bypass values | meaningless | meaningless | meaningless | drop |
| windowsPE RunSynchronous starting NVDA/Narrator from `\charon\pe` | no audio stack (section 5) | same | same | drop; nothing speaks |
| specialize `BypassNRO` | n/a | n/a | n/a | drop |
| specialize Narrator `Accessibility\Configuration` values, `NarratorAfterSigninResetCompleted`, `WinEnterLaunchEnabled` | value exists (Ease of Access list), the Narrator-specific ones do not | partly | partly | per-version table; test |
| oobeSystem `HideOnlineAccountScreens`, `HideLocalAccountScreen`, `HideOEMRegistrationScreen` | do not exist — an unknown setting makes Setup FAIL the pass | exist | exist | emit per version |
| oobeSystem `HideEULAPage`, `NetworkLocation`, `ProtectYourPC`, `HideWirelessSetupInOOBE`, `UserAccounts/LocalAccounts`, `AutoLogon`, `FirstLogonCommands` | exist | exist | exist | keep |
| BitLocker `PreventDeviceEncryption` (r401) | n/a | n/a | applies to 8.1 device encryption | keep for 8.1 only |
| `DriverPaths` (new) | exists | exists | exists | add for 7 |

A wrong component or setting name does not get ignored by old Setup: it stops with "Windows could
not parse or process the unattend answer file". With a silent Setup that means a dead, mute
machine, so the Windows 7 answer file must be validated against the Windows 7 schema (Windows SIM
from the Windows 7 AIK, or simply repeated VM runs) before release.

### 4.1 Charon's PowerShell scripts on old systems
Shipped PowerShell: 2.0 on 7 SP1, 3.0 on 8, 4.0 on 8.1 (5.1 on 10/11). Constructs found today in
`tools/charon-addon/*.ps1` (counts by grep; all eight scripts are affected):

| Construct | Needs | Files | On 7 (PS 2.0) | On 8 (3.0) | On 8.1 (4.0) |
|---|---|---|---|---|---|
| `Get-CimInstance`/`Invoke-CimMethod` | PS 3.0 | MakeRoom, SecureBoot(7), WindowsProfile, Install-Charon, Install-CharonAddon | breaks (use `Get-WmiObject`) | ok | ok |
| `Invoke-WebRequest`, `ConvertFrom-Json` | PS 3.0 | Network, Readers(5), WindowsProfile | breaks (use `System.Net.WebClient`) | ok | ok |
| `[pscustomobject]`, `[ordered]`, `-in`/`-notin`, `$PSScriptRoot` in a script | PS 3.0 | all | breaks | ok | ok |
| `Get-FileHash` | PS 4.0 | Readers, WindowsProfile | breaks | breaks | ok |
| `Expand-Archive`, `Get-ItemPropertyValue`, `::new()` | PS 5.0 | WindowsProfile and others | breaks | breaks | breaks |
| `Get-Partition`/`Get-Disk`/`Get-Volume`/`Resize-Partition` (Storage module) | Windows 8+ (OS component, not WMF) | MakeRoom(11), Install-Charon(5), Install-CharonAddon(5) | absent even with WMF 5.1 (use diskpart/WMI) | ok | ok |
| `Register-ScheduledTask` etc. | Windows 8+ | Readers, WindowsProfile, Install-* | absent (use `schtasks.exe`) | ok | ok |
| `Confirm-SecureBootUEFI`, `Get-SecureBootUEFI` | Windows 8+ | SecureBoot, MakeRoom, Install-*, CharonEfiSign | absent (irrelevant: Secure Boot must be off) | ok | ok |
| `Get-NetAdapter`, `Get-AppxPackage` | Windows 8+ | Network, WindowsProfile | absent | ok | ok |
| `[Net.SecurityProtocolType]::Tls12` | .NET 4.5 enum | Readers:141, WindowsProfile:726 | PS 2.0 runs on CLR 2: the name does not exist; needs `[enum]::ToObject(...,3072)` AND .NET 3.5 TLS 1.2 update (KB3154518) | ok (.NET 4.5), TLS 1.2 must be switched on | ok |

Options for Windows 7: (a) a separate small script set written for PowerShell 2.0 + `cmd`/`bcdedit`/
`schtasks`/`diskpart`/`certutil -hashfile` (clean, testable; recommended: `Install-Charon7.ps1`,
`Charon-Readers7.ps1`, `Charon-Profile7.ps1`); (b) install .NET 4.8 + WMF 5.1 (KB3191566) first and
reboot — still leaves the Windows 8+ modules missing, adds two large installers and a restart to a
phase where nothing speaks yet. Recommendation: (a). For 8.1 a compatibility shim (own `Get-FileHash`,
`Expand-Archive`, `Get-ItemPropertyValue` fallbacks) in the existing scripts is enough.

### 4.2 Network, TLS, certificates, signatures on a clean Windows 7 SP1
- TLS 1.2 exists in Schannel but is off by default for WinHTTP/.NET: needs KB3140245 + the
  `DefaultSecureProtocols` registry value, and for .NET `SchUseStrongCrypto`. Without it no modern
  HTTPS download works (github.com, nvaccess.org). UNVERIFIED today, well documented by Microsoft.
- Root store is from 2010: many current chains fail until the root update runs.
- SHA-2: KB4490628 (servicing stack) + KB4474419 are required to install any update released after
  July 2019 and to load SHA-2-only signed binaries. VERIFIED: https://support.microsoft.com/help/4472027 ;
  both are in the Update Catalog today (search returns Windows 7 rows).
- Charon's own Windows binaries (the `src/windows` helpers, `nvdaControllerClient.dll`) and the
  reader installers: unsigned or SHA-256-signed user-mode executables RUN on a clean SP1 (signature
  only matters for the UAC prompt/kernel drivers); they must be built for the old API level
  (no `api-ms-win-*` imports newer than 6.1). To be checked per binary.
- Consequence for ordering: everything needed before the network works must be ON THE MEDIA —
  Charon (in firmware, where it has its own network stack) downloads the Windows 7 prerequisites
  and NVDA to `\charon\win7\` before Setup starts, exactly as it already does for NVDA
  (`\charon\readers`). First-logon work is then offline until TLS/roots are fixed.

### 4.3 Charon write-back (reader driver, boot manager files, "Charon" entry)
- Windows 7 x64 on UEFI has the same BCD firmware namespace: `bcdedit /enum firmware`,
  `/copy {bootmgr}`, `/set {fwbootmgr} displayorder`, and `mountvol S: /S` for the ESP all exist
  on 7. UNVERIFIED on a live system (no media), but these are the commands `Install-Charon.ps1`
  already uses (8 `bcdedit` call sites), so the write-back logic ports; only its PowerShell wrapper
  (Storage cmdlets, CIM, scheduled tasks) must be rewritten for 7.
- The Charon reader driver (`Driver####`) and boot manager are firmware-side and independent of
  the Windows version. Voice/network hand-over files written to the ESP: same.
- Unsigned Charon binaries + Secure Boot off (mandatory for 7) = no MOK/shim questions at all on 7.
- Multi-boot: install the older Windows FIRST. A Windows 7 Setup run after Windows 10/11 replaces
  `\EFI\Microsoft\Boot\bootmgfw.efi` with its 2009 version, which cannot start Windows 10/11.
  With one shared BCD store the NEWEST bootmgr must own the ESP. Charon can avoid the whole issue
  the way it already separates systems: keep each Windows' boot files in its own ESP/folder and
  chainload — recommended for 7 (also because 7 needs the Int10h shim run before its loader, which
  only happens when Charon starts it; the plain firmware entry "Windows Boot Manager" would hang on
  a GOP-only machine. The Charon entry must be first in BootOrder and Charon must say why).
  Secure Boot conflict: a machine dual-booting 7 and 11 must run with Secure Boot off for both.

---------------------------------------------------------------------------------------------------

## 5. Accessibility

### 5.1 NVDA version pins (VERIFIED today from NV Access documents and by HEAD requests)
- NVDA changes file (https://download.nvaccess.org/documentation/changes.html):
  2017.4: "The minimum supported Operating System for NVDA is now Windows 7 Service Pack 1";
  2024.1: "NVDA no longer supports Windows 7 and Windows 8. Windows 8.1 is the minimum Windows
  version supported. (#15544)"; 2026.1: "NVDA no longer supports Windows 8.1." and "NVDA no longer
  supports 32-bit Windows or Windows 10 on ARM."
- So: Windows 7 SP1 and Windows 8 -> **NVDA 2023.3.4**; Windows 8.1 -> **NVDA 2025.3.3**.
- NVDA 2023.3.4 user guide, System Requirements: "all 32-bit and 64-bit editions of Windows 7,
  Windows 8, Windows 8.1, Windows 10, Windows 11 ... For Windows 7, NVDA requires Service Pack 1 or
  higher." No further update is named.
  (https://download.nvaccess.org/releases/2023.3.4/documentation/en/userGuide.html)
- NVDA 2025.3.3 user guide: "all 32-bit and 64-bit editions of Windows 8.1, Windows 10, Windows 11
  ... Windows 8.1 ... no longer under active support."
- Download URLs (HTTP 200 today):
  - https://download.nvaccess.org/releases/2023.3.4/nvda_2023.3.4.exe — 32,085,360 bytes
    (the old address www.nvaccess.org/files/nvda/releases/2023.3.4/... redirects to it; the GitHub
    release asset `release-2023.3.4/nvda_2023.3.4.exe` returns 404).
  - https://download.nvaccess.org/releases/2025.3.3/nvda_2025.3.3.exe — 51,064,264 bytes (also on
    GitHub `release-2025.3.3`).
  SHA-256 values must be pinned by the maintainer as for 2026.3 (`Charon-Readers.ps1` line 55-57).
- Both old releases are unmaintained and 2023.3.4 has known later security fixes it will never
  get. Say so.
- Both bundle eSpeak NG (Polish included). The installed copies are 32-bit and live in
  `Program Files (x86)` — `Charon-Readers.ps1` already looks there (line 246-254).
- Silent install: `nvda_x.exe --install-silent` exists in both. NVDA's "start at sign-in / on the
  logon screen" registration differs on 7 (Ease of Access `Accessibility\ATs` + `Configuration`):
  `Set-AtConfiguration` in `Charon-Readers.ps1` must be re-tested per version.
- `nvdaControllerClient.dll` shipped by Charon is x64 and current; the controller client RPC
  interface is old and stable, expected to talk to 2023.3.4 — UNVERIFIED.

### 5.2 Speech in Setup (WinPE)
- Today's mechanism (read from `src/windows/nvda-setup.cmd`, `peaudio.c`, `CharonWsInstall2.h`
  257-345): the answer file starts a script from `\charon\pe` that does `net start audiosrv`,
  loads NVDA stand-ins built for the 64-bit-only WinPE and a synth driver that plays through the
  audio category Setup allows. This depends on the Windows 10/11 Setup image CONTAINING the audio
  service, audio drivers and Narrator.
- Windows 7's WinPE 3.x and Windows 8/8.1's WinPE 4.0/5.0 have no audio service, no audio drivers
  and no Narrator; Microsoft added Narrator to Setup only in a Windows 10 release. UNVERIFIED
  (no media to look inside; consistent with blind users of that era relying on unattended installs
  and the community "Talking Windows PE"). Expected result: NOTHING speaks during Setup on 7, 8, 8.1.
  Making it speak would mean adding the audio stack to boot.wim (WIM editing with files from
  install.wim — the Talking PE method): HARD, fragile, not a first-version item.
- What Charon can do instead: (1) say before the hand-over, in its own voice, "Setup will be silent
  for about N minutes; the computer restarts twice; speech returns at the desktop; if nothing
  speaks after M minutes, ..." ; (2) make Setup fully unattended (any prompt = dead end);
  (3) optional progress cues from WinPE through the PC speaker (`beep` device exists in WinPE;
  many modern machines have no speaker — UNVERIFIED value); (4) at each restart the firmware runs
  Charon again (its entry is first), and Charon CAN speak there: "Windows 7 Setup: stage 2 of 3" —
  this is the one real status channel and is cheap (a stage marker file on the ESP, written by
  RunSynchronous/specialize commands, read by Charon at boot).

### 5.3 After installation
| | 7 SP1 | 8 | 8.1 |
|---|---|---|---|
| Narrator | very basic; English only (Microsoft Anna); no Polish voice; not a usable reader | improved, touch-oriented; more voices with language packs | same as 8, slightly better |
| NVDA | 2023.3.4, eSpeak NG | 2023.3.4 | 2025.3.3 |
| First speech | first logon (FirstLogonCommands -> silent NVDA install -> start) | same | same |
| Sign-in screen | NVDA via Ease of Access registration (test) | test | test |
| RHVoice | UNVERIFIED (minimum OS of the current SAPI5 build and of the NVDA add-on for 2023.3.4's add-on API not checked) | UNVERIFIED | UNVERIFIED |
| JAWS | current JAWS does not support these systems — drop from the dialogue there (UNVERIFIED exact last versions) | | |

Speech after installation is conditional on an audio device with a driver (section 2.2). A Windows 7
install that finishes on a machine with SoundWire audio is complete and permanently mute. Charon
must check the audio hardware BEFORE offering to install and say "no sound driver for Windows 7 on
this computer; connect a USB headset or do not install".

Result per version: Setup speaks NO / NO / NO (Charon speaks before and between the stages).
Installed system speaks YES with NVDA 2023.3.4 / YES with 2023.3.4 / YES with 2025.3.3.

---------------------------------------------------------------------------------------------------

## 6. What the user must be told
- End of support: Windows 7 — 2020-01-14; paid ESU ended 2023-01-10. Windows 8 — 2016-01-12.
  Windows 8.1 — 2023-01-10, no ESU. (Microsoft: https://support.microsoft.com/help/10700 ,
  https://learn.microsoft.com/lifecycle/products/windows-7 ; dates confirmed by search today.)
- No security updates since then; known remotely exploitable holes stay open. Not for a computer
  that is on the internet for banking, mail or anything personal.
- Browsers: Chrome/Edge ended with version 109 (January 2023). Firefox ESR 115 was kept alive by
  Mozilla for 7/8/8.1 "until the end of ESR 115 in August 2026, then announce further extensions or
  an end of support" (https://support.mozilla.org/kb/firefox-users-windows-7-8-and-81-moving-extended-support );
  whether it was extended past August 2026 is UNVERIFIED — check before writing the dialogue text.
- The screen reader is an old, unmaintained NVDA.
- Dialogue rule: each entry states its speech status ("silent during Setup, NVDA 2023.3.4 after
  installation") and risky entries need the warning plus an extra confirmation. All three belong
  in a separate group "Older Windows (unsupported, your own media)", not among the normal versions.
- Secure Boot must be turned off for Windows 7; Charon says what that means and does not do it
  behind the user's back.

---------------------------------------------------------------------------------------------------

## 7. Windows 7 profiles fetched over the internet
Format as in `work/charon-windows-profiles-research-2026-11-23.md`: ADD / add-on / EXCLUDE. None of
the Windows 10/11 profiles (Slim, ReviOS, AtlasOS, Win11Debloat, WinUtil...) apply to 7/8/8.1:
for those the existing profiles are "unchanged" only.

Because a clean SP1 cannot do modern HTTPS (4.2), "over the internet" for Windows 7 means: Charon
downloads in firmware before Setup (to `\charon\win7\`, SHA-256 pinned), Windows installs offline.

### 7.1 "Windows 7 base" (Microsoft Update Catalog) — ADD (required, not optional)
- Contents, in this order: KB4490628 (servicing stack, March 2019), KB4474419 (SHA-2 code signing),
  KB3140245 + registry (TLS 1.2 default for WinHTTP), then optionally KB3020369 + KB3125574
  (the "convenience rollup", everything to April 2016), a later servicing stack, and the last free
  monthly rollup KB4534310 (January 2020).
- VERIFIED: Update Catalog searches for KB3125574, KB4474419, KB4490628, KB4534310 and KB5022338
  return Windows 7 rows today (https://www.catalog.update.microsoft.com/Search.aspx?q=KB...).
  The direct file URLs (download.windowsupdate.com / catalog.s.download.windowsupdate.com) and
  hashes were NOT collected today — to do when implementing; the exact prerequisite chain is
  UNVERIFIED and must be proven in a VM.
- Unattended: `wusa.exe file.msu /quiet /norestart` (exit 3010 = restart needed). Several restarts.
- ESU rollups (February 2020 - January 2023, e.g. KB5022338) are in the catalog but install only
  on a machine with a paid ESU key; the community "ESU bypass" tools defeat a licence check —
  EXCLUDE. Charon stops at January 2020 and says so.
- Effect on speech: none expected (no audio/Narrator components); NVDA is unaffected. Restarts in
  the middle are silent unless NVDA is already installed — install NVDA FIRST, then update.
- Publisher/licence: Microsoft, official. Verdict: ADD as part of every Windows 7 install (minimal
  set: SSU + SHA-2 + TLS) and as an optional "fully updated to January 2020" level.

### 7.2 Legacy Update — ADD as an optional profile, with the "unofficial" warning
- https://github.com/LegacyUpdate/LegacyUpdate , Apache-2.0 (VERIFIED, GitHub API + README),
  release v1.14.1.0 of 2026-10-22, asset `LegacyUpdate-1.14.1.exe` (599,176 bytes), actively
  maintained. Author Adam Demasi. The setup installs prerequisites (per its release notes: SHA-2
  support, servicing stack, root certificate store update) and restores Windows Update/Microsoft
  Update on XP-8.1 through the project's own server (legacyupdate.net) acting as a replica/proxy of
  Microsoft's site; the README states the WEBSITE is not open source ("much of it is Microsoft
  code").
- Unattended: `/S` silent (exit code 3010 when a restart is needed), `/passive`, `/norestart`
  (https://legacyupdate.net/help/flags , via search result today; page not fetched directly).
  Installing the updates afterwards still needs driving the Windows Update Agent (scriptable through
  the `Microsoft.Update.Session` COM object from PowerShell 2.0) — a Charon script of its own.
- Trust: open-source client, single maintainer, third-party server in the update path, updates
  themselves remain Microsoft-signed. Verdict: ADD, labelled unofficial, as the convenient
  alternative to 7.1's long chain (it can also take over the root-certificate step).
- Speech: UNVERIFIED; nothing in its description touches audio or accessibility.

### 7.3 WSUS Offline Update (and its community edition) — EXCLUDE from the dialogue
- The GitHub repository `wsusoffline/wsusoffline` was last pushed 2020-09-19 (VERIFIED, API), no
  licence metadata; the original project ended in 2020 and the community edition lives on GitLab
  (UNVERIFIED state). It is a two-step tool (build an update repository on another PC, then run the
  installer) — it does not fit a single unattended first-logon step, and 7.1/7.2 cover the need.

### 7.4 UpdatePack7R2 (simplix) — EXCLUDE
- A closed-source, single-author repack that redistributes Microsoft updates as one executable
  (UNVERIFIED details; not fetched). Not verifiable, redistribution of Microsoft binaries by a third
  party, and reported to include changes of its own. Even under the "unofficial with a warning"
  rule Charon cannot say what it does. If the user insists: only as a URL the user types.

### 7.5 "Charon clean" for Windows 7 — ADD (Charon's own code, EASY)
- On media that is plain SP1 none of the telemetry updates are present. The profile therefore:
  never installs KB2952664, KB3021917, KB3068708, KB3080149 (compatibility/telemetry) and KB3035583
  (the "Get Windows 10" app) when updating selectively; if the user chose the full rollup level
  (the monthly rollups include the appraiser), it disables the scheduled tasks under
  `\Microsoft\Windows\Application Experience`, `Customer Experience Improvement Program`,
  `Autochk\Proxy`, the `DiagTrack` service, sets CEIP opt-out, hides GWX. KB numbers from general
  knowledge — UNVERIFIED list, to be checked against Microsoft's KB pages when implementing.
- Plus accessibility-neutral tidy-up: no gadgets, no "Windows 10 upgrade" prompts, Windows Update
  set to "never check" after the base set (the service errors out anyway).
- Must NOT touch: Windows Audio / AudioEndpointBuilder, Ease of Access keys, SAPI, Themes service
  (NVDA does not need Aero, but leave it). Same speech guard as `Protect-Speech` in the 10/11 script,
  rewritten for PowerShell 2.0.
- Third-party debloat/privacy tools that still support 7 (WPD, Blackbird, old O&O builds) are
  closed freeware — EXCLUDE; no maintained open-source Windows 7 debloat script from an official
  repository was found (UNVERIFIED: not searched exhaustively).

### 7.6 Modern-software enablers
- .NET Framework 4.8 offline installer (Microsoft, supports 7 SP1) — add-on (needed by many
  programs; UNVERIFIED prerequisites). WMF 5.1 — add-on only if option (b) of 4.1 is chosen.
- Root certificates: via Legacy Update (7.2) or Windows' own automatic root update once TLS and
  SHA-2 are in place — add-on, part of the base; mechanism on 7 UNVERIFIED.
- Firefox ESR 115 (Mozilla, official) — ADD as the offered browser IF Mozilla still updates it
  (UNVERIFIED after August 2026, section 6). NVDA 2023.3.4 works with it.
- Supermium — add-on with the "unofficial" warning: https://github.com/win32ss/supermium , Chromium
  fork "for Windows XP/2003 and up", release v150-r3 of 2026-11-18 (VERIFIED, API; 64-bit installer
  139,779,488 bytes), single maintainer (win32ss), no licence metadata on the repository (Chromium
  is BSD; the patch set's terms UNVERIFIED), silent-install switch UNVERIFIED. Runs a sandbox with
  reduced guarantees on 7. Accessibility with NVDA 2023.3.4: UNVERIFIED (Chromium accessibility is
  IAccessible2/UIA, expected to work).
- VxKex (API extension layer, per-application, does not replace system files) — add-on, unofficial
  warning, opt-in only: https://github.com/i486/VxKex release 1.2.2.2316 of 2026-10-26 (VERIFIED,
  API); no licence metadata on the repository (UNVERIFIED terms); README: "Many programs require
  KB2533623 and KB2670838". It injects DLLs into chosen programs — NVDA also injects into processes;
  interaction UNVERIFIED. Do not enable it for NVDA itself.
- "Extended kernel" projects that replace `kernel32.dll`/`ntdll.dll`/`ntoskrnl` system-wide, patched
  `acpi.sys`, patched-signature driver packs — EXCLUDE (modified Microsoft binaries, break SFC and
  servicing, some need signature enforcement off).

### 7.7 Profiles for 8 / 8.1
- 8.1: "base" = KB2919355 chain + servicing stack + last rollup of January 2023 from the catalog —
  add-on, same pattern as 7.1 (UNVERIFIED chain); Legacy Update supports 8.1 too. TLS 1.2 and
  SHA-2 work out of the box on 8.1, so its downloads can happen in Windows.
- 8.0: updates ended 2016; Store upgrade to 8.1 no longer works. Nothing to offer.

---------------------------------------------------------------------------------------------------

## 8. Recommendations and work lists

### 8.1 Windows 7 SP1 x64 — OFFER (own media only), group "Older Windows", experimental until proven
Warnings in the entry: unsupported since 2020, not for internet use; Setup is silent; NVDA 2023.3.4
(old) speaks after installation; Secure Boot must be off; only on computers Charon's check accepts.

Work list (overall HARD):
1. Own-media path for Windows 7: recognise the media (version from `\sources\idwbinfo.txt`/the WIM
   XML), take the UEFI loader from `efisys.bin`, copy to the install volume. MEDIUM.
2. Pre-flight hardware check with spoken verdict (CPU generation, boot disk controller, xHCI-only,
   audio type, NIC, Secure Boot, CSM/GOP-only) and refusal reasons. MEDIUM.
3. Int10h/VBE shim in Charon (from the BSD VbeShim/VgaShim/UefiSeven lineage), run before starting
   the Windows 7 loader every boot; read-back verification of the legacy region; refusal when the
   region cannot be written. MEDIUM to write, HARD to prove (needs GOP-only hardware; QEMU/OVMF with
   virtio-gpu as the automated test).
4. Windows 7 answer-file variant (section 4 table) incl. `DriverPaths`, no key requirement, stage
   markers for Charon's between-restart announcements. MEDIUM.
5. Driver folder `\charon\drivers7` + INF/PCI-id matching report; documented
   `Prepare-Windows7Media.ps1` (DISM on a running Windows) for NVMe-only/xHCI-only machines. MEDIUM
   (script) — Charon editing WIMs itself: not planned (HARD, low value).
6. PowerShell 2.0 script set: `Install-Charon7` (bcdedit/mountvol/diskpart), `Charon-Readers7`
   (NVDA 2023.3.4 from the media, pinned SHA-256 via `certutil -hashfile`, Ease of Access
   registration), `Charon-Profile7` (base updates, Charon clean, optional Legacy Update / browser).
   MEDIUM.
7. Firmware-side downloads to `\charon\win7\` (NVDA 2023.3.4, KB4490628, KB4474419, KB3140245,
   optional rollups, Legacy Update) with pinned hashes. EASY (existing mechanism) once URLs are
   collected.
8. Boot plan: Charon entry first, Windows 7 always started through Charon (shim), separate boot
   files from newer Windows. MEDIUM.

Must be proven end-to-end before it is shown as working (all need the user's Windows 7 media):
- VMware EFI VM, SATA + e1000 + HD Audio + USB 2: unattended install to the desktop with no key
  press; NVDA 2023.3.4 heard (wavcapture/host speakers) at first logon and at the sign-in screen;
  Charon entry written and first; Charon reader runs at next boot; base updates install; restart
  announcements by Charon between stages.
- VMware with an NVMe disk + prepared media: driver route works; without drivers Charon refuses.
- QEMU/OVMF with a GOP-only display: hangs without the shim, boots with it.
- At least one real GOP-only (no CSM) machine of the supported era and one with CSM. Without real
  hardware the shim and the driver limits remain UNPROVEN and the entry must stay marked
  experimental.
- Polish and English runs (decimal-comma locale).

### 8.2 Windows 8.1 x64 — OFFER (own media only), group "Older Windows"
Warnings: unsupported since January 2023; Setup is silent; NVDA 2025.3.3 after installation;
key required. No shim, Secure Boot may stay on if the firmware still accepts the old boot manager
(else Charon reports the refusal).
Work list (MEDIUM): version detection; answer-file variant (key mandatory, no LabConfig/BypassNRO,
no PE speech, stage markers); compatibility fallbacks in the existing scripts for PowerShell 4.0
(`Expand-Archive`, `Get-ItemPropertyValue`, `::new`); NVDA 2025.3.3 pin; profiles "unchanged" +
optional 8.1 base updates; JAWS removed from the reader list. Proof: VMware EFI + Secure Boot VM,
unattended to the desktop, NVDA heard, write-back, Charon reader at next boot; one real machine.

### 8.3 Windows 8 (8.0) x64 — DO NOT OFFER as an entry
Reasons: unsupported since January 2016 (earliest of the three), no NVMe driver, no updates
obtainable, NVDA stuck at 2023.3.4, no user demand distinct from 8.1 (8.0 keys install 8.1), a third
answer-file/script variant (PowerShell 3.0) to maintain and test with media nobody here has. If the
user's own media turns out to be 8.0, Charon says so and explains instead of installing; revisit
only on explicit demand (then: MEDIUM, mostly the 8.1 path plus `Get-FileHash` fallback).

---------------------------------------------------------------------------------------------------

## 9. Fallback only: Windows 7 inside Charon's PC emulator (short)
The emulator (`src/pc`, Bochs 3.1) is built as a 32-bit Pentium-class machine
(`BX_CPU_LEVEL 6`, `BX_SUPPORT_X86_64 0`, no SMP, RAM up to 2048 MB, IDE, no USB/NIC; measured about
73 M emulated instructions per second under Charon with the reader). Windows 7 would have to be the
32-bit edition, would take hours to install and minutes per action, and neither emulated sound card
(SB16, ES1370) has a Windows 7 driver, so NVDA could only speak through a new NVDA synthesizer
add-on that writes text to COM1 for Charon's voice (the Braille 'n Speak path, `pc_bns.cpp`).
Verdict: not a product path. It would be the only option exactly when native 7 is impossible:
CPU/platform newer than the lawful driver limit (ACPI 0xA5), firmware whose legacy region cannot
be written and has no CSM, Secure Boot that cannot be turned off, or no Windows 7 driver for the
only disk or for any audio device. In those cases the honest answer is "this computer cannot run
Windows 7; use a virtual machine under Windows 10/11" rather than the emulator.

---------------------------------------------------------------------------------------------------

## 10. Decisions for the user
1. Confirm the reading: native Windows 7 on real modern UEFI hardware (assumed here), emulator not
   pursued. Is "experimental, supported only on machines Charon's check accepts (about 2011-2018
   platforms)" acceptable, given that newer machines need modified system files Charon will not use?
2. Is a silent Setup (Charon speaks before and between restarts, NVDA from the first desktop)
   acceptable, or is speech inside Windows 7 Setup required (means editing boot.wim: HARD)?
3. Driver route: user-filled `\charon\drivers7` + a DISM preparation script run on another Windows
   for NVMe/xHCI-only machines — acceptable? Or restrict version 1 to SATA + USB 2/CSM machines?
4. May Charon include the BSD Int10h shim and offer Windows 7 on GOP-only firmware, with the
   "may hang at Starting Windows" warning and extra confirmation?
5. Profiles for 7: base updates only to January 2020 (official) — and is Legacy Update (third-party
   update proxy, unofficial warning) wanted? Supermium / VxKex as unofficial add-ons: yes or no?
6. Windows 8.1: offer now (MEDIUM), or only after Windows 7? Windows 8.0: agree to not offer?
7. Who supplies test media: a Windows 7 SP1 x64 ISO (and 8.1) lawfully owned by the user is the
   precondition for any proof; which language edition (Polish media = Polish Setup; NVDA speaks
   Polish regardless)?
8. Product key for 8.1: ask in the dialogue (stored in the answer file on the media, like the
   password — same warning), or rely on the firmware OEM key where present?

## 11. Unverified points (summary)
- Everything about behaviour on real media: Setup/WinPE audio absence on 7/8/8.1, answer-file
  setting names per version, `efisys.bin` loader extraction, `DriverPaths` on xHCI/NVMe-only
  machines, bcdedit write-back on 7, NVDA sign-in registration, controller client with 2023.3.4.
- VMware/QEMU behaviour with Windows 7 EFI guests (long-standing knowledge, not run).
- Hardware limits per CPU generation, driver availability per device, SHA-1 signature state of
  period drivers, I225/I226 and newer NIC support.
- Visual Studio subscription / volume licensing listings; Windows 8.1 evaluation removal;
  activation servers; generic keys; MSDM key use when an answer file is present.
- Exact Microsoft Update Catalog file URLs/hashes and the prerequisite chain; KB2990941 availability
  (catalog search returned nothing); the telemetry KB list.
- Legacy Update flags page (seen through a search summary), its effect on speech; Supermium and
  VxKex licences and silent switches; WSUS Offline community edition state; UpdatePack7R2 details.
- Firefox ESR 115 support status after August 2026; RHVoice and JAWS minimum systems.
- Old boot managers vs. current Secure Boot revocation lists (8/8.1).

## User decisions (2026-11-23, answered by the user in a question dialogue)

- Windows 7: go ahead as an EXPERIMENTAL project ("wiem ze to nie ma wsparcia, ale mozemy taki projekt
  zrobic"). Native on modern UEFI machines, not emulated if at all possible.
- Drivers: Charon fetches them AUTOMATICALLY from the internet (no user-filled folder as the main route).
  For hardware that is too new: unofficial or equivalent drivers are acceptable - under the standing
  "unofficial" warning + extra confirmation rule.
- Security: attach the newest possible security to Windows 7 "somehow". Coordinator's limit, to be told to
  the user: ESU licence-bypass tools stay excluded (they circumvent a paid Microsoft programme); candidates
  to research instead: Microsoft base updates to January 2020, Legacy Update, 0patch micropatches, a
  maintained antivirus that still supports 7, a maintained browser (Supermium), firewall defaults.
- Setup: silent and fully automatic; Charon announces the expected silence and speaks at each restart.
- Profiles/add-ons wanted: Microsoft base updates, Charon clean profile, Legacy Update (unofficial),
  Supermium and VxKex (unofficial).
- Not decided yet: Windows 8.1 (offer now or later), product key handling, who supplies test media.
- Not part of r409; a separate batch that needs the user's own Windows 7 SP1 x64 media to prove anything.
- Later the same day: ADD Windows 8.1 too (same "Older Windows" group; 8.0 not offered). Product key: the
  user types it OR chooses from a list - proposed choices: key typed now / key stored in the firmware (OEM,
  MSDM table) / Microsoft's published generic installation key for the edition (installs without activating;
  activation is the user's own matter) / no key where the media allows it. The user has no key and no test
  media of their own at the moment, so nothing can be tested until media exists.
