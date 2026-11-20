<#
Charon installer for Windows (r391; the r385 add-on installer with three
ways to install). GPL-2.0-or-later.

  -Mode Reader   (default, as r385) CHARONVOICE.EFI as Driver####, below.
  -Mode Shim     screen reader through shim without Charon's menu: a boot
                 entry "Charon czytnik" -> \EFI\HAK\shimx64.efi -> Charon
                 (\EFI\HAK\grubx64.efi, marker reader-only), which starts
                 the current default (Windows Boot Manager) at once and reads
                 it. Starts after the firmware's own menus (not read).
  -Mode Manager  Charon boot manager (menu and reader), boot entry "Charon";
                 through shim when Secure Boot is on. r399: ALSO the screen
                 reader outside Charon (CHARONVOICE.EFI as Driver####: speech
                 from power-on, firmware menus and setup) unless -NoReader.
  -NoReader      Manager: only the boot manager, no Driver#### reader.
  -Default       Shim and Manager: Charon first in BootOrder (Shim always).
  -FirstEntry D  r403: at the end, the existing boot entry with description D
                 (e.g. CharonDOS) is put first in BootOrder - Windows Setup put
                 Windows Boot Manager first; CharonDOS installed before Windows
                 stays the default (its WINNT command starts Windows).
  -Yes           no questions (otherwise a dialogue and "TAK" to confirm).
Shim and MokManager (shimx64.efi, mmx64.efi, signed by Microsoft's UEFI CA)
are taken from -ShimDir (default: shim next to this script, e.g. from a
Debian or Ubuntu installation medium \EFI\boot\bootx64.efi and mmx64.efi).
With Secure Boot on, Charon's files are signed with this computer's
certificate (as for the add-on) and the certificate is saved as
\EFI\HAK\charon-mok.cer. Windows has no mokutil: at the next start shim
cannot verify Charon and opens MokManager; there "Enroll key from disk"
selects that file and confirms it (MokManager does not speak; the steps are
printed). Charon's Continue is set to the current first boot entry unless
Charon already has a Continue target.

Add-on mode (r385):

Installs CHARONVOICE.EFI as a UEFI driver option (Driver####) on the EFI
system partition of THIS Windows computer. Firmware then loads it at every
power-on, before its own boot menu; it speaks and reads every text and
graphical screen (firmware menus and setup, Windows Boot Manager, recovery,
other boot managers) until the operating system takes over
(ExitBootServices). Windows stays the default: BootOrder, BCD and the
Windows Boot Manager are never changed.

  Install-CharonAddon.ps1 [-Source DIR] [-Language pl|en] [-SuspendBitLocker]
  Install-CharonAddon.ps1 -Uninstall [-KeepFiles]
  Install-CharonAddon.ps1 -Status
  Install-CharonAddon.ps1 -DryRun -PartitionNumber N -StartLba L -SizeLba S -PartitionGuid G
      (prints the Driver#### payload in hex; nothing is read or written)

Requirements: UEFI boot (GPT system disk), administrator rights.
Secure Boot: with Secure Boot on, the installer signs the copied
CHARONVOICE.EFI and AudioDxe.efi with a certificate of THIS computer
("Charon UEFI add-on (this computer)", created once in the administrator's
certificate store, private key not exportable) and saves the certificate as
\EFI\HAK\charon-db.cer on the EFI partition and charon-db.cer next to this
script. The owner then adds that certificate to the db list in firmware setup
(e.g. Secure Boot > Key Management > db > Append / Enroll from file); until
then firmware skips the driver and the computer starts as before, without
speech. Microsoft's certificates stay in db, so Windows keeps starting.
BitLocker: a new firmware driver changes the measured boot state; with
BitLocker on, Windows may ask for the recovery key at the next start. The
script stops unless -SuspendBitLocker is given (suspends protection for one
restart, the usual procedure for firmware changes).
#>
[CmdletBinding()]
param(
    [ValidateSet('', 'Reader', 'Shim', 'Manager')][string]$Mode = '',
    [switch]$Default,
    [switch]$NoReader,
    [switch]$Yes,
    [string]$ShimDir = '',
    [switch]$Sign,
    [string]$Source = '',
    [ValidateSet('', 'pl', 'en')][string]$Language = '',
    [switch]$SuspendBitLocker,
    [switch]$Uninstall,
    [switch]$KeepFiles,
    [switch]$Status,
    [switch]$DryRun,
    [uint32]$PartitionNumber,
    [uint64]$StartLba,
    [uint64]$SizeLba,
    [string]$PartitionGuid,
    [string]$FirstEntry = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Source) { $Source = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'EFI\HAK' }
$GlobalGuid = '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
$Description = 'Charon voice'           # the same name CHARON.EFI uses (BootMenu.h)
$DriverFile = '\EFI\HAK\CHARONVOICE.EFI'
$EspType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
. (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'CharonEfiSign.ps1')
$Pl = if ($Language) { $Language -eq 'pl' } else { (Get-UICulture).Name -like 'pl*' }
function Say([string]$textPl, [string]$textEn) { if ($Pl) { Write-Host $textPl } else { Write-Host $textEn } }   # r402: a parameter named $pl hid the $Pl flag (names are case-insensitive): everything was said in Polish
function Fail([string]$textPl, [string]$textEn) { if ($Pl) { throw $textPl } else { throw $textEn } }

# ---- load option bytes (UEFI 2.10, 3.1.3 and 10.3) --------------------------
function New-DriverOption([uint32]$Number, [uint64]$Start, [uint64]$Size, [guid]$Guid) {
    $hd = New-Object byte[] 42
    $hd[0] = 4; $hd[1] = 1; $hd[2] = 42; $hd[3] = 0        # MEDIA_DEVICE_PATH / HARDDRIVE
    [BitConverter]::GetBytes([uint32]$Number).CopyTo($hd, 4)
    [BitConverter]::GetBytes([uint64]$Start).CopyTo($hd, 8)
    [BitConverter]::GetBytes([uint64]$Size).CopyTo($hd, 16)
    $Guid.ToByteArray().CopyTo($hd, 24)                    # GPT partition GUID (mixed-endian, as on disk)
    $hd[40] = 2; $hd[41] = 2                               # GPT partition format, GUID signature
    $name = [Text.Encoding]::Unicode.GetBytes($DriverFile + [char]0)
    $file = New-Object byte[] (4 + $name.Length)
    $file[0] = 4; $file[1] = 4; [BitConverter]::GetBytes([uint16]$file.Length).CopyTo($file, 2)
    $name.CopyTo($file, 4)
    $end = [byte[]](0x7f, 0xff, 4, 0)
    $path = $hd + $file + $end
    $desc = [Text.Encoding]::Unicode.GetBytes($Description + [char]0)
    $attributes = [BitConverter]::GetBytes([uint32]3)      # LOAD_OPTION_ACTIVE | LOAD_OPTION_FORCE_RECONNECT
    return [byte[]]($attributes + [BitConverter]::GetBytes([uint16]$path.Length) + $desc + $path)
}
if ($DryRun) {
    if (-not $PartitionNumber -or -not $SizeLba -or -not $PartitionGuid) { throw '-DryRun needs -PartitionNumber, -StartLba, -SizeLba and -PartitionGuid' }
    $bytes = New-DriverOption $PartitionNumber $StartLba $SizeLba ([guid]$PartitionGuid)
    Write-Output (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
    return
}

# ---- firmware variables through kernel32 ------------------------------------
Add-Type -Namespace CharonAddon -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern uint GetFirmwareEnvironmentVariableExW(string name, string guid, byte[] buffer, uint size, out uint attributes);
[DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern bool SetFirmwareEnvironmentVariableExW(string name, string guid, byte[] buffer, uint size, uint attributes);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool GetFirmwareType(out uint type);
[DllImport("advapi32.dll", SetLastError=true)]
public static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
[DllImport("advapi32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern bool LookupPrivilegeValueW(string system, string name, out long luid);
[StructLayout(LayoutKind.Sequential, Pack=4)]
public struct TokenPrivileges { public uint Count; public long Luid; public uint Attributes; }
[DllImport("advapi32.dll", SetLastError=true)]
public static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges state, uint length, IntPtr previous, IntPtr returned);
[DllImport("kernel32.dll")]
public static extern IntPtr GetCurrentProcess();
'@
function Enable-FirmwarePrivilege {
    $token = [IntPtr]::Zero; $luid = 0
    if (-not [CharonAddon.Native]::OpenProcessToken([CharonAddon.Native]::GetCurrentProcess(), 0x28, [ref]$token)) { Fail 'Nie można otworzyć tokenu procesu.' 'Cannot open the process token.' }
    if (-not [CharonAddon.Native]::LookupPrivilegeValueW($null, 'SeSystemEnvironmentPrivilege', [ref]$luid)) { Fail 'Brak uprawnienia SeSystemEnvironmentPrivilege.' 'SeSystemEnvironmentPrivilege not found.' }
    $state = New-Object CharonAddon.Native+TokenPrivileges
    $state.Count = 1; $state.Luid = $luid; $state.Attributes = 2
    [void][CharonAddon.Native]::AdjustTokenPrivileges($token, $false, [ref]$state, 0, [IntPtr]::Zero, [IntPtr]::Zero)
    if ([Runtime.InteropServices.Marshal]::GetLastWin32Error() -ne 0) { Fail 'Windows nie przyznał uprawnienia do zmiennych firmware (uruchom jako administrator).' 'Windows did not grant the firmware variable privilege (run as administrator).' }
}
function Get-Var([string]$Name) {
    $buffer = New-Object byte[] 4096; $attr = 0
    $n = [CharonAddon.Native]::GetFirmwareEnvironmentVariableExW($Name, $GlobalGuid, $buffer, 4096, [ref]$attr)
    if ($n -eq 0) {
        $e = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($e -eq 203) { return $null }                   # ERROR_ENVVAR_NOT_FOUND
        Fail "Odczyt zmiennej $Name nie powiódł się (błąd $e)." "Reading variable $Name failed (error $e)."
    }
    return [byte[]]$buffer[0..($n - 1)]
}
function Set-Var([string]$Name, [byte[]]$Data) {
    $size = if ($Data) { $Data.Length } else { 0 }
    if (-not [CharonAddon.Native]::SetFirmwareEnvironmentVariableExW($Name, $GlobalGuid, $Data, [uint32]$size, 7)) {
        $e = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Fail "Firmware odrzuciło zapis $Name (błąd $e). Nic więcej nie zmieniono." "Firmware rejected writing $Name (error $e). Nothing else was changed."
    }
}
function Get-Order { $b = Get-Var 'DriverOrder'; $ids = @(); if ($b) { for ($i = 0; $i + 1 -lt $b.Length; $i += 2) { $ids += [BitConverter]::ToUInt16($b, $i) } }; return , $ids }
function Set-Order([uint16[]]$Ids) { $b = @(); foreach ($id in $Ids) { $b += [BitConverter]::GetBytes([uint16]$id) }; Set-Var 'DriverOrder' ([byte[]]$b) }
function VarName([int]$Id) { return 'Driver{0:X4}' -f $Id }
function Find-Charon {
    foreach ($id in (Get-Order)) {
        $o = Get-Var (VarName $id)
        if ($o -and $o.Length -gt 6) {
            $desc = [Text.Encoding]::Unicode.GetBytes($Description + [char]0)
            if ($o.Length -ge 6 + $desc.Length -and [Linq.Enumerable]::SequenceEqual([byte[]]$o[6..(5 + $desc.Length)], [byte[]]$desc)) { return $id }
        }
    }
    return $null
}

# ---- checks -------------------------------------------------------------------
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Fail 'Uruchom jako administrator.' 'Run as administrator.' }
$type = 0; [void][CharonAddon.Native]::GetFirmwareType([ref]$type)
if ($type -ne 2) { Fail 'Ten Windows nie wystartował w trybie UEFI; dodatek UEFI nie zadziała (tryb BIOS/CSM).' 'This Windows did not start in UEFI mode; a UEFI add-on cannot work (BIOS/CSM mode).' }
Enable-FirmwarePrivilege
$existing = Find-Charon

if ($Status) {
    if ($null -eq $existing) { Say 'Dodatek Charon nie jest zainstalowany.' 'The Charon add-on is not installed.' }
    else { Say ("Dodatek Charon jest zainstalowany: {0}, pozycja {1} w DriverOrder." -f (VarName $existing), ([array]::IndexOf((Get-Order), [uint16]$existing) + 1)) ("The Charon add-on is installed: {0}, position {1} in DriverOrder." -f (VarName $existing), ([array]::IndexOf((Get-Order), [uint16]$existing) + 1)) }
    return
}

$esp = Get-Partition | Where-Object { $_.GptType -eq $EspType -and $_.IsSystem } | Select-Object -First 1
if (-not $esp) { $esp = Get-Partition | Where-Object { $_.GptType -eq $EspType } | Select-Object -First 1 }
if (-not $esp) { Fail 'Nie znaleziono partycji systemowej EFI (dysk GPT).' 'No EFI system partition found (GPT disk).' }
$disk = Get-Disk -Number $esp.DiskNumber
$sector = [uint64]$disk.LogicalSectorSize
function Mount-Esp {
    $letter = [char[]](90..68) | Where-Object { -not (Test-Path "$($_):\") } | Select-Object -First 1
    if (-not $letter) { Fail 'Brak wolnej litery dysku do zamontowania partycji EFI.' 'No free drive letter to mount the EFI partition.' }
    Add-PartitionAccessPath -DiskNumber $esp.DiskNumber -PartitionNumber $esp.PartitionNumber -AccessPath "$($letter):\"
    return "$($letter):"
}
function Dismount-Esp([string]$Root) { Remove-PartitionAccessPath -DiskNumber $esp.DiskNumber -PartitionNumber $esp.PartitionNumber -AccessPath "$Root\" -ErrorAction SilentlyContinue }

if ($Uninstall) {
    # r391: Charon's boot entries ("Charon", "Charon czytnik") too.
    $bo = Get-Var 'BootOrder'; $bootIds = @(); if ($bo) { for ($i = 0; $i + 1 -lt $bo.Length; $i += 2) { $bootIds += [BitConverter]::ToUInt16($bo, $i) } }
    $keep = @()
    foreach ($id in $bootIds) {
        $o = Get-Var ('Boot{0:X4}' -f $id); $d = ''
        if ($o -and $o.Length -gt 8) { $end = 6; while ($end + 1 -lt $o.Length -and -not ($o[$end] -eq 0 -and $o[$end + 1] -eq 0)) { $end += 2 }; $d = [Text.Encoding]::Unicode.GetString($o, 6, $end - 6) }
        if ($d -in @('Charon', 'Charon czytnik')) { Set-Var ('Boot{0:X4}' -f $id) $null; Say "Usunięto wpis rozruchu $('Boot{0:X4}' -f $id) ($d)." "Removed boot entry $('Boot{0:X4}' -f $id) ($d)." }
        else { $keep += $id }
    }
    if ($keep.Count -ne $bootIds.Count) { $b = @(); foreach ($id in $keep) { $b += [BitConverter]::GetBytes([uint16]$id) }; Set-Var 'BootOrder' ([byte[]]$b) }
    if ($null -ne $existing) {
        Set-Order ([uint16[]]((Get-Order) | Where-Object { $_ -ne $existing }))
        Set-Var (VarName $existing) $null
        Say "Usunięto wpis sterownika $(VarName $existing) z firmware." "Removed driver entry $(VarName $existing) from firmware."
    } else { Say 'Wpis sterownika Charon nie istniał.' 'There was no Charon driver entry.' }
    if (-not $KeepFiles) {
        $root = Mount-Esp
        try {
            $hak = Join-Path $root 'EFI\HAK'
            # Only the add-on's own files; a full Charon installation on the same partition stays.
            if (Test-Path (Join-Path $hak 'installed-by.txt')) {
                Remove-Item $hak -Recurse -Force   # r391: installed by this installer: all of EFI\HAK
            } elseif (Test-Path (Join-Path $hak 'HAKBOOT.EFI')) {
                foreach ($n in 'addon.txt') { if (Test-Path (Join-Path $hak $n)) { Remove-Item (Join-Path $hak $n) -Force } }
            } elseif (Test-Path (Join-Path $hak 'addon.txt')) {
                Remove-Item $hak -Recurse -Force
            }
            Say 'Pliki dodatku usunięte z partycji EFI.' 'Add-on files removed from the EFI partition.'
        } finally { Dismount-Esp $root }
    }
    return
}

# ---- r391: what to install ----------------------------------------------------------
try { $secureNow = Confirm-SecureBootUEFI } catch { $secureNow = $false }
if (-not $Mode) {
    if ($Yes) { $Mode = 'Reader' } else {
        Say 'Co zainstalować?' 'What to install?'
        Say '  1. Czytnik ekranu od włączenia komputera (czyta też menu i ustawienia firmware).' '  1. Screen reader from power-on (reads firmware menus and setup too).'
        Say '  2. Czytnik ekranu przez shim, bez menu Charona (Secure Boot z kluczem MOK; menu firmware nie są czytane).' '  2. Screen reader through shim, without Charon''s menu (Secure Boot with a MOK key; firmware menus are not read).'
        Say '  3. Menedżer rozruchu Charon z czytnikiem ekranu, do tego czytnik od włączenia komputera (poza Charonem).' '  3. Charon boot manager with the screen reader, plus the reader from power-on (outside Charon).'
        $answer = Read-Host $(if ($Pl) { 'Wybierz 1, 2 albo 3 [1]' } else { 'Choose 1, 2 or 3 [1]' })
        switch ($answer) { '' { $Mode = 'Reader' } '1' { $Mode = 'Reader' } '2' { $Mode = 'Shim' } '3' { $Mode = 'Manager' } default { Fail 'Nic nie zmieniono.' 'Nothing was changed.' } }
        if ($Mode -eq 'Manager' -and -not $Default) {
            $answer = Read-Host $(if ($Pl) { 'Ustawić Charona jako pierwszego w kolejności rozruchu (domyślny)? [T/n]' } else { 'Make Charon the first boot entry (default)? [Y/n]' })
            $Default = $answer -notmatch '^[nN]'
        }
        if ($Mode -eq 'Manager' -and -not $NoReader) {
            $answer = Read-Host $(if ($Pl) { 'Zainstalować też czytnik ekranu od włączenia komputera, poza Charonem (menu i ustawienia firmware, F12)? [T/n]' } else { 'Also install the screen reader from power-on, outside Charon (firmware menus and setup, F12)? [Y/n]' })
            $NoReader = $answer -match '^[nN]'
        }
    }
}
if ($Mode -eq 'Shim') { $Default = $true }
$useShim = $Mode -eq 'Shim' -or ($Mode -eq 'Manager' -and $secureNow)
# r399: the boot manager comes with the reader outside Charon (Driver####) by default.
$withReader = $Mode -eq 'Reader' -or ($Mode -eq 'Manager' -and -not $NoReader)
if ($Mode -ne 'Reader') {
    if (-not $ShimDir) { $ShimDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
    if (-not (Test-Path (Join-Path $Source 'HAKBOOT.EFI'))) { Fail "Brak HAKBOOT.EFI w $Source." "Missing HAKBOOT.EFI in $Source." }
    if ($useShim -and -not ((Test-Path (Join-Path $ShimDir 'shimx64.efi')) -and (Test-Path (Join-Path $ShimDir 'mmx64.efi')))) {
        Fail "Brak shimx64.efi i mmx64.efi w $ShimDir (podpisane przez Microsoft, np. z nośnika Debiana: EFI\boot\bootx64.efi zapisany jako shimx64.efi oraz mmx64.efi). Nic nie zmieniono." "Missing shimx64.efi and mmx64.efi in $ShimDir. Nothing was changed."
    }
}
# The current first boot entry (not Charon) becomes Charon's Continue target.
function Get-BootOrder { $b = Get-Var 'BootOrder'; $ids = @(); if ($b) { for ($i = 0; $i + 1 -lt $b.Length; $i += 2) { $ids += [BitConverter]::ToUInt16($b, $i) } }; return , $ids }
function BootName([int]$Id) { return 'Boot{0:X4}' -f $Id }
function Move-FirstEntry {
    if (-not $FirstEntry -or $DryRun) { return }
    $ids = Get-BootOrder; $want = $null
    foreach ($id in $ids) { $o = Get-Var (BootName $id); if ($o -and (Option-Description $o) -eq $FirstEntry) { $want = $id; break } }
    if ($null -eq $want) { Say ("Nie ma wpisu rozruchu {0}; kolejność bez zmian." -f $FirstEntry) ("There is no boot entry {0}; boot order unchanged." -f $FirstEntry); return }
    $newOrder = @([uint16]$want) + @($ids | Where-Object { $_ -ne $want })
    $b = @(); foreach ($id in $newOrder) { $b += [BitConverter]::GetBytes([uint16]$id) }; Set-Var 'BootOrder' ([byte[]]$b)
    Say ("{0} jest znowu pierwszy w kolejności rozruchu ({1})." -f $FirstEntry, (BootName $want)) ("{0} is first in the boot order again ({1})." -f $FirstEntry, (BootName $want))
}
function Option-Description([byte[]]$o) { $end = 6; while ($end + 1 -lt $o.Length -and -not ($o[$end] -eq 0 -and $o[$end + 1] -eq 0)) { $end += 2 }; return [Text.Encoding]::Unicode.GetString($o, 6, $end - 6) }
$continueId = $null
if ($Mode -ne 'Reader') {
    foreach ($id in (Get-BootOrder)) { $o = Get-Var (BootName $id); if ($o -and (Option-Description $o) -notin @('Charon', 'Charon czytnik')) { $continueId = $id; break } }
}
if (-not $Yes) {
    Say '' ''
    switch ($Mode) {
        'Reader' { Say '- czytnik ekranu jako sterownik firmware (Driver####); kolejność rozruchu bez zmian' '- screen reader as a firmware driver (Driver####); boot order unchanged' }
        'Shim' { Say '- czytnik ekranu przez shim jako pierwszy wpis rozruchu' '- screen reader through shim as the first boot entry' }
        'Manager' { Say ("- menedżer rozruchu Charon{0}{1}" -f $(if ($useShim) { ' przez shim' } else { '' }), $(if ($Default) { ', pierwszy w kolejności rozruchu' } else { '' })) ("- Charon boot manager{0}{1}" -f $(if ($useShim) { ' through shim' } else { '' }), $(if ($Default) { ', first boot entry' } else { '' })) }
    }
    if ($Mode -eq 'Manager' -and $withReader) { Say '- czytnik ekranu od włączenia komputera, poza Charonem (Driver####)' '- screen reader from power-on, outside Charon (Driver####)' }
    if ($Mode -eq 'Manager' -and $withReader -and $secureNow) { Say '- Secure Boot: czytnik od włączenia komputera ruszy dopiero po dopisaniu certyfikatu do listy db (MOK go nie obejmuje)' '- Secure Boot: the reader from power-on starts only after its certificate is added to db (MOK does not cover it)' }
    if ($null -ne $continueId) { $d = Option-Description (Get-Var (BootName $continueId)); Say ("- Kontynuuj w Charonie: {0} ({1})" -f (BootName $continueId), $d) ("- Charon's Continue: {0} ({1})" -f (BootName $continueId), $d) }
    if ($useShim -and $secureNow) { Say '- przy następnym starcie trzeba potwierdzić klucz Charona w MokManagerze (Enroll key from disk)' '- at the next start, confirm Charon''s key in MokManager (Enroll key from disk)' }
    $answer = Read-Host $(if ($Pl) { 'Aby kontynuować, wpisz TAK' } else { 'Type YES to continue' })
    if ($answer -notin @('TAK', 'tak', 'YES', 'yes')) { Fail 'Nic nie zmieniono.' 'Nothing was changed.' }
}

# ---- install ------------------------------------------------------------------
foreach ($n in 'CHARONVOICE.EFI', 'AudioDxe.efi') { if (-not (Test-Path (Join-Path $Source $n))) { Fail "Brak pliku $n w $Source." "Missing $n in $Source." } }
try { $secure = Confirm-SecureBootUEFI } catch { $secure = $false }
$Sign = $Sign -or $secure
if ($secure) { Say 'Secure Boot jest włączony: sterownik zostanie podpisany certyfikatem tego komputera.' 'Secure Boot is on: the driver will be signed with this computer''s certificate.' }
$bitlocker = $null
try { $bitlocker = Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop } catch { }
if ($bitlocker -and $bitlocker.ProtectionStatus -eq 'On') {
    if (-not $SuspendBitLocker) { Fail 'BitLocker jest włączony. Nowy sterownik firmware zmienia pomiar rozruchu i Windows może zażądać klucza odzyskiwania. Uruchom instalator z -SuspendBitLocker (zawiesza ochronę na jeden restart) albo przygotuj klucz odzyskiwania. Nic nie zmieniono.' 'BitLocker is on. A new firmware driver changes the measured boot and Windows may ask for the recovery key. Run the installer with -SuspendBitLocker (suspends protection for one restart) or have the recovery key ready. Nothing was changed.' }
    Suspend-BitLocker -MountPoint $env:SystemDrive -RebootCount 1 | Out-Null
    Say 'BitLocker zawieszony na jeden restart.' 'BitLocker suspended for one restart.'
}

$files = @('CHARONVOICE.EFI', 'AudioDxe.efi')
if ($Mode -ne 'Reader') { $files += 'HAKBOOT.EFI'; if (Test-Path (Join-Path $Source 'CharonShell.efi')) { $files += 'CharonShell.efi' } }
$cues = @(Get-ChildItem (Join-Path $Source 'audio') -Filter *.pcm -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch '^(background|start)-' })
# r399: Memtest86+ shipped with Charon's boot manager, offered in its device list (Manager/Shim modes only).
$memtest = Join-Path $Source 'tools\memtest.efi'
$haveMemtest = ($Mode -ne 'Reader') -and (Test-Path $memtest)
$needed = ($files | ForEach-Object { (Get-Item (Join-Path $Source $_)).Length } | Measure-Object -Sum).Sum + ($cues | Measure-Object Length -Sum).Sum + $(if ($haveMemtest) { (Get-Item $memtest).Length } else { 0 })
$staged = $Source
if ($Sign) {
    $certificate = Get-CharonSigningCertificate -Create
    $staged = Join-Path $env:TEMP ('charon-addon-' + [guid]::NewGuid().ToString('n'))
    New-Item -ItemType Directory $staged | Out-Null
    foreach ($n in $files) { Copy-Item (Join-Path $Source $n) (Join-Path $staged $n); [void](Invoke-CharonEfiSign (Join-Path $staged $n) $certificate) }
    [IO.File]::WriteAllBytes((Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'charon-db.cer'), $certificate.RawData)
}
$root = Mount-Esp
try {
    $free = (Get-Volume -Partition $esp).SizeRemaining
    if ($free -lt $needed + 1MB) { Fail ("Za mało miejsca na partycji EFI: wolne {0:N1} MB, potrzeba {1:N1} MB. Nic nie zmieniono." -f ($free / 1MB), ($needed / 1MB)) ("Not enough space on the EFI partition: {0:N1} MB free, {1:N1} MB needed. Nothing was changed." -f ($free / 1MB), ($needed / 1MB)) }
    $hak = Join-Path $root 'EFI\HAK'
    New-Item -ItemType Directory -Force (Join-Path $hak 'audio') | Out-Null
    foreach ($n in $files) {
        $target = Join-Path $hak $n
        Copy-Item (Join-Path $staged $n) "$target.new" -Force
        if ((Get-FileHash "$target.new").Hash -ne (Get-FileHash (Join-Path $staged $n)).Hash) { Fail "Kopia $n jest uszkodzona." "The copy of $n is damaged." }
        Move-Item "$target.new" $target -Force
    }
    foreach ($c in $cues) { Copy-Item $c.FullName (Join-Path $hak "audio\$($c.Name)") -Force }
    if ($haveMemtest) {
        New-Item -ItemType Directory -Force (Join-Path $hak 'tools') | Out-Null
        Copy-Item $memtest (Join-Path $hak 'tools\memtest.efi') -Force
    }
    # r400: optional parts travel with the boot manager when the package has them:
    # network drivers, packages (CharonDOS), the installers themselves.
    if ($Mode -ne 'Reader') {
        foreach ($d in 'drivers', 'packages', 'addon') {
            $from = Join-Path $Source $d
            if (Test-Path $from) { Copy-Item $from $hak -Recurse -Force }
        }
    }
    # Language of the reader until settings are changed in Charon itself: English unless -Language pl (r399).
    if (-not (Test-Path (Join-Path $hak 'audio-language'))) { [IO.File]::WriteAllText((Join-Path $hak 'audio-language'), $(if ($Language -eq 'pl') { 'pl' } else { 'en' })) }
    if ($Sign) { [IO.File]::WriteAllBytes((Join-Path $hak 'charon-db.cer'), $certificate.RawData) }
    [IO.File]::WriteAllText((Join-Path $hak 'addon.txt'), "Charon UEFI screen reader add-on r385; installed by Install-CharonAddon.ps1`r`n")
    if ($Mode -ne 'Reader') {
        [IO.File]::WriteAllText((Join-Path $hak 'installed-by.txt'), "Charon r391; installed by Install-Charon.ps1, mode $Mode`r`n")
        if ($Mode -eq 'Shim') { [IO.File]::WriteAllText((Join-Path $hak 'reader-only'), "reader-only`r`n") } elseif (Test-Path (Join-Path $hak 'reader-only')) { Remove-Item (Join-Path $hak 'reader-only') -Force }
        if ($useShim) {
            Copy-Item (Join-Path $ShimDir 'shimx64.efi') (Join-Path $hak 'shimx64.efi') -Force
            Copy-Item (Join-Path $ShimDir 'mmx64.efi') (Join-Path $hak 'mmx64.efi') -Force
            Copy-Item (Join-Path $staged 'HAKBOOT.EFI') (Join-Path $hak 'grubx64.efi') -Force   # shim starts grubx64.efi from its own folder
            if ($Sign) { [IO.File]::WriteAllBytes((Join-Path $hak 'charon-mok.cer'), $certificate.RawData) }
        }
        # Continue target: HAKCONT1, id, size, the Boot#### option, HEND (BootMenu.h SaveContinue).
        if ($null -ne $continueId -and -not (Test-Path (Join-Path $hak 'continue-set'))) {
            $o = Get-Var (BootName $continueId)
            $record = [byte[]]([Text.Encoding]::ASCII.GetBytes('HAKCONT1') + [BitConverter]::GetBytes([uint16]$continueId) + [BitConverter]::GetBytes([uint32]$o.Length) + $o + [Text.Encoding]::ASCII.GetBytes('HEND'))
            [IO.File]::WriteAllBytes((Join-Path $hak 'continue-set'), [byte[]](1))
            [IO.File]::WriteAllBytes((Join-Path $hak 'continue.bin'), $record)
        }
    }
} finally { Dismount-Esp $root; if ($Sign -and $staged -ne $Source) { Remove-Item $staged -Recurse -Force -ErrorAction SilentlyContinue } }

if ($Mode -ne 'Reader') {
    $script:DriverFile = if ($useShim) { '\EFI\HAK\shimx64.efi' } else { '\EFI\HAK\HAKBOOT.EFI' }
    $script:Description = if ($Mode -eq 'Shim') { 'Charon czytnik' } else { 'Charon' }
    $bootOption = New-DriverOption $esp.PartitionNumber ([uint64]($esp.Offset / $sector)) ([uint64]($esp.Size / $sector)) ([guid]$esp.Guid)
    [BitConverter]::GetBytes([uint32]1).CopyTo($bootOption, 0)       # LOAD_OPTION_ACTIVE (no FORCE_RECONNECT for a boot entry)
    $orderNow = Get-BootOrder; $bootId = $null
    foreach ($id in $orderNow) { $o = Get-Var (BootName $id); if ($o -and (Option-Description $o) -eq $Description) { $bootId = $id } }
    if ($null -eq $bootId) { for ($c = 0; $c -lt 0x1000; $c++) { if ($null -eq (Get-Var (BootName $c))) { $bootId = $c; break } } }
    Set-Var (BootName $bootId) $bootOption
    $rest = @($orderNow | Where-Object { $_ -ne $bootId })
    $newOrder = if ($Default) { @([uint16]$bootId) + $rest } else { $rest + @([uint16]$bootId) }
    $b = @(); foreach ($id in $newOrder) { $b += [BitConverter]::GetBytes([uint16]$id) }; Set-Var 'BootOrder' ([byte[]]$b)
    Say ("Zainstalowano {0}: {1} ładuje {2}{3}." -f $Description, (BootName $bootId), $DriverFile, $(if ($Default) { ', pierwszy w kolejności rozruchu' } else { '' })) ("Installed {0}: {1} loads {2}{3}." -f $Description, (BootName $bootId), $DriverFile, $(if ($Default) { ', first boot entry' } else { '' }))
    if ($useShim -and $Sign) {
        Say 'Przy następnym starcie shim pokaże MokManagera (niebieski ekran, bez mowy): 1. naciśnij klawisz w ciągu 10 sekund; 2. wybierz Enroll key from disk; 3. wybierz partycję EFI, potem EFI, HAK, charon-mok.cer; 4. Continue, potem Yes; 5. Reboot. Kolejność pozycji bywa różna w wersjach shim (niesprawdzone na komputerze).' 'At the next start shim opens MokManager (blue screen, no speech): 1. press a key within 10 s; 2. Enroll key from disk; 3. the EFI partition, EFI, HAK, charon-mok.cer; 4. Continue, then Yes; 5. Reboot. Item order differs between shim versions (not tried on a computer).'
    }
    if (-not $withReader) { Move-FirstEntry; return }
    # r399: and the reader outside Charon, as in -Mode Reader.
    $script:DriverFile = '\EFI\HAK\CHARONVOICE.EFI'; $script:Description = 'Charon voice'
}
$option = New-DriverOption $esp.PartitionNumber ([uint64]($esp.Offset / $sector)) ([uint64]($esp.Size / $sector)) ([guid]$esp.Guid)
$id = $existing
if ($null -eq $id) {
    for ($c = 0; $c -lt 0x1000; $c++) { if ($null -eq (Get-Var (VarName $c))) { $id = $c; break } }
    if ($null -eq $id) { Fail 'Brak wolnego numeru Driver####.' 'No free Driver#### number.' }
}
Set-Var (VarName $id) $option
$order = Get-Order
if ($order -notcontains [uint16]$id) {
    try { Set-Order ([uint16[]](@([uint16]$id) + $order)) }
    catch { if ($null -eq $existing) { Set-Var (VarName $id) $null }; throw }
}
$check = Get-Var (VarName $id)
if (-not $check -or -not [Linq.Enumerable]::SequenceEqual([byte[]]$check, [byte[]]$option) -or (Get-Order) -notcontains [uint16]$id) {
    Fail 'Odczyt kontrolny wpisu sterownika nie zgadza się z zapisem.' 'The driver entry read back does not match what was written.'
}
if ($Mode -eq 'Manager') {
    Say ("Zainstalowano też czytnik od włączenia komputera: {0} ładuje {1}. Mówi i czyta menu oraz ustawienia firmware zanim wystartuje Charon; w Charonie można go wyłączyć opcją Mowa od włączenia komputera." -f (VarName $id), $DriverFile) `
        ("Also installed the reader from power-on: {0} loads {1}. It speaks and reads firmware menus and setup before Charon starts; Charon's Speech from power-on setting turns it off." -f (VarName $id), $DriverFile)
} else {
Say ("Zainstalowano: {0} ładuje {1} z partycji EFI (dysk {2}, partycja {3}). Kolejność rozruchu i Windows Boot Manager bez zmian. Od następnego włączenia komputera Charon mówi i czyta ekrany do startu Windows." -f (VarName $id), $DriverFile, $esp.DiskNumber, $esp.PartitionNumber) `
    ("Installed: {0} loads {1} from the EFI partition (disk {2}, partition {3}). Boot order and Windows Boot Manager unchanged. From the next power-on Charon speaks and reads the screens until Windows starts." -f (VarName $id), $DriverFile, $esp.DiskNumber, $esp.PartitionNumber)
}
if ($Sign) {
    Say ("Secure Boot: dopisz certyfikat do listy db w ustawieniach firmware (np. Secure Boot > Key Management > db > Append / Enroll from file). Plik: \EFI\HAK\charon-db.cer na partycji EFI albo charon-db.cer obok instalatora. Odcisk SHA-1: {0}. Klucze Microsoftu zostaw. Do tego czasu firmware pomija sterownik i komputer startuje bez mowy." -f $certificate.Thumbprint) `
        ("Secure Boot: add the certificate to the db list in firmware setup (e.g. Secure Boot > Key Management > db > Append / Enroll from file). File: \EFI\HAK\charon-db.cer on the EFI partition or charon-db.cer next to the installer. SHA-1 thumbprint: {0}. Keep Microsoft's keys. Until then firmware skips the driver and the computer starts without speech." -f $certificate.Thumbprint)
}
Move-FirstEntry
