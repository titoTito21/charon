<#
Charon: screen readers for the installed Windows (first logon). GPL-2.0-or-later.

Runs once, elevated, from the answer file's FirstLogonCommands, BEFORE the
Windows profile, and installs the screen readers the user checked in Charon:

  Charon-Readers.ps1 -Readers nvda,jaws,narrator -First nvda|jaws|narrator
      [-Language pl|en|<Windows language, e.g. de-DE>] [-Source DIR] [-Yes] [-DryRun] [-NoSpeech]

  nvda      NV Access's installer. Charon put it on the install media
            (DIR\readers\nvda_*.exe, SHA-256 checked there and again here);
            without it, it is downloaded from NV Access's GitHub release.
            Installed with --install-silent.
  jaws      Polish (-Language pl): the Polish JAWS is made and sold by Altix,
            which publishes no public download address. An Altix installer
            put by the user in DIR\readers (any .exe whose name holds "altix"
            or "pl", or JAWS*PL*.exe) is used; without one Charon says so and
            installs the JAWS of Freedom Scientific (English and the other
            languages it ships), so that a reader is there at all.
            Other languages: the offline installer of Freedom Scientific
            (the address and SHA-256 as published in the winget repository),
            /type silent. Without a licence JAWS runs 40 minutes per Windows start.
  narrator  part of Windows, nothing to install.

  -First    the reader that starts after logon and on the logon screen; the
            others stay installed and are started by their own keys
            (NVDA: Control+Alt+N, Narrator: Control+Windows+Enter, JAWS: its
            desktop shortcut). Narrator is stopped once another first reader
            is really running, and taken off Windows' start lists, so two
            readers never talk at once. If the first reader cannot be started,
            Narrator is switched on and started instead: never silence.
Progress is spoken with System.Speech (SAPI) and written to
%ProgramData%\Charon\readers.log. Exit codes: 0 success, 1 a reader could
not be installed (the others are), 2 bad arguments.
#>
[CmdletBinding()]
param(
    [string]$Readers = 'nvda,narrator',
    [ValidateSet('nvda', 'jaws', 'narrator')][string]$First = 'nvda',
    [string]$Language = '',
    [string]$Source = '',
    [switch]$Yes,
    [switch]$DryRun,
    [switch]$NoSpeech
)
$ErrorActionPreference = 'Stop'
if (-not $Source) { $Source = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Language) { $Language = (Get-UICulture).Name }
$IsPolish = $Language -like 'pl*'
$Chosen = @($Readers.Split(',') | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
# (Write-Host, not Write-Error: with $ErrorActionPreference Stop that ended the script with code 1, not the documented 2.)
foreach ($r in $Chosen) { if ($r -notin 'nvda', 'jaws', 'narrator') { Write-Host "Charon-Readers: unknown reader $r"; exit 2 } }
if ($First -notin $Chosen) { $Chosen += $First }

# Pinned: NVDA 2026.3 (SHA-256 checked by Charon's maintainer against the
# download, equal to the winget manifest), JAWS 2026.2609.84.400 (winget manifest).
$Nvda = @{ Version = '2026.3'; Url = 'https://github.com/nvaccess/nvda/releases/download/release-2026.3/nvda_2026.3.exe';
           Sha256 = 'A8A87CB823DA0C62CFBAE4A444AE5AE238D54DC82AD6D46D3FDB3FEF082799B2' }
$Jaws = @{ Version = '2026.2609.84.400';
           x64 = @{ Url = 'https://software.vfo.digital/JAWS/2026/2026.2609.84.400/6824AC12-A96A-43F1-A5C5-D33CFC10B790/J2026.2609.84.400-Offline-x64.exe';
                    Sha256 = '6EEB05BBCF48E886F63A21AA276F190D35F452F3FE2E1D17077C1757EBC07E02' }
           arm64 = @{ Url = 'https://software.vfo.digital/JAWS/2026/2026.2609.84.400/6824AC12-A96A-43F1-A5C5-D33CFC10B790/J2026.2609.84.400-Offline-arm64.exe';
                      Sha256 = 'C73C2A2A5A057CF0383E8409EF6AE1B00E8FB8B78B58036E4457DAAE095C73D5' } }

$LogDir = Join-Path $env:ProgramData 'Charon'
$LogPath = Join-Path $LogDir 'readers.log'
if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $LogDir | Out-Null }
function Write-Log([string]$Text) {
    $line = '{0:yyyy-MM-dd HH:mm:ss} {1}' -f (Get-Date), $Text
    Write-Host $line
    if (-not $DryRun) { Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8 }
}
$script:Synth = $null
$script:UserToken = ''
function Speak([string]$TextPl, [string]$TextEn) {
    $text = if ($IsPolish) { $TextPl } else { $TextEn }
    Write-Log $text
    if ($NoSpeech -or $DryRun) { return }
    try {
        if (-not $script:Synth) {
            Add-Type -AssemblyName System.Speech -ErrorAction Stop
            $script:Synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
            $voice = $script:Synth.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -like ($(if ($IsPolish) { 'pl*' } else { 'en*' })) } | Select-Object -First 1
            if ($voice) { $script:Synth.SelectVoice($voice.VoiceInfo.Name) }
        }
        $script:Synth.Speak("Charon: $text")
    } catch { }
}
function Test-Sha256([string]$Path, [string]$Expected) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected.ToUpperInvariant()
}
function Get-Download([string]$Url, [string]$Expected, [string]$Target) {
    if ($DryRun) { Write-Log "dry run: would download $Url"; return $false }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.ServicePointManager]::SecurityProtocol
    for ($try = 1; $try -le 3; $try++) {
        try {
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $Url -OutFile $Target -UseBasicParsing
            if (Test-Sha256 $Target $Expected) { return $true }
            Write-Log "SHA-256 mismatch for $Url"; Remove-Item -LiteralPath $Target -Force
            return $false
        } catch { Write-Log "download try $try failed: $($_.Exception.Message)"; Start-Sleep -Seconds (10 * $try) }
    }
    return $false
}
# Newest version from the winget repository (address and SHA-256), when the pinned one is gone.
function Get-WingetInstaller([string]$PackagePath, [string]$Architecture) {
    try {
        $ProgressPreference = 'SilentlyContinue'
        $list = Invoke-RestMethod -Uri "https://api.github.com/repos/microsoft/winget-pkgs/contents/manifests/$PackagePath" -UseBasicParsing
        $versions = @($list | Where-Object { $_.type -eq 'dir' -and $_.name -match '^\d+(\.\d+)+$' } | ForEach-Object { $_.name })
        if (-not $versions) { return $null }
        $newest = $versions | Sort-Object { [version]($_ -replace '^(\d+(\.\d+){0,3}).*$', '$1') } | Select-Object -Last 1
        $files = Invoke-RestMethod -Uri "https://api.github.com/repos/microsoft/winget-pkgs/contents/manifests/$PackagePath/$newest" -UseBasicParsing
        $installer = $files | Where-Object { $_.name -like '*.installer.yaml' } | Select-Object -First 1
        $yaml = (Invoke-WebRequest -Uri $installer.download_url -UseBasicParsing).Content
        $url = $null; $sha = $null; $arch = $null
        foreach ($line in $yaml -split "`n") {
            if ($line -match '^\s*-?\s*Architecture:\s*(\S+)') { $arch = $Matches[1] }
            if ($line -match '^\s*InstallerUrl:\s*(\S+)' -and ($arch -eq $Architecture -or -not $url)) { $url = $Matches[1]; $sha = $null }
            if ($line -match '^\s*InstallerSha256:\s*([0-9A-Fa-f]{64})' -and -not $sha) { $sha = $Matches[1] }
            if ($url -and $sha -and $arch -eq $Architecture) { break }
        }
        if ($url -and $sha) { return @{ Version = $newest; Url = $url; Sha256 = $sha } }
    } catch { Write-Log "winget repository lookup failed: $($_.Exception.Message)" }
    return $null
}
# r407: "start after sign-in" (HKCU) and "on the sign-in screen" (HKLM) are each one comma-separated
# list of assistive programs, the value Configuration; a program's name in it is its key under
# Accessibility\ATs. NVDA's check boxes "Start NVDA after I sign in" and "Use NVDA during sign-in"
# and Narrator's own two settings write exactly these lists (NVDA: source/easeOfAccess.py,
# setAutoStart). The chosen reader is put on both and the other screen readers are taken off, so
# two readers never start together; what else is on the lists (Magnifier ...) stays.
function Set-AtStart([string]$Token) {
    $paths = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility', 'HKLM:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility'
    $readers = @('narrator', 'nvda_nvda_v1', (Get-AtToken 'nvda' ''), (Get-AtToken 'jaws|jfw' ''), $Token) | Where-Object { $_ }
    foreach ($p in $paths) {
        $old = ''
        try { $old = [string](Get-ItemProperty -Path $p -Name Configuration -ErrorAction Stop).Configuration } catch { }
        $list = @($old.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -and ($readers -notcontains $_) })
        if ($Token) { $list += $Token }
        $new = $list -join ','
        if ($DryRun) { Write-Log "dry run: $p Configuration = $new"; continue }
        if (-not (Test-Path $p)) { New-Item -Path $p -Force | Out-Null }
        Set-ItemProperty -Path $p -Name Configuration -Value $new -Type String
        Write-Log "$p Configuration: '$old' -> '$new'"
    }
    # r407: at the first sign-in Windows empties the user's list once (its "Narrator after sign-in"
    # reset) and then sets this mark; on a fresh installation that happened after this script and
    # took NVDA off the list again. With the mark set, Windows leaves the list alone.
    if (-not $DryRun) {
        try { Set-ItemProperty -Path $paths[0] -Name NarratorAfterSigninResetCompleted -Value 1 -Type DWord } catch { Write-Log "setting the reset mark failed: $($_.Exception.Message)" }
    }
    $script:UserToken = $Token
}
# r407: said list is looked at once more when everything is done; if Windows emptied it meanwhile, it is put back.
function Confirm-AtStart {
    if ($DryRun -or -not $script:UserToken) { return }
    $p = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility'
    $now = ''
    try { $now = [string](Get-ItemProperty -Path $p -Name Configuration -ErrorAction Stop).Configuration } catch { }
    if (@($now.Split(',') | ForEach-Object { $_.Trim() }) -contains $script:UserToken) { Write-Log "start after sign-in: '$now' (kept)"; return }
    Write-Log "start after sign-in was changed to '$now' meanwhile; putting $($script:UserToken) back"
    Set-AtStart $script:UserToken
}
# r407: the installed NVDA is started the way Windows starts it after sign-in: not elevated. (This
# script is elevated; NV Access's installer does not start NVDA from an elevated process either.)
# A one-time task with limited rights does that; if the task fails, NVDA is started directly.
# True only when an nvda.exe process is really there afterwards.
function Test-NvdaRuns { return [bool](Get-Process -Name nvda -ErrorAction SilentlyContinue) }
function Start-Nvda([string]$Exe) {
    if (Test-NvdaRuns) { return $true }
    $task = 'CharonStartNVDA'
    try {
        $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument ('/c start "" "{0}"' -f $Exe)
        $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
        Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings -Force | Out-Null
        Start-ScheduledTask -TaskName $task
        for ($i = 0; $i -lt 20 -and -not (Test-NvdaRuns); $i++) { Start-Sleep -Seconds 1 }
    } catch { Write-Log "starting NVDA by a task failed: $($_.Exception.Message)" }
    try { Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction Stop } catch { }
    if (-not (Test-NvdaRuns)) {
        Write-Log 'NVDA did not start by the task; starting it directly'
        try { Start-Process -FilePath $Exe } catch { Write-Log "starting NVDA failed: $($_.Exception.Message)" }
        for ($i = 0; $i -lt 20 -and -not (Test-NvdaRuns); $i++) { Start-Sleep -Seconds 1 }
    }
    $runs = Test-NvdaRuns
    Write-Log "NVDA process running: $runs"
    return $runs
}
$arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$work = Join-Path $LogDir 'readers'
if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $work | Out-Null }
$failed = $false
Write-Log ("---- Charon-Readers: readers {0}, first {1}, language {2}, source {3}" -f ($Chosen -join ','), $First, $Language, $Source)

# ---- NVDA ----------------------------------------------------------------------
# r406: NVDA 2026 is a 64-bit program in Program Files; older ones are in Program Files (x86).
# (Looking only in the old place made a successful installation count as failed.)
function Find-Nvda {
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432)) {
        if (-not $root) { continue }
        $exe = Join-Path $root 'NVDA\nvda.exe'
        if (Test-Path -LiteralPath $exe) { return $exe }
    }
    foreach ($key in 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\NVDA', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\NVDA') {
        try {
            $dir = (Get-ItemProperty -Path $key -Name UninstallDirectory -ErrorAction Stop).UninstallDirectory
            if ($dir -and (Test-Path -LiteralPath (Join-Path $dir 'nvda.exe'))) { return (Join-Path $dir 'nvda.exe') }
        } catch { }
    }
    return $null
}
# The name Windows starts an installed assistive program by: its key under Accessibility\ATs.
function Get-AtToken([string]$Match, [string]$Default) {
    try {
        $key = Get-ChildItem -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Accessibility\ATs' -ErrorAction Stop |
            Where-Object { $_.PSChildName -match "(?i)$Match" } | Select-Object -First 1
        if ($key) { return $key.PSChildName }
    } catch { }
    return $Default
}
$nvdaExe = Find-Nvda
if ('nvda' -in $Chosen -and -not $nvdaExe) {
    Speak 'Instaluję NVDA.' 'Installing NVDA.'
    $installer = $null
    $local = Get-ChildItem -LiteralPath (Join-Path $Source 'readers') -Filter 'nvda_*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($local -and (Test-Sha256 $local.FullName $Nvda.Sha256)) { $installer = $local.FullName }
    if (-not $installer) {
        $target = Join-Path $work 'nvda.exe'
        if (Get-Download $Nvda.Url $Nvda.Sha256 $target) { $installer = $target }
        else {
            $newer = Get-WingetInstaller 'n/NVAccess/NVDA' 'x64'
            if (-not $newer) { $newer = Get-WingetInstaller 'n/NVAccess/NVDA' 'x86' }
            if ($newer -and (Get-Download $newer.Url $newer.Sha256 $target)) { $installer = $target }
        }
    }
    if ($installer -and -not $DryRun) {
        # --enable-start-on-logon: NVDA also speaks on the sign-in screen.
        $p = Start-Process -FilePath $installer -ArgumentList '--install-silent', '--enable-start-on-logon=True' -PassThru
        $null = $p.Handle        # keeps ExitCode readable after the process ends (Windows PowerShell 5.1)
        # r407: with a limit. When the installation fails, NVDA shows an error dialog and waits for OK
        # (seen in Setup's specialize pass); nobody may be able to see it.
        if (-not $p.WaitForExit(600000)) {
            Write-Log 'NVDA installer still running after 10 minutes; stopped'
            try { $p.Kill() } catch { }
            Get-Process -Name nvda_noUIAccess, nvda_uiAccess, nvda -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        } else { Write-Log "NVDA installer exit code $($p.ExitCode)" }
        for ($i = 0; $i -lt 30 -and -not ($nvdaExe = Find-Nvda); $i++) { Start-Sleep -Seconds 2 }   # the installer may finish in the background
        Write-Log "NVDA found at: $nvdaExe"
    }
    if ($DryRun -or $nvdaExe) { Speak 'NVDA jest zainstalowane. Włącza je Control, Alt, N.' 'NVDA is installed. Control, Alt, N starts it.' }
    else { $failed = $true; Speak 'Nie udało się zainstalować NVDA.' 'NVDA could not be installed.' }
}

# ---- JAWS ----------------------------------------------------------------------
$jawsExe = Get-ChildItem -Path (Join-Path $env:ProgramFiles 'Freedom Scientific\JAWS') -Filter 'jfw.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ('jaws' -in $Chosen -and -not $jawsExe) {
    $installer = $null
    if ($IsPolish) {
        $altix = Get-ChildItem -LiteralPath (Join-Path $Source 'readers') -Filter '*.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'altix' -or $_.Name -match '(?i)jaws.*pl|pl.*jaws' } | Select-Object -First 1
        if ($altix) { $installer = $altix.FullName; Speak 'Instaluję polskiego JAWS z instalatora Altixa.' 'Installing the Polish JAWS from the Altix installer.' }
        else {
            Speak 'Polskiego JAWS wydaje Altix, który nie publikuje adresu do pobrania. Instaluję JAWS firmy Freedom Scientific; polską wersję doinstalujesz instalatorem od Altixa.' `
                  'The Polish JAWS comes from Altix, which publishes no download address. Installing the JAWS of Freedom Scientific; the Polish version is added with the installer from Altix.'
        }
    }
    if (-not $installer) {
        Speak 'Pobieram JAWS od Freedom Scientific. To duży plik, potrwa kilka minut.' 'Downloading JAWS from Freedom Scientific. It is a large file and takes a few minutes.'
        $target = Join-Path $work 'jaws.exe'
        if (Get-Download $Jaws.$arch.Url $Jaws.$arch.Sha256 $target) { $installer = $target }
        else {
            foreach ($year in ((Get-Date).Year + 1)..((Get-Date).Year - 1)) {
                $newer = Get-WingetInstaller "f/FreedomScientific/JAWS/$year" $arch
                if ($newer -and (Get-Download $newer.Url $newer.Sha256 $target)) { $installer = $target; break }
            }
        }
    }
    if ($installer -and -not $DryRun) {
        Speak 'Instaluję JAWS.' 'Installing JAWS.'
        $p = Start-Process -FilePath $installer -ArgumentList '/type silent' -Wait -PassThru
        Write-Log "JAWS installer exit code $($p.ExitCode)"
        $jawsExe = Get-ChildItem -Path (Join-Path $env:ProgramFiles 'Freedom Scientific\JAWS') -Filter 'jfw.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($DryRun -or $jawsExe) { Speak 'JAWS jest zainstalowany. Bez licencji działa 40 minut na jedno uruchomienie Windows.' 'JAWS is installed. Without a licence it runs 40 minutes per Windows start.' }
    else { $failed = $true; Speak 'Nie udało się zainstalować JAWS.' 'JAWS could not be installed.' }
}

# ---- the first reader ---------------------------------------------------------------
$started = $false
switch ($First) {
    'nvda' {
        if ($DryRun -or $nvdaExe) {
            # r406: Windows starts an assistive program by the name of its key under Accessibility\ATs;
            # NVDA registers itself there as nvda_nvda_v1 (the token "nvda" started nothing).
            Set-AtStart (Get-AtToken 'nvda' 'nvda_nvda_v1')
            if (-not $DryRun) { $started = Start-Nvda $nvdaExe }
        }
    }
    'jaws' {
        if ($DryRun -or $jawsExe) {
            # JAWS: its own key under Accessibility\ATs when it registered one; otherwise a Run entry.
            $token = Get-AtToken 'jaws' ''
            Set-AtStart $token
            if (-not $DryRun) {
                if (-not $token) {
                    $run = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run'
                    $has = (Get-ItemProperty -Path $run -ErrorAction SilentlyContinue).PSObject.Properties | Where-Object { "$($_.Value)" -match '(?i)jfw\.exe' }
                    if (-not $has) { Set-ItemProperty -Path $run -Name 'CharonJAWS' -Value ('"{0}"' -f $jawsExe.FullName) -Type String }
                }
                try { Start-Process -FilePath $jawsExe.FullName; $started = $true } catch { Write-Log "starting JAWS failed: $($_.Exception.Message)" }
            }
        }
    }
    'narrator' { Set-AtStart 'narrator' }
}
if ($First -ne 'narrator' -and -not $started -and -not $DryRun) {
    # r407: never silence: the first reader did not start, so Narrator is switched on and started now.
    Set-AtStart 'narrator'
    if (-not (Get-Process -Name Narrator -ErrorAction SilentlyContinue)) {
        try { Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\Narrator.exe') } catch { Write-Log "starting Narrator failed: $($_.Exception.Message)" }
    }
    Speak 'Pierwszy wybrany czytnik nie jest zainstalowany, więc po zalogowaniu dalej mówi Narrator.' 'The first chosen reader is not installed, so Narrator keeps speaking after sign-in.'
}
if ($started) {
    Start-Sleep -Seconds 8
    Get-Process -Name Narrator -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}
Confirm-AtStart
Write-Log ("---- Charon-Readers: done, first {0} started {1}, failures {2}" -f $First, $started, $failed)
if ($failed) { exit 1 }
exit 0
