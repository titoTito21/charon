<#
Charon: make room for a second system (dual boot). GPL-2.0-or-later.

Run from a running Windows, as administrator, BEFORE installing a second
system with Charon. It shrinks the Windows volume and leaves the freed space
UNALLOCATED (no partition is created: the next installer takes the free
space), and prepares Windows for living beside another system. Progress is
spoken with System.Speech (SAPI) as well as printed.

  Charon-MakeRoom.ps1 [-SizeGB 64] [-Language pl|en] [-Yes] [-DryRun]
      [-ClockUtc] [-NoFastStartup:$false] [-LogPath FILE] [-NoSpeech]

  -SizeGB N        space to free, in GB (default 64).
  -Yes             no questions. Without it the plan is read out and the word
                   TAK / YES has to be typed (as in Install-Charon.ps1).
  -DryRun          read-only queries only; prints the plan. Nothing is changed
                   and no log file is written.
  -ClockUtc        Windows keeps the hardware clock in UTC, as Linux does
                   (RealTimeIsUniversal=1), so both systems show the same time.
  -NoFastStartup   ON by default: turns fast start-up off (HiberbootEnabled=0).
                   With fast start-up Windows only hibernates its kernel at
                   shut-down and leaves the NTFS volume "in use"; a second
                   system then mounts it read-only or damages it. Hibernation
                   itself stays available (powercfg /h off would remove it and
                   hiberfil.sys; this script does not do that). Keep fast
                   start-up with -NoFastStartup:$false.
  -LogPath FILE    default %ProgramData%\Charon\makeroom.log
Exit codes: 0 success, 1 error, 2 refused or not confirmed (nothing changed).

What it does, in this order:
 1. checks administrator rights;
 2. reports the disk of the Windows volume: partition style (GPT is required;
    on an MBR disk it stops, because Charon and a UEFI dual boot need GPT),
    every partition, the Windows volume's size and free space, and the largest
    possible shrink (Get-PartitionSupportedSize);
 3. BitLocker / device encryption (Get-BitLockerVolume, else WMI
    Win32_EncryptableVolume, which is what manage-bde reads):
      - protection ON, or encryption in progress: shrinking is fine, but a
        changed boot order makes Windows ask for the recovery key. With consent
        protection is SUSPENDED for the next 2 restarts
        (Suspend-BitLocker -RebootCount 2). The volume stays encrypted.
      - NOT encrypted: PreventDeviceEncryption=1, so that automatic device
        encryption does not start later (the same value Rufus and Charon's
        answer file set).
      - encrypted but protection off ("waiting for activation"): reported,
        nothing is changed.
 4. Secure Boot (Confirm-SecureBootUEFI): if on, warns that some Linux systems
    and Charon itself need a signed loader (shim) and a MOK enrolment;
 5. fast start-up and, if asked, the UTC clock;
 6. shrinks the Windows partition by -SizeGB (Resize-Partition), verifies and
    reports the new size and the unallocated space.
Only the partition holding the running Windows is ever resized. No other
partition or disk is touched; nothing is created, formatted or deleted.

Decisions and limits (honest list; this script was only parsed and run with
-DryRun, never for real):
- RealTimeIsUniversal is written as REG_DWORD 1. That is what the Arch Linux
  wiki documents for current Windows
  (https://wiki.archlinux.org/title/System_time#UTC_in_Microsoft_Windows,
  read 2026-11-20: "add a DWORD value with hexadecimal value 1"). Old advice to
  use a QWORD on 64-bit Windows is not in that page any more. Windows 10/11
  honour the value; the Windows Time service may still write local time in
  rare cases reported by users. NOT verified here on a computer.
- Windows cannot move some files (page file, hibernation file, shadow copies,
  the MFT mirror), so the largest shrink is often far less than the free space.
  The script refuses a shrink larger than Windows allows and says the largest
  possible value; it does not disable the page file or delete restore points.
- At least 10 GB must stay free in Windows after the shrink.
- While BitLocker is still encrypting, Windows may refuse to shrink; the error
  is reported as it is.
- The suspension covers 2 restarts from now. If the second system is installed
  later, run this script again (or have the recovery key:
  https://aka.ms/myrecoverykey).
- MBR disks and dynamic disks are refused, not converted.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 65536)][int]$SizeGB = 64,
    [ValidateSet('', 'pl', 'en')][string]$Language = '',
    [switch]$DryRun,
    [switch]$Yes,
    [switch]$ClockUtc,
    # A switch that is on unless switched off: -NoFastStartup:$false keeps fast start-up.
    [switch]$NoFastStartup = $true,
    [switch]$NoSpeech,
    [string]$LogPath = ''
)
$ErrorActionPreference = 'Stop'
if (-not $LogPath) { $LogPath = Join-Path (Join-Path $env:ProgramData 'Charon') 'makeroom.log' }
$UsePolish = if ($Language) { $Language -eq 'pl' } else { (Get-UICulture).Name -like 'pl*' }
$MinFreeAfterGB = 10              # free space that must remain inside Windows after the shrink
$EspType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$script:ExitCode = 0
$script:Failures = 0
$script:Synth = $null
$script:SpeakPolish = $false

# ---- output: text, log, speech (the same helpers as Charon-WindowsProfile.ps1) --
function Write-Log([string]$Text) {
    if ($DryRun) { return }       # -DryRun writes nothing, not even a log
    try {
        $dir = Split-Path -Parent $LogPath
        if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Add-Content -LiteralPath $LogPath -Value ('{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Text) -Encoding UTF8
    } catch { }
}
function Note([string]$TextPl, [string]$TextEn) {
    if ($UsePolish) { Write-Host $TextPl } else { Write-Host $TextEn }
    Write-Log $TextEn
}
# Speech problems go to the log; -DryRun (which has no log) prints them.
function Write-SpeechNote([string]$Text) { Write-Log $Text; if ($DryRun) { Write-Host ('[dry-run] {0}' -f $Text) } }
function Initialize-Speech {
    if ($NoSpeech) { return }
    try {
        Add-Type -AssemblyName System.Speech -ErrorAction Stop
        $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
        $voices = @($synth.GetInstalledVoices() | Where-Object { $_.Enabled })
        if ($voices.Count -eq 0) { $synth.Dispose(); Write-SpeechNote 'speech: no SAPI voice installed; continuing without speech'; return }
        # Polish text only with a Polish voice; otherwise the English text is spoken.
        $polish = $voices | Where-Object { $_.VoiceInfo.Culture.Name -like 'pl*' } | Select-Object -First 1
        $english = $voices | Where-Object { $_.VoiceInfo.Culture.Name -like 'en*' } | Select-Object -First 1
        if ($UsePolish -and $polish) { $synth.SelectVoice($polish.VoiceInfo.Name); $script:SpeakPolish = $true }
        elseif ($english) { $synth.SelectVoice($english.VoiceInfo.Name) }
        $synth.SetOutputToDefaultAudioDevice()
        $script:Synth = $synth
    } catch {
        $script:Synth = $null
        Write-SpeechNote ('speech: unavailable ({0}); continuing without speech' -f $_.Exception.Message)
    }
}
function Speak([string]$TextPl, [string]$TextEn) {
    if (-not $script:Synth) { return }
    try { if ($script:SpeakPolish) { $script:Synth.Speak($TextPl) } else { $script:Synth.Speak($TextEn) } }
    catch { Write-SpeechNote ('speech: failed ({0})' -f $_.Exception.Message) }
}
function Announce([string]$TextPl, [string]$TextEn) { Note $TextPl $TextEn; Speak $TextPl $TextEn }
function Fail([string]$TextPl, [string]$TextEn) { if ($UsePolish) { throw $TextPl } else { throw $TextEn } }
function Refuse([string]$TextPl, [string]$TextEn) { $script:ExitCode = 2; Fail $TextPl $TextEn }

# ---- changes go through ONE gate; in -DryRun the action is never invoked ---------
function Invoke-Change([string]$What, [scriptblock]$Action) {
    if ($DryRun) { Write-Host ('[dry-run] would: {0}' -f $What); return $false }
    try { & $Action; Write-Log ('done: {0}' -f $What); return $true }
    catch {
        $script:Failures++
        Write-Log ('FAILED: {0}: {1}' -f $What, $_.Exception.Message)
        Write-Host ('! {0}: {1}' -f $What, $_.Exception.Message)
        return $false
    }
}
function Get-RegValue([string]$Path, [string]$Name) {
    try { return (Get-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction Stop).$Name } catch { return $null }
}
function Set-RegDword([string]$Path, [string]$Name, [int]$Value) {
    $now = Get-RegValue $Path $Name
    if ($null -ne $now -and [int]$now -eq $Value) { Note "Już ustawione: $Name = $Value." "Already set: $Name = $Value."; return }
    $p = $Path; $n = $Name; $v = $Value
    [void](Invoke-Change ('set {0}\{1} = {2} (REG_DWORD)' -f $p, $n, $v) {
        if (-not (Test-Path -LiteralPath $p)) { New-Item -Path $p -Force | Out-Null }
        New-ItemProperty -LiteralPath $p -Name $n -Value $v -PropertyType DWord -Force | Out-Null
    })
}
function Test-Admin {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function GB([double]$Bytes) { return ('{0:N1}' -f ($Bytes / 1GB)) }
function Confirm-Step([string]$TextPl, [string]$TextEn) {
    # Typed confirmation, as in Install-Charon.ps1. -Yes answers yes; -DryRun never asks.
    if ($Yes -or $DryRun) { return $true }
    Announce $TextPl $TextEn
    $answer = Read-Host $(if ($UsePolish) { 'Aby kontynuować, wpisz TAK' } else { 'Type YES to continue' })
    return ($answer -in @('TAK', 'tak', 'YES', 'yes'))
}

# ---- BitLocker state, read-only ---------------------------------------------------
function Get-EncryptionState([string]$MountPoint) {
    # Returns @{ Known; Encrypted; InProgress; Protected; Text; Source }.
    $state = @{ Known = $false; Encrypted = $false; InProgress = $false; Protected = $false; Text = 'unknown'; Source = '' }
    try {
        $volume = Get-BitLockerVolume -MountPoint $MountPoint -ErrorAction Stop
        $status = "$($volume.VolumeStatus)"
        $state.Known = $true; $state.Source = 'Get-BitLockerVolume'
        $state.Encrypted = ($status -ne 'FullyDecrypted')
        $state.InProgress = ($status -like '*InProgress*') -or ($status -like '*Paused*')
        $state.Protected = ("$($volume.ProtectionStatus)" -eq 'On')
        $state.Text = '{0}, protection {1}, {2}% encrypted' -f $status, $volume.ProtectionStatus, $volume.EncryptionPercentage
        return $state
    } catch { }
    try {
        # The WMI class behind manage-bde; numbers, so it does not depend on the display language.
        $volume = Get-CimInstance -Namespace 'root\CIMV2\Security\MicrosoftVolumeEncryption' -ClassName Win32_EncryptableVolume -Filter ("DriveLetter='{0}'" -f $MountPoint) -ErrorAction Stop
        if ($volume) {
            $conversion = [int]$volume.ConversionStatus       # 0 fully decrypted, 1 fully encrypted, 2 encrypting, 3 decrypting, 4/5 paused
            $state.Known = $true; $state.Source = 'Win32_EncryptableVolume'
            $state.Encrypted = ($conversion -ne 0)
            $state.InProgress = ($conversion -ge 2)
            $state.Protected = ([int]$volume.ProtectionStatus -eq 1)
            $state.Text = 'ConversionStatus {0}, ProtectionStatus {1}' -f $conversion, $volume.ProtectionStatus
        }
    } catch { }
    return $state
}

# ---- main --------------------------------------------------------------------------
function Invoke-Main {
    if ($DryRun) { Write-Host '[dry-run] Read-only queries only. Nothing is changed; no log file.' }
    Write-Log ('---- Charon-MakeRoom: SizeGB {0}, ClockUtc {1}, NoFastStartup {2}, user {3}' -f $SizeGB, [bool]$ClockUtc, [bool]$NoFastStartup, [Environment]::UserName)
    Announce 'Charon: przygotowanie miejsca na drugi system.' 'Charon: making room for a second system.'

    # 1. administrator rights
    $admin = Test-Admin
    if (-not $admin) {
        if ($DryRun) { Note 'Uwaga: bez uprawnień administratora część odczytów się nie uda (największe zmniejszenie, Secure Boot, BitLocker).' 'Note: without administrator rights some queries fail (largest shrink, Secure Boot, BitLocker).' }
        else { Refuse 'Uruchom jako administrator. Nic nie zmieniono.' 'Run as administrator. Nothing was changed.' }
    }

    # 2. disk layout
    $letter = "$env:SystemDrive".Substring(0, 1)
    $mount = "${letter}:"
    $partition = Get-Partition -DriveLetter $letter
    $disk = Get-Disk -Number $partition.DiskNumber
    $volume = Get-Volume -DriveLetter $letter
    Note ("Dysk {0}: {1}, {2} GB, styl partycji {3}." -f $disk.Number, $disk.FriendlyName, (GB $disk.Size), $disk.PartitionStyle) `
        ("Disk {0}: {1}, {2} GB, partition style {3}." -f $disk.Number, $disk.FriendlyName, (GB $disk.Size), $disk.PartitionStyle)
    $hasEsp = $false
    foreach ($p in @(Get-Partition -DiskNumber $disk.Number | Sort-Object Offset)) {
        $kind = "$($p.Type)"
        if ("$($p.GptType)" -eq $EspType) { $hasEsp = $true }
        $drive = ''
        if ($p.DriveLetter -and [int][char]$p.DriveLetter -ne 0) { $drive = " ($($p.DriveLetter):)" }
        Note ("  partycja {0}: {1}, {2} GB{3}" -f $p.PartitionNumber, $kind, (GB $p.Size), $drive) ("  partition {0}: {1}, {2} GB{3}" -f $p.PartitionNumber, $kind, (GB $p.Size), $drive)
    }
    Note ("Wolne, nieprzydzielone miejsce na dysku teraz: największy ciągły obszar {0} GB." -f (GB $disk.LargestFreeExtent)) `
        ("Unallocated space on the disk now: largest free extent {0} GB." -f (GB $disk.LargestFreeExtent))
    $gpt = ("$($disk.PartitionStyle)" -eq 'GPT')
    if (-not $gpt) {
        Announce "Uwaga: dysk z Windows ma styl partycji $($disk.PartitionStyle), nie GPT. Charon i dwa systemy w trybie UEFI wymagają GPT. Ten skrypt nie konwertuje dysku." `
            "Warning: the Windows disk has partition style $($disk.PartitionStyle), not GPT. Charon and a UEFI dual boot need GPT. This script does not convert the disk."
    } elseif (-not $hasEsp) {
        Announce 'Uwaga: na dysku z Windows nie ma partycji systemowej EFI.' 'Warning: the Windows disk has no EFI system partition.'
    }
    $dynamic = $false
    try { $dynamic = [bool]((Get-CimInstance -ClassName Win32_DiskPartition -Filter ("DiskIndex={0}" -f $disk.Number) -ErrorAction Stop | Where-Object { $_.Type -like '*Logical Disk Manager*' })) } catch { }

    $supported = $null
    try { $supported = Get-PartitionSupportedSize -DriveLetter $letter -ErrorAction Stop }
    catch { Note "Nie można odczytać największego zmniejszenia: $($_.Exception.Message)" "Cannot read the largest shrink: $($_.Exception.Message)" }
    $shrinkBytes = [int64]$SizeGB * 1GB
    $newSize = [int64]$partition.Size - $shrinkBytes
    $newSize = $newSize - ($newSize % 1MB)                 # whole megabytes
    $maxShrink = $null
    if ($supported) { $maxShrink = [int64]$partition.Size - [int64]$supported.SizeMin }
    $maxText = '?'; if ($null -ne $maxShrink) { $maxText = GB $maxShrink }
    Announce ("Windows, dysk {0}: rozmiar {1} GB, wolne {2} GB. Największe możliwe zmniejszenie: {3} GB. Chcesz zwolnić {4} GB." -f $mount, (GB $partition.Size), (GB $volume.SizeRemaining), $maxText, $SizeGB) `
        ("Windows, drive {0}: size {1} GB, free {2} GB. Largest possible shrink: {3} GB. You asked to free {4} GB." -f $mount, (GB $partition.Size), (GB $volume.SizeRemaining), $maxText, $SizeGB)

    # 3. BitLocker
    $encryption = Get-EncryptionState $mount
    Note ("BitLocker na {0}: {1}" -f $mount, $encryption.Text) ("BitLocker on {0}: {1} ({2})" -f $mount, $encryption.Text, $encryption.Source)
    $suspend = $false; $prevent = $false
    if (-not $encryption.Known) {
        Announce 'Nie udało się odczytać stanu BitLockera. Jeśli dysk jest zaszyfrowany, przygotuj klucz odzyskiwania przed zmianą kolejności rozruchu.' `
            'The BitLocker state could not be read. If the drive is encrypted, have the recovery key ready before the boot order changes.'
    } elseif ($encryption.Protected -or $encryption.InProgress) {
        $suspend = $true
        Announce 'Dysk z Windows jest zaszyfrowany BitLockerem albo szyfrowanie trwa. Zmniejszenie partycji jest bezpieczne, ale po zmianie kolejności rozruchu Windows może zażądać klucza odzyskiwania. Dlatego ochrona zostanie zawieszona na dwa najbliższe restarty. Dysk pozostaje zaszyfrowany. Klucz odzyskiwania konta Microsoft: aka.ms/myrecoverykey.' `
            'The Windows drive is encrypted with BitLocker, or encryption is in progress. Shrinking the partition is safe, but after the boot order changes Windows may ask for the recovery key. So protection will be suspended for the next two restarts. The drive stays encrypted. Recovery key of a Microsoft account: aka.ms/myrecoverykey.'
    } elseif ($encryption.Encrypted) {
        Announce 'Dysk z Windows jest zaszyfrowany, ale ochrona jest wyłączona (czeka na aktywację, zwykle na konto Microsoft). Niczego tu nie zmieniam. Po włączeniu ochrony zmiana kolejności rozruchu może wymagać klucza odzyskiwania.' `
            'The Windows drive is encrypted but protection is off (waiting for activation, usually for a Microsoft account). Nothing is changed here. Once protection is on, a changed boot order can require the recovery key.'
    } else {
        $prevent = $true
        Note 'Dysk z Windows nie jest zaszyfrowany. Wyłączę automatyczne szyfrowanie urządzenia, żeby nie zaczęło się później.' `
            'The Windows drive is not encrypted. Automatic device encryption will be prevented, so it does not start later.'
    }

    # 4. Secure Boot
    $secure = $null
    try { $secure = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) } catch { Write-Log ('Secure Boot state not readable: {0}' -f $_.Exception.Message) }
    if ($secure -eq $true) {
        Announce 'Secure Boot jest włączony. Niektóre systemy Linux i sam Charon potrzebują podpisanego programu startowego shim i potwierdzenia klucza MOK przy pierwszym starcie. Ten ekran nie mówi.' `
            'Secure Boot is on. Some Linux systems and Charon itself need the signed loader shim and a MOK key enrolment at the first start. That screen does not speak.'
    } elseif ($secure -eq $false) { Note 'Secure Boot jest wyłączony.' 'Secure Boot is off.' }
    else { Note 'Stanu Secure Boot nie udało się odczytać (brak uprawnień albo tryb BIOS).' 'The Secure Boot state could not be read (no rights, or BIOS mode).' }

    # refusals before anything is changed (in -DryRun they are only reported)
    $problems = @()
    if (-not $gpt) { $problems += @{ pl = 'Dysk nie jest GPT.'; en = 'The disk is not GPT.' } }
    if ($dynamic) { $problems += @{ pl = 'Dysk jest dyskiem dynamicznym.'; en = 'The disk is a dynamic disk.' } }
    if (-not $supported) { $problems += @{ pl = 'Windows nie podał największego możliwego zmniejszenia.'; en = 'Windows did not report the largest possible shrink.' } }
    elseif ($newSize -lt [int64]$supported.SizeMin) {
        $problems += @{ pl = "Windows pozwala zmniejszyć partycję najwyżej o $maxText GB (pliki, których nie da się przenieść: plik stronicowania, hibernacji, punkty przywracania). Podaj mniejsze -SizeGB."
                        en = "Windows allows shrinking by at most $maxText GB (files that cannot be moved: page file, hibernation file, restore points). Give a smaller -SizeGB." }
    }
    if (([int64]$volume.SizeRemaining - $shrinkBytes) -lt ([int64]$MinFreeAfterGB * 1GB)) {
        $problems += @{ pl = "Po zmniejszeniu w Windows zostałoby mniej niż $MinFreeAfterGB GB wolnego miejsca."; en = "Less than $MinFreeAfterGB GB would stay free in Windows after the shrink." }
    }
    if ($problems.Count) {
        foreach ($problem in $problems) { Announce $problem.pl $problem.en }
        if (-not $DryRun) { Refuse 'Nic nie zmieniono.' 'Nothing was changed.' }
        Write-Host '[dry-run] The real run would stop here and change nothing.'
    }

    # the plan
    Note 'Plan:' 'Plan:'
    if ($suspend) { Note '  - zawieszenie ochrony BitLocker na 2 restarty' '  - suspend BitLocker protection for 2 restarts' }
    if ($prevent) { Note '  - PreventDeviceEncryption = 1' '  - PreventDeviceEncryption = 1' }
    if ($NoFastStartup) { Note '  - wyłączenie szybkiego uruchamiania (hibernacja zostaje)' '  - turn fast start-up off (hibernation stays)' }
    if ($ClockUtc) { Note '  - zegar sprzętowy w UTC' '  - hardware clock in UTC' }
    Note ("  - zmniejszenie partycji {0} z {1} GB do {2} GB; {3} GB zostaje nieprzydzielone" -f $mount, (GB $partition.Size), (GB $newSize), $SizeGB) `
        ("  - shrink partition {0} from {1} GB to {2} GB; {3} GB stay unallocated" -f $mount, (GB $partition.Size), (GB $newSize), $SizeGB)
    if (-not (Confirm-Step ("Charon zmniejszy partycję Windows o {0} GB. Zrób wcześniej kopię ważnych plików." -f $SizeGB) ("Charon will shrink the Windows partition by {0} GB. Back up important files first." -f $SizeGB))) {
        Refuse 'Nic nie zmieniono.' 'Nothing was changed.'
    }

    # 3 (action). BitLocker. Consent: -Yes, or the typed confirmation above.
    if ($suspend) {
        $done = Invoke-Change ('suspend BitLocker protection on {0} for 2 restarts' -f $mount) {
            try { Suspend-BitLocker -MountPoint $mount -RebootCount 2 -ErrorAction Stop | Out-Null }
            catch {
                # Editions without the BitLocker cmdlets: the same through manage-bde.
                & (Join-Path $env:SystemRoot 'System32\manage-bde.exe') -protectors -disable $mount -RebootCount 2 | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "manage-bde exit code $LASTEXITCODE" }
            }
        }
        if ($done) { Announce 'Ochrona BitLocker zawieszona na dwa restarty.' 'BitLocker protection suspended for two restarts.' }
        elseif (-not $DryRun) { Announce 'Nie udało się zawiesić BitLockera. Przygotuj klucz odzyskiwania.' 'BitLocker could not be suspended. Have the recovery key ready.' }
    }
    if ($prevent) { Set-RegDword 'HKLM:\SYSTEM\CurrentControlSet\Control\BitLocker' 'PreventDeviceEncryption' 1 }

    # 5. fast start-up, clock
    if ($NoFastStartup) { Set-RegDword 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' 'HiberbootEnabled' 0 }
    if ($ClockUtc) { Set-RegDword 'HKLM:\SYSTEM\CurrentControlSet\Control\TimeZoneInformation' 'RealTimeIsUniversal' 1 }

    # 6. shrink. Only this partition; the freed space is left unallocated.
    if ($DryRun) {
        Write-Host ('[dry-run] would: Resize-Partition -DriveLetter {0} -Size {1}  ({2} GB)' -f $letter, $newSize, (GB $newSize))
        Write-Host '[dry-run] End of the plan. Nothing was changed.'
        return
    }
    Announce 'Zmniejszam partycję Windows. To może potrwać kilka minut.' 'Shrinking the Windows partition. This can take a few minutes.'
    $resized = Invoke-Change ('Resize-Partition -DriveLetter {0} -Size {1}' -f $letter, $newSize) { Resize-Partition -DriveLetter $letter -Size $newSize -ErrorAction Stop }
    $after = Get-Partition -DriveLetter $letter
    $diskAfter = Get-Disk -Number $partition.DiskNumber
    $freed = [int64]$partition.Size - [int64]$after.Size
    Write-Log ('partition size before {0}, after {1}, largest free extent {2}' -f $partition.Size, $after.Size, $diskAfter.LargestFreeExtent)
    if (-not $resized -or $freed -lt ($shrinkBytes - 16MB)) {
        $script:ExitCode = 1
        Announce ("Zmniejszenie się nie udało albo jest niepełne: zwolniono {0} GB z {1}. Partycja Windows ma {2} GB. Szczegóły w dzienniku {3}." -f (GB $freed), $SizeGB, (GB $after.Size), $LogPath) `
            ("The shrink failed or is incomplete: {0} GB of {1} were freed. The Windows partition is {2} GB. Details in the log {3}." -f (GB $freed), $SizeGB, (GB $after.Size), $LogPath)
        return
    }
    Announce ("Gotowe. Partycja Windows ma teraz {0} GB. Nieprzydzielone miejsce na dysku: {1} GB. Nie tworzyłem partycji; instalator drugiego systemu sam zajmie wolne miejsce." -f (GB $after.Size), (GB $diskAfter.LargestFreeExtent)) `
        ("Done. The Windows partition is now {0} GB. Unallocated space on the disk: {1} GB. No partition was created; the installer of the second system takes the free space itself." -f (GB $after.Size), (GB $diskAfter.LargestFreeExtent))
    if ($script:Failures -gt 0) { Announce "Uwaga: $($script:Failures) z kroków przygotowania się nie udało; zobacz dziennik $LogPath." "Warning: $($script:Failures) of the preparation steps failed; see the log $LogPath." }
    if ($NoFastStartup -or $ClockUtc) { Note 'Ustawienia szybkiego uruchamiania i zegara działają od następnego uruchomienia Windows.' 'The fast start-up and clock settings take effect from the next start of Windows.' }
}

Initialize-Speech
try {
    Invoke-Main | Out-Null
} catch {
    if ($script:ExitCode -eq 0) { $script:ExitCode = 1 }
    Write-Log ('ERROR: {0}' -f $_.Exception.Message)
    Write-Host ('Charon: {0}' -f $_.Exception.Message)
    Speak ('Charon: {0}' -f $_.Exception.Message) ('Charon: {0}' -f $_.Exception.Message)
} finally {
    Write-Log ('exit code {0}, failed steps {1}' -f $script:ExitCode, $script:Failures)
    if ($script:Synth) { try { $script:Synth.Dispose() } catch { } }
}
exit $script:ExitCode
