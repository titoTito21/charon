# Read-only HII semantic foundation

`HiiModel` decodes one actual `ExportPackageLists(HiiHandle)` package list into
bounded formset, form, question, option, varstore and label records. It does not
call `SendForm`, intercept a firmware browser, retrieve current configuration,
speak a screen, or write firmware settings. It is a foundation for Charon's
UEFI settings overlay; it is not a completed overlay or accessible UEFI Shell.

Supported question descriptions include one-of enums, numeric values,
checkboxes, strings, protected password fields, actions and ordered lists.
Basic form references and date/time records are identified but flagged as
unsupported for editing. Numeric widths and encoded bounds/steps are preserved;
signed bounds remain explicitly unresolved. Options retain declared values and
default flags: neither means the option is currently selected.

Buffer, EFI-variable and name/value varstore identities are decoded with their
formset scope, GUID, identifier and name/size where present. Duplicate form or
question identifiers within their owning scope, duplicate varstores, bad opcode
lengths, unbalanced scopes and malformed package boundaries fail closed.

Labels use exact requested ASCII language tags. UCS2/UTF16 single/multiple
string blocks, font-tagged variants, duplicate IDs and skipped IDs are supported.
UTF16 surrogate pairs are validated. SCSU and unknown string extensions return
`UnsupportedStrings`: form metadata survives, but the entire selected string
table is withheld rather than exposing shifted/partially decoded IDs. Missing
language packages do not silently substitute another language. The eventual
adapter should prefer the firmware's `HiiString.GetString` for formats handled
by that protocol and apply an explicit language-fallback policy.

The decoder does not evaluate IFR expression bytecode. Suppress/gray/disable,
validation conditions, refresh rules, callbacks, vendor GUID extensions and
unknown opcodes produce conservative unresolved reason flags. Some flags apply
to an entire form or formset when the influence cannot be isolated. A condition
is never guessed false. Password fields expose prompt/help metadata only, with
the `Protected` reason; no password or other configuration values are read.
There is deliberately no `editable` property. Even an ordinary question with
zero reason flags is not permission to change configuration.

## Memory and lifetime

No allocations or firmware calls occur in the decoder. The caller allocates a
`Model` (prefer boot pool, not the EFI stack) and owns immutable snapshot bytes.
Limits: 4 MiB per exported list, 64 nested scopes, 16 formsets, 128 forms, 512
questions, 2048 options, 64 stores and 2048 resolved strings. Capacity exhaustion
returns `Limit`; malformed input returns `Invalid`. Both invalidate all counts
and the snapshot pointer, so partial records must never be published. Label
copying first reports the required UTF16 capacity and does not partially write
an undersized destination.

Parsing is synchronous with finite byte/count limits. It has not yet been split
into cooperative work units for the active audio loop. Do not integrate a large
package parse into a timer callback or assume it meets the audio driver's poll
deadline. An adapter must stage parsing outside TPL_NOTIFY and add cooperative
work budgeting or measure/enforce a suitable snapshot limit before integration.

Snapshot offsets and array indexes are not persistent firmware identities. Bind
the model to its actual HII handle, package-list GUID, driver handle and database
generation. Register package notifications, invalidate on removal/replacement,
and reacquire before an action. Never transfer a question's identity to another
handle merely because its numeric QuestionId or label matches.

## Configuration/callback adapter still required

1. Enumerate HII handles and export each list independently through
   `EFI_HII_DATABASE_PROTOCOL`; retain actual handle/driver ownership. Resolve
   labels through HiiString with the user's language and explicit fallback.
2. Build question visibility/enabled state with a real IFR evaluator and browser
   context. Conditions may depend on other questions, rules, security policy,
   callbacks or dynamic refresh. Unknown evaluation must disable the affected
   action, not bypass a suppressed control.
3. Use the owning `EFI_HII_CONFIG_ACCESS_PROTOCOL.ExtractConfig` and
   `EFI_HII_CONFIG_ROUTING_PROTOCOL` conversions for current values. Match
   GUID/NAME/PATH and validate block lengths, offsets, types, enum membership and
   string bounds. Do not replace routing with blind `SetVariable` writes.
4. Stage edits separately from the snapshot. Follow CHANGING/CHANGED and other
   required browser callbacks, propagate `EFI_BROWSER_ACTION_REQUEST`, and
   honor reset/reconnect/read-only requirements. Validate submit/inconsistent
   expressions and provider errors before an explicit user-confirmed commit.
   Revalidate handle/generation immediately before routing. On stale state,
   reject and refresh rather than replay into a replacement form.
5. Protected input needs dedicated non-echo handling, bounded owned storage and
   erasure on cancellation. Never put secret contents in speech, labels, logs,
   package snapshots or generic undo state. Implement actions through their
   callback/config-string contract, not by treating them as numeric variables.

Firmware forms not exported through standard HII, proprietary setup browsers,
legacy BIOS interfaces and Shell terminal I/O require separate adapters. This
module does not establish universal firmware accessibility or Secure Boot key
management. Shell work is separate from HII question parsing.

## Validation and primary layout sources

The layouts come from the pinned AUDK commit
`dfd5516702b4911f1fc1919a429d637b456df913`:

- `MdePkg/Include/Uefi/UefiInternalFormRepresentation.h`
- `MdePkg/Include/Protocol/HiiDatabase.h`
- `MdePkg/Include/Protocol/HiiString.h`
- `MdePkg/Include/Protocol/HiiConfigAccess.h`
- `MdePkg/Include/Protocol/HiiConfigRouting.h`

`HiiModelTests.cpp` exercises the public parser with genuine package-layout
fixtures, every truncation, malformed lengths/scopes, language selection,
duplicate/skip strings, protected controls, capacity limits and 12000 hostile
mutations. `HiiFixture.vfr` is independently compiled by the actual pinned AUDK
VfrCompile; the resulting `.hpk` is wrapped as an exported package list and
decoded by the same parser. This checks real compiler-produced formset classes,
default stores, enum options, question IDs, varstore offsets and numeric widths.
MSVC `/W4 /WX` builds and these tests pass. This is not a firmware keyboard or
speech integration test.

```text
VfrCompile -n -b -l -o <output-directory> HiiFixture.vfr
cl /EHsc /std:c++17 /W4 /WX /O2 HiiModel.cpp HiiModelTests.cpp
HiiModelTests.exe <output-directory>/HiiFixture.hpk
```
