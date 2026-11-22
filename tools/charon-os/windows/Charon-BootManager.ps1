<#
.SYNOPSIS
Charon boot manager: the firmware boot order and Charon's place in it, from the running
Windows.

.DESCRIPTION
Lists the firmware boot entries in BootOrder with Charon's position, moves entries, makes
Charon first or second or takes it out of the order (its files and its entry stay), sets
Charon's Continue target (\EFI\HAK\continue.bin, as Charon's own SaveContinue writes it),
starts Charon's menu at the next restart only (BootNext), restarts into firmware setup
(OsIndications) and shows the Secure Boot state. Every change of a firmware variable is
asked first and read back afterwards; BitLocker is checked before a change that alters
what firmware starts.

Without switches a dialog opens (standard Windows controls). -Console gives the same
functions as numbered text menus.

  -Console            numbered text menus instead of the dialog
  -Language en|pl     language of the program
  -EspPath DIR        an ordinary folder stands in for the EFI system partition
  -NoFirmware         never call the firmware variable interfaces
  -FirmwareDir DIR    tests: a folder of efivarfs-style files stands in for firmware variables
  -List               print the boot order and exit
  -Move XXXX up|down  move the entry BootXXXX one place
  -Charon first|second|remove|restore
  -Continue XXXX      make BootXXXX Charon's Continue target
  -BootNextCharon     Charon's menu at the next restart only
  -FirmwareSetup      firmware setup at the next restart
  -Restart            restart now
  -Yes                answer the confirmation with yes (non-interactive runs)
  -Version

Exit codes: 0 done, 1 error, 5 request refused, 7 firmware variables not available.
Windows PowerShell 5.1. GPL-2.0-or-later.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$EspPath = '',
    [switch]$NoFirmware,
    [string]$FirmwareDir = '',
    [string]$Language = '',
    [switch]$Console,
    [switch]$List,
    [string]$Move = '',
    [string]$Charon = '',
    [string]$Continue = '',
    [switch]$BootNextCharon,
    [switch]$FirmwareSetup,
    [switch]$Restart,
    [switch]$Yes,
    [switch]$Version,
    [switch]$GuiSelfTest,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest = @()
)
$ErrorActionPreference = 'Stop'
$script:CharonEspPath = $EspPath
$script:CharonFirmwareMode = if ($NoFirmware) { 'none' } elseif ($FirmwareDir) { 'dir' } else { 'real' }
$script:CharonFirmwareDir = $FirmwareDir
$script:CharonLanguage = $Language.ToLowerInvariant()
$script:CharonYes = [bool]$Yes
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'CharonOs.Common.ps1')
$script:Hak = $null
$script:ExitCode = 0
$script:State = $null
function Stop-Refused([string]$Text) { $script:ExitCode = 5; throw $Text }
function Format-BootId([int]$Id) { return ('{0:X4}' -f $Id) }

# ---- reading ---------------------------------------------------------------------------
# ParseContinueTarget (src/efi/ContinueTarget.h): "HAKCONT1", entry number (2 bytes), size
# (4 bytes), the bytes of the Boot#### variable, "HEND".
function Read-ContinueTarget {
    $result = [pscustomobject]@{ State = 'none'; Id = $null; Bytes = $null }
    if (-not $script:Hak) { return $result }
    $file = [IO.Path]::Combine($script:Hak, 'continue.bin')
    if (-not [IO.File]::Exists($file)) { return $result }
    $result.State = 'invalid'
    $b = [IO.File]::ReadAllBytes($file)
    if ($b.Length -lt 18 -or $b.Length -ge 65555) { return $result }
    if ([Text.Encoding]::ASCII.GetString($b, 0, 8) -cne 'HAKCONT1' -or [Text.Encoding]::ASCII.GetString($b, $b.Length - 4, 4) -cne 'HEND') { return $result }
    if ([BitConverter]::ToUInt32($b, 10) -ne ($b.Length - 18)) { return $result }
    $payload = New-Object byte[] ($b.Length - 18); [Array]::Copy($b, 14, $payload, 0, $payload.Length)
    if (-not (Read-CharonLoadOption $payload).Valid) { return $result }
    $result.State = 'ok'; $result.Id = [int][BitConverter]::ToUInt16($b, 8); $result.Bytes = $payload
    return $result
}
function Get-BootState {
    $order = Get-CharonOrder 'BootOrder'
    $entries = @{}
    $ids = @($order) + @(0..255 | Where-Object { $order -notcontains $_ })
    foreach ($id in $ids) {
        $data = Get-FwVar (Get-CharonOptionName 'Boot' $id)
        if ($null -eq $data) { continue }
        $option = Read-CharonLoadOption $data
        $entries[$id] = [pscustomobject]@{ Id = $id; Data = $data; Option = $option; Own = (Test-CharonOwnEntry $option) }
    }
    $next = Get-FwVar 'BootNext'; $current = Get-FwVar 'BootCurrent'; $timeout = Get-FwVar 'Timeout'
    $own = @($ids | Where-Object { $entries.ContainsKey($_) -and $entries[$_].Own })
    # The boot manager entry "Charon" comes before the reader-only entry "Charon czytnik".
    $main = $null
    foreach ($id in $own) { if ($entries[$id].Option.Description -eq 'Charon') { $main = $id; break } }
    if ($null -eq $main -and $own.Count) { $main = $own[0] }
    $script:State = [pscustomobject]@{
        Order = [int[]]$order; Entries = $entries; Own = [int[]]$own; Main = $main
        Next = $(if ($null -ne $next -and $next.Length -ge 2) { [int][BitConverter]::ToUInt16($next, 0) } else { $null })
        Current = $(if ($null -ne $current -and $current.Length -ge 2) { [int][BitConverter]::ToUInt16($current, 0) } else { $null })
        Timeout = $(if ($null -ne $timeout -and $timeout.Length -ge 2) { [int][BitConverter]::ToUInt16($timeout, 0) } else { $null })
        Continue = (Read-ContinueTarget); Secure = (Get-CharonSecureBoot)
    }
    return $script:State
}
function Get-EntryName([int]$Id) {
    $e = $script:State.Entries[$Id]
    if (-not $e) { return ('Boot' + (Format-BootId $Id) + (T ' (no such entry)' ' (brak takiego wpisu)')) }
    $name = $e.Option.Description; if (-not $name) { $name = T '(no name)' '(bez nazwy)' }
    return ($name + ', Boot' + (Format-BootId $Id))
}
function Get-EntryLine([int]$Position, [int]$Id) {
    $e = $script:State.Entries[$Id]
    $text = ('{0}. {1}' -f $Position, (Get-EntryName $Id))
    if (-not $e) { return $text }
    if ($e.Option.File) { $text += ', ' + $e.Option.File }
    if ($e.Own) { $text += ', ' + (T 'this is Charon' 'to jest Charon') }
    if (-not $e.Option.Active) { $text += ', ' + (T 'inactive' 'nieaktywny') }
    $c = $script:State.Continue
    if ($c.State -eq 'ok' -and $c.Id -eq $Id) {
        if (Test-CharonBytesEqual $c.Bytes $e.Data) { $text += ', ' + (T 'Charon''s Continue target' 'cel Kontynuuj Charona') }
        else { $text += ', ' + (T 'Charon''s Continue target, but the entry has changed since it was set: Charon will not start it' 'cel Kontynuuj Charona, ale wpis zmienił się od ustawienia: Charon go nie uruchomi') }
    }
    if ($script:State.Next -eq $Id) { $text += ', ' + (T 'starts at the next restart only' 'uruchomi się tylko przy następnym restarcie') }
    if ($script:State.Current -eq $Id) { $text += ', ' + (T 'started this time' 'uruchomiony tym razem') }
    return $text
}
function Get-OrderLines {
    $s = $script:State
    $lines = @((T ("Boot order, {0} entries:" -f $s.Order.Count) ("Kolejność rozruchu, liczba wpisów: {0}:" -f $s.Order.Count)))
    for ($i = 0; $i -lt $s.Order.Count; $i++) { $lines += (Get-EntryLine ($i + 1) $s.Order[$i]) }
    if ($null -eq $s.Main) { $lines += (T 'Charon has no boot entry in firmware (only the reader add-on may be installed).' 'Charon nie ma wpisu rozruchu we firmware (może być zainstalowany tylko dodatek czytnika).') }
    elseif ($s.Order -notcontains $s.Main) { $lines += (T ("Charon ({0}) is not in the boot order." -f (Get-EntryName $s.Main)) ("Charona ({0}) nie ma w kolejności rozruchu." -f (Get-EntryName $s.Main))) }
    else { $lines += (T ("Charon is at position {0} of {1}." -f ([array]::IndexOf($s.Order, $s.Main) + 1), $s.Order.Count) ("Charon jest na pozycji {0} z {1}." -f ([array]::IndexOf($s.Order, $s.Main) + 1), $s.Order.Count)) }
    if ($null -ne $s.Next) { $lines += (T ("At the next restart only: {0}." -f (Get-EntryName $s.Next)) ("Tylko przy następnym restarcie: {0}." -f (Get-EntryName $s.Next))) }
    if ($s.Continue.State -eq 'none') { $lines += (T 'Continue target: not set; Charon continues with the first other entry of the boot order.' 'Cel Kontynuuj: nie ustawiono; Charon kontynuuje pierwszym innym wpisem z kolejności rozruchu.') }
    elseif ($s.Continue.State -eq 'invalid') { $lines += (T 'Continue target: the file continue.bin is not valid.' 'Cel Kontynuuj: plik continue.bin jest nieprawidłowy.') }
    else { $lines += (T ("Continue target: {0}." -f (Get-EntryName $s.Continue.Id)) ("Cel Kontynuuj: {0}." -f (Get-EntryName $s.Continue.Id))) }
    $lines += (Get-SecureBootLine)
    return , $lines
}
function Get-SecureBootLine {
    switch ($script:State.Secure) {
        'on' { return (T 'Secure Boot: on.' 'Secure Boot: włączony.') }
        'off' { return (T 'Secure Boot: off.' 'Secure Boot: wyłączony.') }
        default { return (T 'Secure Boot: state unknown.' 'Secure Boot: stan nieznany.') }
    }
}
function Get-OrderText([int[]]$Ids) {
    $parts = @(); for ($i = 0; $i -lt $Ids.Count; $i++) { $parts += ('{0}. {1}' -f ($i + 1), (Get-EntryName $Ids[$i])) }
    return ($parts -join '; ')
}

# ---- changes: asked first, read back afterwards --------------------------------------
function Set-BootOrder([int[]]$NewOrder, [string]$What) {
    $old = $script:State.Order
    if (($old -join ',') -eq ($NewOrder -join ',')) { return (T 'The boot order is already like that. Nothing was changed.' 'Kolejność rozruchu już taka jest. Nic nie zmieniono.') }
    $question = $What + ' ' + (T ("The firmware variable BootOrder will change. Now: {0}. After the change: {1}." -f (Get-OrderText $old), (Get-OrderText $NewOrder)) ("Zmieni się zmienna firmware BootOrder. Teraz: {0}. Po zmianie: {1}." -f (Get-OrderText $old), (Get-OrderText $NewOrder)))
    if (-not (Confirm-Charon $question)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    if (-not (Confirm-CharonBitLocker)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    # The order must still be what was shown (another program may have changed it meanwhile).
    if (((Get-CharonOrder 'BootOrder') -join ',') -ne ($old -join ',')) { Stop-Refused (T 'The boot order changed in the meantime. Nothing was written; look at the list again.' 'Kolejność rozruchu zmieniła się w międzyczasie. Nic nie zapisano; sprawdź listę ponownie.') }
    Set-FwVar 'BootOrder' (ConvertTo-CharonOrder $NewOrder)
    [void](Get-BootState)
    if (($script:State.Order -join ',') -ne ($NewOrder -join ',')) { throw (T 'The boot order read back from firmware is not what was written.' 'Kolejność rozruchu odczytana z firmware nie jest tą, którą zapisano.') }
    return ((T 'Done. Read back from firmware: ' 'Gotowe. Odczytano z firmware: ') + (Get-OrderText $script:State.Order))
}
function Move-BootEntry([int]$Id, [string]$Direction) {
    $order = @($script:State.Order)
    $at = [array]::IndexOf($order, $Id)
    if ($at -lt 0) { Stop-Refused (T ("Boot{0} is not in the boot order." -f (Format-BootId $Id)) ("Boot{0} nie ma w kolejności rozruchu." -f (Format-BootId $Id))) }
    $to = $at - 1; if ($Direction -eq 'down') { $to = $at + 1 }
    if ($to -lt 0) { Stop-Refused (T 'It is already the first entry.' 'To już pierwszy wpis.') }
    if ($to -ge $order.Count) { Stop-Refused (T 'It is already the last entry.' 'To już ostatni wpis.') }
    $order[$at] = $order[$to]; $order[$to] = $Id
    return (Set-BootOrder ([int[]]$order) (T ("Move {0} to position {1}?" -f (Get-EntryName $Id), ($to + 1)) ("Przenieść {0} na pozycję {1}?" -f (Get-EntryName $Id), ($to + 1))))
}
function Set-CharonPlace([string]$Place) {
    $s = $script:State
    if ($null -eq $s.Main) { Stop-Refused (T 'Charon has no boot entry in firmware; install the Charon boot manager first.' 'Charon nie ma wpisu rozruchu we firmware; najpierw zainstaluj menedżer rozruchu Charon.') }
    $others = @($s.Order | Where-Object { $_ -ne $s.Main })
    switch ($Place) {
        'first' { return (Set-BootOrder ([int[]](@($s.Main) + $others)) (T 'Make Charon the first boot entry? Firmware then starts Charon, which speaks its menu.' 'Ustawić Charona jako pierwszy wpis rozruchu? Firmware będzie wtedy uruchamiać Charona, który odczyta swoje menu.')) }
        'second' {
            $system = @($others | Where-Object { -not ($s.Entries[$_] -and $s.Entries[$_].Own) })
            if ($system.Count -eq 0) { Stop-Refused (T 'There is no other boot entry to put before Charon.' 'Nie ma innego wpisu rozruchu, który można postawić przed Charonem.') }
            $at = [array]::IndexOf($others, $system[0])
            $new = @(); for ($i = 0; $i -lt $others.Count; $i++) { $new += $others[$i]; if ($i -eq $at) { $new += $s.Main } }
            return (Set-BootOrder ([int[]]$new) (T ("Make Charon the second boot entry, after {0}? The computer then starts that entry without Charon's menu." -f (Get-EntryName $system[0])) ("Ustawić Charona jako drugi wpis rozruchu, po {0}? Komputer będzie wtedy uruchamiać tamten wpis bez menu Charona." -f (Get-EntryName $system[0]))))
        }
        'remove' {
            $kept = @($s.Order | Where-Object { -not ($s.Entries[$_] -and $s.Entries[$_].Own) })
            if ($kept.Count -eq $s.Order.Count) { return (T 'Charon is not in the boot order. Nothing was changed.' 'Charona nie ma w kolejności rozruchu. Nic nie zmieniono.') }
            if ($kept.Count -eq 0) { Stop-Refused (T 'Charon is the only boot entry; the boot order would be empty. Nothing was changed.' 'Charon jest jedynym wpisem rozruchu; kolejność byłaby pusta. Nic nie zmieniono.') }
            $voice = $null; try { $voice = Find-CharonVoice } catch { }
            $warning = if ($null -ne $voice) { T 'The reader add-on stays installed and keeps reading the screens before the system.' 'Dodatek czytnika pozostaje zainstalowany i nadal czyta ekrany przed systemem.' } else { T 'The reader add-on is not installed: the computer will then start without any speech before the system.' 'Dodatek czytnika nie jest zainstalowany: komputer będzie wtedy startował bez mowy przed systemem.' }
            return (Set-BootOrder ([int[]]$kept) (T ("Take Charon out of the boot order? Its boot entry and its files on the EFI partition stay, so it can be put back. " + $warning) ("Usunąć Charona z kolejności rozruchu? Jego wpis rozruchu i pliki na partycji EFI zostają, więc można go przywrócić. " + $warning)))
        }
        'restore' {
            if ($s.Order -contains $s.Main) { return (T 'Charon is already in the boot order. Nothing was changed.' 'Charon już jest w kolejności rozruchu. Nic nie zmieniono.') }
            return (Set-BootOrder ([int[]](@($s.Order) + @($s.Main))) (T 'Put Charon back into the boot order, as the last entry?' 'Przywrócić Charona do kolejności rozruchu, jako ostatni wpis?'))
        }
        default { Stop-Refused '-Charon first|second|remove|restore' }
    }
}
# SaveContinue (src/efi/BootMenu.h): the marker continue-set first, then continue.bin is
# deleted and written anew. The target must be in BootOrder, must not be Charon, and its
# Boot#### bytes are stored as they are, because Charon starts the target only while the
# firmware entry is still byte-identical (ValidateContinueTarget).
function Set-ContinueTarget([int]$Id) {
    $s = $script:State
    if (-not $script:Hak) { Stop-Refused (T 'Charon is not installed on the EFI partition (no \EFI\HAK): the Continue target cannot be stored.' 'Charon nie jest zainstalowany na partycji EFI (brak \EFI\HAK): nie można zapisać celu Kontynuuj.') }
    if ($s.Order -notcontains $Id) { Stop-Refused (T ("Boot{0} is not in the boot order." -f (Format-BootId $Id)) ("Boot{0} nie ma w kolejności rozruchu." -f (Format-BootId $Id))) }
    $e = $s.Entries[$Id]
    if (-not $e) { Stop-Refused (T ("There is no entry Boot{0}." -f (Format-BootId $Id)) ("Nie ma wpisu Boot{0}." -f (Format-BootId $Id))) }
    if ($e.Own) { Stop-Refused (T 'Charon cannot continue with itself.' 'Charon nie może kontynuować samym sobą.') }
    if (-not $e.Option.Valid -or $e.Data.Length + 18 -ge 65555) { Stop-Refused (T 'Charon does not start this entry (inactive, a firmware application, or a damaged path).' 'Charon nie uruchamia tego wpisu (nieaktywny, aplikacja firmware albo uszkodzona ścieżka).') }
    $question = T ("Make {0} the entry Charon starts by itself (Continue)? This writes continue.bin in \EFI\HAK on the EFI partition; no firmware variable changes." -f (Get-EntryName $Id)) ("Ustawić {0} jako wpis, który Charon uruchamia sam (Kontynuuj)? Zapisuje to continue.bin w \EFI\HAK na partycji EFI; żadna zmienna firmware się nie zmienia." -f (Get-EntryName $Id))
    if (-not (Confirm-Charon $question)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    $record = [byte[]]([Text.Encoding]::ASCII.GetBytes('HAKCONT1') + [BitConverter]::GetBytes([uint16]$Id) + [BitConverter]::GetBytes([uint32]$e.Data.Length) + $e.Data + [Text.Encoding]::ASCII.GetBytes('HEND'))
    $marker = [IO.Path]::Combine($script:Hak, 'continue-set')
    $stream = New-Object IO.FileStream($marker, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.WriteByte(1); $stream.Flush($true) } finally { $stream.Dispose() }
    $file = [IO.Path]::Combine($script:Hak, 'continue.bin')
    if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
    Write-CharonFileDurably $file $record
    if (-not (Test-CharonBytesEqual ([IO.File]::ReadAllBytes($file)) $record)) { throw (T 'continue.bin read back does not match what was written.' 'Odczyt kontrolny continue.bin nie zgadza się z zapisem.') }
    [void](Get-BootState)
    return (T ("Done. Read back: Charon continues with {0}." -f (Get-EntryName $script:State.Continue.Id)) ("Gotowe. Odczytano: Charon kontynuuje wpisem {0}." -f (Get-EntryName $script:State.Continue.Id)))
}
function Set-BootNextCharon {
    $s = $script:State
    if ($null -eq $s.Main) { Stop-Refused (T 'Charon has no boot entry in firmware.' 'Charon nie ma wpisu rozruchu we firmware.') }
    $question = T ("Start Charon's menu at the next restart only? The firmware variable BootNext is set to {0}; the boot order stays as it is." -f (Get-EntryName $s.Main)) ("Uruchomić menu Charona tylko przy następnym restarcie? Zmienna firmware BootNext zostanie ustawiona na {0}; kolejność rozruchu zostaje bez zmian." -f (Get-EntryName $s.Main))
    if (-not (Confirm-Charon $question)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    if (-not (Confirm-CharonBitLocker)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    Set-FwVar 'BootNext' ([BitConverter]::GetBytes([uint16]$s.Main))
    [void](Get-BootState)
    if ($script:State.Next -ne $s.Main) { throw (T 'BootNext read back from firmware is not what was written.' 'BootNext odczytany z firmware nie jest tym, co zapisano.') }
    return (T ("Done. Read back from firmware: at the next restart only, {0}." -f (Get-EntryName $script:State.Next)) ("Gotowe. Odczytano z firmware: tylko przy następnym restarcie {0}." -f (Get-EntryName $script:State.Next)))
}
function Set-FirmwareSetup {
    $supported = Get-FwVar 'OsIndicationsSupported'
    if ($null -eq $supported -or $supported.Length -lt 8 -or (([BitConverter]::ToUInt64($supported, 0) -band 1) -eq 0)) { Stop-Refused (T 'This firmware does not offer a restart into its setup (OsIndicationsSupported).' 'To firmware nie oferuje restartu do swoich ustawień (OsIndicationsSupported).') }
    $question = T 'Open firmware setup at the next restart? The firmware variable OsIndications gets the "boot to firmware user interface" bit. Firmware setup itself does not speak unless the Charon reader add-on is installed.' 'Otworzyć ustawienia firmware przy następnym restarcie? Zmienna firmware OsIndications dostanie bit „uruchom interfejs firmware”. Same ustawienia firmware nie mówią, chyba że zainstalowany jest dodatek czytnika Charon.'
    if (-not (Confirm-Charon $question)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    $now = Get-FwVar 'OsIndications'
    [uint64]$value = 0; if ($null -ne $now -and $now.Length -ge 8) { $value = [BitConverter]::ToUInt64($now, 0) }
    $value = $value -bor [uint64]1
    Set-FwVar 'OsIndications' ([BitConverter]::GetBytes([uint64]$value))
    $check = Get-FwVar 'OsIndications'
    if ($null -eq $check -or $check.Length -lt 8 -or (([BitConverter]::ToUInt64($check, 0) -band 1) -eq 0)) { throw (T 'OsIndications read back from firmware does not have the bit.' 'OsIndications odczytane z firmware nie ma tego bitu.') }
    return (T 'Done. Read back from firmware: the next restart opens firmware setup.' 'Gotowe. Odczytano z firmware: następny restart otworzy ustawienia firmware.')
}
function Restart-Computer-Now {
    if (-not (Confirm-Charon (T 'Restart the computer now? Unsaved work in other programs can be lost.' 'Uruchomić komputer ponownie teraz? Niezapisana praca w innych programach może przepaść.'))) { return (T 'Cancelled.' 'Anulowano.') }
    if ($script:CharonFirmwareMode -ne 'real') { return (T 'Would restart now (stand-in firmware: nothing is restarted).' 'Would restart now: teraz nastąpiłby restart (firmware zastępcze: nic nie jest restartowane).') }
    & (Join-Path $env:SystemRoot 'System32\shutdown.exe') /r /t 0 | Out-Null
    return (T 'Restarting.' 'Restartuję.')
}
# Secure Boot: this program shows the state. Turning Secure Boot off is done by the computer
# maker's own interface and lives in tools/charon-addon/Charon-SecureBoot.ps1 (owned by the
# Windows installer): no maker logic is here. When that file is present it is dot-sourced
# and its two functions are used: Get-CharonSecureBootState (read only) and
# Disable-CharonSecureBoot, which explains, asks for a typed word and for the owner's
# firmware supervisor password, and suspends BitLocker itself. On stand-in firmware it is
# called with -NoFirmware, its trial mode.
function Find-SecureBootScript {
    foreach ($p in @((Join-Path $here 'Charon-SecureBoot.ps1'), (Join-Path $here '..\..\charon-addon\Charon-SecureBoot.ps1'), (Join-Path $here '..\Charon-SecureBoot.ps1'))) { if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path } }
    return $null
}
function Get-SecureBootLines {
    $lines = @((Get-SecureBootLine))
    try { $setup = Get-FwVar 'SetupMode'; if ($null -ne $setup -and $setup.Length -ge 1) { $lines += $(if ($setup[0] -eq 1) { T 'Firmware is in Secure Boot setup mode (no platform key).' 'Firmware jest w trybie konfiguracji Secure Boot (brak klucza platformy).' } else { T 'Firmware is in Secure Boot user mode (a platform key is enrolled).' 'Firmware jest w trybie użytkownika Secure Boot (klucz platformy jest wgrany).' }) } } catch { }
    if (Test-CharonBitLockerOn) { $lines += (T 'BitLocker protects this Windows: a change of the boot order, of BootNext or of Secure Boot can make Windows ask for the recovery key.' 'Ten Windows jest chroniony funkcją BitLocker: zmiana kolejności rozruchu, BootNext albo Secure Boot może spowodować żądanie klucza odzyskiwania.') }
    $lines += (T 'With Secure Boot on, firmware starts Charon only when it is signed with an enrolled key (the Charon installer does that, through shim or with the computer''s own certificate).' 'Przy włączonym Secure Boot firmware uruchomi Charona tylko wtedy, gdy jest podpisany wgranym kluczem (robi to instalator Charona, przez shim albo certyfikatem tego komputera).')
    $helper = Find-SecureBootScript
    if (-not $helper) { $lines += (T 'This program shows the Secure Boot state only; it does not change it.' 'Ten program tylko pokazuje stan Secure Boot; nie zmienia go.'); return , $lines }
    try {
        . $helper
        $state = Get-CharonSecureBootState
        if ($state.Supported) { $lines += (T ("Secure Boot can be turned off from Windows on this computer ({0}) with the maker's own interface; the firmware supervisor password of the owner is needed." -f $state.Model) ("Na tym komputerze ({0}) Secure Boot można wyłączyć z Windows interfejsem producenta; potrzebne jest hasło nadzorcy firmware właściciela." -f $state.Model)) }
        else { $lines += (T ("Turning Secure Boot off from Windows is not offered on this computer ({0}). {1}" -f $state.Model, $state.ReasonEn) ("Wyłączenie Secure Boot z Windows nie jest oferowane na tym komputerze ({0}). {1}" -f $state.Model, $state.ReasonPl)) }
    } catch { $lines += (T ("The maker-interface helper could not be read: {0}" -f $_.Exception.Message) ("Nie udało się wczytać pomocnika interfejsu producenta: {0}" -f $_.Exception.Message)) }
    return , $lines
}
function Test-SecureBootHelper { return ($null -ne (Find-SecureBootScript)) }
function Invoke-SecureBootHelper {
    $helper = Find-SecureBootScript
    if (-not $helper) { return (T 'Charon-SecureBoot.ps1 is not installed; the Secure Boot state can only be shown.' 'Charon-SecureBoot.ps1 nie jest zainstalowany; stan Secure Boot można tylko pokazać.') }
    . $helper
    if (-not (Get-Command Disable-CharonSecureBoot -CommandType Function -ErrorAction SilentlyContinue)) { return (T "The helper $helper does not define Disable-CharonSecureBoot." "Pomocnik $helper nie definiuje Disable-CharonSecureBoot.") }
    $arguments = @{
        Polish = $script:CharonPl
        Say = { param($textPl, $textEn) $t = T $textEn $textPl; if ($script:CharonGui) { Show-CharonMessage $t } else { Out-Line $t } }
        Ask = { param($prompt) if ($script:CharonGui) { return (Read-CharonGuiLine $prompt $false) }; $line = Read-CharonLine ($prompt + ':'); if ($null -eq $line) { return '' }; return $line }
        AskSecret = {
            param($prompt)
            if ($script:CharonGui) { return (ConvertTo-CharonSecureString (Read-CharonGuiLine $prompt $true)) }
            if ([Console]::IsInputRedirected) { return (ConvertTo-CharonSecureString ([Console]::In.ReadLine())) }
            return (Read-Host $prompt -AsSecureString)
        }
    }
    if ($script:CharonFirmwareMode -ne 'real') { $arguments.NoFirmware = $true }
    $result = Disable-CharonSecureBoot @arguments
    return (T ("Result of the maker-interface helper: {0}. {1}" -f $result.Status, $result.Detail) ("Wynik pomocnika interfejsu producenta: {0}. {1}" -f $result.Status, $result.Detail))
}

# ---- console interface -----------------------------------------------------------------
function Select-ConsoleEntry([string]$Prompt) {
    $order = $script:State.Order
    for ($i = 0; $i -lt $order.Count; $i++) { Out-Line (Get-EntryLine ($i + 1) $order[$i]) }
    Out-Line (T '0. Back' '0. Wstecz')
    $n = Read-CharonNumber $Prompt $order.Count
    if ($n -le 0) { return $null }
    return $order[$n - 1]
}
function Invoke-ConsoleAction([scriptblock]$Action) {
    try { Out-Line (& $Action) } catch { Out-Line $_.Exception.Message }
    try { [void](Get-BootState) } catch { }
}
function Show-ConsoleMenu {
    Out-Line (T 'Charon boot manager' 'Menedżer rozruchu Charon')
    foreach ($line in (Get-OrderLines)) { Out-Line $line }
    $list = $true
    while ($true) {
      if ($list) {
        Out-Line ''
        Out-Line (T 'Main menu, 13 items' 'Menu główne, 13 pozycji')
        Out-Line (T '1. List the boot order' '1. Pokaż kolejność rozruchu')
        Out-Line (T '2. Move an entry up' '2. Przenieś wpis w górę')
        Out-Line (T '3. Move an entry down' '3. Przenieś wpis w dół')
        Out-Line (T '4. Make Charon first' '4. Ustaw Charona jako pierwszy')
        Out-Line (T '5. Make Charon second, after the system''s own boot manager' '5. Ustaw Charona jako drugi, po menedżerze rozruchu systemu')
        Out-Line (T '6. Take Charon out of the boot order (its files and its entry stay)' '6. Usuń Charona z kolejności rozruchu (pliki i wpis zostają)')
        Out-Line (T '7. Put Charon back into the boot order' '7. Przywróć Charona do kolejności rozruchu')
        Out-Line (T '8. Set the entry Charon starts by itself (Continue target)' '8. Ustaw wpis, który Charon uruchamia sam (cel Kontynuuj)')
        Out-Line (T '9. Start Charon''s menu at the next restart only' '9. Uruchom menu Charona tylko przy następnym restarcie')
        Out-Line (T '10. Open firmware setup at the next restart' '10. Otwórz ustawienia firmware przy następnym restarcie')
        Out-Line (T '11. Restart now' '11. Uruchom ponownie teraz')
        Out-Line (T '12. Secure Boot' '12. Secure Boot')
        Out-Line (T '13. Program language: English. Switch to Polish' '13. Język programu: polski. Przełącz na angielski')
        Out-Line (T '0. Quit' '0. Zakończ')
      }
        $list = $false
        $n = Read-CharonMenu (T 'Number (L lists the menu again):' 'Numer (L ponownie czyta menu):') 13
        if ($n -eq -2) { $list = $true; continue }
        if ($n -le 0) { Out-Line (T 'Goodbye.' 'Do widzenia.'); return }
        switch ($n) {
            1 { try { [void](Get-BootState); foreach ($line in (Get-OrderLines)) { Out-Line $line } } catch { Out-Line $_.Exception.Message } }
            2 { $id = Select-ConsoleEntry (T 'Number of the entry to move up:' 'Numer wpisu do przeniesienia w górę:'); if ($null -ne $id) { Invoke-ConsoleAction { Move-BootEntry $id 'up' } } }
            3 { $id = Select-ConsoleEntry (T 'Number of the entry to move down:' 'Numer wpisu do przeniesienia w dół:'); if ($null -ne $id) { Invoke-ConsoleAction { Move-BootEntry $id 'down' } } }
            4 { Invoke-ConsoleAction { Set-CharonPlace 'first' } }
            5 { Invoke-ConsoleAction { Set-CharonPlace 'second' } }
            6 { Invoke-ConsoleAction { Set-CharonPlace 'remove' } }
            7 { Invoke-ConsoleAction { Set-CharonPlace 'restore' } }
            8 { $id = Select-ConsoleEntry (T 'Number of the entry Charon should start by itself:' 'Numer wpisu, który Charon ma uruchamiać sam:'); if ($null -ne $id) { Invoke-ConsoleAction { Set-ContinueTarget $id } } }
            9 { Invoke-ConsoleAction { Set-BootNextCharon } }
            10 { Invoke-ConsoleAction { Set-FirmwareSetup } }
            11 { Invoke-ConsoleAction { Restart-Computer-Now } }
            12 {
                foreach ($line in (Get-SecureBootLines)) { Out-Line $line }
                if (Test-SecureBootHelper) {
                    $c = Read-CharonNumber (T '1 turn Secure Boot off with the computer maker''s interface, 0 back:' '1 wyłącz Secure Boot interfejsem producenta komputera, 0 wstecz:') 1
                    if ($c -eq 1) { Invoke-ConsoleAction { Invoke-SecureBootHelper } }
                }
            }
            13 { $script:CharonPl = -not $script:CharonPl; $list = $true }
        }
    }
}

# ---- dialog -------------------------------------------------------------------------------
function Update-BootForm([string]$Message) {
    try { [void](Get-BootState) } catch { $Message = $_.Exception.Message }
    $list = $script:BootList; $keep = $list.SelectedIndex
    $list.BeginUpdate(); $list.Items.Clear()
    for ($i = 0; $i -lt $script:State.Order.Count; $i++) { [void]$list.Items.Add((Get-EntryLine ($i + 1) $script:State.Order[$i])) }
    $list.EndUpdate()
    if ($list.Items.Count) { $list.SelectedIndex = [Math]::Max(0, [Math]::Min($keep, $list.Items.Count - 1)) }
    $lines = Get-OrderLines
    $script:StatusBox.Text = ((@($Message) + $lines[($script:State.Order.Count + 1)..($lines.Count - 1)]) | Where-Object { $_ }) -join "`r`n"
}
function Invoke-FormAction([scriptblock]$Action, [int]$SelectId = -1) {
    $text = ''
    try { $text = & $Action } catch { $text = $_.Exception.Message }
    Update-BootForm $text
    if ($SelectId -ge 0) { $at = [array]::IndexOf($script:State.Order, $SelectId); if ($at -ge 0) { $script:BootList.SelectedIndex = $at } }
    if ($text) { Show-CharonMessage $text }
}
function Get-SelectedBootId {
    $at = $script:BootList.SelectedIndex
    if ($at -lt 0 -or $at -ge $script:State.Order.Count) { Show-CharonMessage (T 'Choose an entry in the list first.' 'Najpierw wybierz wpis na liście.'); return $null }
    return $script:State.Order[$at]
}
function New-BootForm {
    $form = New-Object Windows.Forms.Form
    $form.Text = T 'Charon boot manager' 'Menedżer rozruchu Charon'
    $form.StartPosition = 'CenterScreen'; $form.ClientSize = New-Object Drawing.Size 900, 640; $form.MinimumSize = New-Object Drawing.Size 700, 520
    $form.Font = [Drawing.SystemFonts]::MessageBoxFont; $form.AutoScaleMode = 'Dpi'
    $layout = New-Object Windows.Forms.TableLayoutPanel
    $layout.Dock = 'Fill'; $layout.ColumnCount = 2; $layout.RowCount = 4; $layout.Padding = New-Object Windows.Forms.Padding 8
    [void]$layout.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle 'Percent', 62)); [void]$layout.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle 'Percent', 38))
    [void]$layout.RowStyles.Add((New-Object Windows.Forms.RowStyle 'AutoSize')); [void]$layout.RowStyles.Add((New-Object Windows.Forms.RowStyle 'Percent', 60))
    [void]$layout.RowStyles.Add((New-Object Windows.Forms.RowStyle 'AutoSize')); [void]$layout.RowStyles.Add((New-Object Windows.Forms.RowStyle 'Percent', 40))
    $tab = 0
    $label = New-Object Windows.Forms.Label
    $label.Text = T 'Boot &order (the first entry starts):' '&Kolejność rozruchu (startuje pierwszy wpis):'; $label.AutoSize = $true; $label.TabIndex = $tab++
    $script:BootList = New-Object Windows.Forms.ListBox
    $script:BootList.Dock = 'Fill'; $script:BootList.IntegralHeight = $false; $script:BootList.HorizontalScrollbar = $true; $script:BootList.TabIndex = $tab++
    $script:BootList.AccessibleName = T 'Boot order' 'Kolejność rozruchu'
    $buttons = New-Object Windows.Forms.FlowLayoutPanel
    $buttons.Dock = 'Fill'; $buttons.FlowDirection = 'TopDown'; $buttons.WrapContents = $false; $buttons.AutoScroll = $true; $buttons.TabIndex = $tab++
    $script:ButtonTab = 0
    $add = {
        param([string]$text, [scriptblock]$click)
        $b = New-Object Windows.Forms.Button
        $b.Text = $text; $b.AutoSize = $true; $b.MinimumSize = New-Object Drawing.Size 300, 0; $b.TextAlign = 'MiddleLeft'; $b.TabIndex = $script:ButtonTab++
        $b.Add_Click($click); $buttons.Controls.Add($b); return $b
    }
    [void](& $add (T 'Move &up' 'Przenieś w &górę') { $id = Get-SelectedBootId; if ($null -ne $id) { Invoke-FormAction { Move-BootEntry $id 'up' } $id } })
    [void](& $add (T 'Move &down' 'Przenieś w &dół') { $id = Get-SelectedBootId; if ($null -ne $id) { Invoke-FormAction { Move-BootEntry $id 'down' } $id } })
    [void](& $add (T 'Make Charon &first' 'Charon &pierwszy') { Invoke-FormAction { Set-CharonPlace 'first' } })
    [void](& $add (T 'Make Charon &second, after the system' 'Charon d&rugi, po systemie') { Invoke-FormAction { Set-CharonPlace 'second' } })
    [void](& $add (T '&Take Charon out of the boot order' '&Usuń Charona z kolejności rozruchu') { Invoke-FormAction { Set-CharonPlace 'remove' } })
    [void](& $add (T 'Put Charon &back into the boot order' 'Przy&wróć Charona do kolejności rozruchu') { Invoke-FormAction { Set-CharonPlace 'restore' } })
    [void](& $add (T 'Set the selected entry as &Continue target' 'Ustaw wybrany wpis jako &cel Kontynuuj') { $id = Get-SelectedBootId; if ($null -ne $id) { Invoke-FormAction { Set-ContinueTarget $id } $id } })
    [void](& $add (T 'Charon''s &menu at the next restart only' '&Menu Charona tylko przy następnym restarcie') { Invoke-FormAction { Set-BootNextCharon } })
    [void](& $add (T 'F&irmware setup at the next restart' 'Ustawienia f&irmware przy następnym restarcie') { Invoke-FormAction { Set-FirmwareSetup } })
    [void](& $add (T 'Restart &now' 'Uruchom ponownie &teraz') { Invoke-FormAction { Restart-Computer-Now } })
    [void](& $add (T 'S&ecure Boot...' 'S&ecure Boot...') {
        $text = (Get-SecureBootLines) -join "`r`n"
        if (Test-SecureBootHelper) {
            $r = [Windows.Forms.MessageBox]::Show($text + "`r`n`r`n" + (T 'Turn Secure Boot off with the computer maker''s interface now? It explains the risks and asks for a typed word and the firmware supervisor password first.' 'Wyłączyć teraz Secure Boot interfejsem producenta komputera? Najpierw wyjaśni ryzyko i poprosi o wpisanie słowa oraz hasło nadzorcy firmware.'), 'Secure Boot', [Windows.Forms.MessageBoxButtons]::YesNo, [Windows.Forms.MessageBoxIcon]::Information, [Windows.Forms.MessageBoxDefaultButton]::Button2)
            if ($r -eq [Windows.Forms.DialogResult]::Yes) { Invoke-FormAction { Invoke-SecureBootHelper } }
        } else { Show-CharonMessage $text }
    })
    [void](& $add (T 'Program &language: switch to Polish' '&Język programu: przełącz na angielski') { $script:CharonPl = -not $script:CharonPl; $script:Reopen = $true; $script:Form.Close() })
    $refresh = & $add (T 'Refres&h the list' '&Odśwież listę') { Update-BootForm (T 'The list was read again from firmware.' 'Lista została ponownie odczytana z firmware.') }
    $close = & $add (T 'Close' 'Zamknij') { $script:Form.Close() }
    $statusLabel = New-Object Windows.Forms.Label
    $statusLabel.Text = T 'St&atus:' '&Stan:'; $statusLabel.AutoSize = $true; $statusLabel.TabIndex = $tab++
    $script:StatusBox = New-Object Windows.Forms.TextBox
    $script:StatusBox.Multiline = $true; $script:StatusBox.ReadOnly = $true; $script:StatusBox.ScrollBars = 'Vertical'; $script:StatusBox.Dock = 'Fill'; $script:StatusBox.TabIndex = $tab++
    $script:StatusBox.AccessibleName = T 'Status' 'Stan'
    $layout.Controls.Add($label, 0, 0); $layout.Controls.Add($script:BootList, 0, 1)
    $layout.Controls.Add($buttons, 1, 1); $layout.SetRowSpan($buttons, 3)
    $layout.Controls.Add($statusLabel, 0, 2); $layout.Controls.Add($script:StatusBox, 0, 3)
    $form.Controls.Add($layout)
    $form.AcceptButton = $refresh; $form.CancelButton = $close
    $script:Form = $form
    Update-BootForm ''
    return $form
}
function Write-ControlTree($Control, [int]$Depth) {
    foreach ($child in ($Control.Controls | Sort-Object TabIndex)) {
        $kind = $child.GetType().Name
        $name = $child.AccessibleName; if (-not $name) { $name = $child.Text }
        $value = ''; if ($child -is [Windows.Forms.ListBox]) { $value = ($child.Items -join ' | ') }
        if ($kind -notlike '*LayoutPanel') { Out-Line ((' ' * $Depth) + ('{0} tab={1} name="{2}" value="{3}"' -f $kind, $child.TabIndex, ($name -replace "`r`n", ' | '), $value)) }
        if ($child.Controls.Count) { Write-ControlTree $child ($Depth + 1) }
    }
}
function Show-BootDialog {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()
    $script:CharonGui = $true
    do {
        $script:Reopen = $false
        $form = New-BootForm
        if ($GuiSelfTest) {
            Out-Line ('Form "' + $form.Text + '" accept=' + $form.AcceptButton.Text + ' cancel=' + $form.CancelButton.Text)
            Write-ControlTree $form 1; $form.Dispose(); return
        }
        [void]$form.ShowDialog(); $form.Dispose()
    } while ($script:Reopen)
}

# ---- main ---------------------------------------------------------------------------------
Initialize-CharonConsole
if ($Version) { Out-Line ('Charon-BootManager.ps1 ' + $script:CharonRelease); exit 0 }
$nonInteractive = ($List -or $Move -or $Charon -or $Continue -or $BootNextCharon -or $FirmwareSetup -or $Restart)
try {
    # The EFI partition is needed only for the Continue target; the boot order works without it.
    try { $root = Open-CharonEsp; $script:Hak = Get-CharonHak $root } catch { $script:EspNote = $_.Exception.Message }
    Set-CharonLanguage $script:Hak
    if ($script:CharonFirmwareMode -eq 'none') { $script:ExitCode = 7; throw (Get-CharonNoFirmwareText) }
    try { Initialize-CharonFirmware } catch { $script:ExitCode = 7; throw }
    [void](Get-BootState)
    if ($nonInteractive) {
        $parseId = { param([string]$text) $n = 0; if ($text -notmatch '^[0-9A-Fa-f]{1,4}$') { Stop-Refused (T "Expected a boot entry number such as 0004, got $text." "Oczekiwano numeru wpisu, np. 0004, a jest $text.") }; return [Convert]::ToInt32($text, 16) }
        if ($Move) {
            $direction = ''; if ($Rest.Count) { $direction = $Rest[0].ToLowerInvariant() }
            if ($direction -ne 'up' -and $direction -ne 'down') { Stop-Refused '-Move XXXX up|down' }
            Out-Line (Move-BootEntry (& $parseId $Move) $direction)
        }
        if ($Charon) { Out-Line (Set-CharonPlace $Charon.ToLowerInvariant()) }
        if ($Continue) { Out-Line (Set-ContinueTarget (& $parseId $Continue)) }
        if ($BootNextCharon) { Out-Line (Set-BootNextCharon) }
        if ($FirmwareSetup) { Out-Line (Set-FirmwareSetup) }
        if ($Restart) { Out-Line (Restart-Computer-Now) }
        if ($List) {
            $s = $script:State
            for ($i = 0; $i -lt $s.Order.Count; $i++) {
                $id = $s.Order[$i]; $e = $s.Entries[$id]; $flags = ''
                if ($e -and $e.Own) { $flags += 'C' }
                if ($s.Next -eq $id) { $flags += 'N' }
                if ($s.Continue.State -eq 'ok' -and $s.Continue.Id -eq $id) { $flags += 'T' }
                if (-not $flags) { $flags = '-' }
                Out-Line ('{0} Boot{1} {2} {3}' -f ($i + 1), (Format-BootId $id), $flags, $(if ($e) { $e.Option.Description } else { '' }))
            }
            Out-Line ('bootnext=' + $(if ($null -ne $s.Next) { Format-BootId $s.Next } else { 'none' }))
            Out-Line ('secureboot=' + $s.Secure)
            Out-Line ('continue=' + $(if ($s.Continue.State -eq 'ok') { Format-BootId $s.Continue.Id } else { $s.Continue.State }))
        }
    }
    elseif ($Console) { Show-ConsoleMenu }
    else { Show-BootDialog }
}
catch {
    if ($script:ExitCode -eq 0) { $script:ExitCode = 1 }
    $text = $_.Exception.Message
    if ($script:CharonGui -or (-not $Console -and -not $nonInteractive -and -not $GuiSelfTest)) {
        try { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($text, 'Charon', [Windows.Forms.MessageBoxButtons]::OK, [Windows.Forms.MessageBoxIcon]::Error) } catch { }
    }
    [Console]::Error.WriteLine($text)
}
finally { Close-CharonEsp }
exit $script:ExitCode
