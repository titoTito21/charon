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
    if (Speak-ThroughNvda "Charon: $text") { return }   # r409: once NVDA runs it says the messages itself; no second voice
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
# r409: a message handed to the running NVDA (its controller client, nvdaControllerClient.dll in NVDA's folder).
# False when NVDA is not running or has no such library: the caller then uses a Windows voice.
function Speak-ThroughNvda([string]$Text) {
    if (-not (Get-Process -Name nvda -ErrorAction SilentlyContinue)) { return $false }
    try {
        if (-not ('Charon.NvdaClient' -as [type])) {
            # The library comes with Charon (NV Access's nvdaControllerClient.dll, x64, LGPL 2.1), beside this script;
            # an installed NVDA does not ship it. Looked for in NVDA's own folder as well, in case a later NVDA does.
            $dll = $null
            foreach ($candidate in @((Join-Path $PSScriptRoot 'nvdaControllerClient.dll'), (Join-Path $env:ProgramData 'Charon
vdaControllerClient.dll'))) {
                if (-not $dll -and [IntPtr]::Size -eq 8 -and (Test-Path -LiteralPath $candidate)) { $dll = Get-Item -LiteralPath $candidate }
            }
            foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
                if ($dll) { break }
                if (-not $root -or -not (Test-Path -LiteralPath (Join-Path $root 'NVDA'))) { continue }
                $dll = Get-ChildItem -LiteralPath (Join-Path $root 'NVDA') -Recurse -Filter 'nvdaControllerClient*.dll' -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -notmatch '(?i)arm64' -and (([IntPtr]::Size -eq 8) -eq ($_.FullName -notmatch '(?i)x86|32')) } | Select-Object -First 1
                if ($dll) { break }
            }
            if (-not $dll) { return $false }
            $path = $dll.FullName.Replace('\', '\\')
            Add-Type -Namespace Charon -Name NvdaClient -MemberDefinition @"
[DllImport("$path", CharSet = CharSet.Unicode)] public static extern int nvdaController_testIfRunning();
[DllImport("$path", CharSet = CharSet.Unicode)] public static extern int nvdaController_speakText(string text);
"@
        }
        if ([Charon.NvdaClient]::nvdaController_testIfRunning() -ne 0) { return $false }
        return ([Charon.NvdaClient]::nvdaController_speakText($Text) -eq 0)
    } catch { return $false }
}
# r409: before a download: the network may still be coming up at the first sign-in (Wi-Fi joins only then).
# Waits up to five minutes and says so; the download itself is then tried three times as before.
$script:NetworkSeen = $false
function Wait-Network {
    if ($script:NetworkSeen) { return }
    for ($i = 0; $i -lt 30; $i++) {
        try { Invoke-WebRequest -Uri 'http://www.msftconnecttest.com/connecttest.txt' -UseBasicParsing -TimeoutSec 10 -ErrorAction Stop | Out-Null; $script:NetworkSeen = $true; return }
        catch {
            if ($i % 6 -eq 0) { Speak ('Czekam na połączenie z siecią, zostało do {0} minut.' -f (5 - [int]($i / 6))) ('Waiting for the network connection, up to {0} minutes left.' -f (5 - [int]($i / 6))) }
            Start-Sleep -Seconds 10
        }
    }
    Speak 'Nie ma połączenia z siecią; pobieranie może się nie udać.' 'There is no network connection; the download may fail.'
}
function Test-Sha256([string]$Path, [string]$Expected) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected.ToUpperInvariant()
}
function Get-Download([string]$Url, [string]$Expected, [string]$Target) {
    if ($DryRun) { Write-Log "dry run: would download $Url"; return $false }
    Wait-Network   # r409
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
# ---- r409: the voice Charon itself speaks with, for NVDA ------------------------------------------
# Charon writes \charon\voice.conf (here: $Source\voice.conf): CHARON_SYNTH (rhvoice, espeak, tvox),
# CHARON_VOICE (RHVoice's name in lower case: natan, magda, slt ...), CHARON_RATE and CHARON_PITCH
# (5..20, 10 = normal), CHARON_PUNCTUATION (none, some, most, all).
#   rhvoice  NVDA gets RHVoice: the engine add-on and the add-on of the voice (it holds the language
#            data too), both from github.com/RHVoice, unpacked into NVDA's add-on folder, and RHVoice
#            becomes NVDA's synthesizer with that voice. A voice that has no NVDA add-on (Dasha,
#            Evgeniy and Lyubov among the English ones) is replaced by the first voice of the same
#            language that has one; when nothing can be fetched, NVDA keeps its own synthesizer.
#            All of it is said and logged.
#   espeak   NVDA uses eSpeak NG, which it brings itself.
#   other    NVDA's synthesizer is left alone.
# Rate, pitch and punctuation go into NVDA's configuration in every case. The same files are copied
# to NVDA's configuration for the sign-in screen (systemConfig), so it speaks there with the same voice.
# Narrator has no RHVoice of its own kind: it could only use RHVoice's SAPI 5 voice, whose installer
# has no documented silent mode; Narrator keeps its Windows voice (said in the log).
# Sha256: Charon's own SHA-256 of each file as downloaded on 2026-11-23 (the engine's equals the digest
# GitHub publishes); the addresses are those of src/synth/rhvoice/VOICES.tsv.
$RhvoiceEngine = @{ Url = 'https://github.com/RHVoice/RHVoice/releases/download/1.18.2/RHVoice-1.18.201.nvda-addon'
                    Sha256 = 'f15a127a15b0bdd531d0cb95072ce8750b1399652917f8d192bed05eaf3ca7b9'; Folder = 'RHVoice' }
$RhvoiceVoices = @(      # in the order of VOICES.tsv: the first of a language is that language's stand-in
    @{ Id = 'natan';  Name = 'Natan';  Language = 'pl'; Sha256 = 'ff914efd47e15c79f8117e094d2aa2115a99d42d2a3c62734f37c1ad38efbf16'; Url = 'https://github.com/RHVoice/natan-pol/releases/download/4.12/RHVoice-voice-Polish-Natan-4.12.1021.12.nvda-addon' },
    @{ Id = 'slt';    Name = 'Slt';    Language = 'en'; Sha256 = 'cf1b0c975d3ae8167f9d046c9edcd785624ae82e5dd2ad4e79eee4d99c60193c'; Url = 'https://github.com/RHVoice/slt-eng/releases/download/4.1/RHVoice-voice-English-Slt-4.1.2017.12.nvda-addon' },
    @{ Id = 'alicja'; Name = 'Alicja'; Language = 'pl'; Sha256 = '21c5a3555a3899355d0522f4995b436ab0dd5090aefddf519309d1c9d1475354'; Url = 'https://github.com/RHVoice/alicja-pol/releases/download/v4.1/RHVoice-voice-Polish-Alicja-4.1.1021.12.nvda-addon' },
    @{ Id = 'cezary'; Name = 'Cezary'; Language = 'pl'; Sha256 = 'd095dd161ee6d48923ba2fece0da9a2eb7e347e71a4ef55ac3fc51ed1fa59273'; Url = 'https://github.com/RHVoice/cezary-pol/releases/download/4.1/RHVoice-voice-Polish-Cezary-4.1.1021.12.nvda-addon' },
    @{ Id = 'magda';  Name = 'Magda';  Language = 'pl'; Sha256 = '40cef37a07cc4f31fa2cfb0ee02fa52538aac4c90a45d4ff0d2a169e149d1f65'; Url = 'https://github.com/RHVoice/magda-pol/releases/download/rev46/RHVoice-voice-Polish-Magda-4.46.1021.12.nvda-addon' },
    @{ Id = 'michal'; Name = 'Michal'; Language = 'pl'; Sha256 = '61b3be8cb69a64b46065892c422b6b24942c862421e383ab978c7ccce3b04857'; Url = 'https://github.com/RHVoice/michal-pol/releases/download/4.2/RHVoice-voice-Polish-Michal-4.2.1021.12.nvda-addon' },
    @{ Id = 'alan';   Name = 'Alan';   Language = 'en'; Sha256 = '88862641227e33836fd9bbaa9a449a01e8787e93a0451ffbb25597bd1ac562d1'; Url = 'https://github.com/RHVoice/alan-eng/releases/download/4.0/RHVoice-voice-English-Alan-4.0.2017.12.nvda-addon' },
    @{ Id = 'bdl';    Name = 'Bdl';    Language = 'en'; Sha256 = '240169948fc675a73e0e027e61124019da735fab4ebf102bef3417b751ae2809'; Url = 'https://github.com/RHVoice/bdl-eng/releases/download/4.1/RHVoice-voice-English-Bdl-4.1.2017.12.nvda-addon' },
    @{ Id = 'clb';    Name = 'Clb';    Language = 'en'; Sha256 = 'da547e4b55656ac2da6ba96a17e4696c03b48296918015917c27a8e49c053c6f'; Url = 'https://github.com/RHVoice/clb-eng/releases/download/4.0/RHVoice-voice-English-Clb-4.0.2017.12.nvda-addon' },
    @{ Id = 'ksp';    Name = 'Ksp';    Language = 'en'; Sha256 = '008f993417c41c1924a7cc8fca4a6c2d885b11a431070a72b81c91e2e91c8cfb'; Url = 'https://github.com/RHVoice/ksp-eng/releases/download/4.5/RHVoice-voice-English-Ksp-4.5.2017.12.nvda-addon' }
)
$RhvoiceNoAddon = @{ 'dasha-eng' = 'en'; 'evgeniy-eng' = 'en'; 'lyubov' = 'en' }    # voices of Charon without an NVDA add-on
function Read-VoiceConf {
    $conf = @{}
    $file = Join-Path $Source 'voice.conf'
    if (-not (Test-Path -LiteralPath $file)) { return $conf }
    foreach ($line in Get-Content -LiteralPath $file -Encoding UTF8) { if ($line -match '^(CHARON_[A-Z_]+)=(.*)$') { $conf[$Matches[1]] = $Matches[2].Trim() } }
    return $conf
}
function Expand-NvdaAddon([string]$File, [string]$Folder, [string]$AddonsDir) {
    # An .nvda-addon file is a zip; an add-on is its content in a folder of the add-on's name.
    $target = Join-Path $AddonsDir $Folder
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($File, $target)
    return (Test-Path -LiteralPath (Join-Path $target 'manifest.ini'))
}
function Set-NvdaSpeechConfig([string]$IniPath, [string]$Synth, [string]$Voice, [int]$Rate, [int]$Pitch, [int]$SymbolLevel) {
    # NVDA's configuration is an ini file with nested sections ([speech], [[RHVoice]]). Only the lines
    # named here are changed; everything else is kept. A missing file gets just these lines (NVDA
    # fills in the rest with its defaults).
    $lines = New-Object Collections.Generic.List[string]
    if (Test-Path -LiteralPath $IniPath) { foreach ($l in [IO.File]::ReadAllLines($IniPath)) { $lines.Add($l) } }
    $start = -1; for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\[speech\]\s*$') { $start = $i; break } }
    if ($start -lt 0) { $lines.Add('[speech]'); $start = $lines.Count - 1 }
    $end = $lines.Count; for ($i = $start + 1; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\[[^\[]') { $end = $i; break } }
    $body = New-Object Collections.Generic.List[string]
    $skip = $false
    for ($i = $start + 1; $i -lt $end; $i++) {
        $l = $lines[$i]
        if ($l -match '^\s*\[\[(.+)\]\]\s*$') { $skip = ($Synth -and $Matches[1] -eq $Synth) }      # the chosen synthesizer's own subsection is written anew
        if ($skip) { continue }
        if ($Synth -and $l -match '^\s*synth\s*=') { continue }
        if ($l -match '^\s*symbolLevel\s*=') { continue }
        $body.Add($l)
    }
    $head = New-Object Collections.Generic.List[string]
    if ($Synth) { $head.Add("`tsynth = $Synth") }
    $head.Add("`tsymbolLevel = $SymbolLevel")
    $firstSub = -1; for ($i = 0; $i -lt $body.Count; $i++) { if ($body[$i] -match '^\s*\[\[') { $firstSub = $i; break } }
    if ($firstSub -lt 0) { $firstSub = $body.Count }
    $new = New-Object Collections.Generic.List[string]
    for ($i = 0; $i -lt $firstSub; $i++) { $new.Add($body[$i]) }
    foreach ($l in $head) { $new.Add($l) }
    for ($i = $firstSub; $i -lt $body.Count; $i++) { $new.Add($body[$i]) }
    if ($Synth) {
        $new.Add("`t[[$Synth]]")
        if ($Voice) { $new.Add("`t`tvoice = $Voice") }
        $new.Add("`t`trate = $Rate"); $new.Add("`t`tpitch = $Pitch")
    }
    $all = New-Object Collections.Generic.List[string]
    for ($i = 0; $i -le $start; $i++) { $all.Add($lines[$i]) }
    foreach ($l in $new) { $all.Add($l) }
    for ($i = $end; $i -lt $lines.Count; $i++) { $all.Add($lines[$i]) }
    $dir = Split-Path -Parent $IniPath
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    [IO.File]::WriteAllLines($IniPath, $all, (New-Object Text.UTF8Encoding($false)))
}
function Set-NvdaVoice([string]$NvdaExe) {
    $conf = Read-VoiceConf
    if ($conf.Count -eq 0) { Write-Log 'voice.conf: none; NVDA keeps its own voice'; return }
    $synth = "$($conf['CHARON_SYNTH'])".ToLowerInvariant(); $voice = "$($conf['CHARON_VOICE'])".ToLowerInvariant()
    $scale = { param($v) $n = 10; [void][int]::TryParse("$v", [ref]$n); if ($n -lt 5) { $n = 5 }; if ($n -gt 20) { $n = 20 }; [int](50 + ($n - 10) * 5) }     # 5..20 -> 25..100, 10 -> 50 (NVDA's normal)
    $rate = & $scale $conf['CHARON_RATE']; $pitch = & $scale $conf['CHARON_PITCH']
    $level = switch ("$($conf['CHARON_PUNCTUATION'])") { 'none' { 0 } 'most' { 200 } 'all' { 300 } default { 100 } }
    Write-Log "voice.conf: synth '$synth', voice '$voice', rate $rate, pitch $pitch, punctuation level $level"
    if ($DryRun) { return }
    $userDir = Join-Path $env:APPDATA 'nvda'
    $addons = Join-Path $userDir 'addons'
    $nvdaSynth = ''; $nvdaVoice = ''
    # NVDA is stopped first: it holds its add-ons' libraries open and writes its configuration when it quits.
    $wasRunning = [bool](Get-Process -Name nvda -ErrorAction SilentlyContinue)
    if ($wasRunning) {
        # NVDA writes its configuration when it quits: it must be gone before the file is changed.
        try { & $NvdaExe -q | Out-Null } catch { }
        for ($i = 0; $i -lt 20 -and (Get-Process -Name nvda -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Seconds 1 }
        Get-Process -Name nvda -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
    }
    if ($synth -eq 'espeak') { $nvdaSynth = 'espeak' }
    elseif ($synth -eq 'rhvoice') {
        $entry = $RhvoiceVoices | Where-Object { $_.Id -eq $voice } | Select-Object -First 1
        if (-not $entry) {
            $language = $RhvoiceNoAddon[$voice]; if (-not $language) { $language = $(if ($IsPolish) { 'pl' } else { 'en' }) }
            $entry = $RhvoiceVoices | Where-Object { $_.Language -eq $language } | Select-Object -First 1
            Speak "Głos RHVoice $voice nie ma wersji dla NVDA. NVDA będzie mówić głosem $($entry.Name)." "The RHVoice voice $voice has no version for NVDA. NVDA will speak with the voice $($entry.Name)."
        }
        $folder = Join-Path $work 'rhvoice'; New-Item -ItemType Directory -Force -Path $folder | Out-Null
        $engineFile = Join-Path $folder 'engine.zip'; $voiceFile = Join-Path $folder 'voice.zip'
        $ok = (Get-Download $RhvoiceEngine.Url $RhvoiceEngine.Sha256 $engineFile)
        if ($ok) {
            $ok = (Get-Download $entry.Url $entry.Sha256 $voiceFile)
            if (-not $ok) {     # the chosen voice cannot be fetched: another voice of the same language
                foreach ($other in ($RhvoiceVoices | Where-Object { $_.Language -eq $entry.Language -and $_.Id -ne $entry.Id })) {
                    if (Get-Download $other.Url $other.Sha256 $voiceFile) { Speak "Nie udało się pobrać głosu $($entry.Name); NVDA będzie mówić głosem $($other.Name)." "The voice $($entry.Name) could not be downloaded; NVDA will speak with the voice $($other.Name)."; $entry = $other; $ok = $true; break }
                }
            }
        }
        if ($ok) {
            try {
                New-Item -ItemType Directory -Force -Path $addons | Out-Null
                $voiceFolder = [IO.Path]::GetFileNameWithoutExtension(([uri]$entry.Url).Segments[-1]) -replace '-[0-9.]+$', ''      # RHVoice-voice-Polish-Natan
                $ok = (Expand-NvdaAddon $engineFile $RhvoiceEngine.Folder $addons) -and (Expand-NvdaAddon $voiceFile $voiceFolder $addons)
            } catch { Write-Log "unpacking the RHVoice add-ons failed: $($_.Exception.Message)"; $ok = $false }
        }
        if ($ok) { $nvdaSynth = 'RHVoice'; $nvdaVoice = $entry.Name; Write-Log "RHVoice for NVDA: engine $($RhvoiceEngine.Url), voice $($entry.Url)" }
        else { Speak 'Nie udało się zainstalować RHVoice dla NVDA; NVDA będzie mówić swoim domyślnym głosem.' 'RHVoice for NVDA could not be installed; NVDA will speak with its default voice.' }
        Write-Log 'Narrator: keeps its Windows voice (RHVoice''s SAPI 5 installer has no documented silent mode)'
    } else { Write-Log "voice.conf: synthesizer '$synth' has no counterpart in NVDA; NVDA keeps its own" }
    try {
        Set-NvdaSpeechConfig (Join-Path $userDir 'nvda.ini') $nvdaSynth $nvdaVoice $rate $pitch $level
        # The sign-in screen and UAC prompts: NVDA reads its configuration there from systemConfig in its own folder.
        $system = Join-Path (Split-Path -Parent $NvdaExe) 'systemConfig'
        New-Item -ItemType Directory -Force -Path $system | Out-Null
        Copy-Item -LiteralPath (Join-Path $userDir 'nvda.ini') -Destination (Join-Path $system 'nvda.ini') -Force
        if ($nvdaSynth -eq 'RHVoice') { Copy-Item -LiteralPath $addons -Destination $system -Recurse -Force }
        Write-Log "NVDA configuration: synth '$nvdaSynth', voice '$nvdaVoice', rate $rate, pitch $pitch, symbolLevel $level (user and sign-in screen)"
    } catch { Write-Log "writing NVDA's configuration failed: $($_.Exception.Message)" }
    if ($wasRunning) { [void](Start-Nvda $NvdaExe) }
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
# r409: Charon's own voice for NVDA (voice.conf), before NVDA is started below.
if ('nvda' -in $Chosen -and $nvdaExe) { try { Set-NvdaVoice $nvdaExe } catch { Write-Log "setting NVDA's voice failed: $($_.Exception.Message)" } }

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
