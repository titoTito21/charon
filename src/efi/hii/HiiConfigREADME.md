# HII configuration adapter

`HiiConfig` connects the existing immutable Database/Model snapshot to actual
UEFI ConfigAccess.ExtractConfig and ConfigRouting.RouteConfig protocols. It does
not call SetVariable, emulate browser callbacks, or edit firmware memory.

Bind selects one question in one exported package. Every operation rechecks the
database generation/serial, package identity, HII-to-driver ownership, the same
ConfigAccess/ConfigRouting protocol instances, and the driver's exact device
path. Rebinding/Reset invalidates prior proposals. No allocations survive a call.

Only ordinary buffer varstores and unconditional checkbox, unsigned numeric,
enum and UTF16 string controls are supported. Read-only controls can be read.
Callback/dynamic/conditional/vendor-dependent/signed-unresolved controls remain
unsupported; password fields and aliases overlapping password storage are never
requested. Missing routing is not replaced with a variable write. Strings are
bounded to 255 UTF16 code units, with surrogate validation; numeric width,
min/max/step and actual enum membership are checked. Reset-required flags are
reported to the caller, without resetting the machine.

Each extraction requests only the focused field, not the complete varstore.
The bounded response parser accepts that exact GUID/NAME/PATH/OFFSET/WIDTH and
one VALUE. Additional ranges/ALTCFG, malformed hex, wrong identity, truncated
results or inconsistent progress pointers fail without publishing a value.
Equivalent provider-specific offset/width formatting which does not echo the
canonical request is presently rejected; this conservative interoperability
limitation must not be mistaken for a malformed firmware diagnosis.

Propose validates the intended value and captures freshly extracted baseline
bytes. It performs no write. Commit requires explicit confirmation, verifies the
proposal owner/binding and unchanged baseline, revalidates constraints, and only
then invokes RouteConfig. It independently extracts the result even when routing
returns an error: result.attempted, routeStatus, readbackStatus, changed and applied
distinguish refusal, a partial change, successful application and unknown state.
A failure never triggers an automatic rollback. An editor retaining a value
while the user types must also detect changes since edit start before proposing.

These calls and parsing are synchronous. Firmware callbacks can take arbitrary
time and are not yet integrated with the audio/keyboard scheduler. A fake
protocol test is not a real firmware settings or speech accessibility test.
Unknown expressions and browser action callbacks require a full browser-state
implementation before exposing those controls as editable.

## Source and validation

Encoding and ABI were checked against pinned AUDK
`dfd5516702b4911f1fc1919a429d637b456df913`:

- `MdeModulePkg/Library/UefiHiiLib/HiiLib.c`, HiiConstructConfigHdr (723+): GUID
  bytes and device-path bytes in memory order; NAME UTF16 units as four hex
  digits; lowercase value digits.
- `MdeModulePkg/Universal/HiiDatabaseDxe/ConfigRouting.c`, BlockToConfig (6027+):
  VALUE reverses byte order; request fields use OFFSET/WIDTH hexadecimal.
- `MdePkg/Include/Protocol/HiiConfigAccess.h` and `HiiConfigRouting.h`: actual
  signatures and protocol slots, with local static layout assertions.

`HiiConfigTests.cpp` uses the real pinned VfrCompile-produced HiiFixture.hpk,
actual Database and Model, and controlled protocol implementations. It checks
exact independently specified request encoding, checkbox/enum/numeric/string
read and validation, explicit confirmation, baseline conflict, successful and
partially successful writes, denied writes, protected/aliased storage,
read-only/callback flags, response corruption, stale notifications, changed
driver/path, invalid UTF16, and result-readback behavior. MSVC /W4 /WX passes.

Host build command is recorded in `work/build-hii-config-tests.cmd`; provide the
compiled HiiFixture.hpk as the executable's only argument. This is a test input,
not a firmware package to install.

