# Charon: several systems side by side in one go — specification (2026-11-23)

Status: design and feasibility only. Nothing in the source tree was changed. Source read:
`work/charon-src-r395` (paths below are relative to it). Other agents were editing the same headers
while this was written (comments marked r409 already appear), so the line numbers are those of
2026-11-23 and may have moved by a few lines.

Companion document: `work/charon-detailed-questions-spec-2026-11-23.md` (per-system question
catalogue). At the time of writing it held sections 1–2 only (how questions work today, inventory of
today's questions); it had no answer-set file format yet. Section 7 below therefore defines only the
multi-system part and says what it assumes about the rest.

Marks used: **[doc]** = checked against the official documentation on 2026-11-23, URL given;
**[code]** = read in the source; **[trial]** = recorded in a results file; **[UNVERIFIED]** = not
confirmed by documentation or by a trial, must be proven before it is relied on.

---

## 1. What exists today

### 1.1 Paths that put a system beside another one

| # | Path | Where | What it does | End-to-end trial |
|---|---|---|---|---|
| 1 | Ready image "alongside the existing systems" | `src/efi/CharonWsInstall3.h:343-379` (question and typed YES), `CharonWsImages.h:29-41` (`Remap`), `CharonWsSeed.h:34-95` | The image's partitions (root, its **own** EFI partition, CIDATA seed) are written into the largest gap of a GPT disk with 512-byte sectors; `dual=yes` goes to `firstboot.sh` as argument 7, which puts the entry "Charon" first in BootOrder (`src/linux/firstboot.sh:109-113`) | **PASS for Debian 13 only**, beside a synthetic disk (EFI, data, gap, recovery made by `work/make-dual-disk.py`), not beside a real Windows: `tests/charon-r402-results.txt:60-67`. Arch image in a gap: GRUB rescue, now refused (`CharonWsInstall3.h:348-351`). Ubuntu and Fedora images in a gap: not run (`charon-r402-results.txt:68`) |
| 2 | Debian installer, disk mode "free space only" | `CharonWsInstall.h:431` (`partman-auto/init_automatically_partition select biggest_free`), target choice `:90-128` | Installer takes the largest free gap | **No trial recorded** (r406 trial 8 was a whole disk) |
| 3 | Ubuntu autoinstall, "free space only" | `CharonWsInstall2.h:58-62` (`layout: direct`, `mode: use_gap`, `match: size`) | Installer takes the gap on the largest or smallest disk | **Never run at all** (`charon-r400-results.txt:40`, `charon-r406-results.txt:225`) |
| 4 | Windows, "free space only" and "replace one partition" | `CharonWsInstall2.h:353-390` (`WillWipeDisk=false`, `CreatePartition … Extend`, `InstallTo` by position), partition number computed at `:616-624` | Setup creates one partition in the gap (plus an EFI partition when the disk has none) or reformats one chosen partition | **No trial recorded.** Only the whole-disk mode ran (`charon-r402-results.txt:29-42`, `charon-r406-results.txt:168-178`) |
| 5 | CharonDOS, then Windows 11 beside it | `CharonWsDosSystem.h:481-485` (place), `:561-569` (question, `dosdefault=yes`, then the whole Windows dialogue), `CharonWsInstall2.h:477` (`Install-Charon.ps1 -Mode Reader -FirstEntry CharonDOS` at first logon); entry `Way::dosDual` `CharonWsInstall3.h:25,469-471` | CharonDOS partition at the start of the disk (it **is** the EFI system partition, `:525-526`), rest free; boot order CharonDOS, Charon (`:551-555`) | CharonDOS on a whole disk: PASS (`charon-r403-results.txt:15-21`, `charon-r406-results.txt:193-198`, `charon-r408-results.txt:40-42`). **Windows beside it, free-space mode, `-FirstEntry`: not verified** (`charon-r403-results.txt:22-24`) |
| 6 | CharonDOS in free space of a disk that holds a system | `CharonWsDosSystem.h:491-523` | Basic-data FAT32 partition in the gap | **Not verified** (`charon-r403-results.txt:22`) |
| 7 | `WINNT` in CharonDOS | `src/dos/backend_uefi.cpp:262-282` (`find_windows`: first volume with `\EFI\Microsoft\Boot\bootmgfw.efi`), `:285-309` (stop Charon's voice, LoadImage, StartImage), `src/dos/shell.cpp:752-755` | Starts the first Windows Boot Manager found; no argument, no choice | "Not found" message verified; **starting a real Windows never verified** (`charon-r403-results.txt:6-7`) |
| 8 | Charon's own small EFI partition in free space (`InstallToNewPartition`) | `CharonWsSelfInstall.h:124-176` | When no EFI partition can take Charon: a 96 MB+ partition "Charon" in a gap | PASS in VMware (`charon-r408-results.txt:43-46`). Known defect: a late failure leaves an empty "Charon" partition (`:60-61`) |
| 9 | Making room beside Windows | In Windows: `tools/charon-addon/Charon-MakeRoom.ps1` (copied to the install media, `CharonWsInstall2.h:649`). From memory: only the trial `work/shrink-trial/shrink.sh` (Arch netboot + `ntfsresize`) | Shrinks the Windows partition, leaves the space unallocated | `Charon-MakeRoom.ps1`: never run on a real Windows (`charon-r402-results.txt:50-51`). `shrink.sh`: 10 GB freed on the LTSC disk, result "ok" (`work/shrink-trial/run3.out`, `charon-r405-results.txt:44-48`); **whether that Windows started afterwards is not in any record**. **Not part of Charon's dialogue**: the texts still say "Charon itself does not shrink other systems' partitions" (`CharonWsInstall3.h:363-364`) |
| 10 | Order of boot entries after an install | Linux: `src/linux/install-charon.sh:125-141` (system first, Charon second; Charon first when it was first already), check at every start `:44-82`. Windows: `tools/charon-addon/Install-Charon.ps1` (`-Default`, `-FirstEntry`, `Move-FirstEntry` `:441-449`) | Re-writes BootOrder after the installer changed it | Linux: PASS (`charon-r406-results.txt:144-166`). `-FirstEntry`: parser-checked only (`charon-r403-results.txt:24`) |

### 1.2 Dual-boot questions asked today

| Question | Where | Note |
|---|---|---|
| "What should be done with the disk?" — whole / replace one partition / free space only | `CharonWsInstall2.h:776-785`, choices `diskModes` | "Replace one partition" is refused for Debian and Ubuntu (`:780-784`) |
| Image: "erase the whole disk" / "alongside the existing systems" | `CharonWsInstall3.h:344-347` | |
| `clockutc`: "Will Linux be installed alongside Windows?" → `RealTimeIsUniversal` | `CharonWsInstall2.h:303, 430` | detailed mode only, default **no** |
| `nobitlocker` → `PreventDeviceEncryption=1` | `CharonWsInstall2.h:429` | default yes; effect never measured (`charon-r402-results.txt:50`) |
| CharonDOS: where; "Install Windows 11 in the free space on this disk now?" | `CharonWsDosSystem.h:485, 564` | The user is then told by voice to pick "free space" and "this same disk" by hand (`:567`) |

### 1.3 Limits in today's code that a plan must remove or respect

- One install per dialogue run: every path ends with `ask::Clear()` (`CharonWsInstall2.h:798`,
  `CharonWsInstall3.h:417`) and then leaves Charon for the installer. Nothing survives a restart.
- Asking and running are interleaved (the Windows media is downloaded between two groups of
  questions, `CharonWsInstall2.h:693-749`); a plan needs "ask everything" separated from "run".
- The answer store is flat, 48 keys × 160 bytes, one system at a time (`CharonWsAsk.h`).
- `disks::Disk` knows only the **largest** gap (`CharonWsDisks.h:25, 74`).
- `CharonSeedDisk.h` can add a partition (`AddPartition`, `:1101`) but has no function that removes or
  retypes one.
- Windows unattended: exactly one internal disk, answer file says `DiskID 0`
  (`CharonWsInstall2.h:601-606, 757-763`). Two disks of identical size stop Debian/Ubuntu/Windows
  (`CharonWsInstall.h:109-112`). Ubuntu: largest or smallest disk only, 8 GB RAM
  (`CharonWsInstall2.h:107-123`).
- Free-space modes and CharonDOS: GPT with 512-byte sectors only.
- Fedora and Arch have no unattended installer path: Fedora is a ready image without desktop or
  speech, Arch is an image (whole disk only) or a live system where the user runs `archinstall`
  himself (`CharonWsInstall3.h:47-62, 114-132`). **There is no kickstart or archinstall generator.**
- After a Windows install Windows Boot Manager is first and Charon second (`CharonWsInstall2.h:475`,
  r408); beside other systems Charon goes first (`CharonWsInstall3.h:320-321`). A plan needs one rule.
- Firmware that does not load `Driver####` (VMware's) gets the reader through "Charon first +
  `\EFI\HAK\reader-only`" (`install-charon.sh:38-82`, `BootMenu.h:190-197`); this exists for Linux only.

### 1.4 Summary

Charon has the **building blocks** of dual boot (gap installs, an image remapper, boot-order repair,
BitLocker prevention, a clock question, CharonDOS + `WINNT`), but **no combination of two real
systems has been run end to end**: the only recorded dual-boot PASS is a Debian image beside a
synthetic partition table. There is no plan, no persistence across restarts, no resume.

---

## 2. The plan mechanism

### 2.1 Terms

- **Plan**: an ordered list of 2–6 **systems** plus shared answers, the target disk(s), the computed
  layout, the default boot entry and a state per step.
- **Step**: one unit of work. Step 0 = prepare the disk. One step per system. Last step = finish.
- **Writer class** of a system — who puts it on the disk:
  - **C** (Charon writes it itself, no restart needed to *place* it): CharonDOS, ready images.
  - **I** (its own installer runs, one or more restarts): Windows, Debian, Ubuntu; later Fedora
    kickstart and Arch.
  - Guided installs ("I will install this system myself"), ISO-to-stick entries, netboot.xyz and
    Haiku-style raw images without a seed are **not allowed in a plan** (they cannot run unattended
    or cannot report completion).

### 2.2 Flow

1. Entry question (one system / several). Build the list. Shared answers once. Per-system answers.
2. Charon reads the disks and computes the layout (2.4). Pre-flight (2.5).
3. One spoken and written summary, **one typed confirmation** (section 6, `plan.confirm`).
4. Step 0: answer files for *every* step are generated now (the answers are complete here), the plan
   is written to the EFI partition, the disk is partitioned.
5. Steps run in the computed order (section 3). Class C systems are written first, without restart.
   Each class I system: Charon starts its installer; the machine restarts as often as that installer
   needs; Charon is passed through until the system reports "done"; then the next step.
6. Finish: boot order, default entry, systems registry, report, clean-up of secrets.

During 5 nobody needs to touch the computer. Charon speaks at every start: "Plan, step 2 of 4:
Windows 11 is installing itself. Next: Debian." Space or Escape within the announcement pauses the plan.

### 2.3 Where the plan lives, and how Charon comes back

**State (decision: file is the truth, variable is a pointer).**

- `\EFI\HAK\plan\plan.txt` on the plan's EFI system partition: `key=value` text, one section per step,
  a format version and a checksum line. Written as `plan.new`, old file renamed to `plan.bak`, new
  renamed to `plan.txt` (FAT has no journal; a torn write must leave a readable older state).
- `\EFI\HAK\plan\step<N>\…`: the generated answer files of step N (`Autounattend.xml`, `preseed.cfg`,
  `user-data`), deleted (overwritten with zeros first) when the step is done.
- `\EFI\HAK\plan\step<N>.done` / `step<N>.failed`: written **by the installed system** at the end of
  its own first-start set-up (Linux: `late_command`/`firstboot.sh`, the EFI partition is mounted
  there already; Windows: a `FirstLogonCommands` line after `mountvol S: /S`). Text inside: one line
  of result and, on failure, the last lines of the log.
- `\EFI\HAK\plan\log.txt`: Charon appends one line per event (start of a step, pass-through, result).
- UEFI variable `CharonPlan` (Charon's vendor GUID, NV+BS+RT, under 64 bytes): plan id, partition
  GUID of the EFI partition that holds the plan, current step, state byte. Purpose: Charon started
  from **another medium** (the USB stick) finds the plan and resumes it; and Charon on the disk
  notices a plan that lost its file. It carries no answers and no secrets.
- Secrets: Linux needs only the password hash (already so today). Windows needs the clear password
  in the answer file (already so today: `CharonWsInstall2.h:455-456`, on the install media). In a plan
  that file waits on the EFI partition until its step runs. See decision D4.

**Step states**: `pending` → `prepared` → `launched` (installer started by Charon, counter) →
`handoff` (the system's own loader is being passed through, counter and first time stamp) → `done` |
`failed` | `skipped`.

**Coming back after an installer restarts** — three layers, because no single one is reliable:

1. **Charon is first in BootOrder for the whole plan** and acts as a pass-through. At every start
   with an active plan it reads `plan.txt`:
   - a `.done` file for the current step → mark done, next step;
   - a `.failed` file → stop, section 2.9;
   - state `launched`/`handoff` and no marker → the installer is in the middle (Windows Setup restarts
     2–3 times): start that system's loader directly (Windows: `\EFI\Microsoft\Boot\bootmgfw.efi` on
     the plan's EFI partition; Linux: its loader directory) after a short spoken line. This is what
     Charon already does when it "continues" (`BootMenu.h:352-417`; seen with Windows Setup in
     `charon-r402-results.txt:36-38`). A pass-through budget (default 8 starts or 3 hours of clock
     time per step, Windows 4 hours) turns an endless loop into a `failed` step.
2. **`BootNext`** is set to Charon's own `Boot####` just before Charon starts an installer or passes
   through. The firmware uses `BootNext` once and deletes it, so it only covers the next start, but
   it covers exactly the case "the installer has just put itself first". **[UNVERIFIED on the
   target firmware; the UEFI specification page could not be fetched (HTTP 403)]** — the semantics
   are those of UEFI 2.10 section 3.1.1, https://uefi.org/specs/UEFI/2.10/03_Boot_Manager.html .
3. **Every installed system repairs the order itself** before it writes `.done`: Linux
   `install-charon.sh` (exists; in a plan it is called with "Charon first"), Windows
   `Install-Charon.ps1 -Mode Manager -Default` (exists). Then the system restarts itself and layer 1
   takes over.

What the installers do to BootOrder, and what the plan does about it:

| Installer | Behaviour | Prevention | Repair |
|---|---|---|---|
| Windows Setup / `bcdboot` | "adds a firmware entry in the NVRAM to point to the Windows Boot Manager. By default, this entry is placed as the first item in the boot list" **[doc]** https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/bcdboot-command-line-options-techref-di | none during Setup (the `/p` and `/addlast` switches are bcdboot's, Setup does not expose them) | wanted during Setup (its restarts must reach Windows); `Install-Charon.ps1 -Default` at first logon; layer 2 |
| Debian `grub-install` | new entry first (efibootmgr `--create` behaviour, `install-charon.sh:132`) | debconf `grub2/update_nvram false` **[UNVERIFIED]** — not used; the entry is wanted | `install-charon.sh` in `late_command`; `order-check` at first start (`:53-62`) |
| Ubuntu (curtin) | new entry first | curtin `grub: reorder_uefi` / `update_nvram` **[UNVERIFIED: not on the storage page that was read]** | same script in `late-commands` |
| Fedora (anaconda) | new entry first | `bootloader --leavebootorder`: "prevents the installer from making changes" **[doc]** https://pykickstart.readthedocs.io/en/latest/kickstart-docs.html | `%post` script |
| Ready image | none (Charon writes the entries itself, `CharonWsInstall3.h:318-323`) | — | — |

Remaining hole, stated plainly: if an installer dies **after** taking the first place and **before**
its repair hook, and `BootNext` was already consumed, the machine starts that half-installed system
or nothing. Then the user starts Charon from the start medium; `CharonPlan` makes it say "a plan
stopped at step N" at once. Recommendation in the summary text: **leave Charon's start medium
plugged in until the plan is finished**. Firmware that rewrites BootOrder on its own (some laptops)
cannot be tested in a VM: unknown.

Firmware that does not load `Driver####` (VMware): in plan mode Charon is first anyway, so the reader
is present through Charon itself; `reader-only` is not written during a plan.

### 2.4 Layout, computed up front

Rules (one disk; several disks in 2.10):

1. **One shared EFI system partition (ESP)**, first on the disk.
   - Default **1 GiB** when the plan creates it (two or more loaders, Charon with its synthesizers
     — 47 to 96 MB today, `charon-r408-results.txt:43-45` — Windows' boot files with the 250 MB free
     that BitLocker's layout asks for, kernels of systems that keep them on the ESP). Minimum 550 MB.
     Microsoft's own minimum is 200 MB, 300 MB on 4K-native disks **[doc]**
     https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/configure-uefigpt-based-hard-drive-partitions
   - **When CharonDOS is in the plan, the CharonDOS partition is the shared ESP** (it already is an
     ESP-type FAT32 partition today, `CharonWsDosSystem.h:525-526`; Windows then uses it, `hasEsp`
     at `CharonWsInstall2.h:607`). Size = 1 GiB + CharonDOS's need (512 MB, or profile disk 1–4 GB
     + 1 GB, + 1 GB for copied media; `CharonWsDosSystem.h:486-487`).
   - Exceptions to "one ESP", both deliberate: (a) a **ready image keeps its own small ESP** inside
     its range (its `fstab` and GRUB refer to it; this is the only image layout that passed a
     trial); (b) the escape hatch of 3.5 for two copies of the same distribution.
2. A 16 MB Microsoft reserved partition right after the ESP when any Windows is in the plan (type
   GUID only, no format; "Add an MSR to each GPT drive" **[doc]** same URL).
3. **Physical order = install order**: Windows partitions, then Linux, then the shared data
   partition. Microsoft: utility partitions before Windows, data partitions after the recovery
   partition **[doc]** same URL.
4. **Every system's range is reserved before anything is installed.** Charon creates, for each
   system, a *placeholder* partition (GPT type "Linux reserved"
   `8DA63339-0007-60C0-C436-083AC8230908`, name `Charon plan N: <system>`). Just before step N
   Charon turns the placeholder into what that installer needs:
   - installer that targets an **existing partition** (Windows, Ubuntu, Fedora, Arch): retype it
     (basic data / Linux filesystem); the installer formats it;
   - installer that only takes **free space** (Debian; ready images through `Remap`): delete it, so
     that exactly one gap exists on the disk and "the biggest free space" is unambiguous.
   This is what makes each installer use only its own range, and what lets an abort name and remove
   every unused range (2.9). It needs two new functions beside `seeddisk::AddPartition`: remove and
   retype (host-testable like the existing 164 checks of `CharonSeedDiskTests.cpp`).
5. **No shared swap partition.** Every Linux uses a swap file or zram inside its own partition.
   Reason: an installer that re-formats a found swap partition changes its UUID and the other
   system's `fstab` then waits at every start; hibernation images of two systems in one swap destroy
   each other. Detailed mode may ask for a swap size per system (a file).
6. **Optional shared data partition**, last on the disk, label `DATA`. Default file system:
   **exFAT** when CharonDOS or only Linux systems are in the plan (every system reads and writes it
   without extra drivers, no permissions to clash); **NTFS** offered when a Windows is in the plan
   (formatted by Windows Setup's `ModifyPartition … Format NTFS`; Linux mounts it with `ntfs3`).
   Who formats exFAT: the first Linux step (`mkfs.exfat` in its late command) or, with no Linux, the
   Windows first-logon script (`format /FS:exFAT`). Charon has no exFAT or NTFS formatter and should
   not grow one for this. Linux mounts it with `nofail,uid=1000,gid=1000,umask=022`.
7. Windows recovery tools: in a plan Setup is not given a recovery partition; WinRE stays inside the
   Windows partition **[UNVERIFIED that an unattended Setup onto an existing partition leaves WinRE
   enabled there]**. Detailed mode can reserve a 1 GiB partition of type
   `DE94BBA4-06D1-4D40-A16A-BFD50179D6AC` "immediately after the Windows partition" **[doc]** same URL
   and run `reagentc` at first logon. Each Windows gets its own. Decision D6.

Sizes (GiB). "Minimum" refuses below; "default" is used in the normal mode when the disk allows.

| System | Minimum | Default | Basis |
|---|---|---|---|
| Windows 11 | 64 | 100 | Windows 11's published storage requirement is 64 GB; the partition rule alone is 20 GB + 16 GB free **[doc]** same URL |
| Windows 10 / LTSC / Server | 40 | 80 | 20 GB rule + updates |
| Debian (installer) | 12 | 40 | 12 GB is today's threshold (`CharonWsInstall.h:97-99`) |
| Ubuntu (installer) | 25 | 40 | desktop image 2.9 GB + packages; Ubuntu's own guidance is 25 GB **[UNVERIFIED]** |
| Ready image | image size + 1 GiB (`CharonWsInstall3.h:357`) | 30 | grows at first start |
| CharonDOS | 1 (+ profile) | as computed | `CharonWsDosSystem.h:486-487` |
| Shared data | 1 | the rest | |

Allocation: all minimums first; refuse the plan when they do not fit (say by how much); then raise
each system toward its default in proportion; what is left goes to the place named by
`plan.rest` (default: the shared data partition when asked for, else the last system). All
boundaries on 1 MiB. Result is shown and spoken in the summary; in detailed mode each size can be
typed (`sys.N.size`).

### 2.5 Pre-flight (before the confirmation; nothing is written)

Refuse with a spoken reason: disk not GPT-capable or 4K-native where a step needs 512 (CharonDOS,
images); minimums do not fit; more than 6 systems; a guided or seedless entry in the list; Windows in
the plan with more than one internal disk (until 2.10 is proven); Ubuntu with under 8 GB RAM; two
ready images of the **same** image file (identical file-system UUIDs, 3.5); the plan's own start
medium is the target disk and Charon is not running from memory (`selfram`, r408).
Warn and ask: Secure Boot on (2.8); no network; a system without speech in the list (the existing
`ConfirmSilent`, once per such system, `CharonWsInstall3.h:97-103`); Windows media not present (the
download is step 0.5 and needs a stick or RAM, as today).
Check now, not at step 3: every download address answers (HEAD), so a dead mirror is found before
the disk changes.

### 2.6 How each installer is held to its own partition

| System | Mechanism in the plan | Keys | Status |
|---|---|---|---|
| **Windows** | Charon pre-creates and retypes the partition; answer file: `WillWipeDisk=false`, **no** `CreatePartitions`, one `ModifyPartition` (`PartitionID` N, `Format NTFS`, `Label`), `InstallTo` (`DiskID`, `PartitionID` N). Never `InstallToAvailablePartition` ("first available partition that has enough space") | `InstallTo`: "You must specify valid values for the DiskID and PartitionID" **[doc]** https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-setup-imageinstall-osimage-installto ; `WillWipeDisk` false: "the disk is not erased" **[doc]** https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-setup-diskconfiguration-disk-willwipedisk ; `ModifyPartition` **[doc]** https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-setup-diskconfiguration-disk-modifypartitions-modifypartition | Today's "replace one partition" branch (`CharonWsInstall2.h:377-383`) is exactly this; **never run**. **Open point:** Microsoft writes that the MSR "does not receive a partition ID" **[doc]**, while the whole-disk answer file that passed r402 counts it (EFI 1, MSR 2, Windows 3). How Setup numbers *existing* partitions (by position, with or without the MSR) must be measured in the first trial; the code counts by position (`:619-623`) |
| **Debian** | The step's range is the only gap: `partman-auto/init_automatically_partition select biggest_free` ("only honoured if partman-auto/method … is not set"), disk chosen by size/serial in `partman/early_command` (exists), recipe `atomic` | **[doc]** https://www.debian.org/releases/stable/amd64/apbs04.en.html | Code exists (`CharonWsInstall.h:428-434`), never run in this mode. The preseed documentation has **no key that names an existing partition**; `method{ keep }` and `$reusemethod{ }` of the recipe language do not address a partition by number **[doc]** https://salsa.debian.org/installer-team/debian-installer/-/raw/master/doc/devel/partman-auto-recipe.txt — hence the gap. Reuse of the existing ESP by partman-efi and "no swap partition" (`partman-basicfilesystems/no_swap`) are **[UNVERIFIED]** |
| **Ubuntu** | Preferred: action-based `storage: config:` with `preserve: true` on the disk and on every partition that is not the target ("curtin will attempt reuse the existing storage device"; partition: "curtin will verify that the partition exists"; format `preserve`: "curtin will not format the partition"), disk matched by `serial` | **[doc]** https://curtin.readthedocs.io/en/latest/topics/storage.html ; match keys `serial`, `path`, `size: largest\|smallest` **[doc]** https://canonical-subiquity.readthedocs-hosted.com/en/latest/reference/autoinstall-reference.html | Not written. Fallback: today's `layout: direct` + `mode: use_gap` (`CharonWsInstall2.h:58-62`) with the gap trick — **`mode: use_gap` was not found in the reference text that could be read [UNVERIFIED]**, and the whole Ubuntu path has never run |
| **Fedora** (future) | `ignoredisk --only-use=`, `clearpart --none` ("Do not remove any partitions. This is the default"), `part / --onpart=… --fstype=ext4`, `part /boot/efi --onpart=… --noformat` ("not to format the partition, for use with the --onpart command"), `bootloader --leavebootorder` | **[doc]** https://pykickstart.readthedocs.io/en/latest/kickstart-docs.html | No kickstart generator exists. Fedora in a plan = ready image until one is written |
| **Arch** (future) | `archinstall --config … --creds … --silent`, `disk_config.config_type: pre_mounted_config` ("will not perform any partitioning what so ever", "relies on what's mounted manually" under `/mnt/archinstall`); Charon's script formats and mounts the assigned partition and the shared ESP first; bootloader with `removable: false` | **[doc]** https://archinstall.archlinux.page/cli_parameters/config/disk_config.html , https://archinstall.archlinux.page/installing/guided.html | Not written. Arch image stays whole-disk only (partition number in its GRUB core image, r402) |
| **Ready image** | `Remap` into the step's range instead of "the largest gap" (`remap.gapFirst/gapEnd`, `CharonWsInstall3.h:378`); own ESP inside the range | — | Debian passed; Ubuntu, Fedora never in a gap |
| **CharonDOS** | written by Charon; is the shared ESP | — | whole-disk form passed |

### 2.7 Settings asked once, applied to all

Asked once (`shared.*`), copied into each system's answers; in detailed mode every one can be
overridden per system (`sys.N.<key>`).

| Shared answer | Windows | Linux | CharonDOS |
|---|---|---|---|
| user name, full name | `LocalAccount` | `passwd/username`, `identity`, cloud-init | 9x profile `setup_name` |
| password | clear text in the answer file | SHA-256 crypt hash | — |
| language | `wlocale` (pl_PL → pl-PL table) | `locale` | Charon's language |
| keyboard | from language | `keyboard` | — |
| time zone | **new**: `TimeZone` in the answer file from an IANA → Windows name table (today Windows derives it from the locale) | `timezone` | 9x `setup_zone` |
| host name pattern | `{name}-win11` | `{name}-debian` | `setup_computer` (15 characters) |
| screen readers, synthesizer, voice, rate | `wreaders`, `wfirst` | `readers`, `console`, `gui` | profile reader |
| Wi-Fi network and key | **[UNVERIFIED how; a WLAN profile XML via `netsh wlan add profile` at first logon]** | NetworkManager connection / netplan in the late command | — |

Default host-name pattern: `charon-pc-<short system id>`; two systems must not share a name.

**Hardware clock: UTC everywhere.** Windows gets
`HKLM\SYSTEM\CurrentControlSet\Control\TimeZoneInformation\RealTimeIsUniversal = 1` (the existing
`clockutc` branch, `CharonWsInstall2.h:430`) **automatically whenever the
plan holds a non-Windows system or two Windows** — the question disappears from a plan. Reason: the
Linux side says "maintaining the RTC in the local timezone is not fully supported and will create
various problems with time zone changes and daylight saving adjustments. If at all possible, keep the
RTC in UTC mode" **[doc]** https://www.freedesktop.org/software/systemd/man/latest/timedatectl.html ;
Debian's preseed already says `clock-setup/utc boolean true` (`CharonWsInstall.h:423`); one registry
value on Windows is less than a non-default mode on every Linux. The Windows value itself is not in
the Microsoft pages that were read **[UNVERIFIED by documentation; the Arch wiki page that describes
it refused the fetch]**; its effect has never been measured in a Charon trial — test matrix T1
measures it (same wall-clock time in both systems after a restart from each into the other).
Consequence to check: **Charon's own clock tools and CharonDOS's `DATE`/`TIME` read the firmware
clock**; with UTC in the clock they must apply the plan's time zone (a setting to add if absent).

**Windows fast start-up and hibernation off** in every Windows of a plan:
`powercfg /h off` plus `HiberbootEnabled=0` under
`HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power`, in `FirstLogonCommands`. Reason: a
hibernated NTFS volume must not be mounted read-write by Linux (shared data partition), and
`shrink.sh` refuses such a volume already. `powercfg` reference:
https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/powercfg-command-line-options
**[UNVERIFIED: page not fetched]**. `Charon-MakeRoom.ps1` has the same switch (`-NoFastStartup`).

**BitLocker**: every Windows of a plan gets `PreventDeviceEncryption = 1` ("update the
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\BitLocker registry key: Value:
PreventDeviceEncryption equal to True (1)" **[doc]**
https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/oem-bitlocker ; the same
page says automatic encryption arms only after a Microsoft-account sign-in, and Charon creates a
local account). In a plan the question `nobitlocker` is not offered as "no" without a warning that
later steps and every later boot-order change may ask for the recovery key. An **existing** BitLocker
volume is never shrunk and never written to.

### 2.8 Secure Boot

Never bypassed, never switched off by a plan (`-DisableSecureBoot` is "never part of an unattended
installation", `Install-Charon.ps1:29-35`). The plan only detects and warns before the
confirmation: with Secure Boot on, the firmware starts only signed loaders; Charon's own entry needs
its shim/MOK set-up (r391); CharonDOS and `HAKBOOT.EFI` as first entry may be refused. If Charon
itself is running, its start path already works on this machine; the warning names what will not
(for example "CharonDOS cannot be the default entry while Secure Boot is on, unless it is enrolled").
`linux` and `winnt` under Secure Boot: 4.3.

### 2.9 Failure handling

- A step fails when: Charon's own work returns an error (class C, disk preparation, download); a
  `.failed` marker appears; the pass-through budget runs out; or the user pauses and chooses so.
- **The plan stops.** Charon says exactly: step number and system, phase ("download", "installer
  did not report back after 8 starts", "first start reported: …" with the marker's text), what is
  finished and bootable, what is untouched. The same lines go to `log.txt`.
- Choice (also in the paused state): **try this step again** (its partition is reset to the
  placeholder state first) / **skip this system and continue** / **stop here**. Unattended mode takes
  `plan.on_failure` instead (section 7).
- "Stop here" and the end of every plan run the same **finish** step: boot order for the systems
  that are done, registry, report. For every reserved range that was not used the user is asked
  (default in unattended mode: keep and report): remove the placeholder (space becomes free) / keep
  it. Nothing empty stays unannounced; this also closes the known r408 defect of the silent empty
  "Charon" partition for plan runs.
- Earlier systems stay bootable because (a) no step writes outside its range and the ESP directory
  of its own loader, (b) Charon, not the newest loader, is the menu, (c) the registry lists only
  `done` systems.
- A failure **inside** an installer that neither restarts nor writes a marker (an error dialogue) cannot
  be seen by Charon. Mitigations per system: Windows — `WillShowUI=OnError` shows the dialogue and
  Narrator reads it (speech in Setup exists); Ubuntu — `error-commands` writes `.failed` and restarts
  **[UNVERIFIED key]**; Debian — the background watcher that already runs from `preseed/early_command`
  (`CharonWsInstall.h:393`) gets a time limit and writes `.failed` plus the syslog tail to the ESP.
- **Report**: spoken at the end, and written to `\EFI\HAK\plan\report.txt` on the ESP, to
  `/var/lib/charon/plan-report.txt` in every Linux and `%ProgramData%\Charon\plan-report.txt` in
  every Windows (copied by the next start of each, from the ESP). Content: per system — name,
  partition, size, user, result, duration; boot order; default entry; shared data partition; clock
  convention; what was skipped or failed and why; where secrets were erased.

### 2.10 Disk scenarios

- **Empty disk**: new GPT, layout as 2.4.
- **Disk with a system to keep**: its partitions are never moved or retyped; the plan uses free
  space. The existing ESP stays the shared ESP when Charon and the new loaders fit; else Charon's own
  small ESP (`InstallToNewPartition`, exists) and a note. Making room:
  - existing **Windows**: Charon's helper from memory (`ntfsresize`, to be built from
    `work/shrink-trial/shrink.sh`: a Linux kernel + initrd started from memory, speaks, reports
    through a marker file, restarts into Charon — a "step 0a" of the plan). **Refuses** on BitLocker
    (`-FVE-FS-` signature), hibernation or fast start-up (`hiberfil.sys` `HIBR`/`WAKE`/`RSTR`), and a
    **dirty volume** (user's rule; the trial script currently *forces* a dirty volume "like GParted",
    `shrink.sh:27` — that line must change), keeps at least 10 GB free inside Windows, takes a
    separate typed confirmation that names the partition and both sizes. Alternative offered in the
    refusal text: `Charon-MakeRoom.ps1` inside Windows.
  - existing **Linux**: not shrunk in this version; the plan needs free space already (say so).
  - The kept system gets no clock or fast-start-up change by Charon; the report says what to set.
- **Two or more disks**: default "one system per disk", each disk with its own ESP (a disk can be
  removed without breaking the others); Charon's files and entries on the disk that holds the first
  system (and CharonDOS's disk when present). Limits today: Windows unattended needs exactly one
  internal disk; identical disk sizes stop Debian; Ubuntu can match by serial only after the curtin
  rewrite. So multi-disk plans with Windows are **not** offered until a trial shows a safe way to
  name the disk to Setup (candidates, both **[UNVERIFIED]**: a `windowsPE` `RunSynchronous` diskpart
  script that finds the disk by its GPT disk GUID and re-launches `setup.exe /unattend:` with the
  number filled in; or physically one internal disk per Windows step).

---

## 3. Order per combination

### 3.1 The general rule

1. Disk preparation (ESP, MSR, placeholders).
2. **CharonDOS** (written by Charon, no restart).
3. **Ready images written to their ranges** (no restart; a failed download is found before the long
   steps). Their first start (cloud-init, packages) is a later step.
4. **Windows systems, older version first** (Windows 10 before Windows 11).
5. **Installer-based Linux systems**, in list order.
6. **First start of each ready image**, in list order (Charon passes through to its loader; its
   `firstboot.sh` writes `.done` and restarts).
7. Finish.

The user's list order decides the order *within* a class and the default boot entry proposal; it
does not override the classes.

Why Windows before Linux although Charon repairs the boot order anyway:

- Windows Setup is the least configurable about partitions, the ESP and numbering, and the one whose
  failure mode is a dialogue. It should meet the simplest possible disk and fail before hours are
  spent. Its partition numbers are also smallest and easiest to compute when its partitions come
  first.
- Two Windows share one boot manager on one ESP (3.3); the newest boot manager must be the one that
  stays, so the newest Windows is installed last. "Install the oldest operating system first" is the
  usual Microsoft guidance **[UNVERIFIED: not found on the pages read]**; `bcdboot` is documented to
  keep "any other existing boot entries" and to put the new one on top **[doc]** (bcdboot URL above).
- Windows Setup and `bcdboot` write `\EFI\Microsoft\Boot` and the fallback path
  `\EFI\Boot\bootx64.efi` and put Windows Boot Manager first **[doc for the NVRAM entry; the
  fallback file is common knowledge, UNVERIFIED]**. Linux installers that run later see the final
  ESP. **The plan never relies on `\EFI\BOOT\BOOTX64.EFI`** as anyone's loader; at finish Charon
  writes itself there only when the plan created the ESP (as CharonDOS's installer does today,
  `CharonWsDosSystem.h:548-549`).
- With `WillWipeDisk=false` and `InstallTo` an existing partition, Setup has no instruction to touch
  other partitions; that it really leaves Linux and placeholder partitions alone is the first thing
  T1 measures (hashes of the other partitions before and after, as r408 did for the self-installer).

### 3.2 The combinations named by the user

| Combination | Order of steps | Default entry | Notes |
|---|---|---|---|
| Linux + Windows | prepare → Windows → Linux → finish | Charon's menu, continuing to the user's choice | UTC clock, fast start-up off, BitLocker prevented |
| Windows + Windows | prepare → older → newer → finish | Charon's menu → Windows' own text menu (3.3) | one BCD, two entries |
| Linux + Linux | prepare → (images written) → installers → image first starts → finish | Charon's menu | no os-prober needed; 3.4, 3.5 |
| CharonDOS + Windows | prepare → CharonDOS → Windows → finish | **CharonDOS** | `winnt` |
| CharonDOS + Linux | prepare → CharonDOS → Linux → finish | **CharonDOS** | `linux` (4.3) |
| CharonDOS + Windows + Linux | prepare → CharonDOS → Windows → Linux → finish | **CharonDOS** | both commands |
| three or more, mixed | the general rule | CharonDOS if present, else as asked | up to 6 systems |

### 3.3 Two Windows on one disk

Two models; the first is proposed for the first version, the second needs a trial (decision D2).

- **Model A — shared ESP, one BCD (Microsoft's documented dual boot).** The second Setup adds its
  loader entry to the existing `\EFI\Microsoft\Boot\BCD`. Charon's menu shows **one** "Windows Boot
  Manager"; the choice between the two is made in Windows' own menu. For a blind user that menu must
  be the **text** menu drawn before the kernel starts, which Charon's reader reads; the default
  graphical tile menu appears after Windows has started loading and has no speech. So the first-logon
  script of each Windows sets `bcdedit /set {current} bootmenupolicy legacy` ("Defines the type of
  boot menu the system will use … Legacy | Standard" **[doc]**
  https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/bcdedit--set ),
  `bcdedit /set {current} description "Windows 11 Pro"` and a menu timeout. `winnt` then lands in that
  menu. Weakness: two menus in a row (Charon's, then Windows'); `winnt 2` cannot pick directly.
- **Model B — each Windows has its own 300 MB ESP and its own BCD.** Charon hides the shared ESP
  during the second Setup (GPT type flipped to a private "Charon hidden" type and back), so Setup
  uses the private ESP. Charon then has two different `bootmgfw.efi` files and lists "Windows 10" and
  "Windows 11" itself; `winnt 1` / `winnt 2` start them directly; no Windows menu at all. Weaknesses:
  more partitions; that Windows servicing keeps updating the boot files on the ESP it was started
  from, and that firmware resolves Charon's own `Boot####` while its ESP type is flipped, are
  **[UNVERIFIED]**.

Recovery partitions: one per Windows or none (2.4 rule 7). Both Windows get UTC clock, fast start-up
off, `PreventDeviceEncryption`. Each Windows sees the other's partition as a drive letter; the
first-logon script removes that letter (`mountvol X: /D`) unless detailed mode says to keep it.

### 3.4 Two or more Linux systems: who owns the menu

- **Charon is the top-level menu.** No distribution's GRUB is asked to list the others:
  `GRUB_DISABLE_OS_PROBER` — os-prober "is disabled by default"; "Set this option to 'false' to
  enable" **[doc]** https://www.gnu.org/software/grub/manual/grub/html_node/Simple-configuration.html
  — stays at its default, and the Debian preseed lines `grub-installer/only_debian false` /
  `with_other_os true` (`CharonWsInstall.h:454-455`) only decide *where* GRUB installs, not what it lists.
- Each system keeps its own loader directory `\EFI\<id>\` on the shared ESP (or its own ESP for
  images). On EFI the directory name is the bootloader id, and `GRUB_DISTRIBUTOR` "is the default
  value for the '--bootloader-id' parameter to grub-install" **[doc]** same URL: so **two installs of
  the same distribution write the same directory** — see 3.5.
- No separate `/boot` partitions: each Linux keeps `/boot` inside its root partition (ext4). Systems
  that put kernels on the ESP (systemd-boot, unified kernel images) are the reason the shared ESP is
  1 GiB.
- Each GRUB is set to a short timeout (2 s) by the post-install script so that Charon → GRUB → kernel
  does not add a silent 5-second wait ("The default is '5'" **[doc]** same URL). Charon's reader reads
  GRUB's menu when the user stops it (regression list, `charon-r408-results.txt:25-27`).

### 3.5 Clashes between look-alike systems

| Clash | When | Rule in this version |
|---|---|---|
| Same loader directory (`\EFI\debian`, `\EFI\ubuntu`) and same entry name | two **installer-based** copies of one distribution | **Refused in the first version** with the offer: install the second copy as a ready image (it brings its own ESP, so no clash). Later: a private ESP per copy with the shared ESP hidden during that step (same trick as Windows model B). Renaming the bootloader id is **not** proposed: the distributions' signed GRUB images are built to look for their configuration in their own directory **[UNVERIFIED per distribution]**, so a renamed copy may stop starting under Secure Boot |
| Identical file-system UUIDs | two ready images from the **same image file** | **Refused** (pre-flight). GRUB and `fstab` of both would find the first one. Changing an ext4 UUID from firmware is not something Charon should write. Two different images (Debian + Ubuntu) do not clash |
| Identical partition GUIDs | images | none: `Remap` creates new entries with fresh GUIDs **[code: `images::RandomGuid`; verify for image partitions in the first Linux+Linux trial]** |
| Same host name | any two systems | host-name pattern (2.7); refuse duplicates |
| Same instance id for cloud-init | two images | already random per install (`CharonWsInstall3.h:396`) |
| CIDATA seed partitions | two images | each seed sits in its own image range; cloud-init picks by label — **two volumes labelled CIDATA on one disk at the same time**: the second image's first start could read the first seed **[UNVERIFIED]**. Rule: after an image's first start is `done`, Charon removes (or relabels) its seed partition before the next image's first start |
| Swap | any two Linux | no shared swap (2.4 rule 5) |

---

## 4. CharonDOS in a plan

### 4.1 Rules

- CharonDOS is written first, its partition is the shared ESP, and **it is the default boot entry in
  every combination that contains it**: final BootOrder = `CharonDOS`, `Charon`, then the others
  (today's order, `CharonWsDosSystem.h:551-555`). During the plan Charon is first; the order is
  changed only by the finish step, written by Charon itself from firmware (no dependence on
  `Install-Charon.ps1 -FirstEntry`, which has never run).
- Every later system's repair hook is told "keep CharonDOS first" (Windows: `-FirstEntry CharonDOS`,
  exists; Linux: a new argument of `install-charon.sh`), so an update of GRUB or Windows months later
  does not silently take the first place — the `check` service already runs at every Linux start.
- The reader comes from `Driver####` ("Charon voice"); on firmware that does not load driver options
  CharonDOS starts mute (seen in VMware, `charon-r406-results.txt:199-206`) — for those machines the
  finish step must instead put `Charon` first with a "go straight on to CharonDOS" target (the
  existing `reader-only` + continue mechanism). The firmware class is known at plan time (variable
  `CharonVoiceLoaded`). CharonDOS is then still what starts by default; only the entry order differs.
- Windows 9x/XP profiles inside CharonDOS (`win`) are part of the CharonDOS step and unaffected.

### 4.2 `winnt` with zero, one or several Windows — and its new twin `linux`

Today (`src/dos/backend_uefi.cpp:262-309`, `src/dos/shell.cpp:752-755`, `dos_platform.h:97-109`):
`WINNT` takes no argument, scans every file-system handle, takes the **first** volume with
`\EFI\Microsoft\Boot\bootmgfw.efi`, prints "starting Windows", ends the shell, stops Charon's menu
voice (a failed stop forbids the hand-over), then `LoadImage`/`StartImage`. If the image returns,
"the program ended with an error or could not start" is printed and CharonDOS comes back.

**User decision (2026-11-23): `linux` starts the installed Linux, the counterpart of `winnt`.**
Both commands get the same grammar and the same discovery code (one function, two kinds;
`PL_LINUX = 3` beside `PL_WINNT`, `PL_WIN`).

**Grammar** (same for `WINNT`):

```
LINUX                start the default Linux; ask when there are several and no default
LINUX name           the system whose id or name starts with "name" (LINUX DEBIAN, LINUX UBU)
LINUX n              number n of the list (LINUX 2)
LINUX /L             list the installed Linux systems with their numbers; start nothing
LINUX /D name|n      remember that one as the default (LINUX /D alone forgets it)
```

**Discovery**, in this order; results merged, duplicates (same partition GUID + same path, case
ignored) removed:

1. **Charon's systems registry** `\EFI\HAK\systems.txt` on any volume (written by the plan's finish
   step and refreshed by Charon's menu; section 5 "registry"): id, display name, kind, partition
   GUID, loader path. This is where the good names come from ("Debian 13", "Windows 11 Pro").
2. **Firmware entries**: `BootOrder` → each `Boot####`; kept when the file path is on a hard-disk
   partition and is not Charon (`\EFI\HAK\`), not CharonDOS, not a firmware application. Kind =
   Windows when the path ends in `\EFI\Microsoft\Boot\bootmgfw.efi`, else Linux. The entry's
   description ("debian", "ubuntu", "Fedora") is the name when the registry has none. The short-form
   device path (`HD(...)/File(...)`, as efibootmgr writes it) is resolved to a volume by partition
   GUID — CharonDOS already scans all volumes; Charon's own resolver is `BootMenu.h` (r406).
3. **Scan of every FAT volume** (what `find_windows` does today, extended): for Linux, each directory
   `\EFI\<dir>\` except `Microsoft`, `HAK`, `CharonDOS`, `BOOT`, taking the first that exists of
   `shimx64.efi`, `grubx64.efi`, `systemd-bootx64.efi` (in `\EFI\systemd\`), and unified kernels
   `\EFI\Linux\*.efi`. `shimx64.efi` is preferred over `grubx64.efi` in the same directory: it is the
   loader the distribution registers, and the only one that starts under Secure Boot. Name = a small
   table (`debian` → Debian, `ubuntu` → Ubuntu, `fedora` → Fedora, `arch`/`GRUB` → Arch Linux,
   `opensuse` → openSUSE, `systemd` → "Linux, systemd-boot"), else the directory name.
   `\EFI\BOOT\BOOTX64.EFI` is used only when nothing else was found on that volume **and** it is not
   Charon or Windows (same size as `\EFI\HAK\HAKBOOT.EFI` or `bootmgfw.efi` on that volume → skipped).

Numbers are stable: registry order first (the plan's list order), then firmware order, then scan order.

**Choosing**

- none found → message, errorlevel 2, the shell stays:
  EN "No Linux was found on this computer (no boot entry and no loader under \EFI on any volume)."
  PL "Nie znaleziono Linuksa na tym komputerze (brak wpisu rozruchowego i programu rozruchowego w \EFI na żadnym woluminie)."
  (`WINNT` keeps its existing message, `shell.cpp:80-81`.)
- exactly one → start it.
- several and an argument → that one; no match → "No installed Linux is called X." / "Nie ma
  zainstalowanego Linuksa o nazwie X.", then the list, errorlevel 2.
- several, no argument, a default set (`LINUX /D`, or the plan's finish step wrote one) → the default.
- several, no argument, no default → a spoken numbered choice, on the screen too (the reader reads
  the console): "Which Linux? 1: Debian 13. 2: Ubuntu 26.04. Type a number and Enter; Escape
  cancels." / "Który Linux? 1: Debian 13. 2: Ubuntu 26.04. Wpisz numer i Enter; Escape anuluje."
  Escape → errorlevel 3, nothing started. In a batch file (no console input wanted) the choice is
  not shown: errorlevel 3 and the list, so `AUTOEXEC.BAT` never hangs.
- The default is kept in `\EFI\CharonDOS\BOOT.CFG` (`linux=<id>`, `winnt=<id>`), ids from the
  registry or `dir:<partition GUID>:<path>` for scanned entries.

**Before handing over**: one line, spoken and printed, then the shell ends as today:
EN "Starting Debian 13 from disk 1. CharonDOS ends." PL "Uruchamiam Debian 13 z dysku 1. CharonDOS
kończy pracę." (`WINNT`: "Starting Windows 11 Pro …"; with model A of 3.3 and two Windows: "Starting
the Windows boot menu; choose there." / "Uruchamiam menu rozruchu Windows; wybierz w nim system.")

**The hand-over itself** must be the one Charon's own menu uses for that kind of loader, not a copy
that drifts: drives closed, timer cancelled, sound sink closed (as today, `backend_uefi.cpp:930-935`),
then
- Windows: stop the menu voice first (today's rule; failure forbids the start);
- **Linux: do not silence the reader** before GRUB — Charon's reader reads the GRUB menu and the
  "Loading Linux …" lines (`charon-r406-results.txt:150-152`) and leaves at `ExitBootServices`. What
  exactly `WINNT`'s `MenuVoice.Stop` does to the *resident* reader has to be read in
  `BootMenuVoice.h` before this is coded **[UNVERIFIED]**; with model A the Windows text menu needs
  the reader too, so the same question applies to `WINNT`.
Preferred implementation, to keep one shared hand-over: CharonDOS starts `\EFI\HAK\HAKBOOT.EFI` with
a load option naming the target (`start=<partition GUID>:<path>`), and Charon performs its normal
"start this entry" path. That needs a small load-option parser in Charon; if that is not wanted,
`run_launch()` gets a second branch.

If the started loader returns (GRUB "exit", a refused image): "CharonDOS: Debian 13 did not start
(reason)." and CharonDOS comes back, as `WINNT` does today (`backend_uefi.cpp:939-943`).

**Secure Boot**: `LoadImage` of an unsigned or unenrolled image fails with a security violation;
the message must say so ("The firmware refused this loader: Secure Boot." / "Firmware odrzucił ten
program rozruchowy: Secure Boot.") rather than "could not start". Nothing is bypassed.

Help text: two lines beside `WINNT` in both languages (`shell.cpp:56, 73`). Host test: the same
mechanism as today's `CHARONDOS_TEST_WINNT` (`backend_host.cpp:227-235`) with a fake list, covering
none / one / several / name / number / default / batch.

---

## 5. Finish step, menu and registry

- **BootOrder** written by Charon from firmware: CharonDOS first when present; otherwise `Charon`
  first (it is the menu of a multi-boot machine). The other systems keep their entries after it.
- **Default system and timeout**: Charon's existing "Continue" target (`\EFI\HAK\continue.bin`,
  `BootMenu.h:352-359, 495-505`) = the system chosen in `plan.default`; the existing automatic-start
  delay = `plan.timeout`. With "menu, no automatic start" Charon waits. With CharonDOS present these
  apply to what Charon does when it is started from CharonDOS's `CHARON` command or from the firmware
  menu; the machine's default is CharonDOS.
- **Systems registry** `\EFI\HAK\systems.txt`: one line per installed system — id, display name
  ("Windows 11 Pro", "Debian 13 — GNOME, Orca"), kind, partition GUID of its loader's volume, loader
  path, the plan id, date. Charon's device list uses it to label firmware entries (today's labels
  come from `Boot####` descriptions: "debian", "Windows Boot Manager", `BootMenu.h:946`), so the menu
  says "Debian 13, 2 of 4" instead of "debian". The registry is a *label source*: an entry whose
  loader is gone is not shown as installed; entries without a registry line keep today's label.
  Charon does not rename other systems' `Boot####` variables (their own tools rewrite them).
- Secrets: every `step<N>\` folder is zeroed and deleted; `CharonPlan` is deleted; `plan.txt` is kept
  as `plan-done.txt` without answers (for the report and support).
- Charon's presence, checked for **every** system and reported: `\EFI\HAK\` files on the plan's ESP,
  `Driver####` "Charon voice" present once, `Boot####` "Charon" present once. This is the standing
  rule "every install path leaves reader, manager files and a Charon entry"; in a plan it is checked
  once at the end by Charon itself instead of being trusted from N hooks.

---

## 6. Questions

Same row format as the companion spec. "N" = asked in the normal (express) flow; "D" = detailed mode
only. Ids are new; none collides with today's keys.

### 6.1 Entry and list

| id | Mode | English | Polish | Type | Choices | Default | Depends on |
|---|---|---|---|---|---|---|---|
| `plan.kind` | N | "One system, or several systems side by side?" | „Jeden system czy kilka systemów obok siebie?” | choice | `one` "one system" / „jeden system”; `several` "several systems side by side, installed one after another without you" / „kilka systemów obok siebie, instalowanych po kolei bez twojego udziału” | `one` | first question of "Install a system from the network"; `one` continues exactly as today |
| `plan.preset` | N | "Which set?" | „Który zestaw?” | choice | `win11-debian` "Windows 11 and Debian" / „Windows 11 i Debian”; `dos-win11` "CharonDOS and Windows 11" / „CharonDOS i Windows 11”; `dos-debian` "CharonDOS and Debian" / „CharonDOS i Debian”; `dos-win11-debian` "CharonDOS, Windows 11 and Debian" / „CharonDOS, Windows 11 i Debian”; `debian-ubuntu` "Debian and Ubuntu" / „Debian i Ubuntu”; `win10-win11` "Windows 10 and Windows 11" / „Windows 10 i Windows 11”; `own` "my own list" / „własna lista” | `win11-debian` | `plan.kind=several`. Each choice is followed by its speech status, as system entries are today. Presets whose combination has not passed a trial are not shown (section 8) |
| `plan.list` | N for `own`, D always | "Your list: 1 Windows 11, 2 Debian. What now?" | „Twoja lista: 1 Windows 11, 2 Debian. Co teraz?” | choice (loop) | `add` "add a system" / „dodaj system”; `remove` "remove a system" / „usuń system”; `move` "move a system up or down" / „przesuń system wyżej albo niżej”; `done` "the list is ready" / „lista jest gotowa” | `add` until two systems, then `done` | `add` opens today's group → system choice (only entries allowed in a plan); refusals of 2.5 and 3.5 are spoken at once. `move` uses the grab/move/drop keys of the boot-order page |
| `plan.mode` | N | "How many questions?" | „Ile pytań?” | choice | `quick` "express: shared answers once, defaults for the rest" / „ekspresowo: wspólne odpowiedzi raz, reszta domyślnie”; `detailed` "detailed: also every system's own questions" / „szczegółowo: także pytania każdego systemu” | `quick` | — |

### 6.2 Shared answers

| id | Mode | English | Polish | Type | Default | Note |
|---|---|---|---|---|---|---|
| `shared.user`, `shared.password`, `shared.locale` | N | today's texts of `user`, `password`, `locale` (`CharonWsInstall.h:56-64`), introduced once by "These answers are used for every system." / „Te odpowiedzi posłużą wszystkim systemom.” | | as today | as today | copied to every system |
| `shared.fullname`, `shared.keyboard`, `shared.timezone` | D | today's texts | | as today | derived from language | |
| `shared.hostpattern` | D | "Computer name. Each system gets it with its own ending, for example charon-pc-debian:" | „Nazwa komputera. Każdy system dostanie ją z własną końcówką, na przykład charon-pc-debian:” | name | `charon-pc` | |
| `shared.same` | D | "Use the same user name, password and language in every system?" | „Użyć tej samej nazwy użytkownika, hasła i języka w każdym systemie?” | yes/no | yes | "no": the three are asked per system |
| `shared.readers` | N | "Screen readers, where a system has them:" | „Czytniki ekranu, tam gdzie system je ma:” | multi | today's Linux and Windows lists merged | orca,speakup + nvda,narrator | per-system follow-ups (`console`, `gui`, `wfirst`) only in detailed mode; otherwise today's rules pick |

### 6.3 Per system

| id | Mode | English | Polish | Type | Default | Note |
|---|---|---|---|---|---|---|
| (system's own questions) | D | the system's detailed table from the companion spec, prefixed "System 2 of 3, Debian." / „System 2 z 3, Debian.” | | | its defaults | in `quick` only rows marked quick that are not covered by `shared.*` (for example Debian `desktop`, Windows `profile`) |
| `sys.N.size` | D | "Size for Debian in gigabytes. Smallest 12, proposed 40, largest possible 180:" | „Rozmiar dla Debiana w gigabajtach. Najmniej 12, proponuję 40, najwięcej 180:” | number | computed (2.4) | re-computes the rest; refuses below the minimum |
| `sys.N.swap` | D, Linux | "Swap file size in gigabytes, 0 for none:" | „Rozmiar pliku wymiany w gigabajtach, 0 — bez pliku:” | number | 2 (0 for images) | |
| `sys.N.silent` | N | today's `ConfirmSilent` text | | yes/no | no | once per system without speech; "no" removes it from the list |
| `sys.N.winre` | D, Windows | "A separate recovery partition for this Windows, 1 gigabyte?" | „Osobna partycja odzyskiwania dla tego Windows, 1 gigabajt?” | yes/no | no | decision D6 |

### 6.4 Disk and layout

| id | Mode | English | Polish | Type | Choices | Default | Depends on |
|---|---|---|---|---|---|---|---|
| `plan.disk` | N | "On which disk?" | „Na którym dysku?” | choice | spoken disk labels | first that fits | several disks |
| `plan.spread` | D | "Several disks: all systems on one disk, or one system per disk?" | „Kilka dysków: wszystkie systemy na jednym dysku czy każdy system na osobnym?” | choice | `one` / `each` | `one` | 2+ usable disks and no Windows (2.10) |
| `plan.existing` | N | "This disk holds Windows 11 (120 GB) and 10 GB of free space. What should happen to what is there?" | „Na tym dysku jest Windows 11 (120 GB) i 10 GB wolnego miejsca. Co zrobić z tym, co na nim jest?” | choice | `erase` "erase the whole disk" / „wymaż cały dysk”; `keep` "keep it and add the new systems beside it" / „zostaw i dodaj nowe systemy obok”; `cancel` | `keep` when it fits, never `erase` by default | disk not blank |
| `plan.shrink` | N | "The new systems need 85 GB; 10 GB are free. Shrink the Windows partition from 120 to 45 GB? Charon restarts into a small helper to do it. Back up important files first." | „Nowe systemy potrzebują 85 GB, wolne jest 10 GB. Zmniejszyć partycję Windows ze 120 do 45 GB? Charon uruchomi do tego mały program pomocniczy. Najpierw zrób kopię ważnych plików.” | yes/no | | no | `keep` and not enough room and the neighbour is an unencrypted NTFS Windows. Refusals (BitLocker, hibernation, dirty, would leave under 10 GB free) are spoken with the reason and the alternative `Charon-MakeRoom.ps1` |
| `plan.shrink.confirm` | N | "Type SHRINK to shrink Windows on disk 1 from 120 to 45 gigabytes:" | „Wpisz ZMNIEJSZ, aby zmniejszyć Windows na dysku 1 ze 120 do 45 gigabajtów:” | typed word | | — | separate from the plan's confirmation: it changes somebody else's system |
| `plan.data` | N | "Add a shared data partition that every system can read and write?" | „Dodać wspólną partycję na dane, którą odczyta i zapisze każdy system?” | yes/no | | no | — |
| `plan.data.fs`, `plan.data.size` | D | "File system of the shared partition:" / "Its size in gigabytes, 0 for all the rest:" | „System plików wspólnej partycji:” / „Jej rozmiar w gigabajtach, 0 — cała reszta:” | choice / number | `exfat` / `ntfs` (only with a Windows in the plan) | `exfat`; 0 | `plan.data=yes` |
| `plan.rest` | D | "Who gets the space that is left over?" | „Kto dostanie miejsce, które zostanie?” | choice | each system; shared partition; `free` "leave it free" / „zostaw wolne” | shared partition if any, else last system | — |

### 6.5 Start-up

| id | Mode | English | Polish | Type | Choices | Default | Depends on |
|---|---|---|---|---|---|---|---|
| `plan.default` | N | "Which system should start when you do nothing?" | „Który system ma się uruchamiać, gdy nic nie zrobisz?” | choice | each system; `menu` "none: Charon's menu waits for me" / „żaden: menu Charona czeka na mnie” | first system of the list | **not asked when CharonDOS is in the plan**; instead said: "CharonDOS starts first. In it, WINNT starts Windows and LINUX starts Linux." / „Jako pierwszy uruchamia się CharonDOS. W nim polecenie WINNT uruchamia Windows, a LINUX — Linuksa.” |
| `plan.timeout` | D | "After how many seconds? 0 means at once." | „Po ilu sekundach? 0 — od razu.” | number | 0–60 | 5 | `plan.default` ≠ `menu` |
| `plan.dosdefault.winnt`, `.linux` | D | "Which Windows should WINNT start when you give no name?" / "Which Linux should LINUX start…?" | „Który Windows ma uruchamiać WINNT bez nazwy?” / „Którego Linuksa ma uruchamiać LINUX bez nazwy?” | choice | the systems of that kind; `ask` "ask me each time" / „pytaj za każdym razem” | first of the list | CharonDOS and two or more of that kind |

### 6.6 Summary and confirmation

Spoken and shown as one screen that the review keys can re-read (the dialogue already stands on the
screen, `CharonWsInstall2.h:678-685`):

> "Plan. Disk 1, 512 gigabytes, NVMe, empty. Three systems, in this order: 1, CharonDOS, 2 gigabytes,
> it is also the EFI partition. 2, Windows 11, 100 gigabytes, speaks in Setup. 3, Debian with GNOME
> and Orca, 40 gigabytes, speaks in the installer. Shared data partition, exFAT, 369 gigabytes. User
> anna in every system. The computer's clock is kept in universal time. CharonDOS starts first; WINNT
> and LINUX start the others. Everything on this disk will be erased. The installation takes about
> two hours and restarts the computer several times by itself; you do not need to do anything.
> Leave Charon's stick plugged in until I say the plan is finished. Secure Boot is off."

> „Plan. Dysk 1, 512 gigabajtów, NVMe, pusty. Trzy systemy, w tej kolejności: 1, CharonDOS,
> 2 gigabajty, to także partycja EFI. 2, Windows 11, 100 gigabajtów, mówi w instalatorze. 3, Debian
> z GNOME i Orcą, 40 gigabajtów, mówi w instalatorze. Wspólna partycja na dane, exFAT,
> 369 gigabajtów. Użytkownik anna w każdym systemie. Zegar komputera będzie w czasie uniwersalnym.
> Jako pierwszy uruchamia się CharonDOS; WINNT i LINUX uruchamiają pozostałe. Wszystko na tym dysku
> zostanie wymazane. Instalacja potrwa około dwóch godzin i kilka razy sama uruchomi komputer
> ponownie; nic nie musisz robić. Zostaw pendrive Charona w gnieździe, dopóki nie powiem, że plan
> jest zakończony. Secure Boot jest wyłączony.”

| id | English | Polish | Type |
|---|---|---|---|
| `plan.change` (D) | "Start, or change something?" — `start` / `list` / `sizes` / `shared answers` / `cancel` | „Zacząć czy coś zmienić?” — „zacznij” / „lista” / „rozmiary” / „wspólne odpowiedzi” / „anuluj” | choice |
| `plan.confirm` | "To start the whole plan, type the word YES and press Enter. Escape cancels. Nothing has been changed yet." | „Aby zacząć cały plan, wpisz słowo TAK i naciśnij Enter. Escape anuluje. Jeszcze nic nie zostało zmienione.” | typed word (the one confirmation; same word as today, `CharonWsInstall.h:151-155`) |

During the run: `plan.pause` (Space/Escape during the announcement: "Plan paused. Continue / skip
this system / stop here." / „Plan wstrzymany. Kontynuuj / pomiń ten system / zakończ tutaj.”) and
`plan.failed` (2.9: "try again / skip this system and continue / stop here" — „spróbuj ponownie /
pomiń ten system i kontynuuj / zakończ tutaj”), `plan.leftover` ("Step 3 did not run. Its 40
gigabytes are reserved and empty. Free them, or keep them reserved?" — „Krok 3 się nie wykonał. Jego
40 gigabajtów jest zarezerwowane i puste. Zwolnić je czy zostawić zarezerwowane?”).

---

## 7. Unattended / administrator mode: the plan in an answer-set file

The companion spec owns the file's base format, where it is looked for, per-machine overrides and the
catalogue of keys; none of that was written when this section was. Assumed here: a text file of
`key=value` lines in sections (the shape `CHARONPC.CFG` and `plan.txt` already have), found on
Charon's medium. If the companion spec chooses another syntax, the sections below map one to one.

```ini
[plan]
version=1
name=classroom-b
systems=dos,win11,debian          ; order of the list; Charon still applies the class order of 3.1
default=dos                       ; system id, or "menu"
timeout=5
on_failure=stop                   ; stop | skip | retry-once-then-stop
leftover=keep                     ; keep | free   (reserved ranges of steps that did not run)
data=exfat                        ; none | exfat | ntfs
data_size_gb=0                    ; 0 = the rest
rest=data                         ; data | <system id> | free
report=esp,systems                ; where the report is written
secure_boot=warn                  ; warn | refuse-if-on   (there is no "bypass" value)

[disk.1]                          ; pre-authorisation: the plan runs ONLY on a disk that matches
serial=S6XYNX0R123456             ; all given keys; no match = nothing is touched, spoken refusal
model=Samsung SSD 980*            ; optional, * allowed at the end
min_size_gb=400                   ; optional guard
max_size_gb=600
existing=erase                    ; erase | keep | refuse-if-not-empty
shrink_windows=no                 ; yes needs existing=keep; BitLocker, hibernation and a dirty
                                  ; volume are refused even with yes
esp_size_mb=1024

[shared]                          ; the companion spec's shared keys: user, password / password_hash,
user=anna                         ; locale, keyboard, timezone, hostpattern, readers, wifi …
locale=pl_PL
timezone=Europe/Warsaw
hostpattern=sala-b-{serial4}

[system.dos]
kind=charondos
programs=all
profile=none

[system.win11]
kind=windows
disk=1
size_gb=100
winre=no
; … every Windows key of the companion spec (version, edition, profile, wreaders, wfirst …)

[system.debian]
kind=debian                       ; debian | ubuntu | image-debian | image-ubuntu | image-fedora …
disk=1
size_gb=40
swap_gb=2
; … every Debian key of the companion spec; any shared key repeated here overrides [shared]
```

Rules:

- **No disk section, no run.** In unattended mode the typed confirmation is replaced by the disk
  match; a file without `serial` (or with a serial that matches no disk, or two disks) is refused.
  The serial is the one Charon speaks in its disk label and `charon info` prints; an administrator
  can collect it with Charon first.
- A file may hold several `[disk.N]` alternatives per machine through the companion spec's
  per-machine override mechanism (by DMI serial or MAC); this spec adds nothing to that.
- Charon validates the whole file before anything changes (unknown `kind`, sizes that do not fit,
  refused combinations of 2.5/3.5) and speaks every error, not the first only.
- `on_failure=skip` still never skips disk preparation; a failed step 0 always stops.
- Windows passwords: `password` in clear text is needed for Windows (2.3); the file is the
  administrator's responsibility; Charon's copy under `\EFI\HAK\plan\` is erased at finish.
- Still spoken although unattended: the summary (once, no wait), each step's start, the final report.
  A key press pauses, as in 6.6.
- There is no key that switches off BitLocker prevention silently, shrinks a BitLocker volume, or
  changes Secure Boot.

---

## 8. Feasibility, risks, tests, batches

### 8.1 Per combination

| Combination | Rating | Why | What a VM can prove |
|---|---|---|---|
| Plan engine alone (state, resume, layout, report) | MEDIUM | New, but pure Charon code; layout and state machine are host-testable | everything except odd firmware |
| CharonDOS + ready Debian image | **EASY** | Both written by Charon; both passed alone; only `linux` and ranges are new | all |
| CharonDOS + Windows | MEDIUM | Path exists in code; unproven: Windows into free space / an existing partition, `WINNT` starting a real Windows, order repair | all in VMware (Windows guests hang under WHPX in QEMU, `charon-r402-results.txt:29-30`) |
| Windows + Debian (installer) | MEDIUM | Each passed alone on a whole disk; unproven: `InstallTo` an existing partition and its numbering, `biggest_free`, ESP reuse, clock | all |
| CharonDOS + Windows + Debian | MEDIUM | Sum of the two above; more restarts to survive | all |
| Windows + Debian/Ubuntu **image** | MEDIUM | image-in-gap passed beside a synthetic disk only; second ESP beside Windows' | all |
| Linux + Linux, different distributions (installer + image, or two images) | MEDIUM | CIDATA label clash, Ubuntu image never in a gap | all |
| Windows + Ubuntu (installer) | **HARD** | Ubuntu autoinstall has never run at all; storage section must be rewritten for `preserve`; 8 GB RAM; no speech in its installer | all |
| Windows + Windows | **HARD** | Shared BCD; the choice menu must be made text-mode and readable; model B unproven; two downloads/media | all, including the reader on the legacy menu |
| Keep an existing Windows, add systems (shrink) | **HARD** | Changes somebody else's system; helper not built into Charon; Windows' start after the shrink not recorded; BitLocker cases cannot be faked fully | shrink + Windows start: yes. Real OEM installs with device encryption: no |
| Two installer copies of the same distribution | **HARD** | loader-directory clash; needs hidden-ESP trick | yes |
| Fedora kickstart, Arch archinstall in a plan | HARD (new work) | no generator exists; keys are documented | yes |
| Several disks with Windows | HARD | Setup's disk numbering cannot be known from firmware | partly (VM disk order is not hardware's) |
| Anything on real hardware | UNKNOWN | nothing in this project has run on a physical computer (`charon-r408-results.txt:4, 117`); firmware that rewrites BootOrder, ignores BootNext or driver options | no |

### 8.2 Hardest risks

1. **Coming back to Charon** after each installer on real firmware (BootNext honoured? BootOrder
   left alone? `Driver####` loaded?). VMs prove the logic, not the firmware zoo.
2. **Windows Setup onto an existing partition**: numbering with an MSR, behaviour toward placeholder
   and Linux partitions, WinRE, a second ESP on the disk. Never run in this project.
3. **Secrets on the ESP** across restarts (decision D4).
4. **Undetected installer errors** (a dialogue that waits): bounded only by time budgets.
5. **Speech continuity**: the user rule "no crashes, both voices work" applies to up to a dozen
   hand-overs in one plan; the audio hand-over is the crash-prone area (r406 trial 14).
6. **Shrinking** a real Windows unattended.
7. Scope: the refactor that separates asking from running touches all three dialogue files that other
   work is editing now.

### 8.3 Test matrix (VMware Workstation through `vmrun` unless noted; audible; one VM per case, deleted after)

Common measurements for every case: SHA-256 of every partition outside the step's range before and
after each step; `BootOrder`/`Boot####`/`Driver####` dumped after every restart; serial trace of
Charon's speech; `log.txt` and the report; each system started twice from Charon's menu; `\EFI\HAK`
present, one "Charon" entry, one "Charon voice" entry.

| # | Case | Proves | Run |
|---|---|---|---|
| T0 | Host tests: layout calculator, plan file round trip and torn write, state machine, seeddisk remove/retype, `linux`/`winnt` selection (CharonDOS host backend) | logic without a VM | first, with batch 1 |
| T1 | **Windows 11 + Debian (installer)**, empty 200 GB disk | existing-partition `InstallTo` + numbering, `biggest_free`, shared ESP, pass-through over Setup's restarts, order repair, **clock** (same time in both after cross restarts), fast start-up off, `PreventDeviceEncryption` value present | **first end-to-end** |
| T2 | **CharonDOS + Windows 11** | CharonDOS partition as ESP for Windows, CharonDOS stays first, `WINNT` starts a real Windows (first time ever) | second |
| T3 | **CharonDOS + Debian image** (QEMU possible) | `LINUX`, reader through GRUB, default order; cheapest full plan | with T2 |
| T4 | CharonDOS + Windows 11 + Debian | three systems, both commands, six or more restarts | third |
| T5 | Debian (installer) + Ubuntu (image) | two Linux, two GRUBs, Charon as menu, CIDATA rule, registry names | fourth |
| T6 | Failure injection on T1: network cut during step 2; power-off in the middle of Windows Setup; wrong download address | stop / retry / skip / stop-here, leftover question, report, earlier system still starts | with T1's VM snapshot |
| T7 | Windows 10 + Windows 11, model A; then model B as an experiment | BCD entries, legacy menu read by the reader, `winnt` | after T4 |
| T8 | Existing Windows 11 kept (installed by hand first), shrink, + Debian | helper, refusals (hibernated; dirty; a BitLocker-formatted test volume), Windows starts and runs chkdsk-free afterwards | after T5 |
| T9 | Unattended file on T1's plan, serial match and a deliberate mismatch | no-touch refusal, zero key presses | after T6 |
| T10 | Windows 11 + Ubuntu (installer) | Ubuntu autoinstall at all, curtin `preserve` | when Ubuntu alone has passed |
| T11 | Two disks, Debian + Ubuntu image, one per disk | per-disk ESP, removal of one disk | late |
| — | QEMU note | OVMF started with `bootindex` deletes entries outside its list (`charon-r401-results.txt:38`): plan tests in QEMU run without `bootindex`; VMware firmware does not load driver options (reader arrives through Charon) | |

### 8.4 Implementation order (batches)

1. **Plan core.** Data model and per-system answer namespaces (the 48×160 store grows or becomes one
   store per system); "ask" separated from "run" in `CharonWsInstall*.h` and `CharonWsDosSystem.h`;
   layout calculator; all-gaps disk scan; seeddisk remove/retype; plan file + `CharonPlan`; step
   state machine with pass-through and budgets; BootNext; finish step, registry, report; questions
   6.1–6.6 with the presets hidden. Host tests T0.
2. **CharonDOS and images under the plan + `linux` / `winnt` grammar** (CharonDOS source,
   host-tested) → T3. Cheapest proof of the whole loop.
3. **Windows into an existing partition + Debian into the plan's gap**, done-markers and repair hooks
   in `Install-Charon.ps1` / `install-charon.sh`, clock, fast start-up, time zone → T1, T2, T4, T6.
   Presets `win11-debian`, `dos-win11`, `dos-debian`, `dos-win11-debian` become visible as each passes.
4. **Linux + Linux**: images into assigned ranges, CIDATA rule, GRUB timeout, registry labels in the
   menu → T5.
5. **Unattended plan file** (after the companion spec fixes the base format) → T9.
6. **Two Windows** (model per D2) → T7. **Existing system kept + shrink helper** → T8.
7. **Ubuntu curtin storage**, then Fedora kickstart / Arch pre-mounted generators, same-distribution
   copies, several disks → T10, T11.

One build and one test pass per batch; the boot-manager regression once at the end of batches 1 and 3
(they change `BootMenu.h`).

### 8.5 Decisions needed from the user

- **D1 — the top entry without CharonDOS**: Charon's menu first with a default system and timeout
  (proposed), or the default system's own loader first and Charon second (today's single-system
  rule, r408)?
- **D2 — two Windows**: model A (one shared boot store, Windows' own text menu after Charon's,
  proposed for the first version) or model B (each Windows its own EFI partition, both listed by
  Charon, `winnt 2` direct; needs a trial first)?
- **D3 — one EFI partition, strictly?** Proposed: one shared, except that a ready image keeps its own
  small one. Or: forbid images in plans until they can be merged onto the shared one?
- **D4 — the Windows password between restarts**: keep the answer file (clear password) on the EFI
  partition until its step runs, then erase (proposed; fully unattended) — or ask for the password
  again when the Windows step starts (safer, but the plan then waits for the user once)?
- **D5 — dirty Windows volume when shrinking**: refuse (the standing rule, proposed) or allow like
  the r405 trial script did (chkdsk at the next Windows start)?
- **D6 — Windows recovery partition** in a plan: none by default with WinRE inside the Windows
  partition (proposed), or always one per Windows?
- **D7 — shared data partition default**: exFAT (proposed) or NTFS when a Windows is present?
- **D8 — same distribution twice** (two Debians): refuse in the first version and offer "installer +
  image" (proposed), or build the hidden-ESP trick now?
- **D9 — `linux`/`winnt` hand-over**: through `HAKBOOT.EFI` with a load option (one shared
  implementation, proposed) or a second branch inside CharonDOS?

### 8.6 Not verified (collected)

- UEFI `BootNext` wording (specification page refused the fetch) and its behaviour on VMware/OVMF and
  on real firmware; whether firmware resolves a `Boot####` whose partition type was changed.
- How Windows Setup numbers existing partitions when an MSR is present; that it leaves WinRE usable
  without a recovery partition; that it ignores non-Windows partitions with `WillWipeDisk=false`;
  what it does with two ESPs on one disk; "older Windows first" as official guidance; the fallback
  file `\EFI\Boot\bootx64.efi` being overwritten.
- `RealTimeIsUniversal` (no Microsoft page read; Arch wiki refused the fetch) and its measured
  effect; `powercfg /h off` page; `TimeZone` name table; Wi-Fi profile hand-over to Windows.
- Debian: partman reusing the existing ESP in `biggest_free` mode, `no_swap`, `grub2/update_nvram`.
- Ubuntu: `mode: use_gap` (in today's code, not found in the reference text read), curtin `grub:`
  keys, `error-commands`; the whole autoinstall path has never run.
- Fedora documentation site refused the fetch; pykickstart's own reference was read instead.
  `timezone --utc` text not reached.
- Signed GRUB's fixed configuration directory per distribution; two CIDATA volumes on one disk;
  fresh partition GUIDs for remapped image partitions.
- What `MenuVoice.Stop` does to the resident reader (matters for `linux` and for the Windows text menu).
- Whether Charon and CharonDOS can show local time when the firmware clock holds UTC.
- `Charon-MakeRoom.ps1` and `Install-Charon.ps1 -FirstEntry` have never run on a real Windows; the
  shrink trial's Windows was not started afterwards in any record.
- Everything on physical hardware.

## User decisions (2026-11-23, answered by the user in a question dialogue)

- D1: without CharonDOS, Charon's own menu is the FIRST boot entry (default system + timeout).
- D2: two Windows on one disk share one boot store; Windows' text menu follows Charon's (v1).
- D4: the Windows answer file with the password waits on the EFI partition until its step and is erased right after it.
- D7: NO shared data partition by default; the question exists only in detailed mode.
- CharonDOS command `linux` boots the installed Linux (counterpart of `winnt`).
- D3, D5, D6, D8, D9: not asked; the proposals in this spec stand until the user says otherwise.
## Requirement added by the user (2026-11-23): everything works from the RAM disk

When Charon runs from memory (RAM volume; started from a stick that was then removed, or from its own
installation on the internal disk), ALL of this must behave exactly as when started from a stick:
answer-set files (load, save, unattended mode), multi-system plans (build, persist, resume after every
restart, finish), dual boot, Windows installation from the RAM disk like from a physical medium, and the
write-back of the Charon reader, boot manager files and "Charon" entry. Consequences for the design:
- Plan state and the answer set in use must not live ONLY on a partition that the plan repartitions or
  wipes. Before the disk is changed they are held in memory, and they are written back to the new EFI
  partition as the first step after the layout is created (plus the small UEFI variable pointer), so a
  restart at any point finds them.
- When the only disk is both Charon's home and the target, Charon itself is re-created on the new EFI
  partition before the first installer restart, otherwise there is nothing to return to.
- Saved answer sets on Charon's own EFI partition are carried over the wipe (memory copy -> new EFI
  partition); they stay excluded from the copy into installed systems.
- Nothing may steer the user to "I will install this system myself" because of the RAM run.
- Every one of these cases needs its own end-to-end VM run without a stick before it is called done.

## More user decisions (2026-11-23, after phase A)

- Hardware clock: UTC in EVERY plan, including "CharonDOS + Windows" alone; CharonDOS and Charon's clock
  tools get a time zone and show local time.
- Unattended mode: the typed confirmation word is ALWAYS required at the machine, even with an answer-set
  file; the [disk.N] serial must still match (mismatch = refusal). No `confirm=disk-match` shortcut.
