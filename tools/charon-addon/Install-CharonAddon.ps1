<#
Charon UEFI screen reader add-on (r385). GPL-2.0-or-later.

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
    [string]$PartitionGuid
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
            if (Test-Path (Join-Path $hak 'HAKBOOT.EFI')) {
                foreach ($n in 'addon.txt') { if (Test-Path (Join-Path $hak $n)) { Remove-Item (Join-Path $hak $n) -Force } }
            } elseif (Test-Path (Join-Path $hak 'addon.txt')) {
                Remove-Item $hak -Recurse -Force
            }
            Say 'Pliki dodatku usunięte z partycji EFI.' 'Add-on files removed from the EFI partition.'
        } finally { Dismount-Esp $root }
    }
    return
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
$cues = @(Get-ChildItem (Join-Path $Source 'audio') -Filter *.pcm -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch '^(background|start)-' })
$needed = ($files | ForEach-Object { (Get-Item (Join-Path $Source $_)).Length } | Measure-Object -Sum).Sum + ($cues | Measure-Object Length -Sum).Sum
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
    # Language of the reader until settings are changed in Charon itself: English unless -Language pl (r399).
    if (-not (Test-Path (Join-Path $hak 'audio-language'))) { [IO.File]::WriteAllText((Join-Path $hak 'audio-language'), $(if ($Language -eq 'pl') { 'pl' } else { 'en' })) }
    if ($Sign) { [IO.File]::WriteAllBytes((Join-Path $hak 'charon-db.cer'), $certificate.RawData) }
    [IO.File]::WriteAllText((Join-Path $hak 'addon.txt'), "Charon UEFI screen reader add-on r385; installed by Install-CharonAddon.ps1`r`n")
} finally { Dismount-Esp $root; if ($Sign -and $staged -ne $Source) { Remove-Item $staged -Recurse -Force -ErrorAction SilentlyContinue } }

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
Say ("Zainstalowano: {0} ładuje {1} z partycji EFI (dysk {2}, partycja {3}). Kolejność rozruchu i Windows Boot Manager bez zmian. Od następnego włączenia komputera Charon mówi i czyta ekrany do startu Windows." -f (VarName $id), $DriverFile, $esp.DiskNumber, $esp.PartitionNumber) `
    ("Installed: {0} loads {1} from the EFI partition (disk {2}, partition {3}). Boot order and Windows Boot Manager unchanged. From the next power-on Charon speaks and reads the screens until Windows starts." -f (VarName $id), $DriverFile, $esp.DiskNumber, $esp.PartitionNumber)
if ($Sign) {
    Say ("Secure Boot: dopisz certyfikat do listy db w ustawieniach firmware (np. Secure Boot > Key Management > db > Append / Enroll from file). Plik: \EFI\HAK\charon-db.cer na partycji EFI albo charon-db.cer obok instalatora. Odcisk SHA-1: {0}. Klucze Microsoftu zostaw. Do tego czasu firmware pomija sterownik i komputer startuje bez mowy." -f $certificate.Thumbprint) `
        ("Secure Boot: add the certificate to the db list in firmware setup (e.g. Secure Boot > Key Management > db > Append / Enroll from file). File: \EFI\HAK\charon-db.cer on the EFI partition or charon-db.cer next to the installer. SHA-1 thumbprint: {0}. Keep Microsoft's keys. Until then firmware skips the driver and the computer starts without speech." -f $certificate.Thumbprint)
}
