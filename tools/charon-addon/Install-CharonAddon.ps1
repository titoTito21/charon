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
# r408: what the installer said and why it stopped also goes to %ProgramData%\Charon\install-charon.log, in
# English: at the first sign-in after an unattended installation nobody reads this window. -DryRun and
# -Status write nothing.
$LogPath = Join-Path (Join-Path $env:ProgramData 'Charon') 'install-charon.log'
function Write-InstallLog([string]$Text) {
    if ($DryRun -or $Status -or -not $Text) { return }
    try {
        $dir = Split-Path -Parent $LogPath
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Add-Content -LiteralPath $LogPath -Value ('{0:yyyy-MM-dd HH:mm:ss} {1}' -f (Get-Date), $Text) -Encoding UTF8
    } catch { }
}
trap { Write-InstallLog ('ERROR: ' + $_.Exception.Message); break }
function Say([string]$textPl, [string]$textEn) { if ($Pl) { Write-Host $textPl } else { Write-Host $textEn }; Write-InstallLog $textEn }   # r402: a parameter named $pl hid the $Pl flag (names are case-insensitive): everything was said in Polish
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
[DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
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
    Enable-FirmwarePrivilege   # r409: before every firmware call (Confirm-SecureBootUEFI switches the privilege off again; see Install-Charon.ps1)
    $e = 0
    foreach ($capacity in 4096, 1048576) {                 # r408: a load option with much optional data is larger than 4 KiB
        $buffer = New-Object byte[] $capacity; $attr = 0
        $n = [CharonAddon.Native]::GetFirmwareEnvironmentVariableExW($Name, $GlobalGuid, $buffer, [uint32]$capacity, [ref]$attr)
        if ($n -ne 0) { return [byte[]]$buffer[0..($n - 1)] }
        $e = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($e -eq 203) { return $null }                   # ERROR_ENVVAR_NOT_FOUND
        if ($e -ne 122) { break }                          # ERROR_INSUFFICIENT_BUFFER: once more, with the larger buffer
    }
    Fail "Odczyt zmiennej $Name nie powiódł się (błąd $e)." "Reading variable $Name failed (error $e)."
}
function Set-Var([string]$Name, [byte[]]$Data) {
    Enable-FirmwarePrivilege   # r409
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
if (-not $esp) {
    # r408: never the EFI partition of some other disk (the installation stick has one too): only the one on the disk Windows is on.
    $windowsDisk = (Get-Partition -DriveLetter "$env:SystemDrive".Substring(0, 1)).DiskNumber
    $esp = Get-Partition -DiskNumber $windowsDisk | Where-Object { $_.GptType -eq $EspType } | Select-Object -First 1
}
if (-not $esp) { Fail 'Nie znaleziono partycji systemowej EFI (dysk GPT).' 'No EFI system partition found (GPT disk).' }
$disk = Get-Disk -Number $esp.DiskNumber
$sector = [uint64]$disk.LogicalSectorSize
# r408: mounting the EFI partition. A letter it already has is used and left in place. Otherwise a letter that
# is no drive of any kind (GetLogicalDrives: disks, empty card readers and CD drives, SUBST and connected
# network drives; Test-Path alone took an empty CD drive or a disconnected network drive for free) and is not
# remembered for a network drive of this user. If Windows refuses the letter, the next one is tried, and for
# the partition this Windows started from also "mountvol X: /S" (documented: mounts the EFI system partition).
$script:EspMounted = ''
function Mount-Esp {
    $have = "$($esp.DriveLetter)".Trim([char]0, ' ')
    if ($have) { $script:EspMounted = ''; return "$($have):" }
    $used = @([Environment]::GetLogicalDrives() | ForEach-Object { $_.Substring(0, 1).ToUpperInvariant() })
    try { $used += @(Get-ChildItem 'HKCU:\Network' -ErrorAction Stop | ForEach-Object { $_.PSChildName.ToUpperInvariant() }) } catch { }
    $why = ''
    foreach ($code in 90..68) {
        $letter = [string][char]$code
        if ($used -contains $letter) { continue }
        try {
            Add-PartitionAccessPath -DiskNumber $esp.DiskNumber -PartitionNumber $esp.PartitionNumber -AccessPath "$($letter):\" -ErrorAction Stop
            $script:EspMounted = 'partition'; return "$($letter):"
        } catch { $why = $_.Exception.Message }
        if ($esp.IsSystem) {
            & (Join-Path $env:SystemRoot 'System32\mountvol.exe') "$($letter):" /S | Out-Null
            if ($LASTEXITCODE -eq 0 -and (Test-Path "$($letter):\")) { $script:EspMounted = 'mountvol'; return "$($letter):" }
        }
    }
    Fail "Nie udało się zamontować partycji EFI pod żadną wolną literą dysku ($why)." "The EFI partition could not be mounted on any free drive letter ($why)."
}
function Dismount-Esp([string]$Root) {
    if ($script:EspMounted -eq 'partition') { Remove-PartitionAccessPath -DiskNumber $esp.DiskNumber -PartitionNumber $esp.PartitionNumber -AccessPath "$Root\" -ErrorAction SilentlyContinue }
    elseif ($script:EspMounted -eq 'mountvol') { & (Join-Path $env:SystemRoot 'System32\mountvol.exe') $Root /D | Out-Null }
    $script:EspMounted = ''
}
# r408: where the EFI partition is, as the firmware counts it. UEFI's hard drive device path wants "the entry
# in the partition table, starting with entry 1", the first LBA and the size in logical blocks (512 or 4096
# bytes), and edk2 matches a short path by signature AND partition number. Windows' partition number counts
# the used entries only, which is another number when the GPT entry array has gaps (tables made by other
# systems), so the entry is read from the disk itself: LBA 1 is the GPT header, entries follow at the LBA it
# names. If the disk cannot be read, Windows' own numbers are used, as before.
function Read-GptEntry([IO.Stream]$Stream, [uint32]$SectorSize, [guid]$Unique) {
    $read = {
        param([uint64]$lba, [int]$sectors)
        $bytes = New-Object byte[] ([int]($sectors * $SectorSize))
        [void]$Stream.Seek([int64]($lba * $SectorSize), [IO.SeekOrigin]::Begin)
        $got = 0
        while ($got -lt $bytes.Length) { $n = $Stream.Read($bytes, $got, $bytes.Length - $got); if ($n -le 0) { throw 'short read' }; $got += $n }
        return , $bytes
    }
    $header = & $read 1 1
    if ([Text.Encoding]::ASCII.GetString($header, 0, 8) -ne 'EFI PART') { return $null }
    $entriesLba = [BitConverter]::ToUInt64($header, 72); $count = [int][BitConverter]::ToUInt32($header, 80); $entrySize = [int][BitConverter]::ToUInt32($header, 84)
    if ($entrySize -lt 128 -or $entrySize -gt 4096 -or $count -lt 1 -or $count -gt 4096) { return $null }
    $table = & $read $entriesLba ([int][Math]::Ceiling(($count * $entrySize) / [double]$SectorSize))
    $want = $Unique.ToByteArray()
    for ($i = 0; $i -lt $count; $i++) {
        $at = $i * $entrySize; $same = $true
        for ($k = 0; $k -lt 16 -and $same; $k++) { if ($table[$at + 16 + $k] -ne $want[$k]) { $same = $false } }
        if ($same) {
            $first = [BitConverter]::ToUInt64($table, $at + 32); $last = [BitConverter]::ToUInt64($table, $at + 40)
            if ($last -lt $first) { return $null }
            return [pscustomobject]@{ Number = [uint32]($i + 1); Start = [uint64]$first; Size = [uint64]($last - $first + [uint64]1) }
        }
    }
    return $null
}
function Get-EspLocation {
    $location = [pscustomobject]@{ Number = [uint32]$esp.PartitionNumber; Start = [uint64]($esp.Offset / $sector); Size = [uint64]($esp.Size / $sector) }
    $entry = $null
    try {
        $handle = [CharonAddon.Native]::CreateFileW("\\.\PhysicalDrive$($esp.DiskNumber)", [uint32]2147483648, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)   # GENERIC_READ, shared, OPEN_EXISTING
        if (-not $handle.IsInvalid) {
            $stream = New-Object IO.FileStream($handle, [IO.FileAccess]::Read, 1, $false)
            try { $entry = Read-GptEntry $stream ([uint32]$sector) ([guid]$esp.Guid) } finally { $stream.Dispose() }
        }
    } catch { Write-InstallLog ('GPT not read from the disk: ' + $_.Exception.Message) }
    if (-not $entry) { return $location }
    if ($entry.Number -ne $location.Number -or $entry.Start -ne $location.Start -or $entry.Size -ne $location.Size) {
        Say ("Partycja EFI w tablicy GPT: wpis {0}, pierwszy blok {1}, {2} bloków (Windows podaje numer {3}, blok {4}, {5} bloków). Do firmware trafiają wartości z dysku." -f $entry.Number, $entry.Start, $entry.Size, $location.Number, $location.Start, $location.Size) `
            ("The EFI partition in the GPT: entry {0}, first block {1}, {2} blocks (Windows reports number {3}, block {4}, {5} blocks). The firmware gets the values from the disk." -f $entry.Number, $entry.Start, $entry.Size, $location.Number, $location.Start, $location.Size)
    }
    return $entry
}

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
# r408: protection is read with the BitLocker cmdlets and, where they are missing or fail, from the WMI class
# behind manage-bde (Win32_EncryptableVolume, ProtectionStatus 1 = on). "Cannot tell" is said, not taken for "off".
$protected = $null
try { $protected = ("$((Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop).ProtectionStatus)" -eq 'On') } catch { }
if ($null -eq $protected) {
    try {
        $encryptable = Get-CimInstance -Namespace 'root\CIMV2\Security\MicrosoftVolumeEncryption' -ClassName Win32_EncryptableVolume -Filter ("DriveLetter='{0}'" -f $env:SystemDrive) -ErrorAction Stop
        $protected = [bool]($encryptable -and ([int]$encryptable.ProtectionStatus -eq 1))
    } catch { }
}
if ($null -eq $protected) { Say 'Nie udało się odczytać stanu BitLockera. Jeśli dysk jest zaszyfrowany, przygotuj klucz odzyskiwania przed następnym uruchomieniem komputera.' 'The BitLocker state could not be read. If the drive is encrypted, have the recovery key ready before the next start of the computer.' }
if ($protected) {
    if (-not $SuspendBitLocker) { Fail 'BitLocker jest włączony. Nowy sterownik firmware zmienia pomiar rozruchu i Windows może zażądać klucza odzyskiwania. Uruchom instalator z -SuspendBitLocker (zawiesza ochronę na jeden restart) albo przygotuj klucz odzyskiwania. Nic nie zmieniono.' 'BitLocker is on. A new firmware driver changes the measured boot and Windows may ask for the recovery key. Run the installer with -SuspendBitLocker (suspends protection for one restart) or have the recovery key ready. Nothing was changed.' }
    try { Suspend-BitLocker -MountPoint $env:SystemDrive -RebootCount 1 -ErrorAction Stop | Out-Null }
    catch {
        # Without the cmdlet: the same through manage-bde (-protectors -disable <drive> -RebootCount n).
        & (Join-Path $env:SystemRoot 'System32\manage-bde.exe') -protectors -disable $env:SystemDrive -RebootCount 1 | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail 'Nie udało się zawiesić BitLockera. Nic nie zmieniono.' 'BitLocker could not be suspended. Nothing was changed.' }
    }
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
    # r408: the copy next to the installer is a convenience (the certificate is also saved on the EFI partition, below):
    # read-only installation media must not stop the installation.
    try { [IO.File]::WriteAllBytes((Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'charon-db.cer'), $certificate.RawData) }
    catch { Write-InstallLog ('charon-db.cer not written next to the installer: ' + $_.Exception.Message) }
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
    [IO.File]::WriteAllText((Join-Path $hak 'addon.txt'), "Charon UEFI screen reader add-on r410; installed by Install-CharonAddon.ps1`r`n")
} finally { Dismount-Esp $root; if ($Sign -and $staged -ne $Source) { Remove-Item $staged -Recurse -Force -ErrorAction SilentlyContinue } }

$espAt = Get-EspLocation
$option = New-DriverOption $espAt.Number $espAt.Start $espAt.Size ([guid]$esp.Guid)
$id = $existing
if ($null -eq $id) {
    for ($c = 0; $c -lt 0x1000; $c++) { if ($null -eq (Get-Var (VarName $c))) { $id = $c; break } }
    if ($null -eq $id) { Fail 'Brak wolnego numeru Driver####.' 'No free Driver#### number.' }
}
$current = Get-Var (VarName $id)
if (-not $current -or -not [Linq.Enumerable]::SequenceEqual([byte[]]$current, [byte[]]$option)) { Set-Var (VarName $id) $option }   # r408: an entry that is already right is not written again
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
