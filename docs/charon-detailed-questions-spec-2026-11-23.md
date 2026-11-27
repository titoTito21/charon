# Charon: detailed installation questions — specification (2026-11-23)

Status: design only. Nothing in the source tree was changed. Companion documents:
`work/charon-windows-profiles-research-2026-11-23.md` (Windows profiles, add-ons, unofficial images)
and `work/charon-multiboot-spec-2026-11-23.md` (several systems in one run; section 6.6 here says how
the two fit, and the answer-file format of section 6.1 is the one that document assumed). Source read: `work/charon-src-r395`
(paths below are relative to it; `src/efi/` is omitted for the `CharonWs*.h` files).

Audiences: (1) a blind power user who wants a system set up to personal taste; (2) an administrator
(IT staff, school, office) who deploys machines for blind users and wants precise, repeatable set-ups.

Hard rule from the user: **the normal flow stays exactly as short as it is today.** Every new question
belongs to the detailed mode only. Each new question has an explicit default; the express mode, the
"standard" flow and unattended files that do not mention the key use that default silently. In almost
all rows the default is "what Charon does today", so the generated answer file of an express install
does not change at all.

Legend for the last column of the question tables:
**E** easy (maps onto a mechanism that exists), **M** medium (new script section or new generator
branch), **H** hard or risky (reason given). **⚠A** = the answer can make the system inaccessible:
warning + extra confirmation (section 7). **🔒** = security-sensitive value (section 8).
"Level": **D** = detailed mode, personal groups; **A** = detailed mode, administration groups (asked
only after the gate "Show the administration groups?" is answered yes).

---

## 1. How questions work today

### 1.1 The mechanism (`CharonWsAsk.h`)

- A question is one row of a `const ask::Question` table:
  `{key, quick, kind, textPl, textEn, fallback, choices, count, helpPl, helpEn}`.
  Kinds: `text`, `secret` (typed twice, spoken as "star"), `choice`, `yesNo`, `name` (lower-case
  login/host name, validated), `multi` (check boxes, value = checked values joined with commas).
- `ask::Run(table, count, quick)` first stores every row's `fallback` for keys that have no value yet,
  then asks the rows — in express mode only those with `quick==true`. So a skipped question already
  has its default. Help text is spoken only in the detailed mode. Escape goes back one question and
  leaves at the first one; F1 repeats.
- Answers live in one flat store: `ask::Put/Get/Is/Has/Any`, **48 keys × 160 bytes**
  (`maxAnswers=48`, `valueUnits=160`). `ask::Clear()` wipes it at the start of the dialogue and after
  the installer is started (passwords do not stay in memory). Both limits must grow for this spec.
- Questions that depend on earlier answers are not table rows today; they are code after the tables:
  `ResolveReaders()` (`CharonWsReaders.h:53-84`) and `ResolveWindowsReaders()` (`CharonWsInstall2.h:210-226`).
- **A mode question already exists** and this spec builds on it:
  - installer-based systems (`Classic()`, `CharonWsInstall2.h:687`, choices `CharonWsInstall.h:32-35`):
    `quick` "express mode: as few questions as possible" / `detailed` "detailed mode: more questions" /
    `guided` "I will install this system myself";
  - ready images (`RunImage()`, `CharonWsInstall3.h:323-326`): `quick` / `detailed`;
  - CharonDOS (`dossys::Run`, `CharonWsDosSystem.h:424`): **no mode question**; it asks everything
    directly with `ask::Choose`/`Prompt`, not through a table.
- How an answer reaches the installed system:

| System | Carrier | Generator | Buffer |
|---|---|---|---|
| Debian (installer) | `preseed.cfg` in the initrd cpio | `DebianPreseed()` `CharonWsInstall.h:402-455`, `DebianLateCommand()` `:390-400` | 12 000 bytes (`:471`) |
| Ubuntu (installer) | `\user-data` on a FAT volume labelled CIDATA | `UbuntuUserData()` `CharonWsInstall2.h:43-83` | 8 000 bytes (`:135`) |
| Debian/Ubuntu/Arch/Fedora ready image | cloud-init seed partition | `ImageUserData()` `CharonWsInstall3.h:221-282` | 9 000 bytes (`:385`) |
| Linux post-install | positional arguments | `ReaderArguments()` `CharonWsReaders.h:96-103` → `src/linux/speech.sh CONSOLE GUI LIST LOGIN`, `src/linux/firstboot.sh setup CONSOLE GUI READERS LOGIN DESKTOPS DUAL` | 200 bytes |
| Windows | `\Autounattend.xml` on the install media | `WindowsAnswers()` `CharonWsInstall2.h:324-500` | 16 000 bytes (`:628`) |
| Windows post-install | command-line parameters in `FirstLogonCommands` | `tools/charon-addon/Charon-Readers.ps1` (`-Readers -First -Language -Source`), `Charon-WindowsProfile.ps1` (`-Profile -Language …`) | 400 bytes per command |
| CharonDOS Windows profile | `C:\PC\<ID>\CHARONPC.CFG`, `key=value` | `MakeProfile()` `CharonWsDosSystem.h:363-409`; read by `src/pc/pc_config.cpp:166-203` | 2 048 bytes |
| CharonDOS itself | load option `pl`; `C:\AUTOEXEC.BAT` is run at start (`src/dos/shell.cpp:926`) | nothing writes AUTOEXEC.BAT today | — |

- Linux scripts are files in `src/linux/` compiled in by `tools/gen-charon-linux-scripts.py`
  (`CharonLinuxScripts.h`); the Windows scripts are copied from `\EFI\HAK\addon\` to `\charon\` on the
  media (`CharonWsInstall2.h:640-651`).
- **Testing today.** There is no host test of the dialogue or of the four generators: a search of
  `tests/` and `tools/` for `CharonWsAsk`/`CharonWsInstall` finds only result files. They are proven
  by VM trials recorded in `tests/charon-r40*-results.txt`. Two paths are recorded as not proven:
  Ubuntu autoinstall ("napisane, NIESPRAWDZONE", `charon-r400-results.txt:40`; "Not run at all",
  `charon-r406-results.txt:225`) and the Windows Server administrator password (comment at
  `CharonWsInstall2.h:448-450`).
- **The "fields" mechanism** (`tools/charon-os/tests/charon_fields.py`, `gen-windows-fields.py`,
  `windows/CharonOs.Fields.ps1`, `linux/charon-settings`) is a different thing: one Python table that
  describes Charon's own settings record (`CharSettings`) and generates the Windows and Linux
  settings programs plus their host tests. It does not carry install answers. Its pattern is the one
  to copy (section 3.1).

### 1.2 Things that exist and are reused

| Need | Exists | Where |
|---|---|---|
| Mode question | yes (express / detailed / guided) | above |
| "This system does not speak" confirmation | yes, `ConfirmSilent()` | `CharonWsInstall3.h:97-103` |
| Typed word before a disk changes | yes ("TAK"/"YES") | `ConfirmPlan()` `CharonWsInstall.h:130-157`; `CharonWsInstall3.h:368-370`; `CharonWsDosSystem.h:511`; `images::ConfirmErase` |
| Spoken plan before the typed word | partly: system, user, disk only | `ConfirmPlan()` |
| Go back one question | yes (Escape) | `ask::Run` |
| Go back and change one chosen answer | no | — |
| Summary of all answers | no | — |
| Saved answer set / personal profile | no. The only user-editable input file is `\EFI\HAK\windows-iso-url.txt` | `CharonWsWinUsb.h:13,225` |
| Unattended mode, per-machine overrides, report | no | — |

### 1.3 Limits in today's code that bound what a question can offer

- Debian and Ubuntu: "replace one chosen partition" is refused (`CharonWsInstall2.h:778-782`).
- Windows: express/detailed only with exactly one internal disk; the answer file says `DiskID 0`
  (`CharonWsInstall2.h:599-604, 756-761`).
- Ubuntu: the disk can only be named as largest or smallest (`CharonWsInstall2.h:105-113`), and needs
  8 GB of RAM (`:118-121`).
- Two disks of identical size stop a Debian/Ubuntu/Windows express or detailed install
  (`CharonWsInstall.h:109-112`).
- Fedora image: no desktop, no speech (`CharonWsInstall3.h:61-62, 330`). Arch from the network: Charon
  only starts the live system; the user runs `archinstall` himself (`:47-49, 114-132`).
- Windows 95/98/ME/XP media are never downloaded; the user supplies them (`CharonWsDosSystem.h:457-460`).

---

## 2. Inventory of today's questions

"Q" = asked in the express mode too. "Consumed" lists every generator line that reads the answer.

### 2.1 Flow questions (all installer-based systems)

| id | Text (EN) | Choices | Default | Consumed |
|---|---|---|---|---|
| (group) | Which group? | Windows / Linux: Debian and Ubuntu / Linux for blind people / ready-made images / tools and netboot.xyz / CharonDOS | first | `CharonWsInstall3.h:442` |
| (system) | Which system? | entries of the group, each with its speech status | first | `:454` |
| (silent) | "…this system does not speak… Continue anyway?" | yes/no | — | `:97-103, 486` |
| (mode) | How do you want to install? | express / detailed / I will install this system myself | express | `CharonWsInstall2.h:687` |
| `diskmode` | What should be done with the disk? | erase whole disk / replace one partition / free space only | whole | asked `CharonWsInstall2.h:776`; `CharonWsInstall.h:93-127, 136-148, 428-429`; `CharonWsInstall2.h:61, 352-382, 615-622` |
| (disk) | On which disk? | spoken disk labels | first | `CharonWsInstall.h:107` |
| (partition) | Which partition should be replaced? (Windows only) | partitions ≥ 12 GB, never EFI/reserved/recovery | first | `:124` |
| (confirm) | Type YES to start | typed word | — | `:154` |

### 2.2 Linux — common table `commonQuestions` (`CharonWsInstall.h:56-64`); Debian, Ubuntu and ready images

| id | Q | Text | Type | Choices | Default | Consumed |
|---|---|---|---|---|---|---|
| `locale` | Q | System language: | choice | pl_PL, en_US, en_GB, de_DE, fr_FR, es_ES, it_IT, uk_UA, cs_CZ | en_US (pl_PL when Charon speaks Polish, `CharonWsInstall2.h:766`) | preseed `CharonWsInstall.h:405`; autoinstall `CharonWsInstall2.h:48, 73`; image `CharonWsInstall3.h:225, 244` |
| `keyboard` | | Keyboard layout: | choice | pl, us, gb, de, fr, es, it, ua, cz | from language (`DeriveDefaults`, `CharonWsInstall.h:74-85`) | `CharonWsInstall.h:406`; `CharonWsInstall2.h:49`; `CharonWsInstall3.h:235, 239` |
| `timezone` | | Time zone: | choice | 13 zones | from language | `CharonWsInstall.h:421`; `CharonWsInstall2.h:50`; `CharonWsInstall3.h:226` |
| `hostname` | | Computer name: | name | — | charon-pc | `CharonWsInstall.h:408, 410`; `CharonWsInstall2.h:52`; `CharonWsInstall3.h:224, 390` |
| `fullname` | | Your full name: | text | — | = user | `CharonWsInstall.h:417`; `CharonWsInstall2.h:53`; `CharonWsInstall3.h:247` |
| `user` | Q | User name: | name | — | user | `CharonWsInstall.h:134, 418`; `CharonWsInstall2.h:54`; `CharonWsInstall3.h:246` |
| `password` | Q | User password: | secret | — | (must be typed) | hashed by `HashPassword()` `CharonWsInstall2.h:664-674` (SHA-256 crypt) → `hash`: `CharonWsInstall.h:415, 419`; `CharonWsInstall2.h:55`; `CharonWsInstall3.h:248` |

### 2.3 Debian (`debianQuestions`, `CharonWsInstall.h:65-72`)

| id | Q | Text | Type | Choices | Default | Consumed |
|---|---|---|---|---|---|---|
| `desktop` | Q | Desktops: | multi | GNOME, MATE, KDE Plasma, Xfce, Cinnamon, LXQt, LXDE, i3 with I38 | gnome-desktop | `tasksel/first` `:442-445`; `:397, 447` |
| `readers` | Q | Screen readers: | multi | Orca, Cthulhu, Speakup+espeakup, Fenrir, Emacspeak | orca,speakup | `ReaderPackages` → `pkgsel/include` `:446-448`; `ReaderArguments` → `speech.sh` `:397-398` |
| `mirror` | | Package server: | choice | deb.debian.org, PL, DE, UK, US | deb.debian.org | `mirror/http/hostname` `:412` |
| `root` | | A separate root account with a password? | yes/no | | no | `passwd/root-login`, `root-password-crypted` (same hash as the user) `:415-416` |
| `ssh` | | Install an SSH server? | yes/no | | no | task `ssh-server` `:444` |
| `nonfree` | | Add non-free firmware…? | yes/no | | yes | `apt-setup/non-free-firmware` `:440` |

Follow-ups for every Linux (`ResolveReaders`, `CharonWsReaders.h`):

| id | Asked if | Text | Choices | Consumed |
|---|---|---|---|---|
| `console` | two or three of Speakup/Fenrir/Emacspeak checked | Which screen reader should speak in the console? | the checked ones | `:62-65` → `speech.sh` argument 1 |
| `gui` | a desktop and both Orca and Cthulhu | Which screen reader should start on the desktop? | Orca / Cthulhu | `:66-70` → argument 2 |
| `login` | a desktop was chosen | How do you want to sign in? | graphical speaking sign-in screen / console + `startx` | `:76-82` → argument 4 |
| `speech` | derived, not asked | — | yes/no | `:73`; picks the speaking initrd `CharonWsInstall.h:465` |

### 2.4 Ubuntu (`ubuntuQuestions`, `CharonWsInstall2.h:14-19`)

| id | Q | Text | Type | Choices | Default | Consumed |
|---|---|---|---|---|---|---|
| `desktop` | Q | Desktops: | multi | Ubuntu (GNOME), Kubuntu, Xubuntu, Ubuntu MATE, Lubuntu, Ubuntu Cinnamon, i3 with I38 | ubuntu-desktop-minimal | `packages:` `:65` |
| `readers` | Q | Screen readers: | multi | as Debian | orca,speakup | `:64, 79-80` |
| `ssh` | | Install an SSH server? | yes/no | | no | `ssh: {install-server: …}` `:56` |
| `updates` | | Download updates during the installation? | yes/no | | no | `updates: all` / `security` `:57` |

Not asked, fixed: release 26.04 (`:20`), storage layout `direct` (`:60`), package mirror, root (always sudo).

### 2.5 Ready images (`imageQuestions`, `CharonWsInstall3.h:193-197`): Debian 13, Ubuntu 26.04, Arch, Fedora 45

| id | Q | Text | Type | Choices | Default | Consumed |
|---|---|---|---|---|---|---|
| (mode) | Q | Which mode? | choice | express / detailed | express | `:326` |
| `desktop` | Q | Desktops: | multi | GNOME, KDE Plasma, Xfce, MATE, Cinnamon, LXQt, i3 with I38 | none | `DesktopPackages` `:205-219, 259-265` → `firstboot.sh` argument 6 |
| `readers` | Q | Screen readers: | multi | as Debian | orca,speakup | `:267` |
| `ssh` | | Allow signing in with a password over SSH? | yes/no | | no | `ssh_pwauth` `:253` |
| (place) | Q | What should be done with the disk? | whole disk / alongside the existing systems | whole | `:340`; sets `dual` → `firstboot.sh` argument 7 `:273` |
| (disk), (confirm) | Q | On which disk? / typed YES or `ConfirmErase` | | | `:360, 369, 374-375` |

Plus the common table (2.2) and the follow-ups (2.3). Haiku image: no questions except disk and confirmation.

### 2.6 Windows (`windowsQuestions`, `CharonWsInstall2.h:284-306`)

Asked only when no install media is found (`:692-747`):

| id | Text | Choices | Default | Consumed |
|---|---|---|---|---|
| (source) | Where should the Windows 11 installer come from? | download to a USB stick / download to a RAM disk | whichever fits the computer | `:714` |
| `wversion` | Which Windows version? | 11 26H2; 10 22H2; 11 Enterprise eval; 11 Enterprise LTSC 2024 eval; 11 IoT Enterprise LTSC 2024 eval; Server 2025 eval; 10 Enterprise LTSC 2021 eval | 11 26H2 | `:721-726`; sets `windex`; `:451` |
| (MS language) | Windows language, Microsoft's list: | list fetched from Microsoft | Charon's language | `CharonWsRamDisk.h:156` → `wlocale` `:741` |
| (stick) | To which USB stick? + confirmation | sticks ≥ 9 GB | first | `CharonWsWinUsb.h:388, 398` |

Architecture is fixed (x64); no question.

| id | Q | Text | Type | Choices | Default | Consumed |
|---|---|---|---|---|---|---|
| `wlocale` | Q | Windows language: | choice | pl-PL, en-US, en-GB, de-DE, fr-FR, es-ES, it-IT, uk-UA, cs-CZ | en-US | UI, input, system and user locale, all the same value: `:325-333, 439-441`; NVDA language in Setup `:248`; `Charon-Readers.ps1 -Language` `:480` |
| `hostname` | | Computer name: | name | | charon-pc | `ComputerName` `:396` |
| `user` | Q | User name: | name | | user | `LocalAccount` (group Administrators) `:452`; `AutoLogon` `:459` |
| `password` | Q | User password: | secret | | (typed) | **plain text** in the answer file `:453, 460`; Server administrator `:451` |
| `edition` | | Windows edition: | choice | whatever media/firmware key selects; Home; Pro (generic keys) | auto | `ProductKey` `:390`; `windex` `:609-613` |
| `bypass` | | Skip the hardware requirement checks: TPM, Secure Boot, RAM? | yes/no | | no | `LabConfig` values `:344-348` |
| `localonly` | | Create a local account, no Microsoft account, no network needed? | yes/no | | yes | `BypassNRO` `:401`; `HideOnlineAccountScreens`, `HideWirelessSetupInOOBE` `:444-445` |
| `privacy` | | Turn off optional diagnostic data and ads? | yes/no | | yes | `AllowTelemetry=1`, `DisableWindowsConsumerFeatures=1` `:429-432`; `ProtectYourPC` 3/1 `:445` |
| `wreaders` | Q | Screen readers: | multi | NVDA (speaks in Setup), JAWS, Narrator | nvda,narrator | `:210-226, 341-343, 477-481, 625` |
| `wfirst` | (cond.) | Which screen reader should start on the sign-in screen and after sign-in? | NVDA / JAWS | — (asked only when both are checked) | `:222`; `:402, 416, 458, 465, 480` |
| `nobitlocker` | | Prevent automatic BitLocker disk encryption? | yes/no | | yes | `PreventDeviceEncryption` `:427` |
| `clockutc` | | Will Linux be installed alongside Windows? (clock in UTC) | yes/no | | no | `RealTimeIsUniversal` `:428` |
| `profile` | Q | Windows profile: | choice | unchanged / slim / ReviOS / AtlasOS / Win11Debloat script | none | `Charon-WindowsProfile.ps1 -Profile` `:484-488` |

Not asked, fixed: time zone (Windows derives it from the locale), EFI partition 300 MB, one NTFS
partition, the account is always an administrator, NVDA version (`nvda_2026.3.exe`, `:206-208`).

### 2.7 CharonDOS (`CharonWsDosSystem.h`, `CharonWsDosApps.h`)

| id | Asked if | Text | Choices | Default | Consumed |
|---|---|---|---|---|---|
| (profile) | always | Which Windows profile should be added to CharonDOS? | none / Windows 95 / 98 / ME / XP with NVDA 2017.3 | none | `:431`; folder `C:\PC\<ID>`, `profile=` `:379` |
| (programs) | always | Which programs should be downloaded? | all tested / editors / files and folders / text viewing and help / tools / archives / programming / games / no programs | all | `CharonWsDosApps.h:89-95`; fetched `CharonWsDosSystem.h:545-546` |
| (reader) | 95/98/ME profile | Which screen reader should be installed in this Windows? (after the spoken warning that all are English, `:451`) | JAWS 3.31 (95, 98), JAWS 4.02 (ME, 98), Window-Eyes 2.0 demo (95, 98), Hal95 1.03 demo (95), none | first that fits | `:454`; `reader=`, `reader_setup=`, `com1=` `:383-387` |
| (media) | a profile | What should Windows be installed from? | found discs / ISO / VHD / VMDK / IMG; search again; no media now | first found | `:470`; `cdrom=`/`disk=`, `boot=`, `state=` `:388-396` |
| (key) | 9x from a disc | The Windows product key… | 25 characters or empty | empty | `:343`; `setup_key=` `:399` |
| (name) | 9x from a disc | Your name or Windows user name… | text | User / Uzytkownik | `:355`; `setup_name=` `:400` |
| (computer) | 9x from a disc | Computer name, up to 15 letters and digits: | text | CHARON | `:358`; `setup_computer=` `:401` |
| (place) | not `dual` | Where should CharonDOS be installed? | whole disk (rest left free) / free space on a disk that already holds a system | whole | `:485` |
| (disk), (confirm) | always | On which disk? + `ConfirmErase` or typed YES | | | `:490, 498, 506, 511` |
| (win11) | whole disk | Install Windows 11 in the free space on this disk now? | yes / no | no | `:564`; `dosdefault=yes` → `CharonWsInstall2.h:475`; then the whole Windows dialogue |

Not asked, fixed in `MakeProfile()`: `memory` 128 MB (XP 512) `:381`; `sound=sb16` `:384`;
`net=ne2000` `:385` (ignored by CharonPC stage 1, `pc_config.cpp:344`); `lang` = Charon's language
`:380`; disk size by profile 1/2/2/4 GB `:189-195`; `setup_zone=Central European` only when Polish
`:402`; CPU by profile (`pc_config.cpp:221`); partition size computed `:486-487`; boot order CharonDOS
first, Charon second `:551-555`; XP reader always NVDA 2017.3 `:439`; code page 437 with best-fit
Polish letters (`src/dos/util.cpp:143`); prompt `$P$G` (`src/dos/dosint.cpp:1071`).

### 2.8 Count of existing questions

| System | Table questions | Conditional follow-ups | Flow (mode, disk, confirm…) | In express mode |
|---|---|---|---|---|
| Debian | 13 (7 common + 6) | 3 | 4 | 5 + flow |
| Ubuntu | 11 (7 + 4) | 3 | 4 | 5 + flow |
| Ready image (Debian/Ubuntu/Arch/Fedora) | 10 (7 + 3) | 3 | 4 | 5 + flow |
| Windows | 12 | 1 (+4 when media must be downloaded) | 5 | 5 + flow |
| CharonDOS | 10 (all direct, 3 of them only for 9x from a disc, 1 only for 9x) | — | included | no modes |


---

## 3. How new questions are declared, carried and tested

### 3.1 One table, generated (copy the "fields" pattern)

New file `tools/charon-install/charon_questions.py` (same idea as `tools/charon-os/tests/charon_fields.py`):
one Python list of rows, the single source for

1. `src/efi/CharonWsQuestions.h` (generated `ask::Question` tables, one per group and system);
2. the key list the Linux and Windows scripts accept (generated `src/linux/answers-keys.sh`,
   `tools/charon-addon/Charon-Answers.Keys.ps1`), so a key the UEFI side writes and a script does not
   know is a build error, not a silent no-op;
3. the documentation table of an answer-set file (section 6);
4. host test vectors.

Row fields: `id, group, level (D|A), systems, kind, en, pl, help_en, help_pl, choices, default,
when, flags (risky, secret, long, unverified), apply` (free text naming the consumer).

### 3.2 `ask::Question` gets optional trailing members

```cpp
struct Question {
    const char* key; bool quick; Kind kind; const char16_t* pl; const char16_t* en;
    const char* fallback; const Choice* choices; size_t count; const char16_t* helpPl; const char16_t* helpEn;
    // new, all optional: existing initialisers keep compiling and keep their meaning
    const char* group = nullptr;      // group id; null = "main" (today's tables)
    uint8_t level = 0;                // 0 personal, 1 administration
    bool (*when)() = nullptr;         // asked only if it returns true (the default is still stored)
    uint8_t flags = 0;                // 1 risky (needs ConfirmRisk), 2 secret-in-file, 4 long value
    int32_t low = 0, high = 0;        // Kind::number
};
```

New kinds: `number` (typed digits, or Up/Down in steps; range checked), `list` (free text, items
separated by commas or spaces, stored in the long store), `address` (IPv4/host name/URL, validated),
`file` (a file picked on a mounted volume with the existing browser, `CharonWsBrowser.h`).

Store: `maxAnswers` 48 → 384; a second "long" store (32 values × 2 048 bytes) for SSH keys, software
lists, scripts' URLs. `quick` stays as it is: **every new row has `quick=false`**, so
`ask::Run(table, n, quick=true)` never asks it and only stores its `fallback` — this is exactly the
"default used silently" rule, with no new code path. Rows with `level=1` are skipped the same way
unless the administration gate was answered yes.

Generator buffers must grow (preseed 12 000 → 48 000, autoinstall 8 000 → 32 000, cloud-init 9 000 →
32 000, unattend 16 000 → 64 000); each generator already reports `overflow` and refuses to start.

### 3.3 Carrying many answers to the installed system

Positional arguments (`speech.sh CONSOLE GUI LIST LOGIN`) do not scale. One new carrier per family:

- **Linux:** `/var/lib/charon/answers.env`, `KEY='value'` lines, mode 0600, written
  - Debian: as one more file in the cpio (`AppendCharon` takes one extra file today; make it a list),
    copied by `preseed/late_command` next to `speech.sh`;
  - Ubuntu: `\charon-install\ubuntu\answers.env` beside the other files (`CharonWsInstall2.h:137-144`),
    copied by the existing `early-commands`/`late-commands`;
  - images: a `write_files:` entry in `ImageUserData()` (the key is already used for Debian's keyboard file).
  New script `src/linux/personal.sh` (compiled in like the others) sources it and applies every
  post-install setting; `speech.sh` keeps its arguments and additionally reads the speech keys from
  the same file. `personal.sh` runs after `speech.sh` and, like `firstboot.sh`, retries what failed
  for lack of a network at each start until done. It deletes secrets from `answers.env` when finished.
- **Windows:** `\charon\answers.ini` on the media (`key=value`, UTF-8), copied to `%ProgramData%\Charon`
  by the existing `xcopy` FirstLogonCommand (`CharonWsInstall2.h:472`). New script
  `tools/charon-addon/Charon-Personal.ps1 -Answers %ProgramData%\Charon\answers.ini -Yes`, added to
  the `extras[]` copy list (`:647`) and run after `Charon-Readers.ps1` and before
  `Charon-WindowsProfile.ps1`. Machine-wide values that must exist before the first sign-in stay
  `RunSynchronousCommand` lines in the specialize pass (pattern at `:401-432`); per-user values go
  into the default user hive there (pattern at `:407-411`, `reg load HKU\CharonDefault`).
- **CharonDOS:** keys in `CHARONPC.CFG` (most exist in `pc_config.cpp`), a generated
  `C:\AUTOEXEC.BAT`, and Charon's own settings record for speech.

### 3.4 Tests each new question must come with

1. **Host generator test (new, cheap, catches most mistakes).** `DebianPreseed`, `UbuntuUserData`,
   `ImageUserData` and `WindowsAnswers` are pure text functions over the answer store. A host
   program `tests/CharonInstallAnswersTests.cpp` includes them with a stubbed `ask::` store and a
   fake disk, feeds answer sets from `charon_questions.py` and compares with golden files. Rules
   checked for every row: (a) with all defaults the output is byte-identical to today's (the express
   flow did not change); (b) a non-default answer changes exactly the expected lines; (c) XML is
   well-formed, YAML parses (`python -c yaml.safe_load`), preseed lines match `^(d-i|tasksel|popularity-contest) \S+ \S+`.
2. **Script dry runs.** `personal.sh --dry-run` and `Charon-Personal.ps1 -DryRun` print the actions
   for an `answers` file (the two existing PowerShell scripts already have `-DryRun`); run on the
   host for every vector.
3. **VM trial per batch**, not per question (user's batch rule): one install per system with a
   "everything non-default" answer set, then an audit script inside the guest that checks the
   effect from outside the reader (file contents, `gsettings get`, registry values, `systemctl is-enabled`),
   in the Polish locale. Result in `tests/charon-rNNN-results.txt`.

---

## 4. UX: many questions without a burden

### 4.1 Modes (building on the existing question)

The first question stays "How do you want to install?" with today's three choices, in today's order,
express first. Two entries are added **after** them, and only when they apply:

| Value | EN | PL | When shown |
|---|---|---|---|
| `quick` | express mode: as few questions as possible | tryb ekspresowy: jak najmniej pytań | always (unchanged) |
| `detailed` | detailed mode: all questions, in groups | tryb szczegółowy: wszystkie pytania, w grupach | always (text extended) |
| `guided` | I will install this system myself | zainstaluję ten system sam | as today |
| `saved` | use a saved answer set | użyj zapisanego zestawu odpowiedzi | at least one answer set exists |
| `auto` | unattended, from the answer file on this stick | bez pytań, z pliku odpowiedzi na tym nośniku | a file with `unattended=yes` exists (section 6.3) |

CharonDOS gets the same question (it has none today): `quick` = exactly today's dialogue.
Ready images keep their two modes plus `saved`/`auto`.

A separate "Standard" tier is **not** proposed: the user's rule is that the normal flow stays as it
is, and a middle tier would be a third list to maintain. The group gates below give the same effect
(a detailed install where only two groups are opened is a "standard" install).

### 4.2 The detailed mode

1. Today's detailed questions for the system come first, unchanged (they are group `main`).
2. Then the **group list** — a list like the check-box list, one line per group:
   "Speech and braille: defaults, 14 questions" / "…: changed, 3 of 14". Keys: Up/Down, letters jump,
   **Enter** opens the group, **D** reads the group's defaults in one sentence, **F3** search,
   **F10** or the last entry "Done: go to the summary". Nothing forces the user through a group.
   Screen: the same line with icons (`►` for a group), per the icons-not-role-words rule.
3. Opening a group asks its questions in order with `ask::Run` semantics (Escape = previous
   question; at the first question, back to the group list). Each question shows and speaks its
   default as the preselected value; **F2** speaks "default: …" again; **Delete** resets the answer to
   the default; **F1** repeats (exists); help is spoken before the question (exists).
4. **Gate variant for linear users:** a setting "ask me group by group" (default off) walks the groups
   in order with the gate "Customise this group? no, use the defaults / yes / hear the defaults".
   EN: "Speech and braille. Customise this group?" PL: "Mowa i brajl. Dostosować tę grupę?"
5. **Administration gate**, once, at the end of the personal groups:
   EN "Show the administration groups: accounts, network, policies, deployment?" PL "Pokazać grupy
   administracyjne: konta, sieć, zasady, wdrażanie?" Default **no**.
6. **Search / jump (F3):** type part of a word ("hasło", "wifi", "braille"); the list of matching
   questions (id, text, current value) is read; Enter jumps to that question, Escape returns.
   Search covers EN and PL texts and the ids.
7. **Summary before the typed word.** `ConfirmPlan()` is extended: after the disk sentence it says
   "N answers differ from the defaults" and reads **only the non-default answers**, grouped; the full
   list (defaults included) is on the screen for the review keys and is read on **F5**. Secrets are
   read as "set" / "not set". Then the choices: "start / change an answer / save these answers as a
   set / cancel". "Change an answer" opens the group list (or F3) and returns to the summary.
   Only "start" leads to the typed TAK/YES, which stays exactly as it is.
8. **Risky answers** (⚠A) are confirmed when given and repeated in the summary (section 7).

All of this is one shared implementation in `CharonWsAsk.h`, used by every system's dialogue, so
the reader shortcuts and progress behave identically everywhere (user's "shared implementation" rule).

### 4.3 Saved answer sets (personal profiles)

- "Save these answers as a set" asks a name (`Kind::name`) and writes
  `\EFI\HAK\answers\<name>.ini` on Charon's own volume (format in section 6.1). Passwords are not
  saved unless the user says so (section 8).
- Mode `saved` lists the sets ("name, system, date, N answers"); choosing one loads it, then shows
  the summary of 4.2.7 — the user can change single answers and must still pick the disk and type
  the confirmation. Missing secrets are asked.
- A set made for one system loads for another: unknown keys are reported ("12 answers do not apply
  to Windows and were ignored"), never applied blindly.
- Nothing like this exists today (section 1.2).
- **Required code change:** `\EFI\HAK` is copied whole into the new system's EFI partition
  (`HakTree`, `CharonWsInstall.h:238-263`; `CopyTree` to the Windows media, `CharonWsInstall2.h:637-639`;
  `images::CharonOnto`). `answers` must be added to `SkipInArchive` (`CharonWsInstall.h:216`) and to
  the other two copies, otherwise every answer set (and any secret in it) is duplicated onto each
  installed machine's EFI partition and onto the Windows media.

---

## 5. Proposed additional questions

Conventions: "L" = Debian, Ubuntu and the ready Linux images unless a narrower list is given; "W" =
Windows; "today" = the behaviour of r395, which is also what the default produces. Wording is
"EN / PL". A choice list starts with the default.

### 5.1 Language, keyboard, time (group `lang`) — Linux and Windows

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `formats` | D | Dates, numbers and currency as in: / Daty, liczby i waluta jak w kraju: | choice | same as the system language; the 9 locales | same as language | — | L: `LC_TIME, LC_NUMERIC, LC_MONETARY, LC_PAPER, LC_MEASUREMENT` in `/etc/default/locale` or `/etc/locale.conf` (personal.sh), locale generated. W: `UserLocale` (already a separate element at `CharonWsInstall2.h:332, 440`) | E |
| `extra_langs` | D | Additional languages to install: / Dodatkowe języki do zainstalowania: | multi | the 9 locales | none | — | Debian `d-i localechooser/supported-locales multiselect …` (in the official example preseed); other Linux: `locale-gen`. W: language pack by `Install-Language` (unverified) | E (L), H (W: large download, cmdlet unverified) |
| `kb_variant` | D | Keyboard variant: / Wariant klawiatury: | choice | standard; for pl: programmer's (default in X) / typist's 214; for us: international; Dvorak | standard | — | Ubuntu `keyboard: {layout, variant}` and cloud-init `keyboard: variant` (both documented); Debian: `XKBVARIANT` in `/etc/default/keyboard` (the file Charon already writes for the Debian image, `CharonWsInstall3.h:230-238`). W: `InputLocale` value such as `0415:00010415` vs `0415:00000415` (value form unverified) | M |
| `kb_second` | D | Second keyboard layout: / Drugi układ klawiatury: | choice | none; the 9 layouts | none | — | Ubuntu: comma-separated `layout` plus `toggle` (documented); Debian `keyboard-configuration/toggle` (in the example preseed). W: second entry in `InputLocale` (unverified) | M |
| `kb_toggle` | D | Key that switches layouts: / Klawisz przełączania układów: | choice | Alt+Shift; Ctrl+Shift; Caps Lock; Win+Space | Alt+Shift | `kb_second` set | Ubuntu `toggle: alt_shift_toggle` (documented example value); XKB option `grp:…` elsewhere. Warn: Caps Lock is the Orca/NVDA laptop modifier | M |
| `tz_other` | D | Time zone not in the list — type its name, for example Europe/Vienna: / Strefa spoza listy — wpisz nazwę, na przykład Europe/Vienna: | text | — | (list answer) | the user chose "another" in `timezone` | L: as `timezone` today. Validated against a built-in list of tz names | E |
| `wtimezone` | D | Windows time zone: / Strefa czasowa Windows: | choice | follow the Windows language (today); the 13 zones | follow language | W | `<TimeZone>` in Microsoft-Windows-Shell-Setup (specialize/oobeSystem; value is a Windows zone name, list by `tzutil /l`) — needs a 13-row IANA→Windows table (e.g. Europe/Warsaw → "Central European Standard Time") | E |
| `clock_fmt` | D | Clock: 24-hour or 12-hour? / Zegar: 24-godzinny czy 12-godzinny? | choice | as the country; 24; 12 | as the country | — | L: `LC_TIME` choice / GNOME `org.gnome.desktop.interface clock-format`. W: `HKCU\Control Panel\International` (value names unverified) | M |
| `ntp` | D | Set the clock from the Internet? / Ustawiać zegar z Internetu? | yes/no | yes | yes | — | Debian `clock-setup/ntp` (already written true); cloud-init `ntp: {enabled}`; W: default | E |

### 5.2 Accounts and sign-in (group `acct`)

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `autologin` | D | Sign in automatically, without typing the password? / Logować automatycznie, bez wpisywania hasła? | yes/no | no | no | — | L: GDM `[daemon] AutomaticLoginEnable/AutomaticLogin`, LightDM `autologin-user=`, console `getty@tty1` drop-in (the pattern `speech.sh:413` uses for Emacspeak). W: `AutoLogon` without `LogonCount` (element exists at `CharonWsInstall2.h:459`), and the three clean-up commands at `:491-495` are then not emitted | M 🔒 (W: the password stays in the registry in clear text; say so) |
| `pw_none` | D | Account without a password? / Konto bez hasła? | yes/no | no | no | `autologin`=yes | L: `passwd -d`; W: empty `<Password><Value/>` (unverified that Setup accepts it). Refused when `ssh`=yes or any remote access is on | M 🔒 |
| `root_pw` | D | Root password (empty: the same as yours): / Hasło roota (puste: takie jak twoje): | secret | — | same as user | L, `root`=yes | separate hash → `passwd/root-password-crypted` (today the user's hash is reused, `CharonWsInstall.h:415`) | E 🔒 |
| `sudo_nopw` | D | Run administrator commands without asking for the password? / Wykonywać polecenia administratora bez pytania o hasło? | yes/no | no | no | L | `/etc/sudoers.d/90-charon` `user ALL=(ALL) NOPASSWD:ALL` (images: `sudo:` value at `CharonWsInstall3.h:251`) | E 🔒 |
| `shell` | D | Command shell: / Powłoka poleceń: | choice | bash; zsh; fish | bash | L | package + `chsh` (images: `shell:` `CharonWsInstall3.h:252`) | E |
| `groups` | D | Add the account to extra groups: / Dodać konto do grup: | multi | audio, video, plugdev, dialout (braille over serial/USB), lp, bluetooth, docker, libvirt, brlapi | distribution default | L | Debian `d-i passwd/user-default-groups` (example preseed); Ubuntu `identity: groups`; cloud-init `groups:` | E |
| `w_role` | D | Your Windows account: administrator, or standard with a separate administrator account? / Twoje konto Windows: administrator czy zwykłe z osobnym kontem administratora? | choice | administrator (today); standard + administrator account | administrator | W | second `<LocalAccount>` with `<Group>Users</Group>` vs `Administrators` (`CharonWsInstall2.h:452-454`); asks `admin_name`, `admin_pw`. Note: UAC credential prompts appear on the secure desktop; NVDA must be installed with "use on the sign-in and secure screens" (it is, by `--install-silent`; unverified) | M 🔒 |
| `w_pin_hello` | D | Offer Windows Hello PIN set-up at first sign-in? / Proponować ustawienie kodu PIN Windows Hello przy pierwszym logowaniu? | yes/no | yes (Windows default) | yes | W | policy `HKLM\SOFTWARE\Policies\Microsoft\PassportForWork` (unverified) | M |
| `secq` | D | (information only) Local Windows accounts made by an answer file have no security questions. | — | — | — | W | nothing; spoken once in the group | — |

### 5.3 Disk, encryption, boot (group `disk`)

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `layout` | D | Partitions: / Partycje: | choice | everything in one (today); separate /home; separate /home, /var and /tmp | one | Debian, whole disk or free space | `d-i partman-auto/choose_recipe select atomic|home|multi` (documented values; today `atomic`, `CharonWsInstall.h:430`) | E |
| `lvm` | D | Use LVM, so partitions can be resized later? / Użyć LVM, żeby partycje dało się później zmieniać? | yes/no | no | no | Debian or Ubuntu, whole disk | Debian `partman-auto/method string lvm` + `partman-auto-lvm/guided_size string max` + the LVM confirmations (documented); Ubuntu `storage: layout: name: lvm`, `sizing-policy: all` (documented; today `direct`) | E |
| `encrypt` | D | Encrypt the disk? You will type a passphrase at every start, before anything speaks. / Zaszyfrować dysk? Hasło trzeba będzie wpisać przy każdym uruchomieniu, zanim cokolwiek zacznie mówić. | yes/no | no | no | Debian or Ubuntu, whole disk | Debian `partman-auto/method string crypto` (documented) + passphrase keys (`partman-crypto/passphrase`, not in the official example: unverified) + `partman-auto-crypto/erase_disks boolean false` (documented); Ubuntu `storage: layout: {name: lvm, password: …}` (documented) | H ⚠A 🔒: the kernel's passphrase prompt is not spoken and Charon is no longer running then. Mitigation in section 7 |
| `encrypt_pw` | D | Disk passphrase: / Hasło dysku: | secret | — | — | `encrypt`=yes | as above | H 🔒 |
| `fs` | D | File system: / System plików: | choice | ext4; btrfs; xfs | ext4 | Debian or Ubuntu | Ubuntu `layout: name: btrfs` (listed as supported); Debian `partman/default_filesystem` (not in the fetched pages: unverified) | M |
| `swap` | D | Swap: / Pamięć wymiany: | choice | as the installer decides (today); none; swap file as large as the RAM (needed for hibernation) | installer default | L | images: cloud-init `swap: {filename, size, maxsize}` (documented); Debian `partman-basicfilesystems/no_swap` (unverified) or a swap file made by personal.sh | M |
| `hibernate` | D | Enable hibernation? / Włączyć hibernację? | yes/no | no (L) / Windows default (W) | as today | laptops and desktops | W: `powercfg /hibernate on|off` (E). L: needs `swap`=RAM, `resume=` kernel parameter and initramfs update | E (W), H (L) |
| `sys_size` | D | Size of the system partition in gigabytes (0: all the free space): / Rozmiar partycji systemowej w gigabajtach (0: całe wolne miejsce): | number | 0, 12–8192 | 0 | W always; Debian with `lvm` | W: `<Size>` instead of `<Extend>true</Extend>` (`CharonWsInstall2.h:357, 368`); Debian `partman-auto-lvm/guided_size` (documented: size, percentage or max) | E (W), M (Debian) |
| `data_part` | D | Make a second partition for your files (D:) from the rest? / Zrobić z reszty drugą partycję na pliki (D:)? | yes/no | no | no | W, whole disk, `sys_size`>0 | one more `CreatePartition`/`ModifyPartition` pair | M |
| `esp_size` | D | Size of the EFI system partition: / Rozmiar partycji systemowej EFI: | choice | 300 MB (today); 1 GB (room for Charon, several systems and their kernels) | 300 MB | W whole disk; CharonDOS computes its own | `<Size>300</Size>` `CharonWsInstall2.h:355` | E |
| `boot_first` | D | Which starts first when the computer is switched on? / Co ma się uruchamiać jako pierwsze po włączeniu komputera? | choice | the new system, Charon second (today); Charon's menu; the system that is first now | new system | — | `install-charon.sh` (Linux), `Install-Charon.ps1 -FirstEntry/-Default` (`CharonWsInstall2.h:473`), `PutOption(first=…)` for images (`CharonWsInstall3.h:307-314`) | M |
| `grub_wait` | D | How long the Linux boot menu waits, in seconds: / Ile sekund czeka menu rozruchu Linuksa: | number | 0–60 | distribution default | L | `GRUB_TIMEOUT` in `/etc/default/grub`, `update-grub`/`grub-mkconfig` (personal.sh) | E |
| `grub_tune` | D | Play a short tune when the Linux boot menu appears? / Zagrać krótki sygnał, gdy pojawi się menu rozruchu Linuksa? | yes/no | no | no | L | `GRUB_INIT_TUNE="480 440 1"` (useful when Charon's reader add-on is not in the firmware) | E |
| `boot_text` | D | Show start-up messages as text instead of a splash screen, so a console reader can read them? / Pokazywać komunikaty startowe jako tekst zamiast ekranu powitalnego, żeby czytnik konsoli mógł je czytać? | yes/no | distribution default | as today | L | remove `quiet splash` from `GRUB_CMDLINE_LINUX_DEFAULT` | E |
| `w_bitlocker` | D | BitLocker: / BitLocker: | choice | prevent automatic encryption (today's `nobitlocker`=yes); leave it to Windows; (administration: turn on, section 5.12) | prevent | W | replaces the yes/no `nobitlocker`; same registry value `CharonWsInstall2.h:427` | E |

Not offered (and the dialogue says why when asked through F3): replacing one chosen partition for
Debian/Ubuntu, Windows on a second internal disk, manual partition tables. They stay in "I will
install this system myself".

### 5.4 Speech and braille in the installed system (group `speech`)

Defaults follow what the user hears in Charon. Charon's settings record already holds the values
(`tools/charon-os/tests/charon_fields.py`: `rate` 5–20 with 10 = 100 %, `pitch`, `speechVolume` 1–10,
`punctuation`, `capitals`, `screenLanguage`, `synthKey` 0 = TVox built in / add-on synthesizer,
`synthVoice` index). Charon's add-on synthesizers are eSpeak NG and **RHVoice 1.18.5 with the voices
Natan (Polish) and Slt (English)** (`tools/build-charon-synths.py:548-549, 628-629`; ids like
`rhvoice-natan`, `src/efi/CharonSynthModule.h:71`). Mapping rule for the defaults:

- `synth`: Charon speaks with RHVoice → `rhvoice`; with eSpeak NG or the built-in TVox → `espeak-ng`
  (today's behaviour).
- `rh_voice_pl` = Natan, `rh_voice_en` = Slt (the voices Charon itself carries), unless Charon's
  current voice id names another RHVoice voice.
- `rate`: Charon 5…20 → percent 50…200 → Speech Dispatcher −100…+100 by a fixed table (10 → 0);
  NVDA 0…100 by a second table. Only applied when the user opens the group and accepts
  "the same speed as Charon"; otherwise each reader keeps its own default (so express is unchanged).

#### Linux

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `synth` | D | Speech synthesizer: / Syntezator mowy: | choice | eSpeak NG; RHVoice; both (RHVoice speaks, eSpeak NG for other languages) | see rule above (eSpeak NG when in doubt) | speech = yes | packages, section 5.4.1; Speech Dispatcher `DefaultModule "rhvoice"` / `"espeak-ng"` (option documented in `speechd.conf`). Speakup's console voice stays eSpeak NG (`espeakup` is eSpeak-only); the dialogue says so. `speechd-up` exists in Debian 13 as the bridge (not tried) | M |
| `rh_voice_pl` | D | RHVoice voice for Polish: / Głos RHVoice dla polskiego: | choice | Natan (public domain, the voice of Charon); Michał (public domain); Magda (CC BY 4.0); Alicja (CC BY 4.0); Cezary (non-commercial licence) | Natan | `synth` has RHVoice and Polish is a system or speech language | `/etc/RHVoice/RHVoice.conf` (`doc/en/Configuration-file.md` in the RHVoice tree); Arch: package `rhvoice-voice-<name>`; Debian/Ubuntu: `rhvoice-polish` carries the Polish voices of that version | M |
| `rh_voice_en` | D | RHVoice voice for English: / Głos RHVoice dla angielskiego: | choice | Slt (the English voice of Charon); Alan; Bdl; Clb | Slt | `synth` has RHVoice | as above (`rhvoice-english`, `rhvoice-voice-slt` …). Voices from RHVoice Lab (e.g. Evgeniy-Eng, Lyubov) are CC BY-NC-ND and are offered only with the licence spoken | M |
| `speech_lang` | D | Speech language: / Język mowy: | choice | same as the system language; the 9 languages | same | — | `LANGV` in `speech.sh:39` becomes an input: `DefaultLanguage` (`speech.sh:180, 381`), `default_voice` for espeakup (`:355-359`) | E |
| `rate` | D | Speech rate: / Tempo mowy: | number | −100…100 (spoken as percent) | reader's own default; "as in Charon" offered | — | `DefaultRate` in `speechd.conf` (documented −100…+100); Speakup `/sys/accessibility/speakup/soft/rate` through a udev rule or service (path unverified); Orca profile | E (speechd), M (Speakup, Orca) |
| `pitch` | D | Voice pitch: / Wysokość głosu: | number | −100…100 | default | — | `DefaultPitch` | E |
| `volume` | D | Speech volume: / Głośność mowy: | number | −100…100 | 100 (speechd's own) | — | `DefaultVolume` | E |
| `punct` | D | Punctuation spoken: / Wymawiana interpunkcja: | choice | some; none; all | some (reader default; Charon's level offered) | — | `DefaultPunctuationMode "none|some|all"` (documented); Speakup `punc_level` (unverified) | E |
| `caps` | D | Capital letters: / Wielkie litery: | choice | no signal; a sound; the word "capital" | no signal | — | `DefaultCapLetRecognition "none|icon|spell"` (documented) | E |
| `keyecho` | D | Echo while typing: / Echo podczas pisania: | choice | characters; words; characters and words; nothing | Orca default (characters) | desktop reader | Orca settings (`enableKeyEcho`, `enableEchoByWord` … in `user-settings.conf`; key names unverified) written to `/etc/skel` and the user's profile | M |
| `orca_layout` | D | Orca keyboard layout: / Układ klawiatury Orki: | choice | desktop (numeric keypad); laptop (Caps Lock) | desktop | Orca or Cthulhu | Orca setting `keyboardLayout` (unverified name) | M |
| `braille` | D | Braille display: / Monitor brajlowski: | choice | detect automatically (today: BRLTTY is installed with the desktop reader); none; choose the driver | automatic | — | `/etc/brltty.conf`: `braille-driver auto`, `braille-device usb:` / `bluetooth:` (documented directives); console-only systems: also install `brltty` (today only with a desktop reader, `speech.sh:147`) | E |
| `braille_table` | D | Braille table: / Tablica brajlowska: | choice | by language (automatic); pl; en_US; de … | automatic | `braille` ≠ none | `text-table auto|pl|…` (documented; `pl` is listed) | E |
| `braille_grade` | D | Contracted braille: / Skróty brajlowskie: | choice | none; the language's table | none | `braille` ≠ none | `contraction-table` (documented); Orca: liblouis table in its settings (unverified key) | M |
| `login_sound` | D | A sound when the sign-in screen or console is ready? / Dźwięk, gdy ekran logowania albo konsola są gotowe? | yes/no | yes | as today (`charon-tone` at the end of first start only) | — | a oneshot unit running `charon-tone` after `display-manager`/`getty` | E |
| `no_speech` | D | (not a question) Unchecking every screen reader | — | — | — | — | see section 7 | ⚠A |

##### 5.4.1 RHVoice on Linux (checked 2026-11-23)

| System | Packages | Source | Checked |
|---|---|---|---|
| Debian 13 | `rhvoice`, `speech-dispatcher-rhvoice`, `rhvoice-polish`, `rhvoice-english` (1.14.0-2) — component **non-free** | Debian archive (madison API) | yes |
| Ubuntu 26.04 | `rhvoice`, `speech-dispatcher-rhvoice`, `rhvoice-polish`, `rhvoice-english` | Ubuntu archive (madison); component (multiverse?) not read | yes (existence) |
| Arch | `rhvoice`, `rhvoice-language-polish`, `rhvoice-language-english`, `rhvoice-voice-natan`, `-michal`, `-magda`, `-alicja`, `-cezary`, `-slt`, `-alan`, `-bdl`, `-clb` (1.16.5 in `extra`, 1.18.5 in `extra-testing`) | archlinux.org package search | yes |
| Fedora | not in Fedora (mdapi: no `rhvoice`). RHVoice's own packaging page names the Copr `lostmemories/RHVoice`: `rhvoice`, `rhvoice-speech-dispatcher-plugin` | RHVoice `doc/en/Packaging-status.md` | Copr not checked |
| Any | snap `rhvoice` (the tree has a `snap/` folder); Speech Dispatcher is not registered automatically: add the module by hand, on Ubuntu `/snap/rhvoice/current/bin/sd_rhvoice` | same document | text only |
| Any | build from source: `doc/en/Compiling-on-Linux.md` | RHVoice tree | not built |

Consequences: Debian needs `non-free` in the sources (today only `non-free-firmware` is asked): choosing
RHVoice on Debian adds `d-i apt-setup/non-free boolean true` and says so. The Debian/Ubuntu version
(1.14) is older than Charon's (1.18.5): voice quality differs slightly and the newest voices are
missing. On Fedora RHVoice is offered only with the remark "from a community Copr repository".
The Speech Dispatcher module name is `rhvoice` (binary `sd_rhvoice`, `src/sd_module/CMakeLists.txt`);
the exact `AddModule "rhvoice" "sd_rhvoice" "rhvoice.conf"` line as shipped by each distribution was
not read (unverified; the packages normally install it).

Licences (RHVoice `doc/en/License.md`; zlotowicz.pl/synteza for Polish): engine LGPL 2.1+ (GPL 3+
with MAGE). Polish: **Natan and Michał CC0, Magda and Alicja CC BY 4.0, Cezary non-commercial /
no derivatives.** All voices from RHVoice Lab (rhvoice.su) are CC BY-NC-ND 4.0; Talgat and Natia have
their own non-commercial terms. Rule: voices with a non-commercial licence are never a default and
are hidden when the administration answer `org_use` (5.12) is yes.

#### Windows

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `w_synth` | D | NVDA's synthesizer: / Syntezator NVDA: | choice | eSpeak NG (built into NVDA); RHVoice; Windows OneCore voices; SAPI 5 voices | see rule above (NVDA's own choice when in doubt) | NVDA checked | `[speech] synth = …` in `nvda.ini` (key documented in NVDA's `configSpec.py`; Charon already writes this key for Setup, `src/windows/nvda.ini`). Driver names `espeak`, `oneCore`, `sapi5`, `RHVoice`: unverified spelling | M |
| `w_rh_voice_pl` / `w_rh_voice_en` | D | RHVoice voice for Polish / for English | choice | as on Linux | Natan / Slt | `w_synth` = RHVoice | section 5.4.2 | M |
| `w_rh_sapi` | D | Install RHVoice for other programs too (SAPI 5)? / Zainstalować RHVoice także dla innych programów (SAPI 5)? | yes/no | no | no | `w_synth` = RHVoice, or JAWS/Narrator-only set-ups that want RHVoice | section 5.4.2 | M |
| `w_rate`, `w_pitch`, `w_volume` | D | Speech rate / pitch / volume of NVDA | number | 0–100 | NVDA defaults; "as in Charon" offered | NVDA | per-synthesizer section `[speech] [[<synth>]] rate/pitch/volume` — these keys are created by the driver and are **not** in `configSpec.py`: unverified | M |
| `w_punct` | D | Punctuation and symbols: / Interpunkcja i symbole: | choice | some; none; most; all | some | NVDA | `[speech] symbolLevel = 0|100|200|300` (key documented, default 100) | E |
| `w_keyecho` | D | Echo while typing: / Echo podczas pisania: | choice | characters; words; both; nothing | characters (NVDA default) | NVDA | `[keyboard] speakTypedCharacters`, `speakTypedWords` (documented, 0–2) | E |
| `w_layout` | D | NVDA keyboard layout: / Układ klawiatury NVDA: | choice | desktop; laptop | desktop | NVDA | `[keyboard] keyboardLayout = desktop|laptop` (documented) | E |
| `w_modifier` | D | NVDA key: / Klawisz NVDA: | multi | numeric-keypad Insert; extended Insert; Caps Lock | both Inserts | NVDA | `[keyboard] NVDAModifierKeys` bit mask 1 Caps Lock, 2 keypad Insert, 4 extended Insert (documented, default 6) | E |
| `w_autolang` | D | Switch the voice language automatically with the text? / Przełączać język głosu automatycznie według tekstu? | yes/no | yes | yes | NVDA | `[speech] autoLanguageSwitching` (documented) | E |
| `w_braille` | D | Braille display: / Monitor brajlowski: | choice | detect automatically; none | automatic | NVDA | `[braille] display = auto|noBraille` (key documented; value `noBraille` unverified), `translationTable`, `inputTable` | E |
| `w_braille_table` | D | Braille table: / Tablica brajlowska: | choice | by NVDA's language; Polish grade 1; English … | automatic | `w_braille` ≠ none | `[braille] translationTable = auto|<file>` (file names unverified) | M |
| `w_ducking` | D | Lower other sounds while NVDA speaks? / Ściszać inne dźwięki, gdy NVDA mówi? | choice | no; while speaking; always | no | NVDA | `[audio] audioDuckingMode = 0|1|2` (key documented) | E |
| `w_nvda_update` | D | Let NVDA check for updates and add-on updates? / Pozwolić NVDA sprawdzać aktualizacje programu i dodatków? | yes/no | yes | yes | NVDA | `[update] autoCheck`, `[addonStore] automaticUpdates = notify|update|disabled` (documented) | E |
| `w_nvda_stats` | D | Send anonymous usage statistics to NV Access? / Wysyłać anonimowe statystyki użycia do NV Access? | yes/no | no | no | NVDA | `[update] allowUsageStats`, `askedAllowUsageStats = True` (documented) | E |
| `w_nvda_welcome` | D | Show NVDA's welcome dialog at its first start? / Pokazać okno powitalne NVDA przy pierwszym uruchomieniu? | yes/no | yes | yes | NVDA | `[general] showWelcomeDialogAtStartup` (documented) | E |
| `w_addons` | D | NVDA add-ons: / Dodatki NVDA: | multi | from the catalogue (section 9.4) | none | NVDA | `.nvda-addon` files placed for installation at NVDA's next start (mechanism unverified: NVDA has no documented silent add-on install switch) | H |
| `w_narrator_rate` | D | Narrator speech rate: / Tempo mowy Narratora: | number | 0–20 | Windows default | Narrator is the first reader | `HKCU\Software\Microsoft\Narrator` (value names unverified) | H |
| `w_sound_scheme` | D | (see 5.6) | | | | | | |

How NVDA settings are applied: `Charon-Readers.ps1` writes `%APPDATA%\nvda\nvda.ini` for the user
before NVDA's first start and copies it to NVDA's system configuration folder so the sign-in screen
uses the same voice (folder name and the need for elevation: unverified; NVDA does this itself from
"Use currently saved settings during sign-in"). JAWS: no settings are offered — its own start-up
wizard is spoken and its configuration format is not public.

##### 5.4.2 RHVoice on Windows (checked 2026-11-23)

- Not in winget (`winget search RHVoice`: nothing). Official sources only.
- NVDA engine add-on: `https://github.com/RHVoice/RHVoice/releases/download/1.18.2/RHVoice-1.18.201.nvda-addon`
  (address given on zlotowicz.pl/synteza: "install the engine first, then one or more voices").
- Voices and languages are separate packages (RHVoice `doc/en/Binaries.md`): NVDA
  `RHVoice-voice-<Language>-<Name>-<version>.nvda-addon`, SAPI 5
  `RHVoice-voice-<Language>-<Name>-v<version>-setup.exe`; hosts: `rhvoice.eu-central-1.linodeobjects.com`,
  GitHub releases of the voice's own repository, and for RHVoice Lab voices
  `https://rhvoice.su/downloads/?voice=<voice>&type=nvda|sapi&version=<version>`. An English language
  package (`RHVoice-language-English-…`) is needed only when no English voice is installed.
- The exact current file URLs of the five Polish voices (NVDA and SAPI 5) were **not** captured: the
  page that lists them was summarised without the links. They must be pinned (URL + SHA-256, as
  `nvdaUrl`/`nvdaSha256` at `CharonWsInstall2.h:206-208`) before implementation.
- Unattended switches of the SAPI 5 `-setup.exe` installers: unverified (the voice.info files carry
  `msi_upgrade_code` and `exe_upgrade_code`, so they are MSI-based; `/quiet` is likely, not confirmed).
- NVDA add-on store: RHVoice's presence there was not checked.

### 5.5 Desktop and sign-in (Linux; group `desk`)

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `session_default` | D | Which desktop starts by default? / Który pulpit uruchamia się domyślnie? | choice | the checked desktops | first checked | two or more desktops | AccountsService `Session=` for the user (`/var/lib/AccountsService/users/<user>`; unverified) or `~/.dmrc`/`.xinitrc` for `startx` | M |
| `dm` | D | Sign-in screen program: / Program ekranu logowania: | choice | the desktop's own (today); GDM; LightDM (speaks with Orca, set up by `speech.sh:199-201`); SDDM | desktop's own | graphical sign-in | Debian debconf `shared/default-x-display-manager` (unverified), `systemctl enable` elsewhere. SDDM: no spoken sign-in — ⚠A, warn | M ⚠A (SDDM) |
| `wayland` | D | Graphics: Wayland or X11? / Grafika: Wayland czy X11? | choice | distribution default; X11 (Orca features such as mouse review and some key handling work best here); Wayland | distribution default | GNOME or KDE | GDM `WaylandEnable=false` in `/etc/gdm3/daemon.conf` (Debian/Ubuntu) or `/etc/gdm/custom.conf` (unverified per release: recent GNOME drops the X11 session) | M |
| `flatpak` | D | Add Flatpak and the Flathub repository? / Dodać Flatpak i repozytorium Flathub? | yes/no | no | no | — | package `flatpak` (in Debian, Ubuntu, Arch, Fedora — checked) + `flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo` | E |
| `snap` | D | Snap packages: / Pakiety Snap: | choice | keep (Ubuntu default); remove snapd | keep | Ubuntu | `apt purge snapd` + pin. Firefox on Ubuntu is a snap: removing it needs another browser source — warn | M |
| `aur` | D | Helper for the Arch User Repository: / Pomocnik dla repozytorium użytkowników Archa (AUR): | choice | none; yay; paru | none | Arch | built as the user with `makepkg` (both exist in the AUR — checked; the Stormux repository Charon already adds for Fenrir also ships `yay`, `speech.sh:26-28`) | M |
| `release` | D | Release: / Wydanie: | choice | Debian: stable (today), testing. Ubuntu: LTS (today), newest interim | stable / LTS | Debian or Ubuntu installer | Debian: `d-i mirror/suite string testing` (documented) **and** the installer images from `dists/testing/…` (`CharonWsInstall.h:461, 466` are fixed to `stable`). Ubuntu: `ubuntuBase` (`CharonWsInstall2.h:20`) is a constant; an interim netboot directory must exist (not checked) | M |
| `components` | D | Package sources besides the free ones: / Źródła pakietów poza wolnymi: | multi | non-free firmware (today's `nonfree`); contrib; non-free | non-free firmware | Debian | `apt-setup/non-free-firmware`, `apt-setup/contrib`, `apt-setup/non-free` (all in the example preseed) | E |
| `u_mirror` | D | Ubuntu package server: / Serwer pakietów Ubuntu: | choice | automatic by country (today); main archive; type an address | automatic | Ubuntu | `apt: mirror-selection: primary: [country-mirror | {uri: …}]`, `geoip: true` (documented) | E |
| `u_codecs`, `u_drivers` | D | Install media codecs? / third-party drivers (graphics, Wi-Fi)? — Zainstalować kodeki multimedialne? / sterowniki producentów (grafika, Wi-Fi)? | yes/no | no / no | as today | Ubuntu | `codecs: {install: true}`, `drivers: {install: true}` (documented) | E |
| `kernel` | D | Kernel: / Jądro: | choice | default; newer hardware-enablement kernel (Ubuntu `hwe`); long-term (Arch `linux-lts`) | default | Ubuntu, Arch image | Ubuntu `kernel: {flavor: hwe}` (documented); Debian `base-installer/kernel/image` (documented); Arch: `pacman -S linux-lts` + boot entry | M |
| `audio_stack` | D | Sound system: / System dźwięku: | choice | distribution default; PipeWire; PulseAudio | distribution default | — | packages + user services; `AudioOutputMethod` in speechd.conf already set to `"pulse,alsa"` (`speech.sh:504-507`); `pipewire` is a documented value | M ⚠A (a wrong switch silences speech; audit by tone before finishing) |
| `upgrade_now` | D | Upgrade all packages during the installation? / Zaktualizować wszystkie pakiety podczas instalacji? | choice | no; safe upgrade; full upgrade | no | Debian (Ubuntu has `updates`) | `d-i pkgsel/upgrade select none|safe-upgrade|full-upgrade` (documented values); images: `package_upgrade: true` (documented) | E |
| `popcon` | D | Take part in Debian's package popularity survey? / Brać udział w ankiecie popularności pakietów Debiana? | yes/no | no | no (today) | Debian | `popularity-contest popularity-contest/participate boolean` (`CharonWsInstall.h:450`) | E |
| `console_font` | D | Large console font for low vision? / Duża czcionka konsoli dla słabowidzących? | yes/no | no | no | — | `console-setup` `FONTFACE`/`FONTSIZE` in `/etc/default/console-setup` (unverified values) | M |

Arch from the network and Fedora: see section 10.

### 5.6 Appearance and behaviour (group `look`)

Linux values are written to a system dconf database (`/etc/dconf/db/local.d/00-charon` +
`dconf update`) so they apply to every user and to the sign-in screen where it honours them; MATE,
Xfce and KDE get their own equivalents where they exist. The gsettings key names below are from
memory, not re-checked this session (unverified), except where noted.

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied (Linux) | Applied (Windows) | |
|---|---|---|---|---|---|---|---|---|---|
| `contrast` | D | High contrast? / Wysoki kontrast? | yes/no | no | no | desktop | `org.gnome.desktop.a11y.interface high-contrast` | a contrast theme for the default user (mechanism unverified) | M / H |
| `text_size` | D | Text size: / Rozmiar tekstu: | choice | 100 %; 125 %; 150 %; 200 % | 100 % | desktop | `org.gnome.desktop.interface text-scaling-factor` | `HKCU\Software\Microsoft\Accessibility\TextScaleFactor` (unverified) | M |
| `magnifier` | D | Start the screen magnifier at sign-in? / Uruchamiać lupę przy logowaniu? | yes/no | no | no | desktop | `org.gnome.desktop.a11y.applications screen-magnifier-enabled` | `Accessibility\Configuration` value `magnifierpane` (same value Charon sets to `narrator`, `CharonWsInstall2.h:403`; combined form unverified) | M |
| `sounds` | D | System sounds: / Dźwięki systemu: | choice | on (default); off | on | — | `org.gnome.desktop.sound event-sounds` | sound scheme `.None` for the default user (unverified) | M |
| `start_sound` | D | Play the Windows start-up sound? / Odtwarzać dźwięk startu Windows? | yes/no | Windows default | as today | W | — | `HKLM\…\Authentication\LogonUI\BootAnimation\DisableStartupSound` (unverified) | M |
| `dark` | D | Dark theme? / Ciemny motyw? | yes/no | no | no | desktop | `org.gnome.desktop.interface color-scheme` | `…\Themes\Personalize\AppsUseLightTheme` (unverified) | M |
| `numlock` | D | Num Lock at start: / Num Lock po starcie: | choice | off (desktop layouts of Orca and NVDA use the keypad for navigation); on | off | — | `numlockx` / dconf `remember-numlock-state` | `HKU\.DEFAULT\Control Panel\Keyboard\InitialKeyboardIndicators` (unverified) | M |
| `w_ext` | D | Show file name extensions? / Pokazywać rozszerzenia nazw plików? | yes/no | Windows default (hidden) | as today | W | — | `HideFileExt=0` in `…\Explorer\Advanced` of the default user; the same switch exists as Win11Debloat's `ShowKnownFileExt` (in its default settings, per the Windows research) | E |
| `w_hidden` | D | Show hidden files? / Pokazywać ukryte pliki? | yes/no | no | no | W | — | `Hidden=1` (unverified) | E |
| `w_menu` | D | Classic right-click menu in Windows 11? / Klasyczne menu kontekstowe w Windows 11? | yes/no | no | no | W11 | — | `HKCU\Software\Classes\CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\InprocServer32` empty default (unverified here; WinUtil's Advanced preset does the same: `RightClickMenu`) | E |
| `w_start_classic` | D | Classic Start menu (Open-Shell)? / Klasyczne menu Start (Open-Shell)? | yes/no | no | no | W | — | add-on from the Windows research §3.7: winget `Open-Shell.Open-Shell-Menu` (exists — checked). Only after a test with NVDA and Narrator; the stable build lacks the screen-reader fixes of the pre-release | M, blocked by the reader test |
| `w_taskbar` | D | Taskbar: / Pasek zadań: | multi | Start button on the left; hide Widgets; hide the search box; hide Chat/Copilot buttons | none changed | W11 | — | `TaskbarAl`, `TaskbarDa`, `SearchboxTaskbarMode` (unverified names) or Win11Debloat `-DisableWidgets -HideChat` (listed in the research) | M |
| `w_explorer_start` | D | File Explorer opens: / Eksplorator plików otwiera: | choice | Home (default); This PC | Home | W | — | `LaunchTo=1` (unverified) | E |
| `w_sticky` | D | Keep the "press Shift five times" Sticky Keys shortcut? / Zostawić skrót Klawiszy trwałych „pięć razy Shift"? | yes/no | yes | yes | W | — | `HKCU\Control Panel\Accessibility\StickyKeys\Flags` (unverified) | M |
| `shortcut_reader` | D | Keyboard shortcut that starts the screen reader: / Skrót klawiszowy uruchamiający czytnik ekranu: | choice | the system's own (Orca: Alt+Super+S; NVDA: Ctrl+Alt+N; Narrator: Ctrl+Win+Enter) | own | — | information only in batch 1; custom shortcuts later | — | — |

### 5.7 Network (group `net`)

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `wifi_keep` | D | Remember a Wi-Fi network in the new system? / Zapamiętać sieć Wi-Fi w nowym systemie? | yes/no | no | no | — | asks `wifi_ssid` (prefilled with the network Charon is connected to, when it is) and `wifi_psk` | M 🔒 |
| `wifi_ssid`, `wifi_psk` | D | Network name: / Password: — Nazwa sieci: / Hasło: | text / secret | — | — | `wifi_keep`=yes | L: NetworkManager keyfile `/etc/NetworkManager/system-connections/<ssid>.nmconnection`, mode 0600 (format unverified); Ubuntu: `network:` Netplan `wifis:` (the `network` key takes Netplan, documented). W: `netsh wlan add profile filename=… user=all` with a profile XML (unverified). Note: the Debian installer itself needs the cable or pre-seeded Wi-Fi (`netcfg/wireless_essid`, WPA keys: not in the official example — unverified) | M (installed system), H (installer over Wi-Fi) |
| `firewall` | D | Firewall: / Zapora sieciowa: | choice | distribution default (today); on, incoming blocked except what you enable here | default | L | `ufw` (Debian, Ubuntu, Arch — packages checked) `ufw default deny incoming; ufw allow ssh` when `ssh`=yes; Fedora `firewalld` (exists) | E |
| `ssh_key` | D | Public SSH key to accept (paste or pick a file): / Klucz publiczny SSH do zaakceptowania (wklej albo wskaż plik): | file / list | — | none | `ssh`=yes | Ubuntu `ssh: authorized-keys: […]` (documented); cloud-init `ssh_authorized_keys` under the user; Debian: late_command writes `~/.ssh/authorized_keys`. W: `C:\ProgramData\ssh\administrators_authorized_keys` (unverified) | E (L) |
| `ssh_pw` | D | Allow SSH sign-in with a password? / Pozwolić na logowanie SSH hasłem? | yes/no | yes when no key, else no | as Ubuntu's own rule | `ssh`=yes | Ubuntu `ssh: allow-pw` (documented default exactly this); images `ssh_pwauth` (exists); Debian `sshd_config.d` | E 🔒 |
| `w_ssh` | D | Install the OpenSSH server in Windows? / Zainstalować serwer OpenSSH w Windows? | yes/no | no | no | W | `Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0`; `Set-Service -Name sshd -StartupType 'Automatic'`; firewall rule `OpenSSH-Server-In-TCP` (Microsoft Learn) | E |
| `w_netprofile` | D | Treat the first network as private (the computer is visible to others)? / Traktować pierwszą sieć jako prywatną (komputer widoczny dla innych)? | yes/no | no (public) | no | W | `Set-NetConnectionProfile -NetworkCategory Private` at first logon (unverified) | E |

### 5.8 Software (group `soft`) — user's decisions of 2026-11-23

Presentation: **sets first** (one check-box list), and in the detailed mode each checked set can be
opened to tick single programs.

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `sets` | D | Software sets to install: / Zestawy programów do zainstalowania: | multi | Everyday; For blind users; System tools; Creating and programming | none | — | catalogue file (section 9): each set lists items; items with `default: yes` are installed | M |
| `set_<id>` | D | <set name>: which programs? / <nazwa zestawu>: które programy? | multi | the set's items; each line says source, licence and the accessibility note | the items marked default | the set is checked and the user presses Enter on it | per item, in source order | M |
| `sources` | D | Where programs may come from: / Skąd mogą pochodzić programy: | multi | the system's official repository / winget; Flatpak, AUR or Microsoft Store; installers from the authors' official sites; my own list file | official repository only | — | filters catalogue rows by `source`; turning on Flatpak or AUR implies `flatpak`=yes / asks `aur` | M |
| `own_list` | D | Your own list file: / Własny plik z listą: | file | — | none | `sources` has "own list" | format in section 9.3; default path `\EFI\HAK\answers\software.txt` | M |
| `soft_when` | D | Install programs: / Instalować programy: | choice | after the first start, with spoken progress (speech works first); during the installation | after the first start | any software | Linux: personal.sh after speech (the order rule of `firstboot.sh`); Debian/Ubuntu "during" = `pkgsel/include` / `packages:`. Windows: always first logon (winget) | E |
| `w_apps_remove` | D | Factory apps to remove: / Fabryczne aplikacje do usunięcia: | multi | the Slim list of `Charon-WindowsProfile.ps1` (`$SlimPackagePrefixes`), each as one check box; never offered: Narrator, voices, sound, Edge, Store framework, Quick Assist, Sound Recorder | none (profile decides) | W, profile `none` or `Slim` | new parameter `-Remove a,b,c` / `-Keep` of `Charon-WindowsProfile.ps1` (Slim becomes "this list, all checked") | M |
| `w_onedrive` | D | OneDrive: / OneDrive: | choice | keep; remove | keep | W | part of Slim today; separate switch | E |

### 5.9 Updates and privacy (group `upd`)

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `auto_updates` | D | Automatic updates: / Automatyczne aktualizacje: | choice | distribution default (today); security updates automatically; all updates automatically; only tell me | default | L | Debian/Ubuntu: package `unattended-upgrades` (exists; `dpkg-reconfigure unattended-upgrades`, `Unattended-Upgrade::Origins-Pattern` in `50unattended-upgrades` — Debian wiki; the `APT::Periodic` lines of `20auto-upgrades` were not on that page: unverified). Fedora: `dnf-automatic`, `/etc/dnf/automatic.conf` `[commands] upgrade_type = default|security`, `apply_updates`, timers `dnf-automatic-install.timer` / `-notifyonly.timer` (dnf documentation; the package name in Fedora 45 was not found by the index query — unverified). Arch: no automatic updates (rolling release; the dialogue says so) | M |
| `auto_reboot` | D | May the computer restart by itself after updates? / Czy komputer może sam się uruchomić ponownie po aktualizacjach? | yes/no | no | no | `auto_updates` automatic | `Unattended-Upgrade::Automatic-Reboot` (unverified here); dnf `reboot = never|when-needed` (documented) | E |
| `w_update` | D | Windows Update: / Windows Update: | choice | automatic (Windows default); tell me before downloading; download, tell me before installing; install at a set time; off | automatic | W | `HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU`: `AUOptions` 2 / 3 / 4 (+ `ScheduledInstallDay`, `ScheduledInstallTime`), `NoAutoUpdate=1` (Microsoft Learn, "Manage additional Windows Update settings") | E; "off" needs the risk confirmation (security, and drivers come from Windows Update) |
| `w_update_restart` | D | Never restart for updates while someone is signed in? / Nigdy nie restartować dla aktualizacji, gdy ktoś jest zalogowany? | yes/no | no | no | W | `NoAutoRebootWithLoggedOnUsers=1` (same page) | E |
| `w_telemetry` | D | Diagnostic data sent to Microsoft: / Dane diagnostyczne wysyłane do Microsoftu: | choice | required only (today when `privacy`=yes); optional too (Windows default) | follows `privacy` | W | `AllowTelemetry` 1 / 3 (`CharonWsInstall2.h:430`) | E |
| `w_ads` | D | Turn off: / Wyłączyć: | multi | suggested apps and "consumer features" (today); advertising ID; tips on the lock screen; Bing results in Start; Copilot; Recall; Click to Do | consumer features (when `privacy`=yes) | W | today's `DisableWindowsConsumerFeatures`; the others through Win11Debloat's documented switches `-DisableSuggestions -DisableLockscreenTips -DisableBing -DisableCopilot -DisableRecall -DisableClickToDo` (Windows research §3.1), run with `-Silent`. Never passed: `-ForceRemoveEdge`, `-DisableDriverUpdates`, `-DisableNotifications` | M |
| `w_location` | D | Location services: / Usługi lokalizacji: | choice | on; off (the clock's automatic time zone stops too) | on | W | policy value (unverified) or WinUtil `Location` tweak | M |
| `u_report` | D | Send Ubuntu's hardware report and crash reports? / Wysyłać raport o sprzęcie i raporty o awariach Ubuntu? | yes/no | Ubuntu default | as today | Ubuntu | `ubuntu-report`, `apport` settings (unverified) | M |

Windows profile and add-on questions are aligned with `work/charon-windows-profiles-research-2026-11-23.md`
(not re-researched here):

| id | Level | Change | Default | |
|---|---|---|---|---|
| `profile` | (exists) | list becomes: unchanged; slim; Win11Debloat default; **Win11Debloat privacy only (`DebloatLite`)**; **Win11Debloat AI off (`NoAI`)**; **WinUtil Standard (`WinUtil`)**; optionally WinUtil Minimal; ReviOS; AtlasOS ("24H2 and 25H2 only"); **Windows Server as a workstation (`ServerDesktop`, Server media only)** | none | E for the three script variants (argument table), M for WinUtil (pinned hash), M for ServerDesktop (own code) |
| `profile_url` | D | "Address of your own script (https):" for the `Script` profile — the dialogue cannot pass `-ScriptUrl` today | Win11Debloat | E |
| `waddons` | D | new multi question after the profile: classic Start menu (Open-Shell); hardened, Basic preset (Harden System Security); O&O ShutUp10++ recommended settings; (Sophia Script: only as a script the user runs later) | none | M each; Open-Shell and Harden are blocked by a reader test |
| unofficial image | D | Tiny11 25H2 (NTDEV) as an extra entry of the version list, spoken warning + typed word UNOFFICIAL / NIEOFICJALNY (`ConfirmUnofficial`), SHA-256 pinned, Charon's answer file replaces the image's | not offered in express | H (untested with any reader) |

### 5.10 Power and hardware (group `hw`)

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `sleep_ac` | D | On mains power, sleep after: / Na zasilaniu sieciowym usypiaj po: | choice | system default; never; 15 min; 30 min; 1 h; 2 h | default | — | W: `powercfg /change standby-timeout-ac N` (unverified syntax). L: GNOME `org.gnome.settings-daemon.plugins.power sleep-inactive-ac-timeout`; console: `logind.conf` `IdleAction` (unverified) | M |
| `sleep_dc` | D | On battery, sleep after: / Na baterii usypiaj po: | choice | as above | default | a battery is present (Charon can see one through UEFI? unverified — else always asked) | as above, `-dc` | M |
| `lid` | D | Closing the laptop lid: / Zamknięcie pokrywy laptopa: | choice | system default; sleep; do nothing (keep listening with the lid closed); hibernate; shut down | default | — | L: `HandleLidSwitch=` in `/etc/systemd/logind.conf.d/charon.conf` (unverified). W: `powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION n` (unverified) | M |
| `display_off` | D | Turn the display off after: / Wyłączaj ekran po: | choice | system default; 1 min (saves battery when you do not look at it); never | default | — | `powercfg /change monitor-timeout-ac`; GNOME `org.gnome.desktop.session idle-delay` | M |
| `power_plan` | D | Power plan: / Plan zasilania: | choice | balanced; best performance; power saver | balanced | W | `powercfg /setactive SCHEME_BALANCED|SCHEME_MIN|SCHEME_MAX` (unverified aliases) | E |
| `fast_startup` | D | Fast startup (must be off when another system uses the same disk): / Szybkie uruchamianie (musi być wyłączone, gdy z tego dysku korzysta inny system): | choice | Windows default; off | Windows default | W | `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power\HiberbootEnabled=0` (unverified) or `powercfg /hibernate off`. Help text recommends "off" when `clockutc`=yes | E |
| `tlp` | D | Laptop battery saving (TLP)? / Oszczędzanie baterii laptopa (TLP)? | yes/no | no | no | L | package `tlp` (Debian, Ubuntu, Arch, Fedora — checked), `systemctl enable tlp` | E |
| `bluetooth` | D | Bluetooth: / Bluetooth: | choice | system default; on at start; off | default | — | L: `bluez` + `AutoEnable=` in `/etc/bluetooth/main.conf` (unverified). W: left alone in batch 1 | M |
| `printer` | D | Printing: / Drukowanie: | choice | system default; install printing and find network printers automatically; none | default | L | `cups` (+ `cups-browsed`), `sane-utils` for `scanner` | E |
| `sound_dev` | D | Sound device for speech: / Urządzenie dźwiękowe dla mowy: | choice | the system's default (today); ask me with tones at the first start | default | — | "ask with tones": personal.sh / Charon-Personal.ps1 plays a rising tone on each output in turn and waits for Enter on the one that is heard (new; `charon-tone` and the unmute service of `speech.sh:515` exist) | M |
| `start_volume` | D | Volume at first start, percent: / Głośność przy pierwszym uruchomieniu, w procentach: | number | 20–100 | as today (unmuted, card default) | — | `amixer`/`wpctl set-volume`; W: left at Windows default (no documented command; unverified) | M |
| `w_features` | D | Optional Windows features: / Opcjonalne funkcje Windows: | multi | Windows Subsystem for Linux; Hyper-V; .NET Framework 3.5; Windows Sandbox; OpenSSH server (= `w_ssh`) | none | W | `dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart` and `…:VirtualMachinePlatform …` (Microsoft Learn, WSL manual install); feature names `Microsoft-Hyper-V-All`, `NetFx3`, `Containers-DisposableClientVM`: unverified | E (WSL, SSH), M (others) |
| `w_drivers` | D | Drivers: / Sterowniki: | choice | from Windows Update (today); also from the folder `charon\drivers` on the media | Windows Update | W | `pnputil /add-driver <folder>\*.inf /subdirs /install` at first logon, or `Microsoft-Windows-PnpCustomizationsWinPE` `DriverPaths` in the answer file (both unverified); needed for sound or network cards Windows has no driver for | M |
| `w_key` | D | Windows product key (empty: none): / Klucz produktu Windows (puste: brak): | text | 25 characters | empty | W | `<ProductKey><Key>` (`CharonWsInstall2.h:390`), overriding `edition` | E 🔒 |
| `w_index` | D | Which edition from the media? / Które wydanie z nośnika? | choice | names read from the image (`install.wim` XML data) | as today | W, media holds several editions | `/IMAGE/INDEX` (`CharonWsInstall2.h:386`); today only the count is read (`WindowsImageCount`), names need the WIM XML | M |

### 5.11 Remote help (group `remote`)

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `help_tool` | D | Remote help from a person you trust: / Zdalna pomoc od zaufanej osoby: | multi | Linux: SSH server (= `ssh`); `tmate` terminal sharing on demand. Windows: NVDA's remote access (part of NVDA; presence in 2026.3 unverified); Quick Assist (kept by every Charon profile); TeamTalk 5 (winget `BearWare.dk.TeamTalk5` — checked) | none | — | packages; nothing listens until the user starts it, except SSH | E–M |
| `help_note` | D | (information) Nothing here opens the computer to the Internet: each tool needs you to start it or to give a key. | — | | | | spoken once | — |


### 5.12 Administration layer (level A; asked only after the administration gate)

For IT staff, schools and offices. All defaults are "not configured" (= today's behaviour). Every
row is also a key of the answer-set file, so the usual way to use this layer is section 6.3, not
answering 80 questions by voice.

#### A1. Organisation, accounts, directory (group `adm_id`)

| id | EN / PL | Type | Choices | Default | Applied | |
|---|---|---|---|---|---|---|
| `org_use` | Is this computer set up by an organisation (school, office)? / Czy ten komputer przygotowuje organizacja (szkoła, biuro)? | yes/no | no | no | hides voices and programs whose licence forbids non-personal or commercial use; turns on the written report | E |
| `name_scheme` | Computer name pattern, for example LAB-{serial} or PC-{mac6}-{n}: / Wzorzec nazwy komputera, na przykład LAB-{serial} albo PC-{mac6}-{n}: | text | tokens `{serial}`, `{asset}`, `{mac6}`, `{n}` | none (use `hostname`) | expanded in Charon from SMBIOS (system serial number, asset tag) and the first adapter's MAC; result is `hostname` (15 characters for Windows, validated) | M |
| `asset_tag` | Asset tag to record: / Numer inwentarzowy do zapisania: | text | — | SMBIOS asset tag, if any | written to the report, `/etc/machine-info` (`PRETTY_HOSTNAME`/comment; unverified field) and Windows `RegisteredOwner`/computer description | E |
| `owner`, `organisation` | Registered owner / organisation | text | — | none | W: `RegisteredOwner`, `RegisteredOrganization` in Microsoft-Windows-Shell-Setup (element names from the component reference; the child pages were not opened: unverified); CharonDOS 9x: `setup_name`, `setup_org` (`pc_config.cpp:200-201`) | E |
| `users` | More accounts: / Kolejne konta: | list of records | per account: name, full name, role (administrator / standard), password or "must set at first sign-in", extra groups | none | W: one `<LocalAccount>` each with `<Group>Administrators` or `Users`. Debian: `useradd` in `late_command` (the installer makes one user). Ubuntu: `user-data: users:` (the `user-data` key merges cloud-init data; documented). Images: more `users:` entries | M 🔒 |
| `pw_policy` | Password rules: / Zasady haseł: | multi | minimum length N; must change at first sign-in; expires after N days; lock after N wrong attempts | none | L: `chage -d 0`, `chage -M`, `libpam-pwquality` `minlen`, `pam_faillock` (unverified option names). W: `net accounts /minpwlen:N /maxpwage:N /lockoutthreshold:N`, "must change" per account (unverified) | M |
| `dir_join` | Join a directory: / Dołączyć do katalogu: | choice | none; Active Directory domain; Microsoft Entra ID; FreeIPA; LDAP with SSSD | none | see the four rows below | — |
| `ad_domain`, `ad_user`, `ad_pw`, `ad_ou` | Domain, account allowed to join, its password, organisational unit | text / secret | — | — | W: component `Microsoft-Windows-UnattendedJoin`, `<Identification>` with `<JoinDomain>`, `<Credentials><Domain/><Username/><Password/>`, `<MachineObjectOU>`, pass **specialize** (Microsoft Learn; needs the network in that pass and Pro/Enterprise/Education). Ubuntu installer: `active-directory: {admin-name, domain-name}` (documented; the password is prompted at run time, so not unattended). Other Linux: `realm join -U <user> <domain>` with packages `realmd`, `sssd-tools`, `adcli` (packages exist in Debian/Ubuntu/Fedora — checked; command options unverified) | H 🔒 (a join password in a file: use a delegated low-privilege join account or an offline join blob; see section 8) |
| `entra` | Entra ID join | — | — | — | no answer-file setting exists; only a provisioning package made by the organisation (`.ppkg` on the stick, applied at first start) or Autopilot — unverified, **H**, "bring your own package" only | H |
| `ipa_*`, `ldap_*` | FreeIPA server, realm, enrolment one-time password; LDAP URI and base | text / secret | — | — | `ipa-client-install --unattended …` / SSSD configuration; kickstart has `realm join` (not reached in the fetched document). Unverified | H 🔒 |
| `first_msg` | Message shown and spoken at first sign-in: / Komunikat pokazany i odczytany przy pierwszym logowaniu: | text | — | none | L: `/etc/motd` + an autostart entry that speaks it with `spd-say` once per user. W: `Charon-Personal.ps1` speaks it with System.Speech (as the other Charon scripts do) and writes `legalnoticecaption`/`legalnoticetext` when "before sign-in" is chosen (unverified value names) | E |

#### A2. Network for managed sites (group `adm_net`)

| id | EN / PL | Type | Default | Applied | |
|---|---|---|---|---|---|
| `ip_mode` | Address: automatic (DHCP) or fixed? / Adres: automatyczny (DHCP) czy stały? | choice | DHCP | — | — |
| `ip_addr`, `ip_mask`, `ip_gw`, `ip_dns`, `dns_search` | Address, mask, gateway, DNS servers, search domain | address | — | Debian: `d-i netcfg/disable_autoconfig boolean true`, `netcfg/get_ipaddress`, `get_netmask`, `get_gateway`, `get_nameservers`, `confirm_static` (installation guide, appendix B.4). Ubuntu: `network:` in Netplan form (documented). Images: cloud-init network config (not read). W: `netsh interface ip set address` at first logon, or the `Microsoft-Windows-TCPIP` component (unverified) | M |
| `proxy` | Proxy address: / Adres serwera proxy: | address | none | Debian `d-i mirror/http/proxy string` (written empty today, `CharonWsInstall.h:414`); Ubuntu `proxy:` (documented: used for the install, apt and snapd); cloud-init `apt: proxy` (documented); W: `netsh winhttp set proxy` (unverified). Charon's own downloads need the proxy too (new setting in Charon's network code) | M |
| `vlan` | VLAN number: / Numer VLAN: | number | none | Netplan `vlans:`; NetworkManager; W: adapter-driver specific — not offered for Windows | H |
| `dot1x`, `wifi_ent` | Wired 802.1X / enterprise Wi-Fi (identity, method, CA certificate, password or client certificate) | records | none | NetworkManager `802-1x` settings / Netplan `auth:`; W: `netsh lan|wlan add profile` with an XML made by the organisation. Charon's own pre-boot network does not do 802.1X: the installation itself needs an open port or the cable | H 🔒 |
| `ntp_server` | Time server: / Serwer czasu: | address | distribution/Windows default | Debian `d-i clock-setup/ntp-server string` (example preseed); cloud-init `ntp: {servers: […]}` (documented); kickstart `timesource --ntp-server` (documented); W: `w32tm /config /manualpeerlist:… /syncfromflags:manual /update` (unverified) | E |

#### A3. Updates, repositories, software lists (group `adm_upd`)

| id | EN / PL | Default | Applied | |
|---|---|---|---|---|
| `repo_mirror` | Internal package mirror: / Wewnętrzny serwer pakietów: | none | Debian `mirror/http/hostname` + `directory` (exists as `mirror`); Ubuntu `apt: mirror-selection: primary: [{uri}]`; images `apt: primary: [{arches, uri}]` (documented) | E |
| `repo_extra` | Extra repository and its key: / Dodatkowe repozytorium i jego klucz: | none | Debian `d-i apt-setup/local0/repository string …` and `apt-setup/local0/key string <url>` (appendix B.4); cloud-init `apt: sources:` (documented); Fedora `repo --name --baseurl` (kickstart) / a `.repo` file; Arch: stanza in `pacman.conf` (as `speech.sh` does for Stormux) | M |
| `maint_window` | Update day and hour: / Dzień i godzina aktualizacji: | none | W: `AUOptions=4`, `ScheduledInstallDay` 0–7, `ScheduledInstallTime` 0–23 (documented). L: systemd timer override for `apt-daily-upgrade.timer` / `dnf-automatic-install.timer` (dnf timer documented; apt timer name unverified) | M |
| `wsus` | WSUS server address: / Adres serwera WSUS: | none | `HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate` `WUServer`, `WUStatusServer`, and `AU\UseWUServer=1` (documented) | E |
| `wufb_defer` | Defer feature / quality updates by N days | none | Windows Update for Business policy values (unverified names) | M |
| `no_wu_drivers` | Exclude drivers from Windows Update | no | policy `ExcludeWUDriversInQualityUpdate` (named on the Microsoft page). Warning: Charon relies on Windows Update for sound and network drivers | E ⚠A-adjacent |
| `soft_file` | Software list file for all machines: / Plik z listą programów dla wszystkich komputerów: | none | section 9.3 (the same format as the user's list) | M |
| `kms`, `vl_key` | KMS host and volume licence key: / Serwer KMS i klucz licencji zbiorczej: | none | `cscript //B slmgr.vbs /ipk <key>`, `/skms <host[:port]>`, `/ato` (Microsoft Learn, slmgr options); `/cpky` clears the key from the registry afterwards | E 🔒 |
| `ubuntu_pro` | Ubuntu Pro token | none | `ubuntu-pro: {token}` (documented) | E 🔒 |

#### A4. Encryption with recovery (group `adm_enc`)

| id | EN / PL | Default | Applied | |
|---|---|---|---|---|
| `bl_on` | Turn BitLocker on: / Włączyć BitLocker: | no | `manage-bde -on C: -usedspaceonly` (unverified switches), protectors `manage-bde -protectors -add C: -tpm` and `-recoverypassword` (documented) | H |
| `bl_escrow` | Where the recovery key goes: / Dokąd trafia klucz odzyskiwania: | — | `manage-bde -protectors -adbackup C: -id {…}` (Active Directory) or `-aadbackup` (Entra ID) — both documented; or a file on a named share. **Never** left only on the encrypted disk; Charon refuses `bl_on` without an escrow target that was confirmed written | H 🔒 ⚠A (BitLocker recovery screen; Charon reads it only when its reader add-on is in the firmware) |
| `luks_tpm` | Unlock the Linux disk with the TPM (no passphrase prompt): / Odblokowywać dysk Linuksa przez TPM (bez pytania o hasło): | no | `systemd-cryptenroll --tpm2-device=auto` after installation (unverified); Ubuntu: `layout: name: hybrid` with `encrypted: yes` (TPM-backed, documented as existing). Removes the silent passphrase prompt — the accessible way to encrypt | H |
| `luks_escrow` | Where the recovery passphrase goes | — | written to the report file on a share / printed in the report on the stick (never on the encrypted disk) | H 🔒 |

#### A5. Remote management and configuration bootstrap (group `adm_mgmt`)

| id | EN / PL | Default | Applied | |
|---|---|---|---|---|
| `ssh_keys_admin` | Administrator SSH keys (file): | none | as `ssh_key`, for a named admin account; `ssh_pw` forced to no | E |
| `winrm` | Windows Remote Management: | off | `Enable-PSRemoting -Force` (unverified switch set) | M 🔒 |
| `rdp` | Remote Desktop: | off | `HKLM\SYSTEM\CurrentControlSet\Control\Terminal Server\fDenyTSConnections=0` + firewall group (unverified); Pro and above. Note: a remote session uses the remote reader's audio only with audio redirection | M 🔒 |
| `ansible_pull` | Ansible pull: repository and playbook | none | cloud-init `ansible: {install_method, package_name, pull: [{url, playbook_names}]}` (documented) on images; other Linux: package `ansible` (exists everywhere checked) + a oneshot `ansible-pull -U <url> <playbook>` | M |
| `puppet`, `salt`, `landscape` | Agent bootstrap | none | cloud-init `puppet:`, `landscape: {client: …}` (documented); salt not read | M |
| `mdm` | Intune / MDM enrolment | none | provisioning package supplied by the organisation; no setting of Charon's own (unverified) | H |
| `phone_home` | Report "installed" to a URL: | none | cloud-init `phone_home: {url, post, tries}` (documented) on images; elsewhere `curl` from personal.sh / `Invoke-RestMethod`; payload = the report of section 6.5 without secrets | E |

#### A6. Lock-down, shared and exam machines (group `adm_lock`)

| id | EN / PL | Default | Applied | |
|---|---|---|---|---|
| `dconf_lock` | Lock the settings chosen here so users cannot change them (Linux): | no | `/etc/dconf/db/local.d/locks/charon` listing the keys written in 5.6 (unverified layout) | M |
| `gpo_file` | Policy file to import (Windows): `.reg` or LGPO backup on the stick | none | `reg import <file>` at specialize or first logon. `LGPO.exe` is a separate Microsoft download (unverified) | M 🔒 |
| `kiosk` | Kiosk / single-program mode: | no | W: Assigned Access (unverified); L: a dedicated session. **Must keep the reader and its hot key** | H ⚠A |
| `shared_pc` | Shared computer: guest account that is wiped at sign-out | no | W: Shared PC mode (unverified); L: `systemd-homed`/tmpfs home (unverified) | H |
| `exam` | Exam mode: no network, no USB storage, fixed program list | no | firewall deny-all + `usb-storage` blacklist / `HKLM\SYSTEM\CurrentControlSet\Services\USBSTOR\Start=4` (unverified). **Never blocks USB audio or braille devices** (only storage) | H ⚠A |
| `uac` | UAC level | Windows default | policy values (unverified); "never notify" is not offered | M |

#### A7. Logging, backup, printers, certificates (group `adm_ops`)

| id | EN / PL | Default | Applied | |
|---|---|---|---|---|
| `syslog` | Send logs to: / Wysyłać dzienniki do: | none | `rsyslog` forwarding rule `*.* @@host:514` (cloud-init `rsyslog: remotes` exists but was not on the fetched part: unverified); W: event forwarding (unverified) | M |
| `telemetry_dest` | Windows diagnostic data level 0 (Enterprise/Education only) | — | `AllowTelemetry=0` | E |
| `backup_agent` | Backup agent to install and its server | none | catalogue item (`borgbackup`, `Duplicati.Duplicati` — both checked) + a config file supplied by the organisation | M 🔒 |
| `printers` | Printers to add (name, address, driverless or driver) | none | L: `lpadmin -p NAME -E -v ipp://… -m everywhere` (unverified). W: `Add-Printer`/`Add-PrinterPort` (unverified) | M |
| `root_ca` | Certificates to trust (files): | none | cloud-init `ca_certs: {trusted: […]}` (documented); Debian/Ubuntu `/usr/local/share/ca-certificates/*.crt` + `update-ca-certificates`; Fedora `update-ca-trust`; W: `certutil -addstore -f Root <file>` (unverified) | E |
| `reader_all_users` | Apply the speech and braille settings of 5.4 to every account and the sign-in screen: | yes for accounts made by Charon | L: `/etc/skel` Orca/Cthulhu profile + system `speechd.conf`, `brltty.conf`; W: `nvda.ini` in the default profile and NVDA's system configuration | M |
| `reader_cfg_file` | Ready reader configuration to deploy (NVDA configuration folder, Orca `user-settings.conf`, Cthulhu settings, add-ons) from the stick | none | copied as files; replaces the per-question settings | E |
| `post_script` | Run my script after the installation: / Uruchomić mój skrypt po instalacji: | none | choice: file on the stick (`\EFI\HAK\answers\post\*.sh` or `*.ps1`) or URL (https only) + SHA-256. Linux: copied to `/var/lib/charon/post/`, run as root by personal.sh last, output in the report; Debian/Ubuntu may also run it inside the installer (`late_command`/`late-commands`). Windows: last FirstLogonCommand. The script's text is spoken as "script NAME, N lines, SHA-256 ends with …" in the summary | M 🔒 (runs as administrator: only files from the stick the administrator made, or pinned hashes) |

### 5.13 CharonDOS (group `dos`)

CharonDOS gets the mode question; `quick` = today's dialogue (section 2.7), unchanged.

| id | Level | EN / PL | Type | Choices | Default | Asked if | Applied | |
|---|---|---|---|---|---|---|---|---|
| `dos_size` | D | Size of the CharonDOS partition in megabytes: / Rozmiar partycji CharonDOS w megabajtach: | number | computed minimum … free space | computed (today, `CharonWsDosSystem.h:486-487`) | — | `request.sizeBytes` `:527` | E |
| `dos_first` | D | What starts first? / Co uruchamia się pierwsze? | choice | CharonDOS (today); Charon's menu; the system that is first now | CharonDOS | — | `PutOption(first=…)` `:551-553` | E |
| `dos_lang` | D | Language of CharonDOS: / Język CharonDOS: | choice | Charon's language (today); English; Polish | Charon's | — | load option `pl` (`:551`; `src/dos/backend_uefi.cpp:915-916`) | E |
| `dos_sets` | D | Program sets: / Zestawy programów: | multi | editors; files and folders; text viewing and help; tools; archives; programming; games | all (today's "all the tested programs") | — | `dosapps::Fetch` per group; today one single choice (`CharonWsDosApps.h:89-95`) | E |
| `dos_set_<g>` | D | <set>: which programs? | multi | the catalogue's programs of the group (38 in all, `src/dos/APPS-CATALOG.txt`) | all | a set is opened | filter in `Fetch` | E |
| `dos_autoexec` | D | Start-up file: / Plik startowy: | multi | add every program folder to PATH; say a greeting; show the date; start the file manager Doszip; run my commands (typed) | none (no AUTOEXEC.BAT today) | — | Charon writes `C:\AUTOEXEC.BAT`; CharonDOS already runs it at start (`src/dos/shell.cpp:926`) | E |
| `dos_prompt` | D | Prompt: / Znak zachęty: | choice | folder and > (`$P$G`, today); drive and > (`$N$G`); type your own | `$P$G` | — | `PROMPT` line in AUTOEXEC.BAT (`shell.cpp:739-741`) | E |
| `dos_speech` | D | Speech in CharonDOS: / Mowa w CharonDOS: | choice | as in Charon (today: it is Charon's own reader); set rate, pitch, volume and punctuation now | as in Charon | — | Charon's settings record (the fields the `CHARON RATE/PITCH/VOLUME/PUNCT/VOICE/LANG` commands change, `src/dos/CHARON-COMMANDS.txt`); these are global to Charon, the dialogue says so | E |
| `dos_cp` | D | Code page for Polish letters: / Strona kodowa dla polskich liter: | choice | 437 with nearest letters (today, `src/dos/util.cpp:143`); CP852; Mazovia | 437 | — | **new emulator work**: video and keyboard tables in `src/dos`, and Charon's reader must read the chosen code page as Unicode | H |
| `dos_kbd` | D | Keyboard: / Klawiatura: | choice | US (today); Polish programmer's (right Alt + letter) | US | `dos_cp` ≠ 437 | new emulator work; current support not established (unverified) | H |
| `pc_memory` | D | Memory of the Windows profile in megabytes: / Pamięć profilu Windows w megabajtach: | number | 16–2048 | 128 (XP 512) (`:381`) | a Windows profile | `memory=` (`pc_config.cpp:170`; range 1–2048 per `src/pc/PLAN.txt:51`) | E |
| `pc_cpu` | D | Emulated processor: / Emulowany procesor: | choice | by profile (today); i486dx4; pentium; pentium_mmx; p2_klamath; p3_katmai; p4_willamette; amd_k6_2_chomper; athlon_xp | by profile | a profile | `cpu=` (`pc_config.cpp:164-167`) | E |
| `pc_disk` | D | Size of the Windows disk in megabytes: / Rozmiar dysku Windows w megabajtach: | number | 512–4095 | by profile 1024/2048/2048/4095 | installing from a disc | `disk_create_mb=` (`:393`; max 4095) and the partition size | E |
| `pc_sound` | D | Sound card: / Karta dźwiękowa: | choice | Sound Blaster 16 (today); none | sb16 | a profile | `sound=` (`:384`) | E ⚠A: JAWS speaks through it; "none" with a JAWS profile = silence |
| `pc_synth` | D | How the Windows screen reader speaks (virtual synthesizer): / Jak mówi czytnik ekranu w Windows (wirtualny syntezator): | choice | automatic by reader (today: JAWS → sound card, others → Braille 'n Speak on COM1); COM1; none | automatic | 9x profile | `com1=bns|none` (`:383`; `pc_config.cpp:327-333`) | E ⚠A |
| `pc_net` | D | Network card: / Karta sieciowa: | choice | NE2000 (today; not active yet in CharonPC stage 1); none | ne2000 | a profile | `net=` (`:385`; ignored, `pc_config.cpp:344`) | E (no effect yet — say so) |
| `pc_org` | D | Organisation name for Windows Setup: / Nazwa organizacji dla instalatora Windows: | text | — | empty | 9x from a disc | `setup_org=` (read by `pc_config.cpp:201`, never written today) | E |
| `pc_zone` | D | Time zone for Windows Setup: / Strefa czasowa dla instalatora Windows: | choice | Central European when Charon speaks Polish, else Setup's default (today, `:402`); list | as today | 9x from a disc | `setup_zone=` (zone names as Windows 98 spells them: list to be taken from the media, unverified) | M |
| `pc_reader_lead` | D | Let the Windows reader speak first and Charon's reader stay quiet in Windows? | yes/no | CharonPC default | as today | a profile with a reader | `reader_lead=` (`pc_config.cpp:197`) | E |
| `pc_log` | D | Write an emulator log (for fault reports)? / Zapisywać dziennik emulatora (do zgłoszeń błędów)? | yes/no | no | no | a profile | `log=CHARONPC.LOG` | E |
| `pc_speed` | D | Emulation speed: / Szybkość emulacji: | choice | automatic (today); fixed instructions per second | automatic | a profile | `ips=`, `sync=` (PLAN.txt: a value above what the computer reaches makes sound run fast) | M ⚠A-adjacent (wrong value cuts speech) |
| `pc_disk2` | D | Second disk image (your data): / Drugi obraz dysku (twoje dane): | file | — | none | a profile | `disk2=`, `disk2_format=` (`pc_config.cpp:176-179`) | E |
| `dos_win11_boot` | D | With Windows 11 alongside: which starts first? | choice | CharonDOS, Windows through WINNT (today); Windows Boot Manager; Charon's menu | CharonDOS | Windows 11 alongside | `dosdefault` → `CharonWsInstall2.h:475` | E |

In a multi-system plan the start-up rows above give way to the multi-boot spec's questions and to
the CharonDOS commands `winnt` and `linux` (section 6.6). Existing questions kept as they are: profile, reader (with the "all English" warning), media, key,
name, computer, place, disk, Windows 11 alongside. Gaps noticed while reading (not questions, but
they limit the profile questions): `src/pc/PLAN.txt:129-130` says Windows 95 CDs are not bootable and
need a boot floppy image (`floppy=`), which the dialogue never asks for; unattended Setup
(`AskSetup`) covers 9x only, not XP.

### 5.14 Count of proposed questions

Counted from the table rows above (a row with two ids such as `wifi_ssid`, `wifi_psk` counts once;
information-only rows are not counted; one `set_<id>` row stands for every set).

| Group | New rows | Of these for Linux | For Windows |
|---|---|---|---|
| 5.1 Language, keyboard, time | 9 | 8 | 7 |
| 5.2 Accounts and sign-in | 8 | 6 | 4 |
| 5.3 Disk, encryption, boot | 15 | 12 | 6 |
| 5.4 Speech and braille | 15 (Linux) + 17 (Windows) | 15 | 17 |
| 5.5 Desktop and sign-in | 15 | 15 | — |
| 5.6 Appearance and behaviour | 14 | 6 | 14 |
| 5.7 Network | 7 | 5 | 5 |
| 5.8 Software | 7 | 5 | 6 |
| 5.9 Updates, privacy, Windows profile and add-ons | 11 | 3 | 8 |
| 5.10 Power and hardware | 15 | 9 | 11 |
| 5.11 Remote help | 1 | 1 | 1 |
| **Personal (level D)** | **134** | **85** | **79** |
| 5.12 Administration A1–A7 (level A) | 51 | about 45 | about 45 |
| 5.13 CharonDOS | 23 | — | — |

| System | Asked today (table + conditional) | Proposed new, personal | Proposed new, administration |
|---|---|---|---|
| Linux (Debian; Ubuntu and the images a few fewer) | 13 + 3 | 85 | about 45 |
| Windows | 12 + 1 (+ 4 when media is downloaded) | 79 | about 45 |
| CharonDOS | 10 | 23 | 2 (`owner`/`organisation`; no script hook) |

All 208 new rows have `quick=false`; the express flow asks none of them. The numbers will move with
the decisions of section 12.

---

## 6. Answer sets, unattended deployment, report

### 6.1 File format (the base format; shared with the multi-boot plan)

`\EFI\HAK\answers\<name>.ini` — UTF-8, CRLF or LF, `key=value`, `#` or `;` comments, sections in
brackets. Chosen because Charon already parses and writes this shape (`CHARONPC.CFG`), PowerShell 5.1
and POSIX `sh` read it without a parser library, and a blind administrator can edit it in any editor.

This is the base format that `work/charon-multiboot-spec-2026-11-23.md` (its section 7) assumed: its
sections `[plan]`, `[disk.N]`, `[shared]` and `[system.<id>]` are adopted **unchanged**. A
single-system answer set is simply a plan with one `[system.<id>]`; there is no second syntax. This
spec owns the header, `[secrets]`, `[machine …]`, the value rules and the keys of sections 2 and 5;
the multi-boot spec owns `[plan]`, `[disk.N]` and the keys `size_gb`, `swap_gb`, `winre`, `disk`,
`data*`, `rest`, `default`, `timeout`, `on_failure`, `leftover`.

```ini
[charon-answers]                  ; header (this spec)
format=1                          ; newer than this Charon knows: refused, not guessed
made=2026-11-23 charon-r409
catalogue=charon.ini,site.ini     ; software catalogue files the ids below refer to (section 9)

[plan]                            ; multi-boot spec, section 7; one system = one entry in systems=
version=1
name=school-lab
systems=debian
mode=detailed                     ; quick = every key not written takes its default
unattended=yes                    ; section 6.3
confirm=typed                     ; typed | disk-match (section 6.3)
report=esp,systems

[disk.1]                          ; multi-boot spec: pre-authorisation by serial
serial=S4EWNX0M123456
existing=erase

[shared]                          ; ids of 2.2 and of the groups lang, acct, speech, look, net, soft,
locale=pl_PL                      ; upd, hw, remote and the administration groups — asked once,
user=uczen                        ; copied to every system they apply to
readers=orca,speakup
synth=rhvoice
rh_voice_pl=natan
sets=everyday,blind
set_blind=orca,brltty,ocrfeeder,daisy-player
auto_updates=security

[system.debian]                   ; ids of that system; a shared id repeated here overrides [shared]
kind=debian                       ; debian | ubuntu | image-debian | image-ubuntu | image-arch |
desktop=mate-desktop              ; image-fedora | windows | charondos
layout=home

[secrets]                         ; section 8: by default every value is "ask"
password=ask                      ; Linux: stored only as password_hash=$5$… (multi-boot name kept)
password.windows=enc:v1:...       ; encrypted with a passphrase asked at load time
wifi_psk=ask

[machine serial=PF2ABCDE]         ; per-machine overrides (section 6.4): any key as section.key
shared.hostname=lab-07
shared.asset_tag=INW/2026/0412
disk.1.serial=S4EWNX0M999999

[machine mac=3c:52:82:aa:bb:cc]
shared.hostname=lab-08
```

Key-name alignment with the multi-boot spec (one name per thing; the multi-boot name wins where it
exists): `hostpattern` (its `shared.hostpattern`) is the same value as this spec's `name_scheme` and
takes the same tokens; `password_hash`; `[disk.N] esp_size_mb` replaces `esp_size`; `[plan] default`
and `timeout` replace `boot_first` whenever a plan has more than one system; `[plan] data`,
`data_size_gb` replace `data_part`. Per-system overrides in the dialogue (`sys.N.<key>`) are written
into `[system.<id>]`.

Rules: unknown key → reported and ignored (never fatal, never applied blindly); unknown value for a
choice → the default, reported; the whole file is validated before anything changes and every error
is spoken (the multi-boot rule); a key whose question is ⚠A → the file must also carry
`<key>.confirmed=yes`, otherwise Charon asks the risk confirmation by voice even when unattended.

### 6.2 Export and import

- Export = "save these answers as a set" in the summary (4.2.7), or from a finished install's report.
- Import = mode `saved`; a set can also be picked from any mounted volume (file browser), so a set
  travels on a second stick or comes from `https://` (admin: `answers-url.txt` holding one address;
  the same idea as `windows-iso-url.txt`).
- `tools/charon-install/charon-answers.py check FILE` on the host (and the same code in the OS
  companion programs, `tools/charon-os`) validates a file against `charon_questions.py` and prints
  the summary the installer would speak. This is also the unit test of the format.

### 6.3 Unattended mode

- Triggered only by a file with `[plan] unattended=yes` on Charon's own volume **and** the user
  (or administrator) choosing the mode entry `auto`, or pressing nothing for 30 seconds when
  `[plan] autostart=yes` is also set (for imaging rooms). Spoken first: "Unattended installation
  from the file school-lab. Press Escape within 30 seconds to stop."
- No question is asked. Then one of two disk rules:
  1. default — the plan is spoken and the **typed word is still required** (one human action per
     machine: the existing safety rule);
  2. `confirm=disk-match` with a `[disk.N]` section (`serial=`, optional `model=`, `min_size_gb=`): Charon
     proceeds without typing **only** if exactly one connected disk has that serial; any mismatch,
     duplicate or missing serial → it falls back to rule 1. A serial is the only accepted
     pre-authorisation; "the largest disk", a model name or an index are refused. A list of serials
     (one per machine section) is allowed.
- **Difference to settle (decision 5):** the multi-boot spec's section 7 makes the disk match the
  only confirmation of an unattended plan ("no disk section, no run"); this spec keeps the typed word
  as the default and the disk match as an explicit opt-in (`confirm=disk-match`). Both read the same
  `[disk.N]` section, so only the default differs.
- Needs disk serial numbers in `CharonWsDisks.h` (NVMe identify / ATA identify / USB descriptor through
  the UEFI pass-through protocols); what is available today was not checked — until it exists,
  rule 2 cannot be offered. The serial also fixes two existing limits: Ubuntu's largest/smallest
  rule (`storage: layout: match: {serial: …}` is documented) and the "two disks of the same size" stop.
- Existing guards stay: one internal disk for Windows, RAM for Ubuntu, `ConfirmSilent`, network
  checks. A guard that fails stops the run with a spoken and written reason; it never falls through
  to a different disk mode.

### 6.4 Per-machine overrides

`[machine serial=…]`, `[machine mac=…]`, `[machine uuid=…]` sections are applied on top of `[shared]` and `[system.<id>]` (keys written as `section.key`)
when the SMBIOS system serial number, a network adapter's MAC or the SMBIOS UUID matches. First match
in file order wins and is named in the report. Tokens (`{serial}`, `{mac6}`, `{asset}`, `{n}` from
`counter.txt` beside the file) work in any text value.

### 6.5 Report of what was applied

- Spoken at the end of each stage: "Applied 41 settings; 2 could not be applied: RHVoice voice Magda
  (package not found), firewall (no network)". Details on the screen for the review keys.
- Written: by Charon before it starts the installer — `\EFI\HAK\answers\reports\<hostname>-<date>.txt`
  (the plan: every answer, default or not, secrets as "set"); by the installed system when its
  scripts finish — `/var/lib/charon/install-report.txt` / `%ProgramData%\Charon\install-report.txt`
  with one line per key: `key=value → applied | failed: reason | not applicable`. The Linux and
  Windows scripts already log (`speech.log`, `firstboot.log`, `readers.log`); the report is their
  machine-readable summary. Optional `phone_home`.
- The audit script of 3.4 reads this report and re-checks each line from outside.

### 6.6 Several systems in one run: what this spec leaves to the multi-boot spec

`work/charon-multiboot-spec-2026-11-23.md` owns every question about a plan of several systems
(`plan.kind`, `plan.preset`, `plan.list`, `plan.mode`, `plan.disk`, `plan.spread`, `plan.existing`,
`plan.shrink`, `plan.data*`, `plan.rest`, `plan.default`, `plan.timeout`, `plan.dosdefault.*`,
`sys.N.size`, `sys.N.swap`, `sys.N.winre`, `shared.same`, `shared.hostpattern`). They are not
repeated here. How the two fit:

- Its "per system" step asks **this** spec's detailed tables (section 5), prefixed "System 2 of 3,
  Debian."; its `shared.*` answers are this spec's `[shared]` section. In a plan the groups `lang`,
  `acct`, `speech`, `look`, `net`, `soft`, `upd`, `hw`, `remote` and the administration groups are
  asked once and applied to every system that has the setting; the group list (4.2) shows a third
  state "differs per system" with a per-system override.
- User decisions for plans (made directly by the user, relayed 2026-11-23) and their effect here:
  1. **Without CharonDOS, Charon's own menu is the first boot entry, with a default system and a
     timeout.** So in a plan `boot_first` (5.3) is not asked; `plan.default`/`plan.timeout` replace
     it. `boot_first` keeps today's default only for a single-system install. `grub_wait` still
     applies to a Linux's own menu when it is reached.
  2. **Two Windows share one boot store.** No question here; `w_bitlocker`, `fast_startup`,
     `hibernate` and `clockutc` are forced by the plan (its section 2.7: UTC clock, fast start-up and
     hibernation off, BitLocker prevention) and are shown in the group list as "set by the plan",
     not asked.
  3. **The Windows answer file with the password waits on the EFI partition and is erased right
     after its step.** Section 8 is written to that rule: the same lifetime applies to
     `answers.ini`, to any `[secrets]` value Charon had to keep for a later step, and to
     `\EFI\HAK\plan\`; the report records "answer file erased" per step.
  4. **No shared data partition by default; the question appears only in the detailed mode.**
     This spec's `data_part` (a second Windows partition D:) is therefore offered only for a
     single-system Windows install; in a plan the multi-boot shared-partition question (detailed
     mode only, default no) is the only one. Note: the multi-boot spec's table 6.4 still marks
     `plan.data` as asked in the normal flow ("N"); by this decision it must be "D".
  5. **CharonDOS has the commands `linux` and `winnt`.** The CharonDOS rows `dos_first` and
     `dos_win11_boot` (5.13) apply to a single CharonDOS install; in a plan `plan.default` and
     `plan.dosdefault.winnt/.linux` replace them. `dos_autoexec` may offer "start Windows (`WINNT`)" /
     "start Linux (`LINUX`)" as a start-up action.
- Software sets (5.8, section 9) are one shared answer; each system resolves the ids against its own
  sources, and an item that exists for only one of the systems is installed only there (reported).
- A plan's report is the per-step concatenation of the reports of 6.5.

---

## 7. Answers that can make the system inaccessible

Existing rule: an entry with `Speech::none` needs `ConfirmSilent()` (yes/no, spoken reason). Extend
it to answers, with one function `ConfirmRisk(reasonPl, reasonEn)` in `CharonWsAsk.h`: spoken reason,
then **typed word** `ROZUMIEM` / `I UNDERSTAND` (Enter alone cannot pass it — the same reasoning as
the research's `ConfirmUnofficial`). The risk is repeated in the summary. In an answer file it needs
`<key>.confirmed=yes`.

| Trigger | Why | Extra safety net |
|---|---|---|
| Linux: no screen reader checked (today `speech`=no silently; only a note for Windows) | the installed system is silent | offer "keep Speakup anyway" |
| Linux: no desktop and no console reader; or a desktop with only console readers and graphical sign-in | nothing speaks where the user lands | force `login`=console or add Orca |
| `dm`=SDDM, or any sign-in program without a reader | silent sign-in screen | `autologin` suggestion |
| `encrypt`=yes without `luks_tpm` | silent passphrase prompt at every start | a start-up tone before the prompt (`GRUB_INIT_TUNE`), `boot_text`=yes, and the summary says "after the tone, type the passphrase and Enter" |
| `bl_on` (BitLocker) | recovery screen outside Windows | only with escrow; Charon's reader add-on must be in the firmware |
| `audio_stack` changed; `sound_dev`; `pc_sound`=none; `pc_synth`=none; `pc_speed` fixed | speech may lose its output | the scripts play `charon-tone` and check that the reader's process is alive after the change; on failure they revert |
| Windows: no reader checked (`wreaders` empty → `speech`=no) | Setup and Windows are silent | today not confirmed at all — add the confirmation |
| `w_update`=off, `no_wu_drivers` | no drivers for sound cards that need them | warning only |
| `kiosk`, `exam`, `gpo_file`, `post_script`, `dconf_lock`, add-ons Open-Shell / Harden | may remove the reader's hot key, block its helper programs or the USB braille display | reader start is re-checked after each; the Windows scripts' `Protect-Speech`/`Compare-SpeechState` already do this for profiles |
| unofficial Windows image; Fedora image; Haiku | no confirmed speech | existing `ConfirmSilent` / new `ConfirmUnofficial` |
| `pw_none`, `autologin` with remote access on | not an accessibility risk: security | refused, not confirmed |

---

## 8. Security-sensitive answers

User decision for multi-system plans (2026-11-23): the Windows answer file with the password waits
on the EFI partition and is erased right after its step; the rules below follow it (see 6.6).

Facts today: the Windows password is written **in clear text** into `Autounattend.xml` on the media
(`CharonWsInstall2.h:453, 460`; the user is told to delete the file); Linux passwords are SHA-256-crypt
hashes; the answers are wiped from memory after the installer starts (`ask::Clear`, zeroing at
`CharonWsInstall.h:475, 488`).

Rules for the new material:

1. **Default: no secret is saved in an answer set.** `[secrets]` keys are written as `ask`; on load
   they are asked. This covers `password`, `root_pw`, `admin_pw`, `encrypt_pw`, `wifi_psk`, `w_key`,
   `vl_key`, `ad_pw`, `ubuntu_pro`, `ipa`/`ldap` passwords.
2. If the user chooses "save passwords too": Linux account passwords are stored **only as the hash**
   the installer needs anyway. Values that must be clear text at use (Windows account password,
   Wi-Fi key, domain-join password, disk passphrase, product keys) are stored encrypted
   (`enc:v1:` = AES-256-GCM, key from a passphrase by PBKDF2; `CharonCrypt.h` already has SHA-256 and
   the crypt code — AES and PBKDF2 availability there was not checked) and the passphrase is asked
   at load. An unattended run therefore still needs one typed passphrase per machine, or the
   administrator accepts rule 3.
3. Clear-text secrets in a file are accepted only with `[plan] allow-plain-secrets=yes`, are
   announced at every load ("this file holds 3 passwords in clear text"), and the file is deleted
   from the stick after a successful run when `wipe-after=yes`. The EFI system partition is readable
   by every operating system on the computer and by anyone holding the stick: say this once.
4. Answer sets and reports never leave Charon's own volume by themselves: `answers` is excluded from
   the copies of `\EFI\HAK` (section 4.3).
5. In the installed system: `answers.env` is 0600 root and has its secret lines removed when
   `personal.sh` finishes; `answers.ini` on the Windows media carries no secret that is not already
   in `Autounattend.xml`, and `Charon-Personal.ps1` deletes `%ProgramData%\Charon\answers.ini` at the
   end. Domain-join credentials should be a delegated account that can only join computers to one OU;
   Microsoft's `UnattendedJoin` has no way to avoid the password in the file other than the
   `Provisioning` child of `Identification` (offline join data; its own child names were not read:
   unverified) — prefer that.
6. Scripts and software from outside: https only; a file from a URL must carry a SHA-256 in the
   catalogue or list (as `Get-Download` in the Windows scripts and `dosapps::Install` already
   enforce); a `post_script` from the stick is trusted because the stick is.
7. `sudo_nopw`, `autologin`, `pw_none`, `winrm`, `rdp`, `ssh_pw`: each is repeated in the summary
   under "Security".


---

## 9. Software catalogue

### 9.1 A data file, not code

- Built-in copy: `tools/charon-install/catalogue/*.ini` in the tree, shipped as
  `\EFI\HAK\catalogue\charon.ini` on Charon's volume. Charon reads the file at run time (the same
  small `key=value` reader as answer sets), so **sets change without rebuilding Charon**: replace the
  file, or let Charon fetch a newer one over https (pinned by a signature or at least a SHA-256 list
  published with the release; mechanism to be decided, section 12).
- An administrator adds or overrides with `\EFI\HAK\catalogue\site.ini` (same format; later files
  win; `hide=yes` removes a built-in item).
- The installed system does the installing: Charon passes only the chosen item ids (`sets`,
  `set_<id>`) plus the catalogue file itself (copied next to `answers.env` / `answers.ini`);
  `personal.sh` and `Charon-Personal.ps1` resolve an id to the package name for *that* system.
- CharonDOS keeps its generated `CharonDosCatalog.h` (each program was run in the emulator and is
  pinned by SHA-256, `src/dos/APPS-CATALOG.txt`); its sets map onto the same four categories (9.6).

### 9.2 Catalogue format

```ini
[catalogue]
format=1
date=2026-11-23

[set everyday]
name.en=Everyday
name.pl=Na co dzień
help.en=Browser, mail, office suite, players, messengers.
help.pl=Przeglądarka, poczta, pakiet biurowy, odtwarzacze, komunikatory.

[item firefox]
set=everyday
name.en=Firefox, web browser
name.pl=Firefox, przeglądarka internetowa
default=yes                    ; ticked when its set is checked
licence=MPL-2.0                ; free text; "commercial, 60-day evaluation" etc.
free=yes                       ; yes | no (trial/commercial) | noncommercial
a11y=good                      ; good | usable | poor | unknown
a11y.en=Works with Orca and NVDA.
a11y.pl=Działa z Orką i NVDA.
systems=linux,windows          ; linux | windows | debian | ubuntu | arch | fedora (narrowing allowed)
apt=firefox-esr                ; Debian
apt.ubuntu=firefox             ; a more specific key wins
dnf=firefox
pacman=firefox
aur=
flatpak=org.mozilla.firefox
winget=Mozilla.Firefox
msstore=
url.windows=                   ; installer from the author's official site
url.windows.sha256=
url.windows.args=/S            ; silent switches
url.linux=
needs=                         ; other item ids
nvda-addon=                    ; id in NV Access's add-on store
```

Rules applied by the scripts: source order = `sources` answer (repository first; then Flatpak / AUR /
Store; then `url.*`); an item with no id for an allowed source is reported "not available here", not
replaced by something else; `url.*` without a SHA-256 is refused unless `allow-unpinned=yes` is in
the answer file; `a11y=poor|unknown` items are never `default=yes` and their line is spoken with the
note first ("poorly accessible: …"); `free=noncommercial` items are hidden when `org_use`=yes.

### 9.3 The user's or administrator's own list file

Plain text, one entry per line, `#` comments. Default name `\EFI\HAK\answers\software.txt`.

```
# catalogue ids
firefox
thunderbird
# explicit sources
apt: sox ffmpeg
dnf: sox
pacman: sox
aur: emacspeak
flatpak: org.mozilla.Thunderbird
winget: Notepad++.Notepad++
msstore: 9NKSQGP7F2NH
nvda-addon: speechHistory
# a file from the author's site: URL, optional checks and switches
https://example.org/Setup.exe sha256=<64 hex> args="/S" name="Example"
# a local file on the stick, relative to this list
file: installers\MyTool.msi args="/qn"
```

A line whose prefix does not belong to the system being installed is skipped (reported as "for
another system"), so one list serves a mixed fleet.

### 9.4 Catalogue for Linux and Windows (ids checked 2026-11-23)

How the ids were checked: Debian 13 through the archive's madison API; Ubuntu through its madison
service (present in 26.04 "resolute" unless noted); Arch through archlinux.org package search; AUR
through its RPC; Fedora rawhide through mdapi; Flathub through its appstream API; winget with
`winget show --id … -e --source winget` on this host (winget 1.29). "—" = not in that source.
The accessibility column is general community knowledge, **not tested in this project**; every
"good" should be confirmed in the batch trial before the item stays `default=yes`.
D = default item of its set.

**Everyday**

| Item | Debian / Ubuntu | Fedora | Arch (AUR) | Flatpak | winget | Licence | Accessibility |
|---|---|---|---|---|---|---|---|
| Firefox D | `firefox-esr` / `firefox` | `firefox` | `firefox` | `org.mozilla.firefox` | `Mozilla.Firefox` | MPL-2.0 | good (Orca, NVDA, JAWS) |
| Thunderbird D | `thunderbird` | `thunderbird` | `thunderbird` | `org.mozilla.Thunderbird` | `Mozilla.Thunderbird` | MPL-2.0 | good; NVDA add-on `thunderbirdPlusG5` |
| LibreOffice D | `libreoffice` | `libreoffice` | `libreoffice-fresh` | `org.libreoffice.LibreOffice` | `TheDocumentFoundation.LibreOffice` | MPL-2.0 | usable to good |
| VLC D (Windows) | `vlc` | `vlc` | `vlc` | `org.videolan.VLC` | `VideoLAN.VLC` | GPL-2.0 | usable; NVDA add-on `VLC` |
| mpv D (Linux) | `mpv` | `mpv` | `mpv` | `io.mpv.Mpv` | — (not queried) | GPL | good: keyboard only, no window to read |
| foobar2000 D (Windows) | — | — | — | — | `PeterPawlowski.foobar2000` | freeware | good |
| Chromium / Chrome | `chromium` / — (snap only) | `chromium` | `chromium` | `org.chromium.Chromium`, `com.google.Chrome` | `Google.Chrome` | BSD / freeware | good |
| Element (Matrix) | — | — | `element-desktop` | `im.riot.Riot` | `Element.Element` | AGPL-3.0 | usable |
| Signal | — | — | `signal-desktop` | `org.signal.Signal` | `OpenWhisperSystems.Signal` | AGPL-3.0 | usable |
| Zoom | — | — | — | `us.zoom.Zoom` | `Zoom.Zoom` | proprietary, free use | good; NVDA add-on `zoomEnhancements` |
| Telegram Desktop | — / old releases only | — | `telegram-desktop` | `org.telegram.desktop` | `Telegram.TelegramDesktop` | GPL-3.0 | **poor** (self-drawn interface): never default. On Windows the accessible client is Unigram (Store; not in the winget source — id not checked), NVDA add-on `unigram` |
| Pidgin | `pidgin` | `pidgin` | (AUR `pidgin`) | — | — (not queried) | GPL | usable |
| Console set for systems without a desktop: `lynx`, `w3m`, `edbrowse` (Debian/Ubuntu), `neomutt`, `irssi`, `weechat`, `cmus` (not in Fedora), `yt-dlp` | present in Debian, Ubuntu, Arch, Fedora except as noted | | | | | free | good with Speakup/Fenrir |

**For blind users**

| Item | Debian / Ubuntu | Fedora | Arch (AUR) | Flatpak | winget / other | Licence | Note |
|---|---|---|---|---|---|---|---|
| Orca | `orca` | `orca` | `orca` | — | — | LGPL | the desktop reader (question `readers`) |
| Fenrir | `fenrir` | — | AUR `fenrir` / Stormux repository | — | — | LGPL | console reader |
| espeakup | `espeakup` | — | `espeakup` | — | — | GPL | (confirms "Fedora has no console speech package") |
| eSpeak NG | `espeak-ng` | `espeak-ng` | `espeak-ng` | — | `eSpeak-NG.eSpeak-NG` | GPL-3.0 | SAPI 5 voice on Windows |
| RHVoice | section 5.4.1 | Copr only | `rhvoice` + voices | — | not in winget: section 5.4.2; NVDA add-on store id `RHVoice` (1.18.201, GPL v3) | LGPL/GPL; voices vary | |
| NVDA | — | — | — | — | `NVAccess.NVDA` | GPL-2 | Charon installs it itself (pinned file) |
| JAWS | — | — | — | — | `FreedomScientific.JAWS.2026` | proprietary; 40-minute mode | Charon installs it itself (`Charon-Readers.ps1`) |
| BRLTTY, liblouis | `brltty`, `liblouis-bin` | `brltty`, `liblouis-utils` | `brltty`, `liblouis` | — | — | LGPL | braille |
| Tesseract OCR + Polish | `tesseract-ocr`, `tesseract-ocr-pol` | `tesseract`, `tesseract-langpack-pol` | `tesseract`, `tesseract-data-pol` | — | `UB-Mannheim.TesseractOCR`; NVDA add-on `tesseractOCR` | Apache-2.0 | |
| OCRFeeder | — (Debian 13) / `ocrfeeder` | — | `ocrfeeder` | — | — | GPL | the GNOME OCR front end usable with Orca |
| OCRmyPDF | `ocrmypdf` | `ocrmypdf` | AUR `ocrmypdf` | — | — | MPL-2.0 | command line |
| eBook-speaker, DAISY player | `ebook-speaker`, `daisy-player` | — | — | — | — | GPL | console readers for EPUB and DAISY, made for blind users |
| Thorium Reader | — | — | — | (id not found on Flathub) | `EDRLab.Thorium` | BSD-3 | EPUB and DAISY, designed for screen readers. D on Windows |
| Calibre | `calibre` | `calibre` | `calibre` | `com.calibre_ebook.calibre` | `calibre.calibre` | GPL-3.0 | **poor** with Orca, limited with NVDA: not default (useful for conversion from the command line) |
| TeamTalk 5 | from bearware.dk (address not pinned) | — | AUR `teamtalk` | — | `BearWare.dk.TeamTalk5` | proprietary, free use (licence text not read) | good; made with blind users in mind |
| Emacspeak | `emacspeak` | `emacspeak` | AUR `emacspeak` | — | — | GPL | question `readers` |
| NVDA add-ons (store ids, all GPL v2, URL and SHA-256 come from `https://addonstore.nvaccess.org/en/stable/latest.json`) | | | | | `speechHistory`, `clipspeak`, `wintenApps` (Windows App Essentials), `resourceMonitor`, `Mozilla`, `thunderbirdPlusG5`, `VLC`, `zoomEnhancements`, `BrailleExtender`, `tesseractOCR`, `LION`, `IndentNav`, `instantTranslate`, `placeMarkers`, `clock`, `unigram`, `remote` (Remote Support 2.6.4), `polskieDyktowanie` | | several list NVDA 2026.1 as last tested while Charon installs 2026.3: compatibility per add-on must be read from the store data at install time |
| Text adventures and console games | `frotz`, `tintin++`, `crawl`, `nethack-console`, `bsdgames` | `frotz`, `tintin`, `crawl`, `nethack` | `nethack`; AUR `tintin`, `crawl`, frotz variants | — | `Mudlet.Mudlet` (GPL-2.0; MUD client usable with NVDA) | free | good in the console. Audio games for Windows are mostly from their authors' sites: only through the user's own list |

**System tools**

| Item | Debian / Ubuntu | Fedora | Arch | Flatpak | winget | Licence | Accessibility |
|---|---|---|---|---|---|---|---|
| Backup: Déjà Dup D (desktop) | `deja-dup` | `deja-dup` | `deja-dup` | `org.gnome.DejaDup` | — | GPL | good (GTK) |
| Backup: BorgBackup, rsync D (console) | `borgbackup`, `rsync` | `borgbackup`, `rsync` | `borg`, `rsync` | — | — | BSD / GPL | good (command line) |
| Backup: Timeshift | `timeshift` | `timeshift` | `timeshift` | — | — | GPL | usable |
| Backup (Windows): Duplicati | — | — | — | — | `Duplicati.Duplicati` | MIT | usable (web interface). FreeFileSync: no winget id found |
| Disks: parted, gdisk D | `parted`, `gdisk` | `parted`, `gdisk` | `parted`, `gptfdisk` | — | (Windows: built-in `diskpart`) | GPL | good (command line) |
| Disks: GParted | `gparted` | `gparted` | `gparted` | — | — | GPL | **poor/uncertain**: runs as root, which Orca often cannot read. Not default |
| Disk usage (Windows): WinDirStat | — | — | — | — | `WinDirStat.WinDirStat` | GPL-3.0 | usable |
| Archives: 7-Zip D | `7zip` (`p7zip-full` also present) | `7zip` | `7zip` | — | `7zip.7zip` | LGPL-2.1 | good |
| Monitor: htop D | `htop` | `htop` | `htop` | — | `Microsoft.Sysinternals.ProcessExplorer` (proprietary, free) | GPL | good; `btop` exists everywhere but draws graphs: poor |
| File manager: Midnight Commander D (console) | `mc` | `mc` | `mc` | — | — | GPL | usable with a console reader |
| File manager (Windows): Total Commander | — | — | — | — | `Ghisler.TotalCommander` | **shareware** (not free) | good; not default |
| Network: nmap, curl, wget, OpenSSH client D | `nmap`, `curl`, `wget`, `openssh-client` | `nmap`, `curl`, `wget`, `openssh-clients` | `nmap`, `curl`, `wget`, `openssh` | — | `WinSCP.WinSCP` (GPL-3.0, good), `PuTTY.PuTTY` (MIT; its terminal reads poorly — prefer Windows' own `ssh`) | free | |
| Terminal: tmux, screen | `tmux`, `screen` | same | same | — | `Microsoft.WindowsTerminal` D, `Microsoft.PowerShell` (both MIT) | free | good |
| PowerToys (Windows) | — | — | — | — | `Microsoft.PowerToys` | MIT | usable |
| Drivers | Linux: firmware packages (questions `components`, `u_drivers`); Windows: Windows Update or `w_drivers` — no catalogue items | | | | | | |

**Creating and programming**

| Item | Debian / Ubuntu | Fedora | Arch (AUR) | Flatpak | winget | Licence | Accessibility |
|---|---|---|---|---|---|---|---|
| Visual Studio Code D | — (Microsoft's own apt repository: "author's site" source) | — (Microsoft's yum repository) | `code` (open-source build); AUR `visual-studio-code-bin` | `com.visualstudio.code`, `com.vscodium.codium` | `Microsoft.VisualStudioCode` | Microsoft licence, free; OSS build MIT | good (Orca, NVDA) |
| Notepad++ D (Windows) | — | — | — | — | `Notepad++.Notepad++` | GPL-3.0 | good |
| Git D | `git` | `git` | `git` | — | `Git.Git` | GPL-2.0 | good |
| Python D | `python3`, `python3-pip`, `python3-venv` | `python3`, `python3-pip` | `python`, `python-pip` | — | `Python.Python.3.14` (also `.3.13`) | PSF-2.0 | good |
| Compilers D | `build-essential` | `gcc` | `base-devel` | — | `MSYS2.MSYS2` (BSD-3); `Microsoft.VisualStudio.2022.BuildTools` (proprietary, licence conditions) | free | good |
| Emacs | `emacs` | `emacs` | `emacs` | — | `GNU.Emacs` | GPL | good with Emacspeak / Speechd-el |
| Audio, command line: SoX, FFmpeg D | `sox`, `ffmpeg` | `sox` (ffmpeg not queried) | `sox`, `ffmpeg` | — | (not queried) | free | good |
| Audacity | `audacity` | `audacity` | `audacity` | `org.audacityteam.Audacity` | `Audacity.Audacity` | GPL-3.0 | Windows: good, D there. Linux: **poor** with Orca — not default |
| REAPER | — | — | `reaper` | `fm.reaper.Reaper` | `Cockos.REAPER` | **commercial**; evaluation period, then a paid licence (terms not re-read) | Windows: good with the OSARA extension (author's site; address not pinned). Linux: **poor** (no OSARA). Never default |
| Virtual machines, console D (Linux) | `qemu-system-x86` | `qemu-kvm` | `qemu-full` | — | — | GPL | good (command line) |
| virt-manager, GNOME Boxes | `virt-manager`, `gnome-boxes` | `virt-manager`, `gnome-boxes` | `virt-manager` | `org.gnome.Boxes` | — | GPL | usable (the guest's screen is not readable, as in any VM) |
| VirtualBox | — (Debian 13) / `virtualbox` | — | `virtualbox` | — | `Oracle.VirtualBox` | GPL-3.0 | usable (Qt). VMware Workstation: no winget id found |
| Pandoc, yt-dlp | `pandoc`, `yt-dlp` | `yt-dlp` (pandoc not found by the query) | `pandoc-cli`, `yt-dlp` | — | `JohnMacFarlane.Pandoc`, `yt-dlp.yt-dlp` | free | good |

Left out on purpose, with the reason spoken if the user searches for them: Telegram Desktop, Calibre,
GParted, btop, Audacity and REAPER on Linux (accessibility); Total Commander, REAPER, Visual Studio
Build Tools as defaults (licence); anything that removes Edge or the Store.

### 9.5 Where sources apply

| Source | Debian | Ubuntu | Arch image | Fedora image | Windows |
|---|---|---|---|---|---|
| Official repository | apt | apt (some programs only as snaps) | pacman | dnf | winget, source `winget` |
| Second channel | Flatpak (Flathub) | Flatpak, snap | AUR (needs a helper, question `aur`), Flatpak | Flatpak | Microsoft Store through `winget --source msstore` (needs the Store; not on LTSC/Server) |
| Author's site | deb/tarball + SHA-256 | same | tarball | rpm | installer + SHA-256 + silent switches |
| Own list | yes | yes | yes | yes | yes |

winget at first logon: the client must be registered for the new account before the first command;
whether it is immediately usable in `FirstLogonCommands` on a fresh install was not tested
(unverified) — `Charon-Personal.ps1` must wait for it and say so, and fall back to "at the next
sign-in". Documented switches used: `winget install --id <Id> -e --source winget --silent
--accept-package-agreements --accept-source-agreements --disable-interactivity` (Microsoft Learn).

### 9.6 CharonDOS: the same four categories

CharonDOS installs only programs that were run in its emulator and passed (38 today,
`src/dos/APPS-CATALOG.txt`; FreeDOS 1.4 repository on ibiblio, two older FreeDOS packages, DOS Frotz
from the IF Archive; all pinned by SHA-256). Mapping of the user's categories onto the existing groups:

| Category | CharonDOS groups | Programs today |
|---|---|---|
| Everyday | `view` (text viewing and help), `edit` | EDIT, OSPEDIT, E3, EDLIN, text viewers; no browser, mail or media player has passed the emulator test |
| For blind users | — | none: the reader in CharonDOS is Charon's own. Period DOS screen readers need a hardware synthesizer; the Braille 'n Speak emulation exists only in CharonPC (Windows profiles), not in the DOS emulator |
| System tools | `files`, `tools`, `archive` | Doszip (file manager), XCOPY, MOVE, DELTREE, TREE, ATTRIB, DU, SEARCH, FIND, GREP, SORT, FC, MD5SUM, CAL, BC …; ARJ, ZIP, ZOO |
| Creating and programming | `devel`, hex editors of `edit` | JWasm, TINYASM, WASM, HEXED, UHEX |
| (Games) | `games` | Frotz (text adventures), GNU Chess, ELIZA, ROW4 |

New DOS programs enter only through the existing test procedure (candidates looked at: 101; 12 more
work but are blocked by one emulator gap, `kBlockedByStdHandleBug`). A user's own list for CharonDOS
is limited to "copy these files from the stick into `C:\`" — arbitrary downloads are not offered,
because an untested DOS program can hang the emulator with no speech.

---

## 10. Other systems Charon starts or writes: which questions can apply

| Entry (`CharonWsInstall3.h`) | How it gets onto the computer | Questions that can apply | Why not more |
|---|---|---|---|
| Debian, Ubuntu (installer) | answer file | everything in section 5 marked L | — |
| Debian 13, Ubuntu 26.04 ready images | cloud-init seed + `firstboot.sh` | all post-install groups (speech, look, software, updates, power, network, accounts, administration A1–A3, A5–A7); **not** `layout`, `lvm`, `encrypt`, `fs`, `release`, `sys_size` (the image is a fixed partition layout, grown by `growpart`) | image, not an installer |
| Arch ready image | same | as above; `aur`; `wholeOnly` — never beside another system (`:60`) | — |
| Fedora 45 ready image | same | accounts, language, network, software (dnf, Flatpak), updates (dnf-automatic), administration; **no** desktop and **no** speech questions (`:61-62, 330`) | Fedora has no console speech package (confirmed: no `espeakup`); the entry already needs `ConfirmSilent` |
| Arch from the network (`archNet`) | live system, user runs `archinstall` | none today. Possible later: Charon writes an `archinstall` configuration from the answers (`--config`, `--creds`; documented keys: `archinstall-language`, `audio_config`, `bootloader_config`, `disk_config`, `hostname`, `kernels`, `locale_config {kb_layout, sys_enc, sys_lang}`, `mirror_config`, `ntp`, `packages`, `profile_config`, `swap`, `timezone`, `custom_commands`; credentials `users [{username, enc_password, sudo}]`, `root_enc_password`, `!encryption-password`) | **H**: the file must reach the live system (a second initrd cpio, as for Debian) and `disk_config` needs device paths Charon does not know; the profile keys are not documented on the page read |
| Fedora with its own installer (kickstart) | not offered today | would map: `lang`, `keyboard --xlayouts`, `timezone --utc`, `rootpw --lock`, `user --name --groups --password --iscrypted --gecos`, `network --hostname`, `autopart --type lvm|btrfs|plain --encrypted --passphrase --noswap`, `firewall --enabled --service=ssh`, `services --enabled`, `sshkey --username`, `timesource --ntp-server`, `reboot` (all documented in pykickstart) | **H**: a new install path; whether Anaconda can be made to speak in an unattended run was not looked at |
| Slint, Accessible-Coconut, custom ISO | image to a stick or RAM disk; own speaking installer | none (Charon cannot reach inside); only "which stick" and the confirmations | not Charon's installer |
| Haiku R1 beta 6 | raw image, no seed (`rawNoSeed`) | none; `ConfirmSilent` applies | no reader in official Haiku; no configuration channel |
| FreeBSD | **not an install entry in this source** (only its loader is recognised as a boot manager) | none. If added: `bsdinstall` scripted installation — not researched (unverified) | — |
| netboot.xyz, SystemRescue, GParted Live, Clonezilla | started, not installed | none | tools |
| Windows from a RAM disk (`ramdisk::WindowsFromMicrosoft`) | the user goes through Setup with Narrator | none today (no answer file is written on that path). Possible: the same `Autounattend.xml` into the RAM disk image — would bring every Windows question to that path | M, separate task |
| CharonDOS started without installing | runs from the stick | none | nothing is installed |

---

## 11. Implementation order

**Batch 1 — the frame, plus everything that maps directly (E).**
1. `charon_questions.py` + generated tables; `Question` trailing members; `number`/`list`/`address`/`file`
   kinds; bigger stores and buffers; `when` predicates (move `console`/`gui`/`login`/`wfirst` into tables).
2. Detailed-mode UX: group list, gates, defaults (F2/Delete), search (F3), summary with "change an
   answer", `ConfirmRisk`; the mode question for CharonDOS.
3. Carriers: `answers.env` + `personal.sh`, `answers.ini` + `Charon-Personal.ps1`, report files.
4. Host generator test with golden files (first proof that express output is byte-identical).
5. Questions: `lang` group (Linux part), `acct` (`root_pw`, `sudo_nopw`, `shell`, `groups`), `disk`
   (`layout`, `lvm`, `sys_size` for Windows, `esp_size`, `grub_wait`, `grub_tune`, `boot_text`,
   `boot_first`, `w_bitlocker`), Linux speech through Speech Dispatcher and BRLTTY (`synth` incl.
   RHVoice on Debian/Ubuntu/Arch, voices, `speech_lang`, `rate`, `pitch`, `volume`, `punct`, `caps`,
   `braille*`), NVDA keys that are in `configSpec.py` (`w_punct`, `w_keyecho`, `w_layout`,
   `w_modifier`, `w_autolang`, `w_braille`, `w_ducking`, `w_nvda_update`, `w_nvda_stats`,
   `w_nvda_welcome`), `desk` (`flatpak`, `components`, `u_mirror`, `u_codecs`, `u_drivers`,
   `upgrade_now`, `popcon`), `net` (`firewall`, `ssh_key`, `ssh_pw`, `w_ssh`), `upd` (`w_update`,
   `w_update_restart`, `w_telemetry`), `hw` (`tlp`, `printer`, `w_features` WSL/SSH, `w_key`,
   `fast_startup`, `hibernate` on Windows), Windows profile variants `DebloatLite`, `NoAI`,
   `profile_url`, all CharonDOS rows marked E (`dos_*` except code page/keyboard, `pc_*`).
6. Saved answer sets (save, load, `answers` excluded from the `\EFI\HAK` copies), secrets as `ask`.

**Batch 2 — software, appearance, the rest of M.**
Catalogue file + reader in Charon + resolvers in the two scripts; sets and per-item ticking; own
list; NVDA add-ons from the store data; RHVoice on Windows (pinned voice URLs) and synthesizer
settings per driver; `look` group; `keyecho`, `orca_layout`, Speakup rate; `wifi_keep`; `auto_updates`;
power group; `sound_dev` with tones; `session_default`, `dm`, `wayland`, `kernel`, `audio_stack`,
`release`; `w_role`, `autologin`; `w_apps_remove`; WinUtil and ServerDesktop profiles, `waddons`
after their reader tests; `w_index` (edition names from the image); `w_drivers`; `pc_zone`.

**Batch 3 — administration and the hard ones.**
Unattended mode, per-machine overrides, disk serial numbers and pre-authorisation, encrypted
secrets, export/import tooling; A1–A7 (multiple users, naming scheme, static network and proxy, NTP,
mirrors and extra repositories, WSUS/maintenance window, KMS, certificates, printers, log
forwarding, post-install script, first-logon message, reader configuration for all users, Ansible
and phone-home); then, each only after its own trial: `encrypt` with the start-up tone and
`luks_tpm`, BitLocker with escrow, domain join, lock-down/kiosk/exam, Wi-Fi inside the Debian
installer, CharonDOS code pages and Polish keyboard, the unofficial Windows image, archinstall and
kickstart paths.

---

## 12. Decisions needed from the user

1. **Group navigation:** a free group list (open only what you want; proposed) **or** a linear walk
   with "Customise this group?" before each group as the standard behaviour?
2. **Speech defaults:** in the detailed mode, should the installed reader start with **Charon's own
   rate, pitch and punctuation** (proposed as an offered choice, not automatic) **or** always with
   the reader's factory values?
3. **RHVoice by default:** when Charon itself speaks with RHVoice, make RHVoice the default
   synthesizer of the installed system even in **express** mode (changes today's express result and,
   on Debian, switches on the `non-free` component) **or** only in the detailed mode (proposed)?
4. **Passwords in saved answer sets:** never saved, always asked (proposed default) **or** saved
   encrypted with a passphrase **or** allowed in clear text for administrators who mark the file so?
5. **Unattended disk rule:** one typed confirmation per machine by default, disk match by serial as
   an opt-in (this spec) **or** the disk match as the only confirmation of every unattended run (the
   multi-boot spec's section 7)? Either way disk serial numbers must be readable in Charon first.
6. **Encryption (LUKS) now or later:** offer it in batch 1 with the spoken warning that the passphrase
   prompt is silent, **or** hold it until the TPM unlock and the start-up tone are proven (proposed)?
7. **Software defaults:** when a set is checked, install its default items straight away (proposed)
   **or** always open the list for ticking?
8. **Catalogue updates:** only with a Charon release (file on the stick) **or** also fetched from the
   network at install time (needs a place to host it and a signature)?
9. **Non-commercial voices and trial software** (Cezary, RHVoice Lab voices, REAPER, Total Commander):
   list them with the licence spoken (proposed) **or** leave them out entirely?
10. **Windows add-ons that need a reader test first** (Open-Shell, Harden System Security): test and
    add in batch 2 **or** drop?
11. **CharonDOS Polish code pages (CP852/Mazovia) and keyboard:** worth the emulator work (batch 3)
    **or** keep code page 437 with nearest letters?
12. **Administration layer by voice:** keep all administration questions askable in the dialogue
    (proposed) **or** make them file-only (shorter dialogue, less code to test)?

---

## 13. Sources and what was not verified

### 13.1 Checked against official sources on 2026-11-23

| Topic | Source |
|---|---|
| Debian preseed keys (`partman-auto/method` regular/lvm/crypto, `choose_recipe` atomic/home/multi, `partman-auto-lvm/guided_size`, `partman-auto-crypto/erase_disks`, `pkgsel/upgrade`, `pkgsel/include`, `base-installer/kernel/image`, `apt-setup/non-free`, `contrib`, `non-free-firmware`, `services-select`, `local0/repository`, `local0/key`, `mirror/suite`, `mirror/http/proxy`, `passwd/user-default-groups`, `passwd/user-uid`, `clock-setup/ntp-server`, static `netcfg/*`, `keyboard-configuration/toggle`, `localechooser/supported-locales`, `grub-installer/*`, `debian-installer/add-kernel-opts`) | https://www.debian.org/releases/stable/example-preseed.txt ; https://www.debian.org/releases/stable/amd64/apbs04.en.html |
| Ubuntu autoinstall keys (`keyboard` layout/variant/toggle, `source`, `network`, `proxy`, `apt.mirror-selection`, `storage.layout` name/match/password/sizing-policy, `identity`, `active-directory`, `ubuntu-pro`, `ssh` install-server/authorized-keys/allow-pw, `codecs`, `drivers`, `oem`, `snaps`, `kernel`, `timezone`, `updates`, `shutdown`, `user-data`) | https://canonical-subiquity.readthedocs-hosted.com/en/latest/reference/autoinstall-reference.html |
| cloud-init modules (`keyboard`, `locale`, `ntp`, `package_update/upgrade`, `snap`, `apt`, `ca_certs`, `swap`, `growpart`, `ansible`, `puppet`, `landscape`, `power_state`, `phone_home`) | https://docs.cloud-init.io/en/latest/reference/modules.html (first 100 000 characters) |
| Kickstart commands | https://pykickstart.readthedocs.io/en/latest/kickstart-docs.html (first 200 000 characters) |
| archinstall configuration keys | https://archinstall.archlinux.page/installing/guided.html |
| Windows answer file: `OOBE` children, `TimeZone`, `UnattendedJoin/Identification` | https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-shell-setup-oobe ; …/microsoft-windows-shell-setup-timezone ; …/microsoft-windows-unattendedjoin-identification |
| Windows Update registry values (`AUOptions`, `NoAutoUpdate`, `ScheduledInstallDay/Time`, `NoAutoRebootWithLoggedOnUsers`, `UseWUServer`, `WUServer`) | https://learn.microsoft.com/en-us/windows/deployment/update/waas-wu-settings |
| OpenSSH server capability, service, firewall rule | https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_install_firstuse |
| WSL feature names and DISM commands | https://learn.microsoft.com/en-us/windows/wsl/install-manual |
| winget `install` options | https://learn.microsoft.com/en-us/windows/package-manager/winget/install |
| `slmgr.vbs` options | https://learn.microsoft.com/en-us/windows-server/get-started/activation-slmgr-vbs-options |
| `manage-bde -protectors` | https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/manage-bde-protectors |
| NVDA configuration keys | https://raw.githubusercontent.com/nvaccess/nvda/master/source/config/configSpec.py |
| NVDA add-on store data (ids, versions, licences, URLs, SHA-256) | https://addonstore.nvaccess.org/en/stable/latest.json |
| Speech Dispatcher options | https://raw.githubusercontent.com/brailcom/speechd/master/config/speechd.conf |
| BRLTTY directives | https://raw.githubusercontent.com/brltty/brltty/master/Documents/brltty.conf.in |
| dnf-automatic | https://dnf.readthedocs.io/en/latest/automatic.html |
| unattended-upgrades (package, reconfigure command) | https://wiki.debian.org/UnattendedUpgrades |
| RHVoice licences, packaging, binaries, voice list | local checkout `work/rhvoice-dl/RHVoice` (1.18.5): `doc/en/License.md`, `Packaging-status.md`, `Binaries.md`, `data/voices`; https://zlotowicz.pl/synteza (Polish voices and their licences) |
| Package ids | Debian: api.ftp-master.debian.org/madison; Ubuntu: people.canonical.com/~ubuntu-archive/madison.cgi; Arch: archlinux.org/packages/search/json; AUR: aur.archlinux.org/rpc/v5; Fedora: mdapi.fedoraproject.org/rawhide; Flathub: flathub.org/api/v2/appstream; winget client 1.29 on this host |
| Windows profiles, add-ons, unofficial images | `work/charon-windows-profiles-research-2026-11-23.md` (taken as given, not re-checked) |

### 13.2 Not verified (must be checked before the row is implemented)

1. **Nothing in this document was run.** Every "Applied" cell is a plan.
2. Debian: `partman-crypto/passphrase` keys, `partman/default_filesystem`, `partman-basicfilesystems/no_swap`,
   Wi-Fi keys of `netcfg`, `shared/default-x-display-manager`, the `APT::Periodic` lines and
   `Automatic-Reboot` of unattended-upgrades, `XKBVARIANT` handling in the installer itself.
3. Ubuntu: `storage: layout: mode: use_gap` — **used by today's code** (`CharonWsInstall2.h:61`) but not
   found on the reference page read; together with the r400/r406 notes that Ubuntu autoinstall was
   never proven, this path needs a trial before more is built on it. Also: the component of the
   RHVoice packages, an interim-release netboot directory, `ubuntu-report`/`apport` settings.
4. cloud-init: `users`, `timezone`, `write_files`, `runcmd`, `ssh_pwauth`, `rsyslog`, network config —
   beyond the part of the page that was read (the first five are already used by today's code).
5. Kickstart `realm`, `%packages`, `%post`; archinstall `profile_config` contents and a `--silent` flag.
6. Windows: every registry value marked "unverified" in sections 5.6, 5.9, 5.10 and 5.12
   (`HideFileExt`, `Hidden`, the classic-menu CLSID, `TaskbarAl`, `TaskbarDa`, `SearchboxTaskbarMode`,
   `LaunchTo`, `TextScaleFactor`, `HiberbootEnabled`, `InitialKeyboardIndicators`, `fDenyTSConnections`,
   `USBSTOR`, startup-sound and theme values), `powercfg` aliases, feature names `NetFx3`,
   `Microsoft-Hyper-V-All`, `Containers-DisposableClientVM`, `InputLocale` value forms,
   `RegisteredOwner`/`RegisteredOrganization` (parent page lists nothing; child pages not opened),
   `ProtectYourPC` value meanings (used by today's code), the offline-join child of `Provisioning`,
   `Install-Language`, `netsh wlan add profile`, `pnputil`, `DriverPaths`, `certutil`, `Add-Printer`,
   `Enable-PSRemoting`, `net accounts`, Windows Update for Business deferral values, Entra/Intune
   enrolment, whether an empty local password is accepted, whether winget works in
   `FirstLogonCommands` on a fresh install.
7. NVDA: synthesizer driver names (`espeak`, `oneCore`, `sapi5`, `RHVoice`), per-driver `rate`/`pitch`/
   `volume`/`voice` keys (created by drivers, not in `configSpec.py`), the system-configuration
   folder for the sign-in screen, any silent way to install an add-on, the installer's start-on-logon
   switch, whether remote access is built into NVDA 2026.3, add-on compatibility with 2026.3.
8. RHVoice: current URLs and hashes of the Polish voice packages for NVDA and SAPI 5; silent switches
   of the SAPI 5 installers; licences of the English voices other than what `License.md` states
   (Slt's ARCTIC terms are in `work/rhvoice-dl`; Alan, Bdl, Clb were not read); the Fedora Copr; how
   each distribution registers the Speech Dispatcher module; `speechd-up` with RHVoice in the console.
9. Linux desktop: all gsettings/dconf key names in 5.5–5.6 and 5.10, Orca's settings keys, Speakup's
   sysfs paths, `logind.conf` keys, AccountsService session file, GDM's Wayland switch on current
   releases, `console-setup` values, `systemd-cryptenroll`, PAM password-quality options, `lpadmin`.
10. Fedora: the package name of dnf-automatic in Fedora 45 (the index query found no `dnf-automatic`
    in rawhide), `pandoc`, `cmus`; mdapi was queried for rawhide, not release 45.
11. Catalogue: all accessibility ratings (community knowledge, not tested here); licences of TeamTalk
    and REAPER's evaluation terms; TeamTalk's Linux download; OSARA's address; Unigram, Nmap, VMware
    and FreeFileSync ids; Thorium on Flathub; mpv, Pidgin, SoX and FFmpeg in winget (not queried).
12. Charon internals not read for this document: whether disk serial numbers are available
    (`CharonWsDisks.h`), whether SMBIOS serial/asset tag and a battery can be read, whether the Wi-Fi
    key Charon used is kept, AES/PBKDF2 in `CharonCrypt.h`, CharonDOS keyboard-layout support,
    `src/dos/sound.cpp` options, the `view` group's program names.
13. The coordinator relayed additions during this run (detailed-mode-only rule, administration layer,
    software/settings decisions, RHVoice, the multi-boot decisions of 6.6). They are built in as
    relayed; I did not see the user's own wording.

## User decisions (2026-11-23, answered by the user in a question dialogue)

- Decision 2: the installed system's reader starts with Charon's rate, pitch and punctuation by default; changeable or switchable off in detailed mode.
- Decision 3: when Charon itself speaks RHVoice, the installed system gets RHVoice by default in EXPRESS mode too (fallback to eSpeak NG with a spoken and logged message when it cannot be installed). This overrides the proposal in this spec.
- Decision 4: passwords in saved answer sets - asked at load by default, but an administrator's file MAY contain them in clear text (with a warning). This overrides "never saved".
- Decision 6: LUKS is held back until TPM unlock or a start-up tone at the passphrase prompt is proven.
- Software: thematic sets + per-program list in detailed mode; four categories; four sources; four extra setting groups (recorded earlier).
- Decisions 1, 5, 7-12: not asked; the proposals in this spec stand until the user says otherwise.
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
