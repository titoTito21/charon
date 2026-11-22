<#
.SYNOPSIS
Charon screen reader settings: changes the settings of Charon (the speaking UEFI boot
manager and pre-boot screen reader) from the running Windows.

.DESCRIPTION
Reads and writes Charon's settings record on the EFI system partition
(\EFI\HAK\charon-settings-a.bin and -b.bin) with Charon's own procedure, shows the reader
add-on (firmware driver option "Charon voice") and turns it on or off, sets the
reader-only marker and restores the defaults.

Without switches a dialog opens (standard Windows controls, readable with NVDA, JAWS and
Narrator). -Console gives the same functions as numbered text menus.

  -Console            numbered text menus instead of the dialog
  -Language en|pl     language of the program (default: Polish when Windows or Charon is Polish)
  -EspPath DIR        work on an ordinary folder standing in for the EFI system partition
  -NoFirmware         never call the firmware variable interfaces
  -FirmwareDir DIR    tests: a folder of efivarfs-style files stands in for firmware variables
  -Show               print the record (name=value lines) and exit
  -Set a=1,b=2        change members or rows and save, without questions
  -Defaults           restore the defaults and save
  -Repair             rewrite a record Charon does not accept (values out of range)
  -Format X           record format to write when it cannot be detected (1-9, A, B, C)
  -ReaderOnly on|off  create or remove \EFI\HAK\reader-only
  -Addon on|off|status   the reader add-on (asks first; -Yes answers yes)
  -Synths             list the synthesizer add-ons found on the partition
  -Version            print the version

Exit codes: 0 done, 1 error, 2 no EFI partition or no Charon on it, 4 the record is invalid,
5 request refused (value out of range, not stored by this format), 6 the record or the
installed Charon is newer than this program, 7 firmware variables not available.
Windows PowerShell 5.1. GPL-2.0-or-later.
#>
param(
    [string]$EspPath = '',
    [switch]$NoFirmware,
    [string]$FirmwareDir = '',
    [string]$Language = '',
    [switch]$Console,
    [switch]$Show,
    [string[]]$Set = @(),
    [switch]$Defaults,
    [switch]$Repair,
    [string]$Format = '',
    [string]$ReaderOnly = '',
    [string]$Addon = '',
    [switch]$Synths,
    [switch]$Yes,
    [switch]$Version,
    [switch]$GuiSelfTest
)
$ErrorActionPreference = 'Stop'
$script:CharonEspPath = $EspPath
$script:CharonFirmwareMode = if ($NoFirmware) { 'none' } elseif ($FirmwareDir) { 'dir' } else { 'real' }
$script:CharonFirmwareDir = $FirmwareDir
$script:CharonLanguage = $Language.ToLowerInvariant()
$script:CharonYes = [bool]$Yes
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'CharonOs.Common.ps1')
. (Join-Path $here 'CharonOs.Fields.ps1')

$script:Known = '123456789ABC'          # record formats this program knows (CharSettings.h V1..V12)
$script:Newest = 'C'
$script:SlotNames = @('charon-settings-a.bin', 'charon-settings-b.bin')   # BootMenu.h SettingsSlot
$script:S = @{}                          # members as Charon would use them
$script:Seq = [uint64]0
$script:RecordState = 'none'             # none | ok | invalid
$script:RecordVersion = $null
$script:Target = $null                   # format that will be written
$script:TargetWhy = ''
$script:WriteBlocked = $null
$script:Slots = @()
$script:Hak = $null
$script:Dirty = $false
$script:SynthList = @()

# ---- the record: a port of CharSettings.h ------------------------------------------
function Get-FormatAge([string]$V) { return $script:Known.IndexOf($V) }
function Get-RecordSize([string]$V) { $a = Get-FormatAge $V; if ($a -ge 8) { return 48 } elseif ($a -ge 6) { return 40 } else { return 32 } }
function Get-CrcOffset([string]$V) { return (Get-RecordSize $V) - 4 }
function Get-Crc32([byte[]]$Bytes, [int]$Count) {
    [long]$crc = 4294967295
    for ($i = 0; $i -lt $Count; $i++) {
        $crc = $crc -bxor [long]$Bytes[$i]
        for ($bit = 0; $bit -lt 8; $bit++) { if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 3988292384 } else { $crc = $crc -shr 1 } }
    }
    return ($crc -bxor 4294967295)
}
function Test-AutoDelay([int]$V) { return ($script:CharonAutoDelays -contains $V) }
# DecodeSettings. Returns @{Version; Sequence; Members} or $null ($script:DecodeReason says why).
# -Lenient skips the range checks (structure, CRC and sequence must still be right): used
# only to offer a repair of a record Charon itself rejects.
function Read-CharonRecord([byte[]]$b, [switch]$Lenient) {
    $script:DecodeReason = ''
    if ($null -eq $b -or ($b.Length -ne 48 -and $b.Length -ne 40 -and $b.Length -ne 32)) { $script:DecodeReason = 'length'; return $null }
    $magic = [Text.Encoding]::ASCII.GetString($b, 0, 7)
    if ($magic -cne 'CHARS00') { $script:DecodeReason = 'magic'; return $null }
    $v = [string][char]$b[7]
    $age = Get-FormatAge $v
    if ($age -lt 0) { $script:DecodeReason = 'format ' + $v; return $null }
    if ($b.Length -ne (Get-RecordSize $v)) { $script:DecodeReason = 'length'; return $null }
    $crcAt = Get-CrcOffset $v
    $stored = [long][BitConverter]::ToUInt32($b, $crcAt)
    if ($stored -ne (Get-Crc32 $b $crcAt)) { $script:DecodeReason = 'CRC'; return $null }
    $checks = New-Object System.Collections.ArrayList
    $r = { param($index, $low, $high) [void]$checks.Add(@($index, $low, $high)) }
    & $r 16 0 2; & $r 17 0 1
    & $r 18 0 $(if ($age -eq 0) { 0 } else { 1 })
    & $r 19 0 $(if ($age -ge 2) { 1 } else { 0 })
    $melody = $(if ($age -ge 3) { 4 } else { 0 })
    & $r 20 0 $melody; & $r 21 0 $melody
    if ($age -ge 4) { & $r 22 5 20 } else { & $r 22 0 0 }
    if ($age -ge 5) { & $r 23 1 10; & $r 24 0 10; & $r 25 0 15; & $r 26 0 $(if ($age -ge 6) { 15 } else { 1 }) }
    else { foreach ($i in 23..27) { & $r $i 0 0 } }
    if ($age -ge 6) {
        foreach ($i in 28..30) { & $r $i 5 20 }
        & $r 31 1 10; & $r 32 0 7; & $r 33 0 3
        if ($age -eq 6) { & $r 34 0 0; & $r 35 0 0 } else { & $r 34 5 20; & $r 35 5 20 }
    }
    if ($age -ge 8) {
        & $r 36 0 3; & $r 37 0 7; & $r 38 0 3; & $r 39 0 63
        if ($age -ge 9) {
            if ($age -lt 11) { & $r 40 0 63; & $r 41 0 31 }
            # CharSettings.h line 120: byte 41 above 127 (bit 7, extraLinger) is rejected in every format.
            & $r 41 0 127
            if ($age -eq 9) { & $r 42 0 0; & $r 43 0 0 }
            & $r 43 0 15
        } else { foreach ($i in 40..43) { & $r $i 0 0 } }
    }
    $bad = $null
    foreach ($c in $checks) { if ([int]$b[$c[0]] -lt $c[1] -or [int]$b[$c[0]] -gt $c[2]) { $bad = $c[0]; break } }
    if ($null -eq $bad -and $age -ge 5) {
        if (([int]$b[25] -band 3) -gt 2) { $bad = 25 } elseif (-not (Test-AutoDelay ([int]$b[27]))) { $bad = 27 }
    }
    if ($null -eq $bad -and $age -ge 9 -and ((([int]$b[41] -shr 5) -band 3) -gt 2 -or ([int]$b[41] -band 3) -gt 2)) { $bad = 41 }
    $sequence = [BitConverter]::ToUInt64($b, 8)
    if ($sequence -eq 0) { $script:DecodeReason = 'sequence 0'; return $null }
    if ($null -ne $bad) {
        $script:DecodeReason = ('byte {0} = {1}' -f $bad, $b[$bad])
        if (-not $Lenient) { return $null }
    }
    $m = @{}
    foreach ($name in $script:CharonMembers) { $m[$name] = [int]$script:CharonDefaults[$name] }
    $m.readerExtra = 153                                    # CharSettings{} default (extraLinger set)
    $m.language = [int]$b[16]; $m.wrap = [int]($b[17] -ne 0)
    $m.sounds = [int]($age -eq 0 -or $b[18] -ne 0); $m.explicitMessages = [int]($age -ge 2 -and $b[19] -ne 0)
    if ($age -ge 3) { $m.startMelody = [int]$b[20]; $m.backgroundMelody = [int]$b[21] }
    if ($age -ge 4) { $m.rate = [int]$b[22] }
    if ($age -ge 5) {
        $m.speechVolume = [int]$b[23]; $m.effectsVolume = [int]$b[24]; $m.verbosity = [int]($b[25] -band 3); $m.progress = [int]($b[25] -shr 2)
        $m.rememberLast = [int]($b[26] -band 1); $m.autoSeconds = [int]$b[27]
        if ($age -ge 6) { $m.welcome = [int]($b[26] -shr 1) }
    }
    $m.readerRate = $m.rate; $m.readerVolume = $m.speechVolume
    if ($age -ge 6) {
        $m.pitch = [int]$b[28]; $m.readerRate = [int]$b[29]; $m.readerPitch = [int]$b[30]; $m.readerVolume = [int]$b[31]; $m.detail = [int]$b[32]; $m.timers = [int]$b[33]
        if ($age -eq 6 -and $m.timers -eq 0) { $m.timers = 2 }
        if ($age -ge 7) { $m.messageRate = [int]$b[34]; $m.messagePitch = [int]$b[35] }
    }
    if ($age -ge 8) { $m.punctuation = [int]$b[36]; $m.capitals = [int]$b[37]; $m.screenLanguage = [int]$b[38]; $m.readerFlags = [int]$b[39] }
    if ($age -ge 9) { $m.formatFlags = [int]$b[40]; $m.readerExtra = [int]$b[41] }
    if ($age -ge 10) { $m.synthKey = [int]$b[42]; $m.synthVoice = [int]$b[43] }
    return @{ Version = $v; Sequence = $sequence; Members = $m }
}
# The range EncodeSettings accepts for a member. $null = fine, otherwise the allowed range as text.
function Test-CharonMember([string]$Name, [int]$Value) {
    $r = switch ($Name) {
        'language' { @(0, 2) } 'wrap' { @(0, 1) } 'sounds' { @(0, 1) } 'explicitMessages' { @(0, 1) }
        'startMelody' { @(0, 4) } 'backgroundMelody' { @(0, 4) } 'rate' { @(5, 20) } 'speechVolume' { @(1, 10) }
        'effectsVolume' { @(0, 10) } 'verbosity' { @(0, 2) } 'progress' { @(0, 3) } 'rememberLast' { @(0, 1) }
        'welcome' { @(0, 7) } 'pitch' { @(5, 20) } 'readerRate' { @(5, 20) } 'readerPitch' { @(5, 20) }
        'readerVolume' { @(1, 10) } 'detail' { @(0, 7) } 'timers' { @(0, 3) } 'messageRate' { @(5, 20) }
        'messagePitch' { @(5, 20) } 'punctuation' { @(0, 3) } 'capitals' { @(0, 7) } 'screenLanguage' { @(0, 3) }
        'readerFlags' { @(0, 63) } 'formatFlags' { @(0, 255) } 'readerExtra' { @(0, 255) } 'synthKey' { @(0, 255) }
        'synthVoice' { @(0, 15) } default { @(0, 255) }
    }
    if ($Name -eq 'autoSeconds') { if (Test-AutoDelay $Value) { return $null } else { return ($script:CharonAutoDelays -join ', ') } }
    if ($Value -lt $r[0] -or $Value -gt $r[1]) { return ('{0}..{1}' -f $r[0], $r[1]) }
    if ($Name -eq 'readerExtra' -and (($Value -band 3) -gt 2 -or (($Value -shr 5) -band 3) -gt 2)) { return 'bits 0-1 and bits 5-6: 0..2' }
    return $null
}
# EncodeSettings for the format $V (Charon itself writes only its newest format; older
# formats are laid out as DecodeSettings reads them). Bits no format stores are dropped.
function New-CharonRecord([hashtable]$m, [uint64]$Sequence, [string]$V) {
    $age = Get-FormatAge $V
    $size = Get-RecordSize $V
    $b = New-Object byte[] $size
    [Text.Encoding]::ASCII.GetBytes('CHARS00' + $V).CopyTo($b, 0)
    [BitConverter]::GetBytes([uint64]$Sequence).CopyTo($b, 8)
    $b[16] = [byte]$m.language; $b[17] = [byte]$m.wrap
    if ($age -ge 1) { $b[18] = [byte]$m.sounds }
    if ($age -ge 2) { $b[19] = [byte]$m.explicitMessages }
    if ($age -ge 3) { $b[20] = [byte]$m.startMelody; $b[21] = [byte]$m.backgroundMelody }
    if ($age -ge 4) { $b[22] = [byte]$m.rate }
    if ($age -ge 5) {
        $b[23] = [byte]$m.speechVolume; $b[24] = [byte]$m.effectsVolume; $b[25] = [byte]($m.verbosity -bor ($m.progress -shl 2))
        $welcome = 0; if ($age -ge 6) { $welcome = $m.welcome -shl 1 }
        $b[26] = [byte]($m.rememberLast -bor $welcome); $b[27] = [byte]$m.autoSeconds
    }
    if ($age -ge 6) {
        $b[28] = [byte]$m.pitch; $b[29] = [byte]$m.readerRate; $b[30] = [byte]$m.readerPitch; $b[31] = [byte]$m.readerVolume
        $b[32] = [byte]$m.detail; $b[33] = [byte]$m.timers
        if ($age -ge 7) { $b[34] = [byte]$m.messageRate; $b[35] = [byte]$m.messagePitch }
    }
    if ($age -ge 8) { $b[36] = [byte]$m.punctuation; $b[37] = [byte]$m.capitals; $b[38] = [byte]$m.screenLanguage; $b[39] = [byte]$m.readerFlags }
    if ($age -ge 9) {
        $format = $m.formatFlags; $extra = $m.readerExtra -band 0x7f
        if ($age -lt 11) { $format = $format -band 63; $extra = $extra -band 31 }
        $b[40] = [byte]$format; $b[41] = [byte]$extra
    }
    if ($age -ge 10) { $b[42] = [byte]$m.synthKey; $b[43] = [byte]$m.synthVoice }
    $crcAt = $size - 4
    [BitConverter]::GetBytes([uint32](Get-Crc32 $b $crcAt)).CopyTo($b, $crcAt)
    return , $b
}

# ---- loading: ReadSettingsSlot, LoadCharSettings, ChooseSettings (BootMenu.h, CharSettings.h) ----
function Read-CharonSlot([int]$Index) {
    $file = [IO.Path]::Combine($script:Hak, $script:SlotNames[$Index])
    $slot = @{ Index = $Index; File = $file; Status = 'notfound'; Reason = ''; Record = $null; Lenient = $null; Bytes = $null; Newer = $false }
    if (-not [IO.File]::Exists($file)) { return $slot }
    $slot.Status = 'invalid'
    try {
        $stream = [IO.File]::OpenRead($file)
        try { $buffer = New-Object byte[] 49; $n = $stream.Read($buffer, 0, 49) } finally { $stream.Dispose() }   # charSettingsBytes+1
    } catch { $slot.Reason = $_.Exception.Message; return $slot }
    $bytes = New-Object byte[] $n; [Array]::Copy($buffer, 0, $bytes, 0, $n)
    $slot.Bytes = $bytes
    if ($n -ge 8 -and [Text.Encoding]::ASCII.GetString($bytes, 0, 7) -ceq 'CHARS00' -and (Get-FormatAge ([string][char]$bytes[7])) -lt 0) { $slot.Newer = $true }
    $record = Read-CharonRecord $bytes
    $reason = $script:DecodeReason
    $parity = { param($r) (($r.Sequence % 2) -eq 1) -eq ($Index -eq 0) }     # slot a holds odd sequences
    if ($record -and (& $parity $record)) { $slot.Status = 'ok'; $slot.Record = $record; return $slot }
    if ($record) { $reason = 'sequence parity' }
    $slot.Reason = $reason
    $lenient = Read-CharonRecord $bytes -Lenient
    if ($lenient -and (& $parity $lenient)) { $slot.Lenient = $lenient; $slot.Reason = $script:DecodeReason }
    return $slot
}
function Test-SameMembers([hashtable]$A, [hashtable]$B) { foreach ($n in $script:CharonMembers) { if ($A[$n] -ne $B[$n]) { return $false } }; return $true }
function Import-CharonSettings {
    $a = Read-CharonSlot 0; $b = Read-CharonSlot 1
    $script:Slots = @($a, $b)
    $script:RecordVersion = $null; $script:Seq = [uint64]0; $script:WriteBlocked = $null
    $script:S = @{}; foreach ($n in $script:CharonMembers) { $script:S[$n] = [int]$script:CharonDefaults[$n] }
    $script:S.readerExtra = 153
    $chosen = $null
    if ($a.Status -eq 'notfound' -and $b.Status -eq 'notfound') { $script:RecordState = 'none' }
    elseif ($a.Status -ne 'ok' -and $b.Status -ne 'ok') { $script:RecordState = 'invalid' }
    elseif ($a.Status -eq 'ok' -and $b.Status -eq 'ok' -and $a.Record.Sequence -eq $b.Record.Sequence -and -not (Test-SameMembers $a.Record.Members $b.Record.Members)) { $script:RecordState = 'invalid' }
    else {
        if ($b.Status -ne 'ok' -or ($a.Status -eq 'ok' -and $a.Record.Sequence -ge $b.Record.Sequence)) { $chosen = $a.Record } else { $chosen = $b.Record }
        $script:RecordState = 'ok'; $script:RecordVersion = $chosen.Version; $script:Seq = [uint64]$chosen.Sequence
        foreach ($n in $script:CharonMembers) { $script:S[$n] = [int]$chosen.Members[$n] }
    }
    # Which format to write.
    $binary = Get-CharonBinaryFormat $script:Hak
    if ($a.Newer -or $b.Newer) {
        $script:WriteBlocked = T 'A settings record on the partition has a format newer than this program knows. Nothing will be written; update the Charon tools.' 'Zapis ustawień na partycji ma format nowszy, niż zna ten program. Nic nie zostanie zapisane; zaktualizuj narzędzia Charona.'
    } elseif ($binary -and (Get-FormatAge $binary) -lt 0) {
        $script:WriteBlocked = T "The installed Charon writes settings format $binary, which is newer than this program knows. Nothing will be written; update the Charon tools." "Zainstalowany Charon zapisuje ustawienia w formacie $binary, nowszym niż zna ten program. Nic nie zostanie zapisane; zaktualizuj narzędzia Charona."
    }
    if ($binary -and (Get-FormatAge $binary) -ge 0) { $script:Target = $binary; $script:TargetWhy = T 'the format the installed boot manager writes (found in HAKBOOT.EFI)' 'format zapisywany przez zainstalowany menedżer rozruchu (znaleziony w HAKBOOT.EFI)' }
    elseif ($script:RecordVersion) { $script:Target = $script:RecordVersion; $script:TargetWhy = T 'the format of the record on the partition' 'format zapisu znajdującego się na partycji' }
    elseif ($Format) { $script:Target = $Format.ToUpperInvariant(); $script:TargetWhy = T 'chosen with -Format' 'wybrany przełącznikiem -Format' }
    else { $script:Target = $script:Newest; $script:TargetWhy = T 'no record and no boot manager file to detect the format from: the newest format, read by Charon r397 and later' 'brak zapisu i pliku menedżera rozruchu, z których dałoby się odczytać format: najnowszy format, czytany przez Charona r397 i nowsze' }
    if ((Get-FormatAge $script:Target) -lt 0) { throw (T "Unknown record format $($script:Target)." "Nieznany format zapisu $($script:Target).") }
    $script:Dirty = $false
}
# SaveCharSettings: the next sequence goes to the slot of its parity; that file is deleted,
# created, written, flushed to the disk, read back and compared. The other slot, which
# holds the last complete record, is not touched.
function Export-CharonSettings {
    if ($script:WriteBlocked) { throw $script:WriteBlocked }
    if ($script:RecordState -eq 'invalid') { throw (T 'The settings on the partition are invalid; Charon would use defaults and refuse to save. Use Repair first.' 'Ustawienia na partycji są nieprawidłowe; Charon użyłby domyślnych i odmówił zapisu. Najpierw użyj naprawy.') }
    if ($script:Seq -eq [uint64]::MaxValue) { throw (T 'The sequence number of the record cannot grow any more.' 'Numer kolejny zapisu nie może już wzrosnąć.') }
    foreach ($n in $script:CharonMembers) {
        if ((Get-FormatAge $script:CharonMemberSince[$n]) -gt (Get-FormatAge $script:Target)) { continue }
        $range = Test-CharonMember $n $script:S[$n]
        if ($range) { throw (T "The value $($script:S[$n]) of $n is out of range ($range)." "Wartość $($script:S[$n]) pola $n jest poza zakresem ($range).") }
    }
    [uint64]$next = $script:Seq + [uint64]1
    $bytes = New-CharonRecord $script:S $next $script:Target
    $check = Read-CharonRecord $bytes
    if (-not $check) { throw (T "Internal check failed: Charon would not accept the record ($($script:DecodeReason)). Nothing was written." "Kontrola wewnętrzna nie powiodła się: Charon nie przyjąłby zapisu ($($script:DecodeReason)). Nic nie zapisano.") }
    $index = 1; if (($next % 2) -eq 1) { $index = 0 }
    $file = [IO.Path]::Combine($script:Hak, $script:SlotNames[$index])
    if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
    Write-CharonFileDurably $file $bytes
    $slot = Read-CharonSlot $index
    if ($slot.Status -ne 'ok' -or -not (Test-CharonBytesEqual $slot.Bytes $bytes)) { throw (T 'The record read back does not match what was written.' 'Odczyt kontrolny zapisu nie zgadza się z tym, co zapisano.') }
    Import-CharonSettings
}
function Test-CharonRepairable {
    if ($script:RecordState -ne 'invalid' -or $script:WriteBlocked) { return $false }
    foreach ($s in $script:Slots) { if ($s.Lenient) { return $true } }
    return $false
}
# A record with a correct structure and checksum whose values Charon rejects: the values
# out of range take their defaults, bit 7 of byte 41 is cleared, and the result is saved
# under the next sequence number.
function Repair-CharonSettings {
    $best = $null
    foreach ($s in $script:Slots) { if ($s.Lenient -and ($null -eq $best -or $s.Lenient.Sequence -gt $best.Lenient.Sequence)) { $best = $s } }
    if (-not $best) { throw (T 'Nothing can be recovered from the settings files.' 'Z plików ustawień nie da się nic odzyskać.') }
    $notes = @()
    $m = $best.Lenient.Members
    if ($m.readerExtra -band 128) { $m.readerExtra = $m.readerExtra -band 127 }
    foreach ($n in $script:CharonMembers) {
        if (Test-CharonMember $n $m[$n]) { $notes += ('{0}: {1} -> {2}' -f $n, $m[$n], $script:CharonDefaults[$n]); $m[$n] = [int]$script:CharonDefaults[$n] }
    }
    $script:S = $m; $script:Seq = [uint64]$best.Lenient.Sequence
    foreach ($s in $script:Slots) { if ($s.Lenient -and $s.Lenient.Sequence -gt $script:Seq) { $script:Seq = [uint64]$s.Lenient.Sequence } }
    if (-not (Get-CharonBinaryFormat $script:Hak)) { $script:Target = $best.Lenient.Version }
    $script:RecordState = 'ok'
    Export-CharonSettings
    return (@((T "Repaired from slot $(@('a', 'b')[$best.Index]) ($($best.Reason))." "Naprawiono na podstawie pliku $(@('a', 'b')[$best.Index]) ($($best.Reason)).")) + $notes)
}

# ---- synthesizer add-ons (CharonSynthHost.h: Scan, ParseManifest, KeyOf) -------------------
function Get-CharonSynthKey([string]$Name) {
    [long]$h = 2166136261
    foreach ($ch in $Name.ToCharArray()) {
        $c = [int]$ch
        if ($c -ge 65 -and $c -le 90) { $c += 32 }
        $h = (($h -bxor $c) * 16777619) -band 4294967295
    }
    $k = ($h -bxor ($h -shr 8) -bxor ($h -shr 16) -bxor ($h -shr 24)) -band 255
    if ($k -eq 0) { $k = 1 }
    return [int]$k
}
function Get-CharonSynths {
    $list = @()
    $dir = $null
    foreach ($d in [IO.Directory]::GetDirectories($script:Hak)) { if ([IO.Path]::GetFileName($d) -ieq 'synths') { $dir = $d } }
    if (-not $dir) { return , $list }
    foreach ($file in ([IO.Directory]::GetFiles($dir) | Sort-Object)) {
        $name = [IO.Path]::GetFileName($file)
        if ($name.Length -le 9 -or $name.Length -ge 60 -or -not $name.ToLowerInvariant().EndsWith('.manifest')) { continue }
        $bytes = [IO.File]::ReadAllBytes($file)
        $text = [Text.Encoding]::UTF8.GetString($bytes, 0, [Math]::Min($bytes.Length, 4095))
        $info = @{ File = $name; Key = (Get-CharonSynthKey $name); Name = ''; Module = ''; Voices = @() }
        foreach ($line in $text.Split("`n")) {
            $line = $line.Trim(' ', "`t", "`r")
            if ($line.Length -eq 0 -or $line[0] -eq '#') { continue }
            $eq = $line.IndexOf('='); if ($eq -lt 0) { continue }
            $key = $line.Substring(0, $eq).Trim(' ', "`t"); $value = $line.Substring($eq + 1).Trim(' ', "`t", "`r")
            if ($key -ceq 'name') { $info.Name = $value }
            elseif ($key -ceq 'module') { $info.Module = $value }
            elseif ($key -ceq 'voice' -and $info.Voices.Count -lt 16) {
                $parts = $value.Split('|')
                $id = $parts[0].Trim(' ', "`t")
                $spoken = ''; if ($parts.Count -gt 1) { $spoken = $parts[1].Trim(' ', "`t") }
                if ($id) { if (-not $spoken) { $spoken = $id }; $info.Voices += $spoken }
            }
        }
        if ($info.Module -and $info.Voices.Count -gt 0) { if (-not $info.Name) { $info.Name = $name }; $list += $info }
        if ($list.Count -ge 8) { break }
    }
    return , $list
}
function Get-SynthChoices {
    $choices = @(, @(0, (T 'TVox, built in' 'TVox, wbudowany')))
    foreach ($s in $script:SynthList) { $choices += , @($s.Key, ($s.Name + (T ', add-on' ', dodatek'))) }
    $key = $script:S.synthKey
    if ($key -ne 0 -and -not ($script:SynthList | Where-Object { $_.Key -eq $key })) { $choices += , @($key, (T "add-on not found on the partition (key $key)" "dodatek nieznaleziony na partycji (klucz $key)")) }
    return , $choices
}
function Get-SynthVoiceChoices {
    $synth = $script:SynthList | Where-Object { $_.Key -eq $script:S.synthKey } | Select-Object -First 1
    $choices = @()
    if (-not $synth) { $choices += , @($script:S.synthVoice, $(if ($script:S.synthKey -eq 0) { 'TVox' } else { (T "voice $($script:S.synthVoice)" "głos $($script:S.synthVoice)") })); return , $choices }
    for ($i = 0; $i -lt $synth.Voices.Count; $i++) { $choices += , @($i, $synth.Voices[$i]) }
    if ($script:S.synthVoice -ge $synth.Voices.Count) { $choices += , @($script:S.synthVoice, (T "voice $($script:S.synthVoice) (not in the manifest)" "głos $($script:S.synthVoice) (brak w manifeście)")) }
    return , $choices
}

# ---- rows ---------------------------------------------------------------------------
function Get-VoicePercent([int]$Tenths) { if ($Tenths -le 5) { return 0 }; if ($Tenths -le 10) { return ($Tenths - 5) * 10 }; if ($Tenths -ge 20) { return 100 }; return 50 + ($Tenths - 10) * 5 }
function Test-RowStored($Row) { return ((Get-FormatAge $Row.Since) -le (Get-FormatAge $script:Target)) }
function Get-RowLabel($Row) { if ($script:CharonPl) { return $Row.Pl } else { return $Row.En } }
function Get-RowValue($Row) {
    $v = [int]$script:S[$Row.Member]
    if ($Row.Kind -eq 'check') { return [int](($v -band $Row.Mask) -ne 0) }
    if ($Row.Kind -eq 'choice') { return (($v -shr $Row.Shift) -band $Row.Mask) }
    return $v
}
function Set-RowValue($Row, [int]$Value) {
    $v = [int]$script:S[$Row.Member]
    if ($Row.Kind -eq 'check') { if ($Value) { $v = $v -bor $Row.Mask } else { $v = $v -band (255 -bxor $Row.Mask) } }
    elseif ($Row.Kind -eq 'choice') { $v = ($v -band (255 -bxor ($Row.Mask -shl $Row.Shift))) -bor (($Value -band $Row.Mask) -shl $Row.Shift) }
    else { $v = $Value }
    if ($script:S[$Row.Member] -ne $v) { $script:S[$Row.Member] = $v; $script:Dirty = $true }
}
function Get-SliderText($Row, [int]$Value) { if ($Row.Unit -eq 'percent') { return ('{0}%' -f (Get-VoicePercent $Value)) } else { return ('{0}%' -f ($Value * 10)) } }
function Get-ChoiceText($Choice) { if ($script:CharonPl -and $Choice.Count -gt 2) { return $Choice[2] } else { return $Choice[1] } }
function Get-RowValueText($Row) {
    $value = Get-RowValue $Row
    if ($Row.Kind -eq 'check') { if ($value) { return (T 'checked' 'zaznaczone') } else { return (T 'not checked' 'niezaznaczone') } }
    if ($Row.Kind -eq 'slider') { return (Get-SliderText $Row $value) }
    foreach ($c in $Row.Choices) { if ($c[0] -eq $value) { return (Get-ChoiceText $c) } }
    return [string]$value
}
function Get-PageRows([string]$Page) { return , @($script:CharonRows | Where-Object { $_.Page -eq $Page }) }
function Get-PageTitle($Page) { if ($script:CharonPl) { return $Page.Pl } else { return $Page.En } }
function Reset-CharonDefaults {
    foreach ($n in $script:CharonMembers) { $script:S[$n] = [int]$script:CharonDefaults[$n] }
    if ((Get-FormatAge $script:Target) -lt 11) { foreach ($n in $script:CharonDefaultsAB.Keys) { $script:S[$n] = [int]$script:CharonDefaultsAB[$n] } }
    $script:Dirty = $true
}

# ---- reader add-on, reader-only marker, information ----------------------------------------
function Get-AddonText {
    if (-not (Test-CharonFirmware)) { return (T 'Speech from power-on (reader add-on): unknown, firmware variables are not read in this mode' 'Mowa od włączenia komputera (dodatek czytnika): nieznane, w tym trybie zmienne firmware nie są czytane') }
    $id = Find-CharonVoice
    if ($null -eq $id) { return (T 'Speech from power-on (reader add-on): off, there is no driver entry "Charon voice"' 'Mowa od włączenia komputera (dodatek czytnika): wyłączona, brak wpisu sterownika „Charon voice”') }
    $position = [array]::IndexOf((Get-CharonOrder 'DriverOrder'), $id) + 1
    return (T ("Speech from power-on (reader add-on): on, {0}, position {1} in DriverOrder" -f (Get-CharonOptionName 'Driver' $id), $position) ("Mowa od włączenia komputera (dodatek czytnika): włączona, {0}, pozycja {1} w DriverOrder" -f (Get-CharonOptionName 'Driver' $id), $position))
}
function Set-Addon([bool]$On) {
    if (-not (Test-CharonFirmware)) { throw (Get-CharonNoFirmwareText) }
    $id = Find-CharonVoice
    if ($On) {
        if ($null -ne $id) { return (Get-AddonText) }
        if (-not [IO.File]::Exists([IO.Path]::Combine($script:Hak, 'CHARONVOICE.EFI'))) { throw (T 'CHARONVOICE.EFI is not in \EFI\HAK on the partition; the add-on must be installed with the Charon installer first.' 'Na partycji w \EFI\HAK nie ma pliku CHARONVOICE.EFI; dodatek trzeba najpierw zainstalować instalatorem Charona.') }
        $question = T 'Turn on speech from power-on? This creates the firmware driver entry "Charon voice" (Driver####, first in DriverOrder), which loads \EFI\HAK\CHARONVOICE.EFI at every start.' 'Włączyć mowę od włączenia komputera? Tworzy to we firmware wpis sterownika „Charon voice” (Driver####, pierwszy w DriverOrder), który przy każdym starcie ładuje \EFI\HAK\CHARONVOICE.EFI.'
        if ((Get-CharonSecureBoot) -eq 'on') { $question += ' ' + (T 'Secure Boot is on: firmware loads the driver only when it is signed with a key in db (the installer does that).' 'Secure Boot jest włączony: firmware załaduje sterownik tylko wtedy, gdy jest podpisany kluczem z listy db (robi to instalator).') }
    } else {
        if ($null -eq $id) { return (Get-AddonText) }
        $question = T 'Turn off speech from power-on? Removes the Charon voice driver entry from firmware. The files stay on the partition.' 'Wyłączyć mowę od włączenia komputera? Usuwa wpis sterownika Charon voice z firmware. Pliki zostają na partycji.'
    }
    if (-not (Confirm-Charon $question)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    if (-not (Confirm-CharonBitLocker)) { return (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
    if ($On) { [void](Enable-CharonVoice) } else { [void](Disable-CharonVoice) }
    return ((T 'Read back from firmware: ' 'Odczytano z firmware: ') + (Get-AddonText))
}
function Test-ReaderOnly { return [IO.File]::Exists([IO.Path]::Combine($script:Hak, 'reader-only')) }
function Set-ReaderOnly([bool]$On) {
    $file = [IO.Path]::Combine($script:Hak, 'reader-only')
    if ($On) { if (-not [IO.File]::Exists($file)) { [IO.File]::WriteAllBytes($file, [Text.Encoding]::ASCII.GetBytes("reader-only`r`n")) } }
    elseif ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
}
function Get-ReaderOnlyText {
    $label = T 'Reader only: start the Continue target at once and silently' 'Tylko czytnik: od razu i po cichu uruchom cel Kontynuuj'
    if (Test-ReaderOnly) { return ($label + ': ' + (T 'on' 'włączone')) } else { return ($label + ': ' + (T 'off' 'wyłączone')) }
}
function Get-RecordStateText {
    if ($script:RecordState -eq 'none') { return (T 'No settings record yet: Charon uses its defaults.' 'Nie ma jeszcze zapisu ustawień: Charon używa wartości domyślnych.') }
    if ($script:RecordState -eq 'invalid') { return (T 'The settings files are invalid: Charon uses defaults and refuses to save.' 'Pliki ustawień są nieprawidłowe: Charon używa wartości domyślnych i odmawia zapisu.') }
    return (T ("Settings record: format {0}, sequence {1}." -f $script:RecordVersion, $script:Seq) ("Zapis ustawień: format {0}, numer kolejny {1}." -f $script:RecordVersion, $script:Seq))
}
function Get-InfoLines {
    $lines = @()
    $lines += (T 'EFI system partition: ' 'Partycja systemowa EFI: ') + $script:CharonEspRoot
    foreach ($n in 'installed-by.txt', 'addon.txt') { $t = Get-CharonFirstLine $script:Hak $n; if ($t) { $lines += ($n + ': ' + $t) } }
    foreach ($n in 'HAKBOOT.EFI', 'CHARONVOICE.EFI') { $t = Get-CharonFileLine $script:Hak $n; if ($t) { $lines += $t } else { $lines += ($n + ': ' + (T 'not on the partition' 'brak na partycji')) } }
    $language = Get-CharonFirstLine $script:Hak 'audio-language'
    if ($language) { $lines += ((T 'Language Charon was installed with (audio-language): ' 'Język, z którym zainstalowano Charona (audio-language): ') + $language) }
    $lines += Get-RecordStateText
    foreach ($s in $script:Slots) {
        $name = $script:SlotNames[$s.Index]
        if ($s.Status -eq 'ok') { $lines += ('{0}: {1} {2}, {3} {4}' -f $name, (T 'format' 'format'), $s.Record.Version, (T 'sequence' 'numer kolejny'), $s.Record.Sequence) }
        elseif ($s.Status -eq 'notfound') { $lines += ($name + ': ' + (T 'not present' 'brak')) }
        else { $lines += ('{0}: {1} ({2})' -f $name, (T 'not accepted by Charon' 'nieprzyjmowany przez Charona'), $s.Reason) }
    }
    $lines += (T ("Format that will be written: {0} ({1})." -f $script:Target, $script:TargetWhy) ("Format, który zostanie zapisany: {0} ({1})." -f $script:Target, $script:TargetWhy))
    if ($script:WriteBlocked) { $lines += $script:WriteBlocked }
    $missing = @($script:CharonRows | Where-Object { -not (Test-RowStored $_) } | ForEach-Object { Get-RowLabel $_ })
    if ((Get-FormatAge $script:Target) -lt 10) { $missing += (T 'Synthesizer' 'Syntezator') }
    if ($missing.Count) { $lines += ((T 'Not stored by this Charon version: ' 'Nie zapisywane przez tę wersję Charona: ') + ($missing -join '; ')) }
    $lines += Get-AddonText
    $lines += Get-ReaderOnlyText
    $lines += (T $script:CharonLingerNoteEn $script:CharonLingerNotePl)
    $lines += ('Charon-Settings.ps1 ' + $script:CharonRelease + (T ', settings formats 1 to C' ', formaty ustawień od 1 do C'))
    return , $lines
}
function Write-ShowLines {
    $format = 'none'; if ($script:RecordVersion) { $format = $script:RecordVersion }
    Out-Line ('format=' + $format)
    Out-Line ('sequence=' + $script:Seq)
    foreach ($n in $script:CharonMembers) { Out-Line ('{0}={1}' -f $n, $script:S[$n]) }
}
# -Set: member=value (raw record value) or row=value (a row id of the table).
function Set-FromPair([string]$Pair) {
    $eq = $Pair.IndexOf('=')
    if ($eq -lt 1) { throw (T "Expected name=value, got $Pair." "Oczekiwano nazwa=wartość, a jest $Pair.") }
    $name = $Pair.Substring(0, $eq).Trim(); $value = 0
    if (-not [int]::TryParse($Pair.Substring($eq + 1).Trim(), [ref]$value)) { throw (T "The value of $name must be a number." "Wartość $name musi być liczbą.") }
    $member = $script:CharonMembers | Where-Object { $_ -ceq $name } | Select-Object -First 1
    $row = $null
    if (-not $member) {
        $row = $script:CharonRows | Where-Object { $_.Id -ceq $name } | Select-Object -First 1
        if (-not $row) { throw (T "Unknown setting $name." "Nieznane ustawienie $name.") }
        $member = $row.Member
    }
    $since = $script:CharonMemberSince[$member]; if ($row) { $since = $row.Since }
    if ((Get-FormatAge $since) -gt (Get-FormatAge $script:Target)) { throw (T "$name is not stored by settings format $($script:Target) of the installed Charon." "$name nie jest zapisywane w formacie ustawień $($script:Target) zainstalowanego Charona.") }
    if ($row) {
        if ($row.Kind -eq 'check' -and $value -ne 0 -and $value -ne 1) { throw (T "$name takes 0 or 1." "$name przyjmuje 0 albo 1.") }
        if ($row.Kind -eq 'choice' -and -not ($row.Choices | Where-Object { $_[0] -eq $value })) { throw (T "$name does not have the choice $value." "$name nie ma wyboru $value.") }
        if ($row.Kind -eq 'slider' -and ($value -lt $row.Low -or $value -gt $row.High)) { throw (T "$name takes $($row.Low)..$($row.High)." "$name przyjmuje $($row.Low)..$($row.High).") }
        Set-RowValue $row $value; return
    }
    $range = Test-CharonMember $member $value
    if ($range) { throw (T "The value $value of $member is out of range ($range)." "Wartość $value pola $member jest poza zakresem ($range).") }
    if ($member -eq 'readerExtra' -and ($value -band 128)) { throw (T $script:CharonLingerNoteEn $script:CharonLingerNotePl) }
    if ((Get-FormatAge $script:Target) -lt 11 -and (($member -eq 'formatFlags' -and $value -gt 63) -or ($member -eq 'readerExtra' -and $value -gt 31))) {
        throw (T "The value $value of $member has bits that settings format $($script:Target) of the installed Charon does not store." "Wartość $value pola $member ma bity, których format ustawień $($script:Target) zainstalowanego Charona nie zapisuje.")
    }
    $script:S[$member] = $value; $script:Dirty = $true
}

# ---- console interface: numbered menus, one prompt per line --------------------------
function Show-ConsolePage($Page) {
    $list = $true
    while ($true) {
        $rows = Get-PageRows $Page.Id
        if ($list) {
            Out-Line ''
            Out-Line ((Get-PageTitle $Page) + (T ", $($rows.Count) settings" ", liczba ustawień: $($rows.Count)"))
            for ($i = 0; $i -lt $rows.Count; $i++) {
                $row = $rows[$i]
                if (Test-RowStored $row) { Out-Line ('{0}. {1}: {2}' -f ($i + 1), (Get-RowLabel $row), (Get-RowValueText $row)) }
                else { Out-Line ('{0}. {1}: {2}' -f ($i + 1), (Get-RowLabel $row), (T 'not stored by this Charon version' 'nie zapisywane przez tę wersję Charona')) }
            }
            Out-Line (T '0. Back' '0. Wstecz')
        }
        $list = $false
        $n = Read-CharonMenu (T 'Number of the setting to change (L lists them again):' 'Numer ustawienia do zmiany (L ponownie czyta listę):') $rows.Count
        if ($n -eq -2) { $list = $true; continue }
        if ($n -le 0) { return ($n -lt 0) }
        $row = $rows[$n - 1]
        if (-not (Test-RowStored $row)) { Out-Line (T 'This setting is not stored by the installed Charon version.' 'Zainstalowana wersja Charona nie zapisuje tego ustawienia.'); continue }
        if ($row.Kind -eq 'check') { Set-RowValue $row (1 - (Get-RowValue $row)) }
        elseif ($row.Kind -eq 'choice') {
            Out-Line ((Get-RowLabel $row) + (T ", $($row.Choices.Count) choices" ", liczba wyborów: $($row.Choices.Count)"))
            for ($i = 0; $i -lt $row.Choices.Count; $i++) { Out-Line ('{0}. {1}' -f ($i + 1), (Get-ChoiceText $row.Choices[$i])) }
            Out-Line (T '0. Leave unchanged' '0. Bez zmiany')
            $c = Read-CharonNumber (T 'Number of the choice:' 'Numer wyboru:') $row.Choices.Count
            if ($c -lt 0) { return $true }
            if ($c -gt 0) { Set-RowValue $row $row.Choices[$c - 1][0] }
        } else {
            $steps = @(); for ($v = $row.Low; $v -le $row.High; $v++) { $steps += (Get-SliderText $row $v).TrimEnd('%') }
            Out-Line ((Get-RowLabel $row) + ': ' + (Get-RowValueText $row) + '. ' + (T 'Allowed percent values: ' 'Dozwolone wartości w procentach: ') + ($steps -join ', '))
            $line = Read-CharonLine (T 'Type a percent value, + for one step up, - for one step down, or nothing to leave it:' 'Wpisz wartość w procentach, + o krok w górę, - o krok w dół albo nic, aby zostawić:')
            if ($null -eq $line) { return $true }
            $current = Get-RowValue $row
            if ($line -eq '+') { if ($current -lt $row.High) { Set-RowValue $row ($current + 1) } }
            elseif ($line -eq '-') { if ($current -gt $row.Low) { Set-RowValue $row ($current - 1) } }
            elseif ($line -ne '') {
                $wanted = 0
                if ([int]::TryParse($line.TrimEnd('%').Trim(), [ref]$wanted)) {
                    $best = $row.Low; $bestGap = 1000
                    for ($v = $row.Low; $v -le $row.High; $v++) { $gap = [Math]::Abs([int]((Get-SliderText $row $v).TrimEnd('%')) - $wanted); if ($gap -lt $bestGap) { $best = $v; $bestGap = $gap } }
                    Set-RowValue $row $best
                } else { Out-Line (T 'Not a number; left unchanged.' 'To nie liczba; bez zmiany.') }
            }
        }
        Out-Line ((Get-RowLabel $row) + ': ' + (Get-RowValueText $row))
    }
}
function Show-ConsoleSynth {
    while ($true) {
        Out-Line ''
        Out-Line ((T 'Synthesizer' 'Syntezator') + (T ', 2 settings' ', liczba ustawień: 2'))
        if ((Get-FormatAge $script:Target) -lt 10) { Out-Line (T 'The installed Charon version does not store a synthesizer choice.' 'Zainstalowana wersja Charona nie zapisuje wyboru syntezatora.'); return $false }
        $synths = Get-SynthChoices; $voices = Get-SynthVoiceChoices
        $currentSynth = ($synths | Where-Object { $_[0] -eq $script:S.synthKey } | Select-Object -First 1)[1]
        $currentVoice = ($voices | Where-Object { $_[0] -eq $script:S.synthVoice } | Select-Object -First 1)[1]
        Out-Line ('1. ' + (T 'Synthesizer' 'Syntezator') + ': ' + $currentSynth)
        Out-Line ('2. ' + (T 'Synthesizer voice' 'Głos syntezatora') + ': ' + $currentVoice)
        Out-Line (T '0. Back' '0. Wstecz')
        $n = Read-CharonMenu (T 'Number of the setting to change (L lists them again):' 'Numer ustawienia do zmiany (L ponownie czyta listę):') 2
        if ($n -eq -2) { continue }
        if ($n -le 0) { return ($n -lt 0) }
        $list = $synths; if ($n -eq 2) { $list = $voices }
        for ($i = 0; $i -lt $list.Count; $i++) { Out-Line ('{0}. {1}' -f ($i + 1), $list[$i][1]) }
        Out-Line (T '0. Leave unchanged' '0. Bez zmiany')
        $c = Read-CharonNumber (T 'Number of the choice:' 'Numer wyboru:') $list.Count
        if ($c -lt 0) { return $true }
        if ($c -gt 0) {
            $value = [int]$list[$c - 1][0]
            if ($n -eq 1) { if ($script:S.synthKey -ne $value) { $script:S.synthKey = $value; $script:S.synthVoice = 0; $script:Dirty = $true } }
            elseif ($script:S.synthVoice -ne $value) { $script:S.synthVoice = $value; $script:Dirty = $true }
        }
    }
}
function Save-FromConsole {
    try { Export-CharonSettings; Out-Line (T ("Saved and read back. " + (Get-RecordStateText)) ("Zapisano i odczytano kontrolnie. " + (Get-RecordStateText))) }
    catch { Out-Line ((T 'Not saved: ' 'Nie zapisano: ') + $_.Exception.Message) }
}
function Show-ConsoleMenu {
    Out-Line (T 'Charon screen reader settings' 'Ustawienia czytnika ekranu Charon')
    Out-Line (Get-RecordStateText)
    if ($script:WriteBlocked) { Out-Line $script:WriteBlocked }
    $list = $true
    while ($true) {
        $pages = $script:CharonPages
        if ($list) {
            Out-Line ''
            Out-Line (T 'Main menu, 12 items' 'Menu główne, 12 pozycji')
            for ($i = 0; $i -lt $pages.Count; $i++) { Out-Line ('{0}. {1}' -f ($i + 1), (Get-PageTitle $pages[$i])) }
            Out-Line ('7. ' + (Get-AddonText))
            Out-Line ('8. ' + (Get-ReaderOnlyText))
            Out-Line (T '9. Restore defaults' '9. Przywróć ustawienia domyślne')
            Out-Line (T '10. Information' '10. Informacje')
            Out-Line (T '11. Program language: English. Switch to Polish' '11. Język programu: polski. Przełącz na angielski')
            Out-Line ((T '12. Save' '12. Zapisz') + $(if ($script:Dirty) { T ' (there are unsaved changes)' ' (są niezapisane zmiany)' } else { '' }))
            Out-Line (T '0. Quit' '0. Zakończ')
        }
        $list = $false
        $n = Read-CharonMenu (T 'Number (L lists the menu again):' 'Numer (L ponownie czyta menu):') 12
        if ($n -eq -2) { $list = $true; continue }
        $ended = $false
        if ($n -ge 1 -and $n -le 6) {
            $list = $true
            if ($script:RecordState -eq 'invalid') { Out-Line (T 'The settings on the partition are invalid. Choose 10 for details; a repair is offered there when possible.' 'Ustawienia na partycji są nieprawidłowe. Wybierz 10, aby poznać szczegóły; tam też jest naprawa, jeśli jest możliwa.'); continue }
            if ($pages[$n - 1].Id -eq 'synth') { $ended = Show-ConsoleSynth } else { $ended = Show-ConsolePage $pages[$n - 1] }
        } elseif ($n -eq 7) {
            try { $on = ($null -ne (Find-CharonVoice)); Out-Line (Set-Addon (-not $on)) } catch { Out-Line $_.Exception.Message }
        } elseif ($n -eq 8) {
            $on = Test-ReaderOnly
            $question = if ($on) { T 'Turn reader-only mode off? Charon then shows its menu and waits before it continues.' 'Wyłączyć tryb samego czytnika? Charon będzie wtedy pokazywał swoje menu i czekał przed kontynuacją.' } else { T 'Turn reader-only mode on? Charon then starts its Continue target at once and silently, with the screen reader; Space during the start still opens the menu.' 'Włączyć tryb samego czytnika? Charon będzie wtedy od razu i po cichu uruchamiał cel Kontynuuj, z czytnikiem ekranu; Spacja podczas startu nadal otwiera menu.' }
            if (Confirm-Charon $question) { Set-ReaderOnly (-not $on); Out-Line (Get-ReaderOnlyText) } else { Out-Line (T 'Cancelled. Nothing was changed.' 'Anulowano. Nic nie zmieniono.') }
        } elseif ($n -eq 9) {
            if (Confirm-Charon (T 'Restore all settings to Charon''s defaults? They are written when you choose Save.' 'Przywrócić wszystkim ustawieniom wartości domyślne Charona? Zostaną zapisane po wybraniu Zapisz.')) { Reset-CharonDefaults; Out-Line (T 'Defaults restored. Choose 12 to save them.' 'Przywrócono domyślne. Wybierz 12, aby je zapisać.') }
        } elseif ($n -eq 10) {
            foreach ($line in (Get-InfoLines)) { Out-Line $line }
            if (Test-CharonRepairable) {
                if (Confirm-Charon (T 'The settings record has a correct checksum but values Charon rejects. Repair it now (values out of range take their defaults)?' 'Zapis ustawień ma poprawną sumę kontrolną, ale wartości, które Charon odrzuca. Naprawić go teraz (wartości spoza zakresu przyjmą domyślne)?')) {
                    try { foreach ($line in (Repair-CharonSettings)) { Out-Line $line }; Out-Line (Get-RecordStateText) } catch { Out-Line $_.Exception.Message }
                }
            }
        } elseif ($n -eq 11) { $script:CharonPl = -not $script:CharonPl; $list = $true }
        elseif ($n -eq 12) { Save-FromConsole }
        if ($n -eq 0 -or $n -lt 0 -or $ended) {
            if ($n -eq 0 -and $script:Dirty) {
                Out-Line (T 'There are unsaved changes.' 'Są niezapisane zmiany.')
                $c = Read-CharonNumber (T '1 save and quit, 2 quit without saving, 0 back to the menu:' '1 zapisz i zakończ, 2 zakończ bez zapisywania, 0 wróć do menu:') 2
                if ($c -eq 0) { $list = $true; continue }
                if ($c -eq 1) { Save-FromConsole }
            } elseif ($n -ne 0 -and $script:Dirty) { Out-Line (T 'End of input: quitting without saving.' 'Koniec danych wejściowych: kończę bez zapisywania.') }
            Out-Line (T 'Goodbye.' 'Do widzenia.')
            return
        }
    }
}

# ---- dialog (Windows Forms, standard controls) -----------------------------------------
function Initialize-TrackBarType {
    if ('CharonOs.ValueTrackBar' -as [type]) { return $true }
    try {
        Add-Type -ReferencedAssemblies System.Windows.Forms, Accessibility -TypeDefinition @'
using System;
using System.Windows.Forms;
namespace CharonOs {
    // A track bar whose accessible value is the text the user hears ("50%"), not the raw step.
    public class ValueTrackBar : TrackBar {
        public string[] ValueTexts;
        public string ValueText {
            get { int i = Value - Minimum; return (ValueTexts != null && i >= 0 && i < ValueTexts.Length) ? ValueTexts[i] : Value.ToString(); }
        }
        protected override AccessibleObject CreateAccessibilityInstance() { return new ValueAccessible(this); }
        private class ValueAccessible : Control.ControlAccessibleObject {
            private readonly ValueTrackBar owner;
            public ValueAccessible(ValueTrackBar bar) : base(bar) { owner = bar; }
            public override string Value { get { return owner.ValueText; } set { } }
            public override AccessibleRole Role { get { return AccessibleRole.Slider; } }
        }
    }
}
'@
        return $true
    } catch { return $false }
}
function New-SettingsForm {
    $script:Bindings = @(); $script:Syncing = $true; $script:TabIndex = 0
    $form = New-Object Windows.Forms.Form
    $form.Text = T 'Charon screen reader settings' 'Ustawienia czytnika ekranu Charon'
    $form.StartPosition = 'CenterScreen'; $form.ClientSize = New-Object Drawing.Size 820, 640
    $form.MinimumSize = New-Object Drawing.Size 640, 480
    $form.Font = [Drawing.SystemFonts]::MessageBoxFont; $form.AutoScaleMode = 'Dpi'; $form.ShowInTaskbar = $true
    $tabs = New-Object Windows.Forms.TabControl
    $tabs.Dock = 'Fill'; $tabs.TabIndex = 0; $tabs.AccessibleName = T 'Settings pages' 'Strony ustawień'
    $useValueBar = Initialize-TrackBarType
    $next = { $script:TabIndex++; return $script:TabIndex }
    $newTable = {
        $t = New-Object Windows.Forms.TableLayoutPanel
        $t.Dock = 'Fill'; $t.AutoScroll = $true; $t.ColumnCount = 3; $t.Padding = New-Object Windows.Forms.Padding 8
        [void]$t.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle 'Percent', 50)); [void]$t.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle 'Percent', 38)); [void]$t.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle 'Percent', 12))
        return $t
    }
    $addCombo = {
        param($table, [string]$label, $choices, $current, [scriptblock]$onChange, [bool]$enabled, $context)
        $l = New-Object Windows.Forms.Label
        $l.Text = $label + ':'; $l.AutoSize = $true; $l.UseMnemonic = $false; $l.Anchor = 'Left'; $l.TabIndex = (& $next)
        $c = New-Object Windows.Forms.ComboBox
        $c.DropDownStyle = 'DropDownList'; $c.Dock = 'Fill'; $c.AccessibleName = $label; $c.TabIndex = (& $next); $c.Enabled = $enabled
        foreach ($choice in $choices) { [void]$c.Items.Add((Get-ChoiceText $choice)) }
        for ($i = 0; $i -lt $choices.Count; $i++) { if ($choices[$i][0] -eq $current) { $c.SelectedIndex = $i } }
        $c.Tag = @{ Choices = $choices; Change = $onChange; Context = $context }
        $c.Add_SelectedIndexChanged({ if (-not $script:Syncing -and $this.SelectedIndex -ge 0) { & $this.Tag.Change ([int]$this.Tag.Choices[$this.SelectedIndex][0]) $this.Tag.Context } })
        $row = $table.RowCount; $table.RowCount = $row + 1
        $table.Controls.Add($l, 0, $row); $table.Controls.Add($c, 1, $row); $table.SetColumnSpan($c, 2)
        return $c
    }
    foreach ($page in $script:CharonPages) {
        $tab = New-Object Windows.Forms.TabPage
        $tab.Text = Get-PageTitle $page; $tab.AccessibleName = $tab.Text
        $table = & $newTable
        if ($page.Id -eq 'synth') {
            $stored = ((Get-FormatAge $script:Target) -ge 10)
            $script:VoiceCombo = $null
            $synthCombo = & $addCombo $table (T 'Synthesizer' 'Syntezator') (Get-SynthChoices) $script:S.synthKey {
                param($value)
                if ($script:S.synthKey -ne $value) {
                    $script:S.synthKey = $value; $script:S.synthVoice = 0; $script:Dirty = $true
                    $voices = Get-SynthVoiceChoices
                    $script:Syncing = $true
                    $script:VoiceCombo.Items.Clear(); foreach ($v in $voices) { [void]$script:VoiceCombo.Items.Add($v[1]) }
                    $script:VoiceCombo.Tag.Choices = $voices; $script:VoiceCombo.SelectedIndex = 0
                    $script:Syncing = $false
                }
            } $stored
            $script:VoiceCombo = & $addCombo $table (T 'Synthesizer voice' 'Głos syntezatora') (Get-SynthVoiceChoices) $script:S.synthVoice { param($value) if ($script:S.synthVoice -ne $value) { $script:S.synthVoice = $value; $script:Dirty = $true } } $stored
            $note = New-Object Windows.Forms.Label
            $note.AutoSize = $true; $note.UseMnemonic = $false; $note.TabIndex = (& $next)
            $note.Text = T 'Add-ons are the *.manifest files in \EFI\HAK\synths on the partition.' 'Dodatki to pliki *.manifest w \EFI\HAK\synths na partycji.'
            $r = $table.RowCount; $table.RowCount = $r + 1; $table.Controls.Add($note, 0, $r); $table.SetColumnSpan($note, 3)
        } else {
            foreach ($row in (Get-PageRows $page.Id)) {
                $stored = Test-RowStored $row
                $label = Get-RowLabel $row
                if ($row.Kind -eq 'check') {
                    $c = New-Object Windows.Forms.CheckBox
                    $c.Text = $label; $c.AutoSize = $true; $c.UseMnemonic = $false; $c.AccessibleName = $label; $c.TabIndex = (& $next); $c.Enabled = $stored
                    $c.Checked = [bool](Get-RowValue $row); $c.Tag = $row
                    $c.Add_CheckedChanged({ if (-not $script:Syncing) { Set-RowValue $this.Tag ([int]$this.Checked) } })
                    $r = $table.RowCount; $table.RowCount = $r + 1; $table.Controls.Add($c, 0, $r); $table.SetColumnSpan($c, 3)
                    $script:Bindings += @{ Row = $row; Control = $c }
                } elseif ($row.Kind -eq 'choice') {
                    $c = & $addCombo $table $label $row.Choices (Get-RowValue $row) { param($value, $context) Set-RowValue $context $value } $stored $row
                    $script:Bindings += @{ Row = $row; Control = $c }
                } else {
                    $l = New-Object Windows.Forms.Label
                    $l.Text = $label + ':'; $l.AutoSize = $true; $l.UseMnemonic = $false; $l.Anchor = 'Left'; $l.TabIndex = (& $next)
                    $texts = @(); for ($v = $row.Low; $v -le $row.High; $v++) { $texts += (Get-SliderText $row $v) }
                    if ($useValueBar) { $bar = New-Object CharonOs.ValueTrackBar; $bar.ValueTexts = [string[]]$texts } else { $bar = New-Object Windows.Forms.TrackBar }
                    $bar.Minimum = $row.Low; $bar.Maximum = $row.High; $bar.SmallChange = 1; $bar.LargeChange = 2; $bar.TickFrequency = 1
                    $bar.Dock = 'Fill'; $bar.TabIndex = (& $next); $bar.Enabled = $stored; $bar.Value = [Math]::Max($row.Low, [Math]::Min($row.High, (Get-RowValue $row)))
                    $shown = New-Object Windows.Forms.Label
                    $shown.AutoSize = $true; $shown.Anchor = 'Left'; $shown.UseMnemonic = $false; $shown.TabIndex = (& $next); $shown.Text = Get-SliderText $row $bar.Value
                    $bar.AccessibleName = $label
                    if (-not $useValueBar) { $bar.AccessibleName = $label + ': ' + $shown.Text }
                    $bar.Tag = @{ Row = $row; Shown = $shown; Label = $label; Plain = (-not $useValueBar) }
                    $bar.Add_ValueChanged({
                        $this.Tag.Shown.Text = Get-SliderText $this.Tag.Row $this.Value
                        if ($this.Tag.Plain) { $this.AccessibleName = $this.Tag.Label + ': ' + $this.Tag.Shown.Text }
                        if (-not $script:Syncing) { Set-RowValue $this.Tag.Row ([int]$this.Value) }
                    })
                    $r = $table.RowCount; $table.RowCount = $r + 1
                    $table.Controls.Add($l, 0, $r); $table.Controls.Add($bar, 1, $r); $table.Controls.Add($shown, 2, $r)
                    $script:Bindings += @{ Row = $row; Control = $bar }
                }
            }
        }
        $tab.Controls.Add($table); $tabs.TabPages.Add($tab)
    }
    # Add-on, reader-only marker, information.
    $tab = New-Object Windows.Forms.TabPage
    $tab.Text = T 'Add-on and information' 'Dodatek i informacje'; $tab.AccessibleName = $tab.Text
    $table = & $newTable
    $script:AddonLabel = New-Object Windows.Forms.Label
    $script:AddonLabel.AutoSize = $true; $script:AddonLabel.UseMnemonic = $false; $script:AddonLabel.TabIndex = (& $next); $script:AddonLabel.Text = Get-AddonText
    $r = $table.RowCount; $table.RowCount = $r + 1; $table.Controls.Add($script:AddonLabel, 0, $r); $table.SetColumnSpan($script:AddonLabel, 3)
    $addonButton = New-Object Windows.Forms.Button
    $addonButton.Text = T 'Turn speech from &power-on on or off...' 'Włącz albo wyłącz mowę &od włączenia komputera...'
    $addonButton.AutoSize = $true; $addonButton.TabIndex = (& $next); $addonButton.Enabled = (Test-CharonFirmware)
    $addonButton.AccessibleDescription = Get-AddonText
    $addonButton.Add_Click({
        try { $on = ($null -ne (Find-CharonVoice)); $text = Set-Addon (-not $on) } catch { $text = $_.Exception.Message }
        $script:AddonLabel.Text = Get-AddonText; $this.AccessibleDescription = $script:AddonLabel.Text
        Show-CharonMessage $text
    })
    $r = $table.RowCount; $table.RowCount = $r + 1; $table.Controls.Add($addonButton, 0, $r); $table.SetColumnSpan($addonButton, 3)
    $script:ReaderOnlyBox = New-Object Windows.Forms.CheckBox
    $script:ReaderOnlyBox.Text = T 'Reader only: start the Continue target at once and silently (applied by Save)' 'Tylko czytnik: od razu i po cichu uruchom cel Kontynuuj (stosowane przez Zapisz)'
    $script:ReaderOnlyBox.AccessibleName = $script:ReaderOnlyBox.Text; $script:ReaderOnlyBox.AutoSize = $true; $script:ReaderOnlyBox.UseMnemonic = $false
    $script:ReaderOnlyBox.TabIndex = (& $next); $script:ReaderOnlyBox.Checked = (Test-ReaderOnly)
    $r = $table.RowCount; $table.RowCount = $r + 1; $table.Controls.Add($script:ReaderOnlyBox, 0, $r); $table.SetColumnSpan($script:ReaderOnlyBox, 3)
    $languageCombo = & $addCombo $table (T 'Program language' 'Język programu') @(, @(0, 'English') + , @(1, 'Polski')) ([int]$script:CharonPl) {
        param($value)
        if ([bool]$value -ne $script:CharonPl) { $script:CharonPl = [bool]$value; $script:Reopen = $true; $script:Form.Close() }
    } $true
    $infoLabel = New-Object Windows.Forms.Label
    $infoLabel.Text = T '&Information:' '&Informacje:'; $infoLabel.AutoSize = $true; $infoLabel.TabIndex = (& $next)
    $r = $table.RowCount; $table.RowCount = $r + 1; $table.Controls.Add($infoLabel, 0, $r); $table.SetColumnSpan($infoLabel, 3)
    $script:InfoBox = New-Object Windows.Forms.TextBox
    $script:InfoBox.Multiline = $true; $script:InfoBox.ReadOnly = $true; $script:InfoBox.ScrollBars = 'Vertical'; $script:InfoBox.Dock = 'Fill'; $script:InfoBox.Height = 260
    $script:InfoBox.AccessibleName = T 'Information' 'Informacje'; $script:InfoBox.TabIndex = (& $next); $script:InfoBox.Text = ((Get-InfoLines) -join "`r`n")
    $r = $table.RowCount; $table.RowCount = $r + 1; $table.Controls.Add($script:InfoBox, 0, $r); $table.SetColumnSpan($script:InfoBox, 3)
    $tab.Controls.Add($table); $tabs.TabPages.Add($tab)

    $buttons = New-Object Windows.Forms.FlowLayoutPanel
    $buttons.Dock = 'Bottom'; $buttons.AutoSize = $true; $buttons.FlowDirection = 'LeftToRight'; $buttons.Padding = New-Object Windows.Forms.Padding 8; $buttons.TabIndex = 1
    $newButton = { param([string]$text) $b = New-Object Windows.Forms.Button; $b.Text = $text; $b.AutoSize = $true; $b.TabIndex = (& $next); $buttons.Controls.Add($b); return $b }
    $save = & $newButton (T '&Save' '&Zapisz')
    $defaults = & $newButton (T 'Restore &defaults' 'Przywróć &domyślne')
    $repair = & $newButton (T '&Repair the settings record' '&Napraw zapis ustawień')
    $close = & $newButton (T '&Close' 'Za&mknij')
    $repair.Enabled = (Test-CharonRepairable)
    $save.Enabled = (-not $script:WriteBlocked -and $script:RecordState -ne 'invalid')
    $script:SaveAction = {
        try {
            Export-CharonSettings
            Set-ReaderOnly $script:ReaderOnlyBox.Checked
            $script:InfoBox.Text = ((Get-InfoLines) -join "`r`n")
            Show-CharonMessage (T ("Saved and read back. " + (Get-RecordStateText)) ("Zapisano i odczytano kontrolnie. " + (Get-RecordStateText)))
            return $true
        } catch { Show-CharonMessage ((T 'Not saved: ' 'Nie zapisano: ') + $_.Exception.Message); return $false }
    }
    $save.Add_Click({ [void](& $script:SaveAction) })
    $defaults.Add_Click({
        if (Confirm-Charon (T 'Restore all settings to Charon''s defaults? They are written when you choose Save.' 'Przywrócić wszystkim ustawieniom wartości domyślne Charona? Zostaną zapisane po wybraniu Zapisz.')) {
            Reset-CharonDefaults; $script:Reopen = $true; $script:KeepModel = $true; $script:Form.Close()
        }
    })
    $repair.Add_Click({
        try { $lines = Repair-CharonSettings; Show-CharonMessage (($lines + (Get-RecordStateText)) -join "`r`n"); $script:Reopen = $true; $script:Form.Close() }
        catch { Show-CharonMessage $_.Exception.Message }
    })
    $close.Add_Click({ $script:Form.Close() })
    $form.AcceptButton = $save; $form.CancelButton = $close
    $form.Add_FormClosing({
        $changed = ($script:Dirty -or ($script:ReaderOnlyBox.Checked -ne (Test-ReaderOnly)))
        if ($script:Reopen -or -not $changed -or $script:SelfTest) { return }
        $r = [Windows.Forms.MessageBox]::Show((T 'There are unsaved changes. Save them now?' 'Są niezapisane zmiany. Zapisać je teraz?'), $script:Form.Text, [Windows.Forms.MessageBoxButtons]::YesNoCancel, [Windows.Forms.MessageBoxIcon]::Question)
        if ($r -eq [Windows.Forms.DialogResult]::Cancel) { $_.Cancel = $true }
        elseif ($r -eq [Windows.Forms.DialogResult]::Yes) { if (-not (& $script:SaveAction)) { $_.Cancel = $true } }
    })
    $form.Controls.Add($tabs); $form.Controls.Add($buttons)
    $script:Syncing = $false
    $script:Form = $form
    return $form
}
# -GuiSelfTest: builds the dialog without showing it and prints what a screen reader gets.
function Write-ControlTree($Control, [int]$Depth) {
    foreach ($child in ($Control.Controls | Sort-Object TabIndex)) {
        $kind = $child.GetType().Name
        $name = $child.AccessibleName; if (-not $name) { $name = $child.Text }
        $value = ''
        if ($child -is [Windows.Forms.CheckBox]) { $value = [string]$child.Checked }
        elseif ($child -is [Windows.Forms.ComboBox]) { $value = [string]$child.SelectedItem }
        elseif ($child -is [Windows.Forms.TrackBar]) { $value = $child.AccessibilityObject.Value + ' (role ' + $child.AccessibilityObject.Role + ')' }
        if ($kind -ne 'TableLayoutPanel' -and $kind -ne 'FlowLayoutPanel') { Out-Line ((' ' * $Depth) + ('{0} tab={1} enabled={2} name="{3}" value="{4}"' -f $kind, $child.TabIndex, $child.Enabled, ($name -replace "`r`n", ' | '), $value)) }
        if ($child.Controls.Count -and -not ($child -is [Windows.Forms.ComboBox])) { Write-ControlTree $child ($Depth + 1) }
    }
}
function Show-SettingsDialog {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()
    $script:CharonGui = $true; $script:SelfTest = [bool]$GuiSelfTest
    do {
        $script:Reopen = $false; $script:KeepModel = $false
        $form = New-SettingsForm
        if ($script:SelfTest) {
            Out-Line ('Form "' + $form.Text + '" accept=' + $form.AcceptButton.Text + ' cancel=' + $form.CancelButton.Text)
            Write-ControlTree $form 1
            # With -Set the test operates the controls themselves (not the model) and saves.
            if ($Set.Count) {
                foreach ($item in $Set) {
                    foreach ($pair in $item.Split(',')) {
                        $parts = $pair.Trim().Split('='); $value = [int]$parts[1]
                        $binding = $script:Bindings | Where-Object { $_.Row.Id -ceq $parts[0] } | Select-Object -First 1
                        if (-not $binding) { throw ('no control for ' + $parts[0]) }
                        $control = $binding.Control
                        if ($control -is [Windows.Forms.CheckBox]) { $control.Checked = [bool]$value }
                        elseif ($control -is [Windows.Forms.ComboBox]) { for ($i = 0; $i -lt $control.Tag.Choices.Count; $i++) { if ($control.Tag.Choices[$i][0] -eq $value) { $control.SelectedIndex = $i } } }
                        else { $control.Value = $value }
                    }
                }
                Export-CharonSettings
                Out-Line 'dialog-saved'
                Write-ShowLines
            }
            $form.Dispose(); return
        }
        [void]$form.ShowDialog()
        $form.Dispose()
    } while ($script:Reopen)
}

# ---- main ----------------------------------------------------------------------------------
Initialize-CharonConsole
if ($Version) { Out-Line ('Charon-Settings.ps1 ' + $script:CharonRelease + ' (settings formats 1-C)'); exit 0 }
$code = 0
$nonInteractive = (-not $GuiSelfTest) -and ($Show -or $Set.Count -or $Defaults -or $Repair -or $ReaderOnly -or $Addon -or $Synths)
try {
    if ($Format -and $Format.Length -ne 1) { throw 'Format: 1-9, A, B or C.' }
    $root = Open-CharonEsp
    $script:Hak = Get-CharonHak $root
    Set-CharonLanguage $script:Hak
    if (-not $script:Hak) {
        $code = 2
        throw (T "Charon is not installed on this EFI partition (no \EFI\HAK in $root)." "Charon nie jest zainstalowany na tej partycji EFI (brak \EFI\HAK w $root).")
    }
    if ($script:CharonFirmwareMode -ne 'none' -and (-not $nonInteractive -or $Addon)) {
        try { Initialize-CharonFirmware }
        catch { if ($Addon) { $code = 7; throw }; $script:CharonFirmwareMode = 'none'; $script:FirmwareNote = $_.Exception.Message }
    } elseif (-not $Addon -and $script:CharonFirmwareMode -eq 'real') { $script:CharonFirmwareMode = 'none' }
    Import-CharonSettings
    $script:SynthList = Get-CharonSynths
    if ($nonInteractive) {
        if ($Synths) { foreach ($s in $script:SynthList) { Out-Line ('key={0} file={1} voices={2} name={3}' -f $s.Key, $s.File, $s.Voices.Count, $s.Name) } }
        if ($Repair) {
            if (-not (Test-CharonRepairable)) { $code = 5; throw (T 'There is nothing to repair.' 'Nie ma czego naprawiać.') }
            foreach ($line in (Repair-CharonSettings)) { Out-Line $line }
        }
        if ($Defaults -or $Set.Count) {
            if ($script:WriteBlocked) { $code = 6; throw $script:WriteBlocked }
            if ($script:RecordState -eq 'invalid') { $code = 4; throw (Get-RecordStateText) }
            if ($Defaults) { Reset-CharonDefaults }
            $code = 5
            foreach ($item in $Set) { foreach ($pair in $item.Split(',')) { if ($pair.Trim()) { Set-FromPair $pair.Trim() } } }
            Export-CharonSettings
            $code = 0
        }
        if ($ReaderOnly) {
            if ($ReaderOnly -notmatch '^(?i)(on|off)$') { $code = 5; throw '-ReaderOnly on|off' }
            Set-ReaderOnly ($ReaderOnly -match '^(?i)on$'); Out-Line ('reader-only=' + $(if (Test-ReaderOnly) { 'on' } else { 'off' }))
        }
        if ($Addon) {
            if (-not (Test-CharonFirmware)) { $code = 7; throw (Get-CharonNoFirmwareText) }
            if ($Addon -match '^(?i)(on|off)$') { $code = 5; Out-Line (Set-Addon ($Addon -match '^(?i)on$')); $code = 0 }
            $id = Find-CharonVoice
            Out-Line ('addon=' + $(if ($null -ne $id) { 'on ' + (Get-CharonOptionName 'Driver' $id) } else { 'off' }))
        }
        if ($Show -or $Set.Count -or $Defaults -or $Repair) {
            if ($script:RecordState -eq 'invalid') {
                Out-Line 'record=invalid'
                foreach ($s in $script:Slots) { Out-Line ('slot{0}={1} {2}' -f @('a', 'b')[$s.Index], $s.Status, $s.Reason) }
                $code = 4
            } else { Write-ShowLines }
        }
    }
    elseif ($Console) { Show-ConsoleMenu }
    else { Show-SettingsDialog }
}
catch {
    if ($code -eq 0) { $code = 1 }
    $text = $_.Exception.Message
    if ($script:CharonGui -or (-not $Console -and -not $nonInteractive -and -not $GuiSelfTest)) {
        try { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($text, 'Charon', [Windows.Forms.MessageBoxButtons]::OK, [Windows.Forms.MessageBoxIcon]::Error) } catch { }
    }
    [Console]::Error.WriteLine($text)
}
finally { Close-CharonEsp }
exit $code
