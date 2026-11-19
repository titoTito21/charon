# Boot loader accessibility framework (HAK, Charon r396)

Charon already reads every UEFI program from its screen: console text, text
recognition (Unifont, rEFInd fonts, Windows Boot Manager TrueType, Limine,
OpenCore and Clover cell fonts, GRUB PF2) and built-in profiles of known boot
managers. The framework gives future boot loaders two more ways in, without
waiting for a new Charon release.

## 1. UEFI protocol `HAK_BOOT_A11Y_PROTOCOL` (for boot loader authors)

Header: `hak_boot_accessibility.h` (C, MIT-0: it can be included in GRUB
(GPLv3), systemd-boot (LGPL), Limine (BSD) and any other project).
GUID `{7a3c5e21-4b9d-4f1e-a652-1c8e930d27b4}`, revision 1.

Charon publishes the protocol while its reader runs: in the CHARON.EFI
application for the program it started, and in the CHARONVOICE.EFI add-on from
power-on. A boot loader finds it with `LocateProtocol`; without a reader there
is no protocol and the program works as usual.

| Function | What the reader does |
|---|---|
| `BeginScreen(title, flags)` | says the title and stops reading this program's screen by itself (no double speech); review keys still read the screen |
| `Focus(&focus)` | new item: name, role, value, states, "2 of 5"; same item: only the change (checked/unchecked, new value); edit field: the typed or deleted character, the character under the moved cursor |
| `Countdown(seconds, &stop)` | countdown sounds and speech as set by the user; `stop=1` when the user wants countdowns stopped |
| `Progress(percent, label)` | progress sounds and/or speech |
| `SetKeyHelp(lines, count)` | the program's keys at the top of the key list (Insert+F1, Alt+F1) |
| `Speak(text, flags)` | a message: normal, in the second (message) voice, or an alert |
| `EndScreen()` | the reader reads the screen by itself again (e.g. before the system starts) |
| `GetSettings(&settings)` | language, verbosity, whether positions are spoken, whether countdowns stop, whether a braille display is present |

Roles: text, menu item, submenu, button, check box, radio button, edit,
password (its value is never spoken), combo box, slider, tab, dialog, list item,
heading. States: checked, expanded, collapsed, unavailable, default, read only,
hidden, error. The reader speaks roles and states in the user's language; the
program gives only what it shows.

A complete example boot loader: `example/A11yDemoLoader.c` (menu with a
countdown, an entry editor with a cursor, a check box, a progress bar, its own
keys). Build: `python tools/build-a11y-demo.py DIRECTORY`.

Tested in QEMU (r396d, `work/charon-r396-pick.py ... demo1/demo2`; spoken in
Polish there): "Demo Loader 1.0", "automatic start in 10 seconds", "Debian
GNU/Linux, default, 1 of 5", "Quiet boot, check box, checked, 4 of 5", Space ->
"unchecked", editor: "Kernel options, edit, root=/dev/sda2 ro quiet", arrows ->
the letter under the cursor, typed "a", Backspace -> "a", Alt+F1 -> the
program's keys at the top of the list.

## 2. Profiles from `\EFI\HAK\profiles\*.txt` files (for users)

A boot loader Charon does not know can be described by a text file (UTF-8) on
Charon's disk, without a new Charon release:

```
# my boot loader
name=Demo Loader
signature=HAK accessibility example
key=strzałki wybierają, Enter uruchamia | arrows choose, Enter starts
reveal=esc
hidden=Przytrzymaj Escape | Hold Escape
```

- `name` — what the reader calls the manager (device list, start announcement);
- `signature` — text (at least 6 characters) from the program file, ASCII or UTF-16;
- `key` — up to 8 lines of the key list, Polish before `|`, English after it;
- `reveal` — the key that shows a hidden menu (esc, space, enter, f2, f4, f8,
  f10, f12, up, down, none);
- `hidden` — how a person shows the hidden menu.

Built-in signatures take precedence; files only add managers.
Tested in QEMU: the device list says "USB — … (Demo Loader, found by Charon)".

## 3. What keeps working without the framework

Programs with neither the protocol nor a profile are read by screen recognition
as before: selection from colours, frames, tabs, edit fields with the console
cursor or a drawn block cursor (r396: the Limine editor), countdowns, progress
bars.
