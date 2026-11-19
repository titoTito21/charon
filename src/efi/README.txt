Charon EFI boot manager - development, not installed by the current package
====================================================================

CURRENT PRODUCT SCOPE (2026-11-16, supersedes announcement-only plans):
One Charon menu runs before Haiku. The Haiku-session greeting/menu
must not duplicate it. HAKStartupVoice is still a Haiku program; changing its
autostart is not a pre-OS implementation. This component implements the UEFI
menu only; BIOS needs a separate implementation and verification.

The production menu requires REAL live TVox/eSpeak synthesis. BootMenuVoice.h
specifies queued UTF-8 input and bounded synthesis pumping. No live backend is
currently supplied here: without one the menu explicitly reports console-only
development mode. Cached PCM utterances are NOT a production speech fallback.
Legacy cached announcements require HAK_EFI_DIAGNOSTIC_CACHED_ANNOUNCEMENTS.
Haiku's hk_startup.wav remains in Haiku; pre-OS cues/melodies are separate.

Boot devices use validated BootOrder/Boot#### descriptions and device paths.
Device category precedes the firmware description; this is not an inferred OS
identity. Up/Down/Home/End navigate, Enter/Right open submenus, Left/Backspace/
Escape return. Root Escape uses the same explicit Continue policy. Settings
persist list wrapping, language (automatic/English/Polish) and one UI-sounds
switch in versioned two-slot records with CRC and readback validation.
The same switch covers welcome, navigation, boundaries/wrap, submenus,
popup/message open/close, dialog open/close, selection and Continue sounds.
The real popup-open source filename is popUp.wav; source assets are preserved.
Messages have a visible body and focused OK. Enter/Space/Escape dismiss them
without activating the option underneath. Lifecycle cues are committed only
after successful display, not merely on a requested transition.
No cue player is wired yet. AudioEffects.QueueCue must preserve ordered events
(for example dialogClose then popupOpen), and exit must drain the Continue cue
before mandatory Stop/handoff. The shared mixer alone is not device playback;
see audio_stream/README.md for the current AudioDxe streaming limitation.
Settings also include explicit pending-request confirmations.
The optional profane presentation is disabled by default. In the menu, type
Q R W A consecutively (case-insensitive, no modifiers or Enter) to toggle it.
Another key breaks the sequence; text editors must not feed this shortcut.
The verified persistent setting affects Polish and English titles, options,
settings, confirmations, hints, statuses and authored boot-error messages,
on screen and in speech. Firmware codes and device identities are preserved.
Repeating the sequence restores ordinary language. This is Charon-only and
does not replace original NVDA messages. Settings versions 1/2 migrate with
the mode disabled; version 3 records it without changing the record size.
Optional-language vocabulary references (2026-11-16):
https://jelec-rudelex.web.amu.edu.pl/glossary-pl/
https://jelec-rudelex.web.amu.edu.pl/glossary-en/
https://dobryslownik.pl/slowo/do%2Bchuja/225064/
https://wsjp.pl/haslo/podglad/48032/chujnia
https://wsjp.pl/haslo/podglad/91978/w-pizdu
https://dictionary.cambridge.org/dictionary/english/clusterfuck
https://dictionary.cambridge.org/dictionary/english/shitshow
https://dictionary.cambridge.org/dictionary/english/fuckery
Only ordinary vocabulary informed the original Charon sentences; dictionary
definitions and quoted corpus passages are not copied into the catalogue.
Confirmations default to Cancel. No firmware settings/keys change automatically.
Continue targets are stored on this HAK ESP with an exact boot-option payload;
selection is revalidated before the firmware LoadImage/StartImage chain. Firmware
image authentication is never bypassed. An unconfigured Continue explicitly
returns to firmware order; a stale configured target stays in the menu.

HAKBOOT.EFI is an independent x86-64 UEFI application. It uses standard
BootOrder/Boot#### variables, Simple File System, Loaded Image and LoadImage /
StartImage services. No modification of Haiku's loader or kernel is involved.

Speech ownership: live pre-OS speech owns this menu; stop it before handing
control to an OS. The session must not replay the same menu or welcome. Existing
Haiku settings tools remain diagnostic until their operations are integrated
into the pre-OS interface. Arbitrary vendor firmware settings are not implied.

User-facing UEFI, disk selection and Secure Boot controls belong in the speaking
HAK Boot Manager. Do not install a Deskbar entry or substitute a desktop window
for this interface. The native window described below is a diagnostic test tool.

Diagnostic key manager: tools/hak_key_manager.py [ESP] --language pl|en.
Build its independent native host with tools/build-hak-settings.sh. The host
reuses the native presentation protocol with its own HAK application identity;
the controller imports no NVDA modules. Native controls support database/entry
selection, ESL import/export, exact-entry removal review, certificate/key file
selection, asynchronous signing, explicit confirmation and pending/result views.
Signing alone never queues an update. Cancel invalidates delayed signing results.
Confirmation starts on the read-only change summary, not the commit button.
Existing firmware read errors do not prevent browsing other databases.

Omitting ESP opens a chooser using Haiku partition metadata through
bin/HAKEFIVolumes. It recognizes the GPT provider's "EFI system data" type,
checks actual IsMounted state and revalidates partition identity before mount.
HAKStartupVoice now includes Bootloader > UEFI boot: native ESP discovery,
firmware-entry lists and a separate cancel-first restart confirmation. Up/Down,
Home/End and PageUp/PageDown navigate; Right/Enter opens lists; Left/Backspace/
Escape returns with position retained. Only Enter on the commit choice writes
the exact inventory-bound one-time request. TVox remains the menu synthesizer.
This requires HAKBOOT.EFI installed in the actual firmware boot path and its
inventory on the chosen ESP; it does not convert legacy BIOS boot into UEFI.
Firmware settings and key operations are connected to the spoken menu as
described below; audio deployment and firmware-specific setup remain outstanding.
Firmware UI/status snapshots remain boot-time data. Current diagnostic window
key-manager translations cover Polish/English; other HAK locales are pending.
The entire broader UEFI/settings/accessibility batch remains unreleased.
Interface references:
https://uefi.org/specs/UEFI/2.11/03_Boot_Manager.html
https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html

Its own ESP directory is EFI/HAK. inventory.bin records active boot-category
firmware options, excluding BootCurrent. It has an eight-byte HAKEFI1-NUL
header, repeated little-endian uint16 option-number / uint32 byte-count /
exact EFI_LOAD_OPTION payload, and a final FFFF/zero-length record. Clients
reject incomplete records, duplicate identifiers, malformed UTF-16 and paths.

tools/hak_efi_boot.py can read that inventory on a mounted ESP and atomically
write next.bin. A target identifier includes the SHA-256 of the exact option;
changing its description, path or load options invalidates the old identifier.
next.bin is one inventory record without the inventory header/end marker.
The EFI component consumes the file BEFORE loading, re-reads Boot#### and
compares every byte. A deletion failure refuses to start the target. It passes
the option's device path to LoadImage with BootPolicy=true and preserves the
optional load parameters. Returning falls through to the firmware BootOrder;
it never guesses or overwrites a default OS loader.

Native build: sh tools/build-boot-efi.sh
The Haiku GCC/binutils build preserves ELF relocations until PE linking.
Converting an unlinked ELF object to COFF with objcopy produces incorrect
addends with the tested toolchain and must not be reintroduced. The large code
model supplies DIR64 relocations instead of assuming addresses below 4 GiB.
tests/boot-efi-image-host.py checks the resulting image structure/relocations.
The imported interface headers retain their Fuchsia BSD-style licenses; see
haiku/SOURCE.txt and haiku/LICENSE.

Verified in a separate EDK II / QEMU q35 VM with a native Haiku-formatted FAT
image: actual image chainload, load parameters, one-time consumption, rejection
of a stale option and no repeat without a request. After QEMU exit Haiku mounted
the image read-only: complete inventory persisted and next.bin was absent.
The Haiku client also wrote an exact next request on FAT, read it after a
read-only remount and again after a writable remount, then canceled it. Its
temporary prefix is HAKNEXT-: the tested FAT driver rejects leading-dot names.
The folder-backed QEMU vvfat experiment did not provide reliable host-file
persistence and is not the evidence for the FAT persistence requirement.

Boundaries / outstanding integration:
- No automatic firmware entry installation yet; never install the fixture.
- No integration into the spoken disk submenu yet. Optional native client API
  hak_efi_boot_devices_read / hak_efi_boot_start_once now shares exact identifiers
  with the Python client; read/choose/persistence tests passed on Haiku FAT.
- No discovery of EFI loaders not listed by the firmware BootOrder yet.
- Firmware descriptions are labels, NOT proof of a device name or OS identity.
- No optical-media or actual alternate OS boot verified via this EFI path yet.
- No signed Secure Boot artifact, IA32/ARM build or physical firmware testing.
- Production EFI supports optional pre-OS announcements through BootAudio.h;
  installation and authenticated driver deployment remain outstanding.
  Menu speech remains in the Haiku HAK boot manager.
Do not interpret this component test as completion of full UEFI accessibility.

Requested next interfaces
-------------------------
The speaking Haiku boot manager has candidate UEFI controls for one-time
selection, default OS, order, timeout, Secure Boot status and key operations,
as detailed below. A supported production Secure Boot toggle remains pending.
The SecureBoot status variable itself is read-only; toggling requires an
actual supported HII/vendor policy control, not a write to that indicator.
The user also requests separate Secure Boot key management: inspect/export,
add and remove PK, KEK, db and dbx entries. Changes must use the firmware's
authentication/physical-presence workflow, identify the actual affected entry
and expose confirmation for removal. Turning Secure Boot off must not silently
delete keys. Candidate key controls and firmware tests are described below;
full installation and cross-vendor coverage are not complete.
Availability
must follow actual firmware capabilities; a jump to a silent vendor setup
screen is not an accessible implementation of these controls.

Vendor firmware capabilities remain a separate integration requirement; the
user does not want a duplicate UEFI menu. Investigate standard HII interfaces
when exposing settings through the Haiku HAK Boot Manager; preserve
question types, values, constraints, conditions and vendor callbacks. Do not
assume that every vendor screen or every model exposes equivalent HII forms.
Relevant interface specification:
https://uefi.org/specs/UEFI/2.11/35_HII_Configuration_Processing_and_Browser_Protocol.html
Existing primary research/prototype, not proof of vendor compatibility:
https://arxiv.org/abs/1712.03186
No general vendor-screen support is implemented or claimed at this point.

Firmware boot-settings transport (experimental)
-----------------------------------------------
BootSettings.h validates settings.bin: HAKSET1-NUL, uint16 BootCurrent,
uint8 timeout-present, uint8 SecureBoot (0/1/255 unknown), uint16 Timeout,
uint16 order-count, uint64 OsIndicationsSupported, then uint16 BootOrder entries.
All integers are little-endian. Duplicate entries/truncation are rejected.
settings-next.bin contains uint16 command, uint16 value, followed by the exact
snapshot the client showed. Commands: 1 timeout (0..300), 2 default entry,
3 move entry earlier, 4 move entry later. Only an exact live snapshot match can
reach SetVariable. One command is carried per request; UI batching/pending
change management remains to be designed before exposing multiple controls.
The file is consumed first. The firmware writes settings-result.bin with
HAKRES1-NUL and uint64 EFI status (last attempted change), then refreshes the
snapshot. Result/request correlation still needs integration in the client UI.
BootOrder edits require the current HAK entry already first, never move/remove
it, and validate the chosen active Boot#### entry. Setup must establish this
precondition; the component refuses to guess which entry is HAK.
No SecureBoot, PK, KEK, db or dbx writes occur in this transport.

QEMU/EDK II test with that explicit installation precondition verified live
Timeout=17, rejection of a stale request for18, a changed default Boot#### with
HAK preserved first, then restoration of the scratch VM's settings. Existing
chainload/cancellation tests also passed. The speaking settings UI, Secure Boot
toggle and key-management controls are still outstanding.
Secure Boot key inventory and offline drafts (r336, not released)
--------------------------------------------------------------
The EFI component exports PK, KEK, db and dbx to secure-boot.bin on its
own ESP, with each GetVariable status and attributes. Missing variables,
read failures and empty databases remain distinct. A mandatory end marker
rejects interrupted exports. SecureBoot/SetupMode/AuditMode/DeployedMode
are included as 0/1/255 (unknown). This is a boot-time snapshot, not a live
view of firmware and not evidence that a later update is authorized.

tools/hak_secure_boot.py ESP list|export|draft-add|draft-remove supports
inspection, exclusive-file ESL export and offline edit drafts. Entry IDs
bind the complete database digest and entry offset, so duplicate entries
are distinguishable and stale deletion requests fail. Removal preserves
other lists and their headers. Unknown signature types are preserved.
Import checks ESL dimensions and duplicates, not certificate validity or
trust. PK draft enrollment currently accepts one X.509 entry only.

Drafts are NOT authenticated firmware updates. The separate submit-auth command
queues an AUTHENTICATION_2 replacement only when its payload equals the reviewed
draft and the previous database digest matches the exported inventory. It
preserves existing pending requests. The EFI component consumes the request,
revalidates live database/status/attributes and SetupMode/SecureBoot, then calls
SetVariable with time-based authentication attributes. Firmware decides whether
the signature is authorized. Successful writes are read back; keys-result.bin
binds the EFI status to a 16-byte request ID, checked by the result command.
Only replacement updates using attributes 0x27 are currently supported, not
append writes or AUTHENTICATION_3. A torn request fails length validation.
Secure Boot toggle remains pending. Deleting PK must remain an explicit key
operation, never an implicit
off switch.

tools/hak_secure_boot_sign.py converts PEM/DER certificates to X.509 ESL and
signs reviewed ESL replacements using an existing key/certificate and OpenSSL
CMS SHA-256. Signature input binds UTF-16 variable name, vendor GUID, attributes,
UTC EFI timestamp and full new payload. It never writes firmware or queues a
request. Output files are exclusive; encrypted keys can use --passphrase-file.
Use a timestamp newer than the last accepted update for that variable; the
firmware rejects replay. Private key material is read only by OpenSSL, never
copied into the HAK package or passed as command-line key bytes.

Disposable secure/SMM QEMU testing passed all 14 signed lifecycle stages:
self-signed PK enrollment switches SetupMode to 0; PK authorizes KEK; KEK
authorizes db/dbx; wrong signer, damaged signature and timestamp replay fail
with EFI_SECURITY_VIOLATION and preserve existing data. Signed replacements
and deletion pass; PK-authorized KEK and PK deletion restore SetupMode 1.
This tests variable authorization, not signed HAK image loading or real vendor
firmware. Secure Boot enabled OS boot is not claimed by those fixture tests.

Format reference: UEFI Secure Boot and Driver Signing, signature databases:
https://uefi.org/specs/UEFI/2.11/32_Secure_Boot_and_Driver_Signing.html

Spoken UEFI settings are reached after selecting an EFI partition, at the end
of its firmware-entry list. Timeout uses Left/Right (0 through 300 seconds),
Enter opens cancel-first confirmation, and Escape/Backspace leaves the slider.
Boot order lists non-HAK entries; each offers default, earlier and later.
The HAK entry remains first. Unknown entry descriptions retain Boot#### IDs.
These controls queue one change for the next HAK EFI start; they do not change
live firmware from Haiku. The native client rejects stale snapshots, no-ops
and an existing pending request. Settings require a valid settings.bin snapshot
from HAKBOOT.EFI; a BIOS session does not synthesize firmware settings.
Vendor-specific settings are still pending in this spoken boot interface.
The desktop diagnostic tool is not the product UI.

Settings change result is a third item in that spoken settings menu. It
distinguishes pending, accepted, rejected (including the EFI status code), no
tracked request and unverified results. Enter refreshes the selected report.
Native requests now use HAKREQ2 plus a random 16-byte ID followed by the legacy
command/value/snapshot. The firmware echoes that ID in HAKRES2 with its status;
legacy requests still receive HAKRES1. The native client stores the ID before
completing the pending file and never treats a legacy, truncated, missing or
unmatched result as success. A queued file takes precedence over old results.
This reports firmware acceptance, not a claim that a subsequent OS boot passed.

Secure Boot, key databases is a spoken UEFI-settings submenu. Its first item
reads the four exported mode flags (including unknown). The remaining items
list PK/KEK/db/dbx, distinguishing absent, read-error and empty databases.
Enter/Right opens a readable database and then a selected entry's SHA-256
digest and owner bytes. Left/Escape/Backspace returns to the same position.
Unknown signature types are identified by their raw GUID bytes. SHA-256 entries
show their stored hash; X.509 and other entries show a payload fingerprint.
The native bounded ESL reader rejects malformed/truncated inventories. These
lists now lead to a database menu with browse and load-authenticated-update
actions. Existing diagnostic tooling is not a replacement for boot controls.

The spoken file chooser starts on the selected ESP, includes parent folders,
and excludes symbolic links/devices. Enter/Right opens folders; Back returns
through visited locations. An AUTHENTICATION_2 file is loaded into memory for
review; subsequent filesystem changes cannot swap the reviewed bytes. The
cancel-first confirmation names the database, whole-database replacement and
before/after entry counts. A separate details item reads the full file path,
exact file SHA-256 and each proposed entry's type, digest and owner. Returning
from details resets selection to Cancel. An empty payload
explicitly announces database deletion. Only Enter on save queues it; Right
does not confirm. The backend revalidates ESP identity, snapshot modes and
database status/attributes/bytes, preserves pending requests and persists the
request ID. Firmware remains the authority for signature authorization.
This supports externally prepared authenticated replacements, including
deletions, or a draft signed within the spoken interface.

Last key update result is available from each database menu. It reports the
last globally tracked key request on the selected ESP, not necessarily a change
to the currently selected database. Pending takes precedence; otherwise the
HAKKRES result must match the saved request ID. Missing, truncated or unrelated
results are unverified. Firmware rejection retains its EFI status code.

Enter on an existing entry's details prepares removal of that specific entry,
preserving peers and list headers. Add entries from ESL file appends validated
lists, refusing duplicates and invalid PK contents. Both open a draft menu:
cancel, choose a signed file matching the draft, inspect proposed entries, or
sign the draft with an existing certificate and private key.
Draft operations do not write files or firmware. Their AUTH2 selection must
match the complete proposed ESL byte-for-byte before confirmation is available.
A mismatched signed file produces an explicit no-write message.

Add PEM or DER certificate converts one certificate to an X.509 ESL before
the same draft review. Certificate bundles and trailing DER data are rejected.
The owner GUID bytes are deterministically derived from the certificate's
canonical DER SHA-256, so repeated PEM/DER import retains duplicate detection.
This validates encoding, not certificate trust or signer authorization.
Export ESL draft to folder opens the native file chooser with Save draft in
this folder as its first item. It creates an exclusive randomized HAK-<db>-draft
ESL filename, announces the full path and returns to the draft. It never
overwrites another file or queues firmware changes. Zero-byte exports represent
an empty proposed database; signing and explicit confirmation are still needed
to request deletion in firmware. The native chooser remains keyboard-only and
does not use a Haiku desktop file panel.

Sign draft opens selectors for a PEM certificate, PEM private key and optional
password file, then a separate signing action. Delete clears the password-file
selection. Native HAKBootSigner starts OpenSSL CMS without a shell, feeds the
exact target/GUID/attributes/UTC timestamp/draft content and never reads the
private key/password bytes itself. A private temporary directory contains only
the public content, signature and diagnostics; it is cleaned on completion,
error or cancellation. The menu polls without blocking; Escape terminates and
reaps the signer. A 60-second timeout prevents indefinite waits. An encrypted
key without a password file fails without prompting on a hidden terminal.
Signing success opens cancel-first review, never queues an update. Back returns
to signing settings. Firmware authentication and replay protection still decide
whether a subsequently confirmed request is accepted.

EDK2 Secure Boot preference discovery:
HAKBOOT.EFI exports secure-control.bin (24 bytes, HAKSB01-NUL, uint64 status,
uint32 attributes, preference/SetupMode/effective SecureBoot bytes, reserved0).
It reads only EDK2's SecureBootEnable GUID f0a30bc7-af08-4556-99c4-001009c93a44,
accepting a one-byte value and NV|BS attributes3. Absence/errors remain distinct
from disabled. Enter on the Secure Boot mode summary reads effective state and
the next-boot preference separately. Missing snapshots are announced as such.

The EFI setter is currently called ONLY by the disposable firmware fixture,
not by an OS request file or a production menu action. Tests change and restore
the preference, reject stale state and confirm PK is untouched. Firmware still
enforces its physical-presence/write policy. No CustomMode or implicit PK
deletion is used. A supported authorization/physical-presence flow still needs
integration before exposing a product toggle; do not add a duplicate firmware
menu as a substitute. Setting this
preference is not proof that SecureBoot is effective or an OS can boot securely.
This adapter is EDK2-specific; other firmware needs its documented interface.
Primary source contracts:
https://github.com/tianocore/edk2/blob/master/SecurityPkg/Include/Guid/AuthenticatedVariableFormat.h
https://github.com/tianocore/edk2/blob/master/SecurityPkg/VariableAuthenticated/SecureBootConfigDxe/SecureBootConfigImpl.c

Disposable reset-cycle verification:
tests/SecureBootResetFixture.cpp is NEVER installed as the HAK EFI component.
On fresh private OVMF NVRAM it enrolls only the test fixture image digest in db,
uses the throwaway signed PK request, and checks effective SecureBoot across
four cold resets (enabled, disabled, enabled, signed PK deletion / SetupMode).
tests/efi_fixture_trust.py computes Authenticode SHA256 only for the unsigned,
contiguous x64 PE layout emitted by our build; rejects unsupported layouts.
The trusted fixture must actually boot after protection is enabled. A distinct
untrusted PAYLOAD.EFI must be refused while enabled and load while disabled.
LoadImage may refuse with ACCESS_DENIED or SECURITY_VIOLATION; neither image
is started. The fixture releases any returned handle. These are firmware tests,
not an audible pre-OS UI or proof of Haiku boot under Secure Boot or OEM support.
Hash/status contracts:
https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html

Pre-OS audio proof (announcement stage, no product firmware menu):
tests/EFIAudioFixture.cpp loads a separate OpenCorePkg1.0.8 AudioDxe driver,
connects controllers, discovers AudioIo revision4 outputs and plays signed16
stereo48kHz PCM. src/efi/AudioIo.h preserves the upstream MIT notice and ABI.
The QEMU test uses ich9-intel-hda/hda-duplex with audible DirectSound. A TVox
Adam Polish greeting, rendered by the existing HAK cache, is resampled from
22050Hz mono to48kHz stereo for this hardware test. This is cached TVox speech,
not a live TVox synthesizer running in UEFI. Async playback supports immediate
Space stop, Enter stop/replay, Escape exit; the backing buffer stays alive.
Both serial results and captured PCM are checked. No OS runs in this test VM.
The external driver is currently only in the disposable test ESP, not the
package. Product work still requires announcement lifecycle integration,
output selection, timeout/error handling, packaging
and driver trust for SecureBoot. Secure-capable OVMF was in SetupMode during
audio testing; this does not prove an authenticated audio driver chain.
Driver source/release: https://github.com/acidanthera/OpenCorePkg/tree/1.0.8
Protocol: Include/Acidanthera/Protocol/AudioIo.h (revision4).
Release ZIP SHA256: 2011e8b7216ecb2645d97ea710df965947edd406846e92050b9bc4190be6d27b
X64 AudioDxe.efi SHA256: bda19da04ac380cde3e1a2c0e09a3edef21fde876cb2018f1214c0a0efc53418

BootSpeech.h is the shared cached-speech dispatcher used by the EFI fixture.
SayFile first requires successful StopPlayback, then reads/closes the next PCM
and starts asynchronous output. A failed stop leaves samples untouched. Empty,
unaligned or capacity-sized input is rejected (the latter may be truncated).
Failed async starts trigger stop cleanup. No callbacks retain a stack context.
The caller must keep its sample buffer alive until playback has stopped, even
on a stop failure. The fixture uses a static buffer. BootSpeechTest injects
driver/read errors to check stop-before-overwrite and file closure. This is
the playback layer for upcoming menu dispatch, not a completed menu integration.

AudioDxe restart workaround, verified on QEMU ich9-intel-hda/hda-duplex:
The first async utterance played fully, but subsequent utterances were cut
short, including replay after natural completion. A WaitForEvent input loop
alone did not fix this. Passing a nonzero resetOutputs mask to Speech enables
a stopped-stream 44.1kHz->48kHz setup cycle before every utterance. No samples
are played at44.1kHz; final output stays16bit/stereo48kHz at -12dB. AudioDxe's
format change resets hardware stream/DMA accounting. Every selected output
must advertise both rates and16bit support; the fixture checks this. The
default mask0 does not enable this device-specific workaround. Setup failure
prevents playback; unit tests cover configuration order and failure.
Captured output after rapid stops/restarts and replay after completion contains
two full ~3-second utterances;10ms RMS-envelope correlations to the original
complete synchronous recording exceed0.97. Status codes alone are insufficient.
The workaround is not yet verified on physical controllers or other drivers;
general output selection, driver trust and boot installation remain open.

Shared text-to-speech cache dispatch:
tools/hak_efi_speech_pack.py CACHE NEW_DIRECTORY --languages pl en exports
the existing HAK cache to16bit stereo48kHz PCM. It preserves exact UTF-8 text,
language and FNV64 cache identity (language + newline +175/50/100+ newline +text).
Source hashes, lengths and text-derived filenames are verified; source symlinks,
corruption, unsupported/missing requested languages and existing destinations
are refused. Output lengths/hashes and source hashes are recorded in the final
manifest. A failed export has no completed manifest; never install that directory.
The manifest is export/deployment evidence, not firmware authentication.
Export does not install a loader, alter firmware or overwrite the source cache.
SPEECH under EFI/HAK is the runtime directory. SpeechName.h and Speech.SayText
resolve the same text/language identity without a C++ runtime or Python in EFI.
The fixture exercises Polish/English greetings with P/E, besides stop/replay.
This prepares existing static HAK messages, not arbitrary application/OS names
or a live synthesizer in firmware. There is no silent language fallback.

Speech.Handoff stops firmware playback and permanently rejects subsequent
SayFile/SayText requests, including late menu events. Failure to stop prevents
successful handoff; Handoff may be retried, but speech cannot restart during
that transition. Once stopped, pointers are detached and repeated Handoff/Stop
calls do not touch the driver. The test successor EFI image is loaded only
after handoff succeeds; this is not yet a Haiku boot installation test.

UEFI melodies are NOT implemented yet. The current pre-OS audio tests play
speech only. Planned start/background music must respect the user's settings
and keep playing beneath speech; speech interruption must not interrupt the
background melody. Deskbar/session-ready sound belongs to Haiku. The pre-OS
audio lifecycle must stop all its streams before OS handoff, without replaying
the same event in Haiku. Existing Haiku boot-manager audio remains unchanged.

Dedicated announcement catalogue: BootAnnouncements.json contains firmware_ready
and starting in Polish/English. render-efi-announcements.py renders only these
four TVox Adam phrases to a NEW cache; the generic pack exporter then converts
them for EFI. The first phrase can run only once the HAK EFI application and
audio are initialized, not during hardware POST. The historical80-message menu
pack is a test artifact and is no longer the intended EFI deployment content.
BootMix.h currently supplies an allocation-free mixer core with independent
background, speech and queued chimes. Cancellation preserves background phase
and chimes; Handoff silences all channels. Native tests cover this, but no
continuous EFI stream backend is connected yet. export-efi-melodies.cpp reuses
the original Haiku hakboot::Melody/Bed generators to export four start clips
and four1.8-second loops; it does not modify Haiku audio or user WAV assets.

EFIAutoAnnouncements.cpp tests the automatic Polish phase sequence without a
menu or keyboard: firmware_ready, starting, audio handoff, successor image.
EFIAutoAnnouncementsEn.cpp selects English for the same test flow.
Both call the shared BootAnnouncements.h lifecycle. It waits only on its timer,
leaving pending keyboard input for the successor, and allows at most6000 waits
on a10ms timer across both phrases. Timer setup/wait/close errors and missing speech
files propagate after Handoff; a Stop failure takes precedence and prohibits
unloading playback storage. Only exact pl/en language identifiers are accepted.
BootAnnouncementsTest covers order, language, timeout and failure cleanup.
Speech accepts optional caller-owned Completion storage; the AudioIo callback
only marks completion at firmware notification priority. The next phrase waits
for that signal, not a guessed delay. Stop marks completion only on success.
Storage must outlive the stream, including failed stops; the fixture uses static
storage. A bounded event loop catches absent completion. This is a lifecycle
fixture, not a full Haiku boot test. Production integration is described below.
The automatic fixture adds100ms leading and150ms trailing silent PCM guard
frames per phrase. Captures without guards lost the output onset/tail despite
successful completion callbacks; an extra150ms wait after callback did not fix
it and was removed. Guard frames protect the actual samples and are bounded
against capacity; they do not change speech tempo or text. Completion still
controls sequencing. Defaults remain zero guards for other callers. Tests cover
sample preservation, zero fill and rejection when guards exceed buffer space.

HAKBOOT production announcement path:
BootAudio.h reads EFI/HAK/audio-language on its own ESP: exactly two ASCII bytes,
pl or en, without a newline. Absent configuration disables announcements. Invalid
configuration is rejected. A configured path first locates AudioIo revision4;
if absent, it loads EFI/HAK/AudioDxe.efi via firmware LoadImage/StartImage without
changing Secure Boot variables or bypassing a refusal. Output ports must support
both44.1/48kHz and16bit samples. The same four-phrase PCM pack, lifecycle and
silent guards used in the automatic tests are reused by production HAKBOOT.
All announcements finish and Handoff stops audio before firmware requests or
selected OS startup. Failure before playback leaves boot available. If the driver
cannot stop, HAK retains its image/callback/sample storage and retries Stop; it
must not unload active callback code or start an OS over a running DMA stream.
A permanent Stop failure can therefore block boot and is reported on the console.
This remains a development limitation pending hardware validation/recovery.
No driver is automatically installed into firmware and no duplicate menu is added.

Preparing a coherent EFI bundle:
python3 tools/hak_efi_bundle.py assemble bin/HAKBOOT.EFI /path/AudioDxe.efi \
    /path/final-announcements-pack /path/new-bundle --language pl
python3 tools/hak_efi_bundle.py verify /path/new-bundle
The destination must not exist. All inputs are checked before it is created;
bundle.json is written last. The tree contains EFI/HAK/HAKBOOT.EFI, AudioDxe.efi,
audio-language and exactly the four catalogue recordings plus their manifest.
The driver is pinned to the tested OpenCore1.0.8 X64 artifact. Hashes identify
content; they do not establish Secure Boot trust. Verification rejects missing,
changed or unlisted files, symlinks, unexpected manifest paths and wrong PCM
identity/format. It reserves room for the speech guard frames in the EFI buffer.
No EFI/BOOT file, Boot#### entry, BootOrder edit or firmware key is created by
this tool. Assembly is preparation, not firmware activation or a signed release.
Tests use actual built PE files and a real speech pack, including failure cases.

First installation on an identified Haiku ESP:
tools/hak_efi_install.py supplies review(bundle, helper, partition_id) and
install(bundle, helper, review). The review binds the bundle manifest hash and
partition identity. Installation reuses HAKEFIVolumes discovery/mount checks,
revalidates the mount immediately before writing, verifies input bytes and free
space, and refuses any existing EFI/HAK directory. Other loaders are untouched.
Dependencies are written with fsync/read-back before HAKBOOT.NEW is committed
as HAKBOOT.EFI. An interrupted installation retains INSTALL-PENDING.json and
does not expose an incomplete executable; INSTALL.json records reviewed inputs.
The API intentionally refuses overwriting or repairing an existing installation.
An explicit recovery/update flow is still pending. This installs files only:
it does not register Boot####, change
BootOrder, replace a removable-media loader or activate itself on the next boot.

Spoken installation controls:
HAKStartupVoice exposes Install HAK under the selected EFI partition when a
bundle is available at share/hak/efi-bundle under its installation root (or the
explicit --efi-bundle development override). A fresh ESP needs no inventory.bin
to reach installation. A subprocess validates the package and exact selected
device/offset/size; Escape cancels this read-only review. The confirmation starts
on Cancel. Only Enter on Install commits; Right does not confirm. Hashing and
copying run outside the speech/key loop, and completion is polled even with rapid
keyboard input. Once writes begin, leaving/killing the job is disallowed until
its result is known; the menu timeout is extended while installing. Successful
file installation explicitly reports that firmware activation remains pending.
All controls live in the existing Haiku boot manager; EFI adds no duplicate menu.

