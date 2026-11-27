<#
Charon Windows profile (first logon). GPL-2.0-or-later.

Runs once, elevated, from the answer file's FirstLogonCommands AFTER Windows
has been installed by Charon, and trims Windows the way the user chose in
Charon. The user is blind: every profile keeps Narrator, the speech voices
and the audio services usable, and progress is spoken with System.Speech
(SAPI) as well as printed, so it is heard even when Narrator is silent.

  Charon-WindowsProfile.ps1 -Profile Slim|Script|DebloatLite|NoAI|WinUtil|
      WinUtilMinimal|ReviOS|AtlasOS|ServerDesktop|None [-Yes]
      [-Language pl|en] [-DryRun] [-Restart] [-LogPath FILE] [-NoSpeech]
      [-ScriptUrl https://...] [-ScriptArguments "..."] [-ScriptEntry NAME.ps1]
      [-PlaybookOptions "opt1 opt2"] [-PlaybookUrl https://...]
      [-NarratorAtLogon Keep|On] [-SecurityWaitMinutes N]
  Charon-WindowsProfile.ps1 -RestoreNarrator     (used by the logon task below)

  None     nothing is changed.
  Slim     removes the factory apps that the official tiny11builder script
           removes from an image (list below, with documented exceptions),
           OneDrive, Copilot, Teams/Chat, and turns consumer features and
           advertising off. Windows Update, Windows Security, .NET, Edge and
           the WebView2 runtime, Store framework libraries, sound and
           accessibility parts are left alone.
  ReviOS   downloads the AME CLI (TrustedUninstaller.CLI) and the ReviOS
  AtlasOS  or AtlasOS playbook and runs it. NOT fully unattended, see
           "Limits" below.
  Script         Win11Debloat (Raphire), "-RunDefaults -Silent": its default
                 settings and its default app removal.
  DebloatLite    Win11Debloat, "-RunDefaultsLite -Silent": the same settings,
                 no app is removed.
  NoAI           Win11Debloat, only its switches that turn the AI features off
                 (Copilot, Recall, Click to Do, AI service autostart, AI in
                 Edge, Notepad and Paint).
  WinUtil        Chris Titus Tech's Windows Utility, headless, "-Preset
  WinUtilMinimal Standard" or "-Preset Minimal" (its own tweak presets).
  ServerDesktop  Charon's own code, Windows Server only: sound on, Server
                 Manager not opened at sign-in, no shutdown reason question.
           r409: each of the script profiles downloads ONE pinned release
           (see $ScriptTools: tag in the address, SHA-256 computed by Charon
           and compared with the publisher's digest) into
           %ProgramData%\Charon\profile-script; another file is refused, aloud.
           By hand only: -ScriptUrl (https) with -ScriptArguments/-ScriptEntry
           runs any other zip or .ps1; its hash is logged, not checked.
           r409: every profile keeps Narrator AND NVDA (program, service,
           sign-in registration, voices, audio services) and starts the reader
           that was speaking again if the tool stopped it.

  -DryRun  only read-only queries; prints what would be done. Nothing is
           downloaded, written, removed or started; no log file is written.
  -Yes     no questions (the answer file always passes it).
  -Restart restart at the end (60 s delay). Without it the script never
           restarts the computer itself (the AME CLI may: see Limits).
Exit codes: 0 success (single removals that failed are logged and spoken,
they do not change the code), 1 error, 2 refused (unsupported Windows build, bad
arguments), 3 a manual step was not done in time.

Limits (honest list; nothing here was run on a real fresh installation):
- AME CLI: per the notes of 2026-11-20 it waits for a key press (Console.ReadKey)
  while any of the four Windows Security toggles is on, and again before its
  own restart. Tamper Protection cannot be switched off by any script (that is
  its purpose), so the user must turn that ONE toggle off by hand; this
  script opens the page, speaks the steps, waits, then turns the other three
  off itself. The key press before the restart is also the user's. How the CLI
  treats playbook requirements other than the Defender toggles (pending
  updates, UCPD, mains power) was not tested.
- Because the AME CLI may restart Windows before this script gets control
  back, the Narrator settings are saved first and a logon task ("Charon
  restore Narrator") restores them at the next sign-in and removes itself.
- A playbook (.apbx) is a password-protected archive (password "malte"):
  ReviOS 26.04 is 7z (LZMA2 + AES), AtlasOS 0.5.0-hotfix is zip (ZipCrypto).
  Windows cannot unpack either, so 7zr.exe and the 7-Zip "extra" package
  (7za.exe) are downloaded from 7-zip.org.
- ReviOS 26.04 (the pinned release; 26.10 came out later and is NOT adopted: its
  playbook.conf and option names were not read) lists builds 19044, 19045, 22631,
  26100, 26200 in its playbook.conf; AtlasOS 0.5.0-hotfix lists 26100 and
  26200. The list in the downloaded playbook is authoritative; other builds
  are refused.
- Whether Narrator stays fully usable after ReviOS is UNCONFIRMED (it resets
  Ease of Access keys); AtlasOS turns the Narrator hotkey off. Both are put
  back by this script, but neither was tried.

Sources relied on (fetched 2026-11-20):
  https://raw.githubusercontent.com/ntdevlabs/tiny11builder/master/tiny11maker.ps1  ($packagePrefixes, registry values)
  https://raw.githubusercontent.com/Raphire/Win11Debloat/master/Win11Debloat.ps1    (-RunDefaults, -Silent)
  https://github.com/Raphire/Win11Debloat/releases/latest/download/Win11Debloat.zip (+ SHA256SUMS beside it)
  https://raw.githubusercontent.com/Ameliorated-LLC/trusted-uninstaller-cli/master/README.md
      ("TrustedUninstaller.CLI.exe "<Extracted Playbook Folder>" [options]", password malte)
  https://api.github.com/repos/Ameliorated-LLC/trusted-uninstaller-cli/releases/latest (0.8.4, asset SHA-256)
  https://api.github.com/repos/Atlas-OS/Atlas/releases/tags/0.5.0-hotfix            (asset SHA-256)
  https://api.github.com/repos/meetrevision/playbook/releases/tags/26.04            (asset SHA-256)
  playbook.conf inside both playbooks (SupportedBuilds, Requirements, option names)
  https://www.7-zip.org/a/7zr.exe , https://www.7-zip.org/a/7z2301-extra.7z
#>
[CmdletBinding()]
param(
    # Note: this parameter deliberately has the name the caller asked for; inside
    # the script it hides PowerShell's own $PROFILE variable, which is not used.
    [ValidateSet('None', 'Slim', 'ReviOS', 'AtlasOS', 'Script', 'DebloatLite', 'NoAI', 'WinUtil', 'WinUtilMinimal', 'ServerDesktop')][string]$Profile = 'None',
    [string]$ScriptUrl = '',
    [string]$ScriptArguments = '',
    [string]$ScriptEntry = '',
    [string]$PlaybookOptions = '',
    [string]$PlaybookUrl = '',
    [ValidateSet('', 'pl', 'en')][string]$Language = '',
    [ValidateSet('Keep', 'On')][string]$NarratorAtLogon = 'Keep',
    [int]$SecurityWaitMinutes = 20,
    [switch]$DryRun,
    [switch]$Yes,
    [switch]$Restart,
    [switch]$NoSpeech,
    [switch]$RestoreNarrator,
    [string]$LogPath = ''
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'      # Invoke-WebRequest is many times slower with the progress bar
$ProfileName = $Profile
$CharonData = Join-Path $env:ProgramData 'Charon'
if (-not $LogPath) { $LogPath = Join-Path $CharonData 'profile.log' }
$SnapshotPath = Join-Path $CharonData 'narrator-snapshot.json'
$RestoreTaskName = 'Charon restore Narrator'
$UsePolish = if ($Language) { $Language -eq 'pl' } else { (Get-UICulture).Name -like 'pl*' }
$script:ExitCode = 0
$script:Failures = 0
$script:Synth = $null
$script:SpeakPolish = $false
$script:StepNumber = 0
$script:StepTotal = 0
$script:Started = $false          # true once a profile has begun (then an error still restores Narrator)
$ScriptArgumentsGiven = $PSBoundParameters.ContainsKey('ScriptArguments')

# =============================================================================
# Lists. Edit here; the code below only walks them.
# =============================================================================

# ---- SLIM: factory apps to remove -------------------------------------------
# tiny11maker.ps1 ($packagePrefixes), in its order, WITHOUT the entries listed in
# $SlimKeptFromTiny11. A package is removed when its name starts with a prefix.
$SlimPackagePrefixes = @(
    'AppUp.IntelManagementandSecurityStatus',
    'Clipchamp.Clipchamp',
    'Microsoft.BingNews',
    'Microsoft.BingSearch',
    'Microsoft.BingWeather',
    'Microsoft.Copilot',
    'Microsoft.Windows.CrossDevice',
    'Microsoft.GamingApp',
    'Microsoft.GetHelp',
    'Microsoft.Getstarted',
    'Microsoft.Microsoft3DViewer',
    'Microsoft.MicrosoftOfficeHub',
    'Microsoft.MicrosoftSolitaireCollection',
    'Microsoft.MicrosoftStickyNotes',
    'Microsoft.MixedReality.Portal',
    'Microsoft.MSPaint',
    'Microsoft.Office.OneNote',
    'Microsoft.OfficePushNotificationUtility',
    'Microsoft.OutlookForWindows',
    'Microsoft.Paint',
    'Microsoft.People',
    'Microsoft.PowerAutomateDesktop',
    'Microsoft.SkypeApp',
    'Microsoft.StartExperiencesApp',
    'Microsoft.Todos',
    'Microsoft.Wallet',
    'Microsoft.Windows.DevHome',
    'Microsoft.Windows.Copilot',
    'Microsoft.Windows.Teams',
    'Microsoft.WindowsAlarms',
    'Microsoft.WindowsCamera',
    'microsoft.windowscommunicationsapps',
    'Microsoft.WindowsFeedbackHub',
    'Microsoft.WindowsMaps',
    'Microsoft.Xbox.TCUI',
    'Microsoft.XboxApp',
    'Microsoft.XboxGameOverlay',
    'Microsoft.XboxGamingOverlay',
    'Microsoft.XboxIdentityProvider',
    'Microsoft.XboxSpeechToTextOverlay',
    'Microsoft.YourPhone',
    'Microsoft.ZuneMusic',
    'Microsoft.ZuneVideo',
    'MicrosoftCorporationII.MicrosoftFamily',
    'MSTeams',
    'MicrosoftTeams',
    'Microsoft.549981C3F5F10'
)
# tiny11 removes these too; Charon keeps them, each for a stated reason.
$SlimKeptFromTiny11 = @(
    'DolbyLaboratories.DolbyAccess                 (sound)',
    'DolbyLaboratories.DolbyDigitalPlusDecoderOEM  (sound: audio decoder)',
    'Microsoft.WindowsSoundRecorder                (sound)',
    'MicrosoftCorporationII.QuickAssist            (remote help from a sighted person)',
    'Microsoft.WindowsTerminal                     (may be the console this script runs in; Narrator reads it)'
)
# Safety net: a package whose name matches one of these is NEVER removed, even
# if a prefix above (or a later edit of that list) would match it.
$ProtectedPackagePatterns = @(
    '*Narrator*', '*Accessib*', '*Speech*', '*Voice*', '*Magnif*', '*LanguageExperiencePack*',
    '*Dolby*', '*Realtek*', '*Audio*', '*Sound*',
    '*VCLibs*', '*NET.Native*', '*UI.Xaml*', '*WindowsAppRuntime*', '*WinAppRuntime*',
    '*WindowsStore*', '*StorePurchaseApp*', '*DesktopAppInstaller*', '*Services.Store*',
    '*SecHealth*', '*WebView*', '*MicrosoftEdge*', '*WindowsTerminal*', '*QuickAssist*',
    '*ShellExperienceHost*', '*StartMenuExperienceHost*', '*Windows.Client*', '*CloudExperienceHost*'
)
# ---- SLIM: policies (consumer features, advertising, Copilot, Teams/Chat, OneDrive)
# The values tiny11maker.ps1 writes into the image, applied to the running system.
# Not copied on purpose: the hardware-check bypasses (Setup only), telemetry
# (Charon's answer file has its own privacy question), BitLocker (Charon's answer
# file), the Edge and OneDriveSetup.exe file deletions and anything about Windows
# Update.
$cdm = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager'
$SlimRegistry = @(
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\CloudContent'; Name = 'DisableWindowsConsumerFeatures'; Value = 1 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\CloudContent'; Name = 'DisableConsumerAccountStateContent'; Value = 1 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\CloudContent'; Name = 'DisableCloudOptimizedContent'; Value = 1 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\PushToInstall'; Name = 'DisablePushToInstall'; Value = 1 },
    @{ Path = $cdm; Name = 'OemPreInstalledAppsEnabled'; Value = 0 },
    @{ Path = $cdm; Name = 'PreInstalledAppsEnabled'; Value = 0 },
    @{ Path = $cdm; Name = 'PreInstalledAppsEverEnabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SilentInstalledAppsEnabled'; Value = 0 },
    @{ Path = $cdm; Name = 'ContentDeliveryAllowed'; Value = 0 },
    @{ Path = $cdm; Name = 'FeatureManagementEnabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SoftLandingEnabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SubscribedContentEnabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SubscribedContent-310093Enabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SubscribedContent-338388Enabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SubscribedContent-338389Enabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SubscribedContent-338393Enabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SubscribedContent-353694Enabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SubscribedContent-353696Enabled'; Value = 0 },
    @{ Path = $cdm; Name = 'SystemPaneSuggestionsEnabled'; Value = 0 },
    @{ Path = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\AdvertisingInfo'; Name = 'Enabled'; Value = 0 },
    @{ Path = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Privacy'; Name = 'TailoredExperiencesWithDiagnosticDataEnabled'; Value = 0 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Explorer'; Name = 'DisableSearchBoxSuggestions'; Value = 1 },
    # Outlook and Dev Home are otherwise installed again by the update orchestrator.
    # (Optional: these keys may be closed to administrators on a running system.)
    @{ Path = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Orchestrator\UScheduler\OutlookUpdate'; Name = 'workCompleted'; Value = 1; Optional = $true },
    @{ Path = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Orchestrator\UScheduler\DevHomeUpdate'; Name = 'workCompleted'; Value = 1; Optional = $true }
)
$SlimCopilotRegistry = @(
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsCopilot'; Name = 'TurnOffWindowsCopilot'; Value = 1 },
    @{ Path = 'HKCU:\Software\Policies\Microsoft\Windows\WindowsCopilot'; Name = 'TurnOffWindowsCopilot'; Value = 1 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Edge'; Name = 'HubsSidebarEnabled'; Value = 0 }
)
$SlimTeamsRegistry = @(
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Windows Chat'; Name = 'ChatIcon'; Value = 3 },
    @{ Path = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced'; Name = 'TaskbarMn'; Value = 0 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Teams'; Name = 'DisableInstallation'; Value = 1 }
)
$SlimOneDriveRegistry = @(
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\OneDrive'; Name = 'DisableFileSyncNGSC'; Value = 1 }
)

# ---- Playbooks and tools (URLs from work\charon-os-sources-2026-11-20.md) -----
# Sha256 is the digest GitHub publishes for the release asset (checked against a
# download on 2026-11-20). An empty Sha256 means no official hash to pin: the
# computed hash is only logged.
$Downloads = @{
    AmeCli    = @{ Url = 'https://github.com/Ameliorated-LLC/trusted-uninstaller-cli/releases/download/0.8.4/CLI-Standalone.zip'
                   Sha256 = '70e4946b52c9384ca88730c0dd25646defc63affdef7acadd69cb6317f21825a'; File = 'CLI-Standalone.zip' }
    AtlasOS   = @{ Url = 'https://github.com/Atlas-OS/Atlas/releases/download/0.5.0-hotfix/AtlasPlaybook_v0.5.0-hotfix.apbx'
                   Sha256 = '13c4d8e7fc0ed38c37c5942ebcabf7f636389972e6de391a192ae649967602d8'; File = 'playbook.apbx' }
    ReviOS    = @{ Url = 'https://github.com/meetrevision/playbook/releases/download/26.04/Revi-PB-26.04.apbx'
                   Sha256 = '03c74cf3baecb869e4300b65d6340246a494d135c4f5d081a3abdc055ddd32c1'; File = 'playbook.apbx' }
    # 7zr.exe has no versioned URL and 7-zip.org publishes no hashes: hash logged only.
    SevenZr   = @{ Url = 'https://www.7-zip.org/a/7zr.exe'; Sha256 = ''; File = '7zr.exe' }
    # Not an official hash: the SHA-256 of the file as downloaded on 2026-11-20.
    # The URL is versioned, so a different file is refused.
    SevenExtra = @{ Url = 'https://www.7-zip.org/a/7z2301-extra.7z'
                    Sha256 = 'db3a1cbe57a26fac81b65c6a2d23feaecdeede3e4c1fe8fb93a7b91d72d1094c'; File = '7z-extra.7z' }
}
$PlaybookPassword = 'malte'       # public, documented in the AME CLI README
# Builds known before downloading (so 48 MB are not fetched in vain); the
# SupportedBuilds list in the downloaded playbook.conf is checked again later.
$PlaybookBuilds = @{
    AtlasOS = @(26100, 26200)
    ReviOS  = @(19044, 19045, 22631, 26100, 26200)
}
# Option names are from each playbook.conf. Conservative on purpose: Windows
# Security stays on, security mitigations stay, Windows Update stays automatic,
# Edge stays (a blind user needs a working browser at first sign-in), no extra
# software is installed. -PlaybookOptions replaces the whole list.
# UNVERIFIED: how the CLI behaves when a page the wizard marks "required" (the
# ReviOS browser page) gets no option.
$PlaybookDefaultOptions = @{
    AtlasOS = @('defender-enable', 'mitigations-default', 'auto-updates-default')
    ReviOS  = @('enable-defender', 'enable-hibernate', 'remove-onedrive', 'remove-teams', 'remove-appx-devhome', 'remove-appx-yourphone')
}
# ---- Script profiles (r409) ---------------------------------------------------
# One pinned release per tool. Sha256 is Charon's own SHA-256 of the file downloaded
# on 2026-11-23; both equal the digest GitHub publishes for the release asset, and
# Win11Debloat's equals the SHA256SUMS file of its release. A new release needs a
# new address AND a new hash here; a file with another hash is refused.
#   Win11Debloat 2026.10.30: param() of Win11Debloat.ps1 has every switch used below.
#   WinUtil 26.11.13: param([string]$Config, [ValidateSet("Standard","Minimal","Advanced","")]
#     [string]$Preset, [switch]$Offline); "Headless runs never build a window".
$ScriptTools = @{
    Win11Debloat = @{ Url = 'https://github.com/Raphire/Win11Debloat/releases/download/2026.10.30/Win11Debloat.zip'
                      Sha256 = '6830bf501d2740c3317133ac0ecdc2a3ab80ed7f6c83e7f0f20dc66afbf758a7'; Entry = 'Win11Debloat.ps1' }
    WinUtil      = @{ Url = 'https://github.com/ChrisTitusTech/winutil/releases/download/26.11.13/winutil.ps1'
                      Sha256 = 'e9d05a59d564b4fbb61fcfa1a594504a0f81e8d27c787119e72a99b57ddbe66d'; Entry = 'winutil.ps1' }
}
# Never passed, whatever the profile: -ForceRemoveEdge (the only browser at the first
# sign-in), -DisableDriverUpdates (drivers come from Windows Update),
# -DisableNotifications (screen readers announce through them), WinUtil's Advanced
# preset (it changes the Start menu and the context menu; not tried with a reader).
$ScriptPresets = @{
    Script         = @{ Tool = 'Win11Debloat'; Arguments = '-RunDefaults -Silent' }
    DebloatLite    = @{ Tool = 'Win11Debloat'; Arguments = '-RunDefaultsLite -Silent' }
    NoAI           = @{ Tool = 'Win11Debloat'; Arguments = '-Silent -DisableCopilot -DisableRecall -DisableClickToDo -DisableAISvcAutoStart -DisableEdgeAI -DisableNotepadAI -DisablePaintAI' }
    # WinUtil's Standard preset, as a selection file (-Config, a JSON list of its entry names) WITHOUT
    # WPFTweaksDiskCleanup: that entry runs "cleanmgr.exe /d C: /VERYLOWDISK", which ends with a "Disk
    # Space Notification" window opened behind the others and waits there for a click (measured r409:
    # the headless run stood still until OK was pressed), and WITHOUT WPFTweaksDisableExplorerAutoDiscovery:
    # on a freshly installed Windows it fails ("Cannot find path HKCU:\...\Shell\Bags": the key does not
    # exist before the user has opened a folder) and makes WinUtil end with code 1 (measured r409). The
    # other ten entries are the preset's (config/preset.json of 26.11.13).
    WinUtil        = @{ Tool = 'WinUtil'; Arguments = '-Config "{0}"'; Config = @('WPFTweaksActivity', 'WPFTweaksConsumerFeatures',
                        'WPFTweaksWPBT', 'WPFTweaksLocation', 'WPFTweaksServices', 'WPFTweaksTelemetry',
                        'WPFTweaksDeliveryOptimization', 'WPFTweaksDeleteTempFiles', 'WPFTweaksEndTaskOnTaskbar', 'WPFTweaksRestorePoint') }
    WinUtilMinimal = @{ Tool = 'WinUtil'; Arguments = '-Preset Minimal' }
}

# ---- Narrator and speech: what is watched and put back -----------------------
#   Force   the value is always set to this (the Narrator hotkey Win+Ctrl+Enter).
#   Token   a comma-separated list that must contain this word if it did before
#           the profile (or always with -NarratorAtLogon On). These are the two
#           values Charon's answer file writes when the user asked for Narrator.
#   Revert  the value is put back to what it was before the profile (AtlasOS
#           sets AllowSpeechModelUpdate=0, which stops voice updates).
$WatchedValues = @(
    @{ Id = 'NarratorHotkey'; Path = 'HKCU:\Software\Microsoft\Narrator\NoRoam'; Name = 'WinEnterLaunchEnabled'; Kind = 'DWord'; Force = 1 },
    @{ Id = 'LogonNarrator'; Path = 'HKLM:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility'; Name = 'Configuration'; Kind = 'String'; Token = 'narrator' },
    @{ Id = 'UserNarrator'; Path = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility'; Name = 'Configuration'; Kind = 'String'; Token = 'narrator' },
    # r407: NVDA on the same two lists (Charon-Readers.ps1 puts it there): kept if it was there before the profile.
    @{ Id = 'LogonNvda'; Path = 'HKLM:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility'; Name = 'Configuration'; Kind = 'String'; Token = 'nvda_nvda_v1' },
    @{ Id = 'UserNvda'; Path = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility'; Name = 'Configuration'; Kind = 'String'; Token = 'nvda_nvda_v1' },
    # r405: Windows clears the user's "Narrator after sign-in" once, at the first sign-in, unless
    # this mark is set (seen on 26100: Configuration emptied, the mark appeared).
    @{ Id = 'NarratorResetMark'; Path = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Accessibility'; Name = 'NarratorAfterSigninResetCompleted'; Kind = 'DWord'; Force = 1 },
    @{ Id = 'SpeechModelUpdate'; Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Speech'; Name = 'AllowSpeechModelUpdate'; Kind = 'DWord'; Revert = $true }
)
$AudioServices = @('AudioEndpointBuilder', 'Audiosrv')
# r409: NVDA is guarded like Narrator. What is watched: the program (nvda.exe and its
# eSpeak NG data, the synthesizer that needs no Windows voice), the service that lets
# it speak on the sign-in screen and at UAC prompts ("nvda", nvda_service.exe), its key
# under Accessibility\ATs (the name Windows starts it by; the tokens on the two
# Configuration lists are in $WatchedValues above) and the user's configuration file.
# A service of this list is put back to the start type it had before the profile.
$ReaderServices = @('nvda')
$AtsKey = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Accessibility\ATs'

# =============================================================================
# Output: text, log, speech
# =============================================================================
function Write-Log([string]$Text) {
    # -DryRun writes nothing, not even a log.
    if ($DryRun) { return }
    try {
        $dir = Split-Path -Parent $LogPath
        if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Add-Content -LiteralPath $LogPath -Value ('{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Text) -Encoding UTF8
    } catch { }      # a log that cannot be written must not stop the work
}
function Note([string]$TextPl, [string]$TextEn) {
    if ($UsePolish) { Write-Host $TextPl } else { Write-Host $TextEn }
    Write-Log $TextEn       # the log is always English, so it can be sent to the developers
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
        # Polish text is only spoken by a Polish voice; with any other voice the
        # English text is spoken (an English voice reading Polish is not understandable).
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
function Speak-ThroughNvda([string]$Text) {
    # r409: when NVDA is running, the message is handed to NVDA (its controller client,
    # nvdaControllerClient.dll in NVDA's own folder) and no Windows voice is used: one voice,
    # the user's reader. Looked up at every message, because NVDA may start or stop meanwhile.
    if ($NoSpeech -or -not (Get-Process -Name nvda -ErrorAction SilentlyContinue)) { return $false }
    try {
        if (-not ('Charon.NvdaClient' -as [type])) {
            if ($script:NvdaClientMissing) { return $false }
            # The library comes with Charon (NV Access's nvdaControllerClient.dll, x64, LGPL 2.1), beside this script;
            # an installed NVDA does not ship it. Looked for in NVDA's own folder as well, in case a later NVDA does.
            $dll = $null
            foreach ($candidate in @((Join-Path $PSScriptRoot 'nvdaControllerClient.dll'), (Join-Path $env:ProgramData 'Charon
vdaControllerClient.dll'))) {
                if (-not $dll -and [IntPtr]::Size -eq 8 -and (Test-Path -LiteralPath $candidate)) { $dll = Get-Item -LiteralPath $candidate }
            }
            foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
                if ($dll) { break }
                if (-not $root) { continue }
                $dir = Join-Path $root 'NVDA'
                if (-not (Test-Path -LiteralPath $dir)) { continue }
                $dll = Get-ChildItem -LiteralPath $dir -Recurse -Filter 'nvdaControllerClient*.dll' -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -notmatch '(?i)arm64' -and (([IntPtr]::Size -eq 8) -eq ($_.FullName -notmatch '(?i)x86|32')) } | Select-Object -First 1
                if ($dll) { break }
            }
            if (-not $dll) { $script:NvdaClientMissing = $true; Write-SpeechNote 'speech: NVDA runs but has no controller client; using a Windows voice'; return $false }
            $path = $dll.FullName.Replace('\', '\\')
            Add-Type -Namespace Charon -Name NvdaClient -MemberDefinition @"
[DllImport("$path", CharSet = CharSet.Unicode)] public static extern int nvdaController_testIfRunning();
[DllImport("$path", CharSet = CharSet.Unicode)] public static extern int nvdaController_speakText(string text);
"@
        }
        if ([Charon.NvdaClient]::nvdaController_testIfRunning() -ne 0) { return $false }
        $spoken = ([Charon.NvdaClient]::nvdaController_speakText($Text) -eq 0)
        if ($spoken -and -not $script:NvdaSpeechLogged) { $script:NvdaSpeechLogged = $true; Write-SpeechNote 'speech: messages are spoken through NVDA (controller client); no Windows voice is used while NVDA runs' }
        return $spoken
    } catch { $script:NvdaClientMissing = $true; Write-SpeechNote ('speech: NVDA controller client failed ({0}); using a Windows voice' -f $_.Exception.Message); return $false }
}
function Speak([string]$TextPl, [string]$TextEn) {
    if (Speak-ThroughNvda $(if ($UsePolish) { $TextPl } else { $TextEn })) { return }
    if (-not $script:Synth) { return }
    try { if ($script:SpeakPolish) { $script:Synth.Speak($TextPl) } else { $script:Synth.Speak($TextEn) } }
    catch { Write-SpeechNote ('speech: failed ({0})' -f $_.Exception.Message) }     # never fail because of speech
}
function Announce([string]$TextPl, [string]$TextEn) { Note $TextPl $TextEn; Speak $TextPl $TextEn }
function Fail([string]$TextPl, [string]$TextEn) { if ($UsePolish) { throw $TextPl } else { throw $TextEn } }
function ProfileLabel([bool]$Polish) {
    switch ($ProfileName) {
        'Slim' { if ($Polish) { return 'odchudzony' } else { return 'slim' } }
        'Script' { return 'Win11Debloat' }
        'DebloatLite' { if ($Polish) { return 'Win11Debloat, tylko prywatność' } else { return 'Win11Debloat, privacy only' } }
        'NoAI' { if ($Polish) { return 'Win11Debloat, bez AI' } else { return 'Win11Debloat, AI off' } }
        'WinUtil' { if ($Polish) { return 'WinUtil, zestaw Standard' } else { return 'WinUtil, Standard preset' } }
        'WinUtilMinimal' { if ($Polish) { return 'WinUtil, zestaw Minimal' } else { return 'WinUtil, Minimal preset' } }
        'ServerDesktop' { if ($Polish) { return 'Windows Server jako stacja robocza' } else { return 'Windows Server as a workstation' } }
        default { return $ProfileName }
    }
}
function Step([string]$TextPl, [string]$TextEn) {
    $script:StepNumber++
    Announce ('Charon: profil {0}, krok {1} z {2}. {3}' -f (ProfileLabel $true), $script:StepNumber, $script:StepTotal, $TextPl) `
        ('Charon: applying the {0} profile, step {1} of {2}. {3}' -f (ProfileLabel $false), $script:StepNumber, $script:StepTotal, $TextEn)
}

# =============================================================================
# Changes go through ONE gate. In -DryRun the action block is never invoked.
# =============================================================================
function Invoke-Change([string]$What, [scriptblock]$Action, [switch]$Optional) {
    if ($DryRun) { Write-Host ('[dry-run] would: {0}' -f $What); return }
    try { & $Action; Write-Log ('done: {0}' -f $What) }
    catch {
        # -Optional: Windows protects some keys from administrators; such a miss is
        # logged but not counted or shown as a failed step.
        if ($Optional) { Write-Log ('skipped (optional): {0}: {1}' -f $What, $_.Exception.Message); return }
        $script:Failures++; Write-Log ('FAILED: {0}: {1}' -f $What, $_.Exception.Message); Write-Host ('! {0}: {1}' -f $What, $_.Exception.Message)
    }
}
function Assert-NotDryRun { if ($DryRun) { throw 'Internal error: a changing code path was reached in -DryRun.' } }
function Get-RegValue([string]$Path, [string]$Name) {
    try { return (Get-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction Stop).$Name } catch { return $null }
}
function Set-RegValue([string]$Path, [string]$Name, $Value, [string]$Kind = 'DWord', [switch]$Optional) {
    $now = Get-RegValue $Path $Name
    if ($null -ne $now -and "$now" -eq "$Value") { return }       # already right: nothing to write
    $p = $Path; $n = $Name; $v = $Value; $k = $Kind
    Invoke-Change ('set {0}\{1} = {2}' -f $p, $n, $v) {
        # New-Item only for a key that does not exist (-Force on an existing key would empty it).
        if (-not (Test-Path -LiteralPath $p)) { New-Item -Path $p -Force | Out-Null }
        New-ItemProperty -LiteralPath $p -Name $n -Value $v -PropertyType $k -Force | Out-Null
    } -Optional:$Optional
}
function Test-Admin {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Get-WindowsBuild {
    $b = Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' 'CurrentBuildNumber'
    if ($b) { return [int]$b }
    return [Environment]::OSVersion.Version.Build
}

# =============================================================================
# Narrator, voices, audio: read (always), protect and restore (through the gate)
# =============================================================================
function Get-SpeechState {
    # Read-only. Used before and after a profile to notice anything that vanished.
    $state = [ordered]@{}
    foreach ($w in $WatchedValues) { $state[$w.Id] = Get-RegValue $w.Path $w.Name }
    $state['NarratorExe'] = Test-Path -LiteralPath (Join-Path $env:SystemRoot 'System32\Narrator.exe')
    $state['NarratorRunning'] = [bool](Get-Process -Name Narrator -ErrorAction SilentlyContinue)
    $quick = $null
    try { $quick = Get-AppxPackage -Name 'Microsoft.Windows.NarratorQuickStart' -ErrorAction Stop } catch { }
    $state['NarratorQuickStart'] = [bool]$quick
    $oneCore = 0
    try { $oneCore = @(Get-ChildItem -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens' -ErrorAction Stop).Count } catch { }
    $sapi = 0
    try { $sapi = @(Get-ChildItem -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Speech\Voices\Tokens' -ErrorAction Stop).Count } catch { }
    $state['OneCoreVoices'] = $oneCore
    $state['SapiVoices'] = $sapi
    foreach ($name in $AudioServices) {
        $text = 'missing'
        try { $s = Get-Service -Name $name -ErrorAction Stop; $text = '{0}/{1}' -f $s.StartType, $s.Status } catch { }
        $state["Service_$name"] = $text
    }
    # r409: NVDA.
    $nvda = Find-Nvda
    $state['NvdaExe'] = "$nvda"
    $state['NvdaRunning'] = [bool](Get-Process -Name nvda -ErrorAction SilentlyContinue)
    $state['NvdaEspeak'] = $false
    if ($nvda) { $state['NvdaEspeak'] = [bool](Test-Path -LiteralPath (Join-Path (Split-Path -Parent $nvda) 'synthDrivers\espeak-ng-data')) }
    $state['NvdaAt'] = Get-NvdaAtName
    $state['NvdaConfig'] = [bool](Test-Path -LiteralPath (Join-Path $env:APPDATA 'nvda\nvda.ini'))
    foreach ($name in $ReaderServices) {
        $text = 'missing'
        try { $s = Get-Service -Name $name -ErrorAction Stop; $text = '{0}/{1}' -f $s.StartType, $s.Status } catch { }
        $state["Service_$name"] = $text
    }
    return $state
}
function Find-Nvda {
    # As in Charon-Readers.ps1: NVDA 2026 is in Program Files, older ones in Program Files (x86).
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432)) {
        if (-not $root) { continue }
        $exe = Join-Path $root 'NVDA\nvda.exe'
        if (Test-Path -LiteralPath $exe) { return $exe }
    }
    foreach ($key in 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\NVDA', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\NVDA') {
        $dir = Get-RegValue $key 'UninstallDirectory'
        if ($dir -and (Test-Path -LiteralPath (Join-Path $dir 'nvda.exe'))) { return (Join-Path $dir 'nvda.exe') }
    }
    return ''
}
function Get-NvdaAtName {
    try {
        $key = Get-ChildItem -LiteralPath $AtsKey -ErrorAction Stop | Where-Object { $_.PSChildName -match '(?i)nvda' } | Select-Object -First 1
        if ($key) { return $key.PSChildName }
    } catch { }
    return ''
}
function Show-SpeechState($State, [string]$Title) {
    Write-Host $Title
    foreach ($key in $State.Keys) {
        $value = $State[$key]; if ($null -eq $value) { $value = '(not set)' }
        Write-Host ('  {0} = {1}' -f $key, $value)
        Write-Log ('{0} {1} = {2}' -f $Title, $key, $value)
    }
}
function Test-HasToken($Value, [string]$Token) {
    if ($null -eq $Value) { return $false }
    return (@("$Value".Split(',') | ForEach-Object { $_.Trim().ToLowerInvariant() }) -contains $Token)
}
function Protect-Speech($Before) {
    # $Before: the state saved before the profile ($null = unknown: only forced values).
    foreach ($w in $WatchedValues) {
        $now = Get-RegValue $w.Path $w.Name
        if ($w.ContainsKey('Force')) {
            Set-RegValue $w.Path $w.Name $w.Force $w.Kind
        } elseif ($w.ContainsKey('Token')) {
            $wanted = ($NarratorAtLogon -eq 'On') -and ($w.Token -eq 'narrator')
            if ($Before -and (Test-HasToken $Before.($w.Id) $w.Token)) { $wanted = $true }
            if ($wanted -and -not (Test-HasToken $now $w.Token)) {
                $list = @()
                if ($now) { $list = @("$now".Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
                $list += $w.Token
                Set-RegValue $w.Path $w.Name ($list -join ',') $w.Kind
            }
        } elseif ($w.ContainsKey('Revert') -and $Before) {
            $was = $Before.($w.Id)
            if ("$was" -ne "$now") {
                if ($null -eq $was) {
                    $p = $w.Path; $n = $w.Name
                    Invoke-Change ('remove {0}\{1} (it was not set before the profile)' -f $p, $n) { Remove-ItemProperty -LiteralPath $p -Name $n -ErrorAction Stop }
                } else { Set-RegValue $w.Path $w.Name $was $w.Kind }
            }
        }
    }
    foreach ($name in $AudioServices) {
        $service = $null
        try { $service = Get-Service -Name $name -ErrorAction Stop } catch { Note "Brak usługi dźwięku $name." "Audio service $name is missing."; continue }
        $svc = $name
        if ("$($service.StartType)" -ne 'Automatic') { Invoke-Change ('set service {0} to Automatic' -f $svc) { Set-Service -Name $svc -StartupType Automatic -ErrorAction Stop } }
        if ("$($service.Status)" -ne 'Running') { Invoke-Change ('start service {0}' -f $svc) { Start-Service -Name $svc -ErrorAction Stop } }
    }
    # r409: a reader's own service keeps the start type it had before the profile.
    foreach ($name in $ReaderServices) {
        if (-not $Before) { continue }
        $was = "$($Before."Service_$name")"
        if (-not $was -or $was -eq 'missing') { continue }
        $service = $null
        try { $service = Get-Service -Name $name -ErrorAction Stop } catch { continue }     # gone: Compare-SpeechState says so
        $wasType = $was.Split('/')[0]; $svc = $name
        if ($wasType -in 'Automatic', 'Manual' -and "$($service.StartType)" -ne $wasType) {
            Invoke-Change ('set service {0} back to {1}' -f $svc, $wasType) { Set-Service -Name $svc -StartupType $wasType -ErrorAction Stop }
        }
        if ($was -like '*/Running' -and "$($service.Status)" -ne 'Running') { Invoke-Change ('start service {0}' -f $svc) { Start-Service -Name $svc -ErrorAction Stop } }
    }
}
function Restore-Readers($Before) {
    # r409: the reader that was speaking before the profile speaks after it. A tool that
    # restarts Explorer or stops processes may have taken NVDA or Narrator down.
    if (-not $Before) { return }
    if ($Before.NvdaRunning -and -not (Get-Process -Name nvda -ErrorAction SilentlyContinue)) {
        $exe = Find-Nvda
        if ($exe) {
            Invoke-Change 'start NVDA again (it was running before the profile)' {
                # Not elevated, the way Windows starts it after sign-in (as Charon-Readers.ps1 does).
                $task = 'CharonStartNVDA'
                try {
                    $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument ('/c start "" "{0}"' -f $exe)
                    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
                    Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Force | Out-Null
                    Start-ScheduledTask -TaskName $task
                    for ($i = 0; $i -lt 20 -and -not (Get-Process -Name nvda -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Seconds 1 }
                } finally { try { Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction Stop } catch { } }
                if (-not (Get-Process -Name nvda -ErrorAction SilentlyContinue)) { Start-Process -FilePath $exe }
            }
        }
    }
    if ($Before.NarratorRunning -and -not (Get-Process -Name Narrator -ErrorAction SilentlyContinue)) {
        Invoke-Change 'start Narrator again (it was running before the profile)' { Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\Narrator.exe') }
    }
}
function Compare-SpeechState($Before, $After) {
    # Says aloud when something speech needs has gone. Nothing is repaired here
    # beyond Protect-Speech: a removed voice or Narrator cannot be put back by a script.
    $lost = @()
    if ($Before.NarratorExe -and -not $After.NarratorExe) { $lost += 'Narrator.exe' }
    if ($Before.NarratorQuickStart -and -not $After.NarratorQuickStart) { $lost += 'Microsoft.Windows.NarratorQuickStart' }
    if ([int]$After.OneCoreVoices -lt [int]$Before.OneCoreVoices) { $lost += ('OneCore voices {0} -> {1}' -f $Before.OneCoreVoices, $After.OneCoreVoices) }
    if ([int]$After.SapiVoices -lt [int]$Before.SapiVoices) { $lost += ('SAPI voices {0} -> {1}' -f $Before.SapiVoices, $After.SapiVoices) }
    # r409: NVDA. Nothing here can be repaired by this script except what Protect-Speech and
    # Restore-Readers already did; a missing part is said, with the way to put it back.
    $nvdaLost = @()
    if ("$($Before.NvdaExe)" -and -not "$($After.NvdaExe)") { $nvdaLost += 'nvda.exe' }
    if ($Before.NvdaEspeak -and -not $After.NvdaEspeak) { $nvdaLost += 'NVDA eSpeak NG data' }
    if ("$($Before.NvdaAt)" -and -not "$($After.NvdaAt)") { $nvdaLost += 'NVDA sign-in registration (Accessibility\ATs)' }
    if ($Before.NvdaConfig -and -not $After.NvdaConfig) { $nvdaLost += 'NVDA configuration (nvda.ini)' }
    foreach ($name in $ReaderServices) {
        $was = "$($Before."Service_$name")"; $now = "$($After."Service_$name")"
        if ($was -and $was -ne 'missing' -and ($now -eq 'missing' -or $now -like 'Disabled/*')) { $nvdaLost += ('service {0}: {1} -> {2}' -f $name, $was, $now) }
    }
    if (-not $DryRun) {
        foreach ($name in $AudioServices) {
            $now = "$($After."Service_$name")"
            if ($now -notlike 'Automatic/Running') { $lost += ('audio service {0}: {1}' -f $name, $now) }
        }
    }
    if ($Before.NvdaRunning -and -not $After.NvdaRunning) { $nvdaLost += 'NVDA is not running' }
    if ($Before.NarratorRunning -and -not $After.NarratorRunning) { $lost += 'Narrator is not running' }
    if ($nvdaLost.Count) {
        $lost += $nvdaLost
        Note 'NVDA można naprawić, instalując je ponownie: %ProgramData%\Charon\Charon-Readers.ps1 -Readers nvda -First nvda -Yes.' `
            'NVDA can be repaired by installing it again: %ProgramData%\Charon\Charon-Readers.ps1 -Readers nvda -First nvda -Yes.'
    }
    if ($lost.Count) {
        $script:Failures++
        Announce ('Uwaga: po profilu brakuje części mowy: {0}. Zajrzyj do dziennika {1}.' -f ($lost -join '; '), $LogPath) `
            ('Warning: speech parts are missing after the profile: {0}. See the log {1}.' -f ($lost -join '; '), $LogPath)
    }
}
function Save-Snapshot($State, [hashtable]$Extra) {
    $data = @{}
    foreach ($key in $State.Keys) { $data[$key] = $State[$key] }
    foreach ($key in $Extra.Keys) { $data[$key] = $Extra[$key] }
    $json = $data | ConvertTo-Json
    Invoke-Change ('save the Narrator settings to {0}' -f $SnapshotPath) {
        if (-not (Test-Path -LiteralPath $CharonData)) { New-Item -ItemType Directory -Path $CharonData -Force | Out-Null }
        [IO.File]::WriteAllText($SnapshotPath, $json, (New-Object Text.UTF8Encoding($true)))
    }
}
function Start-NarratorIfSilent {
    # Used when the user has to do something by hand: Narrator must be talking.
    if (Get-Process -Name Narrator -ErrorAction SilentlyContinue) { return }
    Invoke-Change 'start Narrator (a manual step follows)' { Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\Narrator.exe') }
}
function Install-RestoreTask {
    # The AME CLI may restart Windows before this script continues, so the restore
    # is also scheduled for the next sign-in of this user. The task deletes itself.
    $installed = Join-Path $CharonData 'Charon-WindowsProfile.ps1'
    $self = $PSCommandPath
    if ($self -and ($self -ne $installed)) {
        Invoke-Change ('copy this script to {0}' -f $installed) {
            if (-not (Test-Path -LiteralPath $CharonData)) { New-Item -ItemType Directory -Path $CharonData -Force | Out-Null }
            Copy-Item -LiteralPath $self -Destination $installed -Force
        }
    }
    $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    $lang = 'en'; if ($UsePolish) { $lang = 'pl' }
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -RestoreNarrator -Language {1} -NarratorAtLogon {2}' -f $installed, $lang, $NarratorAtLogon
    Invoke-Change ('register the logon task "{0}" for {1}' -f $RestoreTaskName, $user) {
        $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $arguments
        $trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
        $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
        Register-ScheduledTask -TaskName $RestoreTaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    }
}
function Remove-RestoreTask {
    $exists = $null
    try { $exists = Get-ScheduledTask -TaskName $RestoreTaskName -ErrorAction Stop } catch { }
    if ($exists) { Invoke-Change ('remove the logon task "{0}"' -f $RestoreTaskName) { Unregister-ScheduledTask -TaskName $RestoreTaskName -Confirm:$false -ErrorAction Stop } }
}

# =============================================================================
# Downloads (never in -DryRun)
# =============================================================================
function Assert-Https([string]$Url) {
    if ($Url -notmatch '^https://[^/\s]+/') { $script:ExitCode = 2; Fail "Adres musi zaczynać się od https://: $Url" "The address must start with https://: $Url" }
}
function Test-Magic([string]$Path, [byte[]]$Magic) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $head = New-Object byte[] $Magic.Length
        if ($stream.Read($head, 0, $head.Length) -ne $head.Length) { return $false }
        for ($i = 0; $i -lt $Magic.Length; $i++) { if ($head[$i] -ne $Magic[$i]) { return $false } }
        return $true
    } finally { $stream.Dispose() }
}
function Get-ArchiveKind([string]$Path) {
    if (Test-Magic $Path ([byte[]](0x50, 0x4B, 0x03, 0x04))) { return 'zip' }
    if (Test-Magic $Path ([byte[]](0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C))) { return '7z' }
    if (Test-Magic $Path ([byte[]](0x4D, 0x5A))) { return 'exe' }
    return 'other'
}
function Get-Download([string]$Url, [string]$Target, [string]$Sha256, [string[]]$Kinds) {
    Assert-NotDryRun
    Assert-Https $Url
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
    $done = $false; $lastError = ''
    for ($try = 1; $try -le 3 -and -not $done; $try++) {
        try { Invoke-WebRequest -Uri $Url -OutFile $Target -UseBasicParsing -ErrorAction Stop; $done = $true }
        catch { $lastError = $_.Exception.Message; Write-Log ("download try {0} failed: {1}: {2}" -f $try, $Url, $lastError); Start-Sleep -Seconds (10 * $try) }
    }
    if (-not $done) { Fail "Nie udało się pobrać $Url ($lastError)." "Download failed: $Url ($lastError)." }
    $length = (Get-Item -LiteralPath $Target).Length
    if ($length -le 0) { Fail "Pobrany plik jest pusty: $Url" "The downloaded file is empty: $Url" }
    $kind = Get-ArchiveKind $Target
    if ($Kinds -and ($Kinds -notcontains $kind)) { Fail "Pobrany plik nie jest tym, czym powinien ($kind): $Url" "The downloaded file is not what it should be ($kind): $Url" }
    $hash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Sha256) {
        if ($hash -ne $Sha256.ToLowerInvariant()) { Fail "Suma SHA-256 pliku z $Url nie zgadza się (jest $hash). Plik odrzucony." "SHA-256 of the file from $Url does not match (got $hash). File refused." }
        Write-Log ("download ok: {0} ({1} bytes, {2}, sha256 {3} verified)" -f $Url, $length, $kind, $hash)
    } else {
        Write-Log ("download ok: {0} ({1} bytes, {2}, sha256 {3} NOT verified: no published hash)" -f $Url, $length, $kind, $hash)
    }
    return $hash
}
function New-WorkFolder([string]$Name) {
    Assert-NotDryRun
    $path = Join-Path $CharonData ('profile-work\{0}-{1}' -f $Name, (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    return $path
}
function Wait-Internet {
    Assert-NotDryRun
    # At first logon the network may still be coming up (Wi-Fi joins after sign-in: Charon-Network.ps1).
    # r409: up to five minutes, and it is said: once at the start of the wait and then every minute.
    for ($i = 0; $i -lt 30; $i++) {
        try { Invoke-WebRequest -Uri 'https://github.com' -Method Head -UseBasicParsing -TimeoutSec 15 -ErrorAction Stop | Out-Null; if ($i) { Write-Log ('network up after {0} tries' -f ($i + 1)) }; return }
        catch {
            if ($i % 6 -eq 0) {
                Announce ('Charon: czekam na połączenie z internetem, profil musi pobrać pliki. Zostało do {0} minut.' -f (5 - [int]($i / 6))) `
                    ('Charon: waiting for the internet connection; the profile has to download files. Up to {0} minutes left.' -f (5 - [int]($i / 6)))
            }
            Start-Sleep -Seconds 10
        }
    }
    Fail 'Brak połączenia z internetem; profil wymaga pobrania plików. Nic nie zmieniono.' 'No internet connection; this profile has to download files. Nothing was changed.'
}
function Start-Tool([string]$File, [string]$Arguments, [string]$Folder) {
    # Runs a program in THIS console (so its prompts can be answered and Narrator can
    # read them) and says every two minutes that work is still going on.
    Assert-NotDryRun
    Write-Log ('run: "{0}" {1}' -f $File, $Arguments)
    $started = Get-Date
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -WorkingDirectory $Folder -NoNewWindow -PassThru
    $null = $process.Handle        # keeps ExitCode readable after the process ends
    while (-not $process.WaitForExit(120000)) {
        $minutes = [int]((Get-Date) - $started).TotalMinutes
        Speak ("Charon: profil {0} nadal pracuje, {1} minut." -f (ProfileLabel $true), $minutes) ("Charon: the {0} profile is still working, {1} minutes." -f (ProfileLabel $false), $minutes)
    }
    Write-Log ('exit code {0}: "{1}"' -f $process.ExitCode, $File)
    return $process.ExitCode
}

# =============================================================================
# Profile: Slim
# =============================================================================
function Test-Protected([string]$Name) {
    foreach ($pattern in $ProtectedPackagePatterns) { if ($Name -like $pattern) { return $true } }
    return $false
}
function Test-SlimTarget([string]$Name) {
    if (Test-Protected $Name) { return $false }
    foreach ($prefix in $SlimPackagePrefixes) { if ($Name -like "$prefix*") { return $true } }
    return $false
}
function Invoke-Slim($Before) {
    $script:StepTotal = 9
    Step 'Zabezpieczam czytniki ekranu i dźwięk.' 'Securing the screen readers and sound.'
    Protect-Speech $Before

    Step 'Usuwam fabryczne aplikacje z obrazu systemu.' 'Removing factory apps from the system image.'
    # Provisioned packages: what every NEW user would get. Needs elevation.
    $provisioned = $null
    try { $provisioned = @(Get-AppxProvisionedPackage -Online -ErrorAction Stop) }
    catch { Note "Nie można odczytać pakietów w obrazie: $($_.Exception.Message)" "Cannot read the provisioned packages: $($_.Exception.Message)"; if (-not $DryRun) { $script:Failures++ } }
    $count = 0
    foreach ($package in @($provisioned)) {
        if (-not $package) { continue }
        if (-not (Test-SlimTarget $package.DisplayName)) { continue }
        $count++
        $packageName = $package.PackageName
        Invoke-Change ('remove provisioned package {0}' -f $packageName) { Remove-AppxProvisionedPackage -Online -PackageName $packageName -ErrorAction Stop | Out-Null }
    }
    Note "Pakiety w obrazie do usunięcia: $count." "Provisioned packages to remove: $count."

    Step 'Usuwam te aplikacje z konta użytkownika.' 'Removing those apps from this user account.'
    # Limitation: at first logon Windows may still be installing provisioned apps
    # for this user; one installed after this step stays (run the profile again).
    $installed = $null
    try { $installed = @(Get-AppxPackage -ErrorAction Stop) }
    catch { Note "Nie można odczytać aplikacji użytkownika: $($_.Exception.Message)" "Cannot read this user's apps: $($_.Exception.Message)"; if (-not $DryRun) { $script:Failures++ } }
    $count = 0
    foreach ($package in @($installed)) {
        if (-not $package) { continue }
        if (-not (Test-SlimTarget $package.Name)) { continue }
        if ($package.NonRemovable -or ("$($package.SignatureKind)" -eq 'System')) { Write-Log ('skipped (system or not removable): {0}' -f $package.PackageFullName); continue }
        $count++
        $fullName = $package.PackageFullName
        Invoke-Change ('remove app {0}' -f $fullName) { Remove-AppxPackage -Package $fullName -ErrorAction Stop }
    }
    Note "Aplikacje użytkownika do usunięcia: $count." "User apps to remove: $count."

    Step 'Odinstalowuję OneDrive.' 'Uninstalling OneDrive.'
    # tiny11 deletes OneDriveSetup.exe from the image; on a running system the
    # documented way is the setup's own /uninstall, which leaves system files intact.
    $setup = @((Join-Path $env:SystemRoot 'System32\OneDriveSetup.exe'), (Join-Path $env:SystemRoot 'SysWOW64\OneDriveSetup.exe')) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($setup) {
        Invoke-Change ('stop OneDrive and run "{0}" /uninstall' -f $setup) {
            Get-Process -Name OneDrive -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
            $p = Start-Process -FilePath $setup -ArgumentList '/uninstall' -PassThru -Wait
            if ($p.ExitCode -ne 0) { Write-Log ('OneDriveSetup /uninstall exit code {0}' -f $p.ExitCode) }
        }
    } else { Note 'Nie ma instalatora OneDrive w tym systemie.' 'This system has no OneDrive setup.' }
    $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    if ($null -ne (Get-RegValue $runKey 'OneDriveSetup')) { Invoke-Change 'remove the OneDriveSetup autostart entry of this user' { Remove-ItemProperty -LiteralPath $runKey -Name 'OneDriveSetup' -ErrorAction Stop } }
    foreach ($r in $SlimOneDriveRegistry) { Set-RegValue $r.Path $r.Name $r.Value }

    Step 'Wyłączam Copilota.' 'Turning Copilot off.'
    foreach ($r in $SlimCopilotRegistry) { Set-RegValue $r.Path $r.Name $r.Value }

    Step 'Wyłączam Teams i Czat.' 'Turning Teams and Chat off.'
    foreach ($r in $SlimTeamsRegistry) { Set-RegValue $r.Path $r.Name $r.Value }

    Step 'Wyłączam reklamy i polecane aplikacje.' 'Turning advertising and suggested apps off.'
    foreach ($r in $SlimRegistry) { Set-RegValue $r.Path $r.Name $r.Value 'DWord' -Optional:([bool]$r.Optional) }

    Step 'Przywracam ustawienia czytników ekranu.' 'Restoring the screen reader settings.'
    Protect-Speech $Before
    Restore-Readers $Before

    Step 'Sprawdzam mowę i dźwięk.' 'Checking speech and sound.'
    $after = Get-SpeechState
    Show-SpeechState $after 'after:'
    Compare-SpeechState $Before $after
    Note 'Zostawione mimo listy tiny11:' 'Kept although tiny11 removes them:'
    foreach ($line in $SlimKeptFromTiny11) { Write-Host ('  {0}' -f $line) }
}

# =============================================================================
# Profiles: ReviOS, AtlasOS (AME CLI + playbook)
# =============================================================================
function Get-DefenderState {
    # Read-only. $null when Microsoft Defender's cmdlets are not there (another
    # antivirus, or Defender already removed).
    try {
        $status = Get-MpComputerStatus -ErrorAction Stop
        $preference = Get-MpPreference -ErrorAction Stop
        return @{
            Tamper = [bool]$status.IsTamperProtected
            RealTime = [bool]$status.RealTimeProtectionEnabled
            Maps = [int]$preference.MAPSReporting             # 0 = cloud-delivered protection off
            Samples = [int]$preference.SubmitSamplesConsent   # 2 = never send
        }
    } catch { return $null }
}
function Test-DefenderOff($State) {
    if (-not $State) { return $true }
    return (-not $State.Tamper) -and (-not $State.RealTime) -and ($State.Maps -eq 0) -and ($State.Samples -eq 2)
}
function Disable-SecurityToggles {
    # Returns $true when the four toggles the AME CLI checks are off.
    Assert-NotDryRun
    $state = Get-DefenderState
    if (-not $state) { Note 'Microsoft Defender nie odpowiada (inny antywirus albo już usunięty); idę dalej.' 'Microsoft Defender does not answer (another antivirus, or already removed); continuing.'; return $true }
    if ($state.Tamper) {
        # r405: without the Windows Security app (Windows 11 LTSC evaluation, build 26100: the
        # package Microsoft.SecHealthUI is neither installed nor provisioned, and the
        # windowsdefender: link "needs a new app") Tamper Protection cannot be turned off at all.
        $securityApp = $null
        try { $securityApp = Get-AppxPackage -AllUsers -Name 'Microsoft.SecHealthUI' -ErrorAction Stop } catch { }
        if (-not $securityApp) {
            Announce 'Charon: ten Windows nie ma aplikacji Zabezpieczenia Windows, więc ochrony przed naruszeniami nie da się wyłączyć. Bez tego narzędzie AME nie działa: profilu nie da się tu zastosować. Windows zostaje bez zmian.' `
                'Charon: this Windows has no Windows Security app, so Tamper Protection cannot be turned off. The AME tool does not work without that: the profile cannot be applied here. Windows stays unchanged.'
            return $false
        }
        # Tamper Protection exists so that no program can do this. Only the user can.
        Start-NarratorIfSilent
        try { Start-Process 'windowsdefender://threatsettings' } catch { Write-Log ('cannot open Windows Security: {0}' -f $_.Exception.Message) }
        $deadline = (Get-Date).AddMinutes($SecurityWaitMinutes)
        $lastSpoken = [datetime]::MinValue
        while ($state -and $state.Tamper -and (Get-Date) -lt $deadline) {
            if (((Get-Date) - $lastSpoken).TotalSeconds -ge 90) {
                $lastSpoken = Get-Date
                Announce ('Charon: potrzebny jest jeden krok ręczny. Otworzyłem Zabezpieczenia Windows, ustawienia ochrony przed wirusami i zagrożeniami. Naciskaj Tab, aż Narrator powie: Ochrona przed naruszeniami, przełącznik, włączone. Naciśnij spację i potwierdź pytanie systemu. Resztę wyłączę sam. Czekam, zostało {0} minut.' -f [int]($deadline - (Get-Date)).TotalMinutes) `
                    ('Charon: one manual step is needed. I opened Windows Security, Virus and threat protection settings. Press Tab until Narrator says: Tamper Protection, toggle switch, on. Press Space and confirm the system question. I will turn the rest off myself. Waiting, {0} minutes left.' -f [int]($deadline - (Get-Date)).TotalMinutes)
            }
            Start-Sleep -Seconds 5
            $state = Get-DefenderState
        }
        if ($state -and $state.Tamper) { return $false }
    }
    Invoke-Change 'turn off real-time protection, cloud-delivered protection and sample submission (required by the AME CLI)' {
        Set-MpPreference -DisableRealtimeMonitoring $true -MAPSReporting 0 -SubmitSamplesConsent 2 -ErrorAction Stop
    }
    Start-Sleep -Seconds 3
    return (Test-DefenderOff (Get-DefenderState))
}
function Restore-SecurityToggles($Maps, $Samples) {
    # Best effort: after a playbook that removed Defender this simply fails and is logged.
    $m = $Maps; $s = $Samples
    Invoke-Change 'turn real-time protection, cloud-delivered protection and sample submission back on' {
        Set-MpPreference -DisableRealtimeMonitoring $false -MAPSReporting $m -SubmitSamplesConsent $s -ErrorAction Stop
    }
    Announce 'Ochronę przed naruszeniami w Zabezpieczeniach Windows trzeba włączyć z powrotem ręcznie; skrypt nie może tego zrobić.' `
        'Tamper Protection in Windows Security has to be turned back on by hand; a script cannot do that.'
}
function Get-PlaybookOptionList {
    $list = $PlaybookDefaultOptions[$ProfileName]
    if ($PlaybookOptions) { $list = @($PlaybookOptions -split '[,;\s]+' | Where-Object { $_ }) }
    return @($list)
}
function Show-PlaybookRequirements {
    # Read-only checks of what the playbooks list under <Requirements>. They are
    # reported, not enforced: the AME CLI does its own checking.
    $build = Get-WindowsBuild
    Note "Kompilacja Windows: $build." "Windows build: $build."
    $defender = Get-DefenderState
    if ($defender) {
        Note ("Zabezpieczenia Windows: ochrona przed naruszeniami {0}, ochrona w czasie rzeczywistym {1}, chmura {2}, próbki {3}." -f $defender.Tamper, $defender.RealTime, $defender.Maps, $defender.Samples) `
            ("Windows Security: tamper protection {0}, real-time protection {1}, cloud (MAPSReporting) {2}, samples (SubmitSamplesConsent) {3}." -f $defender.Tamper, $defender.RealTime, $defender.Maps, $defender.Samples)
        if ($defender.Tamper) { Note 'Ochrona przed naruszeniami jest włączona: będzie potrzebny jeden krok ręczny (nie da się go zautomatyzować).' 'Tamper Protection is on: one manual step will be needed (it cannot be automated).' }
    } else { Note 'Microsoft Defender nie odpowiada na zapytania.' 'Microsoft Defender does not answer queries.' }
    $pending = (Test-Path -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired') -or (Test-Path -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending')
    if ($pending) { Announce 'Uwaga: Windows czeka na restart po aktualizacji. Narzędzie AME może odmówić pracy, dopóki aktualizacje nie zostaną dokończone.' 'Warning: Windows is waiting for a restart after an update. The AME tool may refuse to work until updates are finished.' }
    $battery = $null
    try { $battery = @(Get-CimInstance -ClassName Win32_Battery -ErrorAction Stop) } catch { }
    if ($battery -and $battery.Count -gt 0 -and $battery[0].BatteryStatus -eq 1) { Announce 'Uwaga: komputer pracuje na baterii. Podłącz zasilacz; playbook tego wymaga.' 'Warning: the computer runs on battery. Plug in the charger; the playbook requires it.' }
    return $build
}
function Invoke-Playbook($Before) {
    $script:StepTotal = 8
    $source = $Downloads[$ProfileName]
    $pbUrl = $source.Url; $pbHash = $source.Sha256
    if ($PlaybookUrl) { Assert-Https $PlaybookUrl; $pbUrl = $PlaybookUrl; $pbHash = '' }     # a caller's own URL has no pinned hash
    $options = Get-PlaybookOptionList

    Step 'Zabezpieczam czytniki ekranu i dźwięk.' 'Securing the screen readers and sound.'
    Protect-Speech $Before

    Step 'Sprawdzam wymagania.' 'Checking the requirements.'
    $build = Show-PlaybookRequirements
    if (-not $PlaybookUrl -and ($PlaybookBuilds[$ProfileName] -notcontains $build)) {
        $script:ExitCode = 2
        $supported = $PlaybookBuilds[$ProfileName] -join ', '
        # r409: said in words a person knows, not only build numbers. Nothing was downloaded yet.
        $namesPl = 'Windows 11 24H2 i 25H2'; $namesEn = 'Windows 11 24H2 and 25H2'
        if ($ProfileName -eq 'ReviOS') { $namesPl = 'Windows 10 21H2 i 22H2 oraz Windows 11 23H2, 24H2 i 25H2'; $namesEn = 'Windows 10 21H2 and 22H2, and Windows 11 23H2, 24H2 and 25H2' }
        Announce "Charon: profil $ProfileName działa tylko na: $namesPl. Ten Windows ma kompilację $build (obsługiwane: $supported). Nic nie pobrano i nic nie zmieniono. Windows zostaje taki, jaki jest." `
            "Charon: the $ProfileName profile works only on: $namesEn. This Windows is build $build (supported: $supported). Nothing was downloaded and nothing was changed. Windows stays as it is."
        return
    }
    if ($DryRun) {
        # The plan only. Nothing below this block runs in -DryRun.
        Write-Host '[dry-run] would: download and verify'
        foreach ($key in @('SevenZr', 'SevenExtra', 'AmeCli')) { Write-Host ('[dry-run]   {0}  sha256 {1}' -f $Downloads[$key].Url, $(if ($Downloads[$key].Sha256) { $Downloads[$key].Sha256 } else { '(logged only)' })) }
        Write-Host ('[dry-run]   {0}  sha256 {1}' -f $pbUrl, $(if ($pbHash) { $pbHash } else { '(logged only)' }))
        Write-Host '[dry-run] would: unpack 7za.exe with 7zr.exe, the CLI with Expand-Archive, the playbook with 7za.exe (password malte)'
        Write-Host '[dry-run] would: check <SupportedBuilds> in playbook.conf against this build, drop option names the playbook does not have'
        Write-Host '[dry-run] would: wait for Tamper Protection to be turned off by hand (if on), then turn the other three Windows Security toggles off'
        Write-Host ('[dry-run] would: register the logon task "{0}" and save the Narrator settings' -f $RestoreTaskName)
        Write-Host ('[dry-run] would: run TrustedUninstaller.CLI.exe "<work folder>\cli\Playbook" {0}' -f ($options -join ' '))
        Write-Host '[dry-run] would: restore the Narrator settings, say that a restart is needed'
        return
    }
    Step 'Pobieram narzędzie AME i playbook.' 'Downloading the AME tool and the playbook.'
    Wait-Internet
    $work = New-WorkFolder $ProfileName
    $sevenZr = Join-Path $work $Downloads.SevenZr.File
    $sevenExtra = Join-Path $work $Downloads.SevenExtra.File
    $cliZip = Join-Path $work $Downloads.AmeCli.File
    $playbookFile = Join-Path $work $source.File
    [void](Get-Download $Downloads.SevenZr.Url $sevenZr $Downloads.SevenZr.Sha256 @('exe'))
    [void](Get-Download $Downloads.SevenExtra.Url $sevenExtra $Downloads.SevenExtra.Sha256 @('7z'))
    [void](Get-Download $Downloads.AmeCli.Url $cliZip $Downloads.AmeCli.Sha256 @('zip'))
    [void](Get-Download $pbUrl $playbookFile $pbHash @('zip', '7z'))

    Step 'Rozpakowuję pliki.' 'Unpacking the files.'
    $sevenDir = Join-Path $work '7z'
    & $sevenZr x $sevenExtra "-o$sevenDir" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "7zr.exe nie rozpakował pakietu 7-Zip (kod $LASTEXITCODE)." "7zr.exe could not unpack the 7-Zip package (code $LASTEXITCODE)." }
    $sevenZa = Join-Path $sevenDir 'x64\7za.exe'
    if (-not (Test-Path -LiteralPath $sevenZa)) { $sevenZa = Join-Path $sevenDir '7za.exe' }
    if (-not (Test-Path -LiteralPath $sevenZa)) { Fail 'W pakiecie 7-Zip nie ma 7za.exe.' 'The 7-Zip package has no 7za.exe.' }
    $cliDir = Join-Path $work 'cli'
    Expand-Archive -LiteralPath $cliZip -DestinationPath $cliDir -Force
    $cli = Get-ChildItem -LiteralPath $cliDir -Recurse -Filter 'TrustedUninstaller.CLI.exe' | Select-Object -First 1
    if (-not $cli) { Fail 'W archiwum AME nie ma TrustedUninstaller.CLI.exe.' 'The AME archive has no TrustedUninstaller.CLI.exe.' }
    # README: the playbook is unpacked inside the CLI folder.
    $playbookDir = Join-Path $cli.DirectoryName 'Playbook'
    & $sevenZa x $playbookFile "-p$PlaybookPassword" "-o$playbookDir" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "Nie udało się rozpakować playbooka (kod $LASTEXITCODE)." "The playbook could not be unpacked (code $LASTEXITCODE)." }
    $conf = Join-Path $playbookDir 'playbook.conf'
    if (-not (Test-Path -LiteralPath $conf)) { Fail 'W playbooku nie ma playbook.conf.' 'The playbook has no playbook.conf.' }
    $confText = Get-Content -LiteralPath $conf -Raw
    $xml = [xml]$confText
    $supported = @($xml.Playbook.SupportedBuilds.string | ForEach-Object { [int]$_ })
    Write-Log ('playbook {0} {1}, supported builds: {2}' -f $xml.Playbook.Name, $xml.Playbook.Version, ($supported -join ', '))
    if ($supported.Count -gt 0 -and ($supported -notcontains $build)) {
        $script:ExitCode = 2
        Announce "Charon: pobrany playbook $ProfileName nie obsługuje tej wersji Windows (kompilacja $build; obsługiwane: $($supported -join ', ')). Nic nie zmieniono." `
            "Charon: the downloaded $ProfileName playbook does not support this Windows version (build $build; supported: $($supported -join ', ')). Nothing was changed."
        return
    }
    $known = @()
    foreach ($option in $options) {
        if ($confText -match ('<Name>\s*' + [regex]::Escape($option) + '\s*</Name>')) { $known += $option }
        else { Note "Playbook nie ma opcji $option; pomijam ją." "The playbook has no option $option; leaving it out." }
    }

    Step 'Przygotowuję Zabezpieczenia Windows.' 'Preparing Windows Security.'
    $defenderBefore = Get-DefenderState
    $extra = @{ Profile = $ProfileName; DefenderChanged = $false; Maps = 2; Samples = 1 }
    if ($defenderBefore) { $extra.Maps = $defenderBefore.Maps; $extra.Samples = $defenderBefore.Samples }
    $keepDefender = ($known -contains 'defender-enable') -or ($known -contains 'enable-defender')
    if (-not (Test-DefenderOff $defenderBefore)) {
        if (-not (Disable-SecurityToggles)) {
            $script:ExitCode = 3
            Announce "Charon: ochrona przed naruszeniami jest nadal włączona, więc narzędzie AME nie może pracować. Profil $ProfileName nie został zastosowany; Windows zostaje bez zmian. Można uruchomić profil ponownie później." `
                "Charon: Tamper Protection is still on, so the AME tool cannot work. The $ProfileName profile was not applied; Windows is unchanged. The profile can be run again later."
            return
        }
        $extra.DefenderChanged = $keepDefender      # put back later only if the chosen options keep Defender
    }
    Save-Snapshot $Before $extra
    Install-RestoreTask

    Step 'Uruchamiam narzędzie AME. To potrwa długo.' 'Starting the AME tool. This takes a long time.'
    Announce 'Narzędzie AME samo nie mówi; pisze w tym oknie. Jeśli na końcu czeka na klawisz przed restartem, naciśnij Enter. Po restarcie Charon przywróci ustawienia Narratora przy logowaniu. Narratora włącza Control, Windows, Enter.' `
        'The AME tool does not speak; it writes in this window. If it waits for a key before restarting at the end, press Enter. After the restart Charon restores the Narrator settings at sign-in. Control, Windows, Enter turns Narrator on.'
    $arguments = '"{0}"' -f $playbookDir
    if ($known.Count) { $arguments = $arguments + ' ' + ($known -join ' ') }
    $code = Start-Tool $cli.FullName $arguments $cli.DirectoryName
    if ($code -ne 0) { $script:Failures++; Announce "Narzędzie AME zakończyło pracę z kodem $code. Szczegóły są w jego własnym oknie i dzienniku." "The AME tool ended with code $code. Details are in its own window and log." }

    Step 'Przywracam ustawienia czytników ekranu.' 'Restoring the screen reader settings.'
    Protect-Speech $Before
    Restore-Readers $Before
    if ($extra.DefenderChanged) { Restore-SecurityToggles $extra.Maps $extra.Samples }
    $after = Get-SpeechState
    Show-SpeechState $after 'after:'
    Compare-SpeechState $Before $after
    # The logon task stays: the playbook finishes its work across the restart and may
    # reset the keys again; the task restores them once more and then removes itself.

    Step 'Gotowe. Potrzebny jest restart komputera.' 'Done. The computer needs a restart.'
}

# =============================================================================
# Profile: Script
# =============================================================================
function Invoke-ScriptProfile($Before) {
    $script:StepTotal = 6
    $url = $ScriptUrl; $arguments = $ScriptArguments; $entry = $ScriptEntry
    $isDefault = -not $url
    $expected = ''
    if ($isDefault) {
        # r409: the profile names its tool and arguments ($ScriptPresets); the tool's one
        # pinned release and its hash are in $ScriptTools. -ScriptArguments (by hand)
        # replaces the arguments; a caller's own -ScriptUrl has no pinned hash.
        $preset = $ScriptPresets[$ProfileName]
        if (-not $preset) { $script:ExitCode = 2; Fail "Profil $ProfileName nie ma przypisanego skryptu. Nic nie zmieniono." "The $ProfileName profile has no script assigned. Nothing was changed." }
        $tool = $ScriptTools[$preset.Tool]
        $url = $tool.Url; $expected = $tool.Sha256
        if (-not $ScriptArgumentsGiven) { $arguments = $preset.Arguments }
        if (-not $entry) { $entry = $tool.Entry }
        $selection = $preset.Config     # a selection file written beside the script (see $ScriptPresets)
        Write-Log ('script profile {0}: tool {1}, {2}, arguments: {3}{4}' -f $ProfileName, $preset.Tool, $url, $arguments, $(if ($selection) { '; selection: ' + ($selection -join ', ') } else { '' }))
    }
    Assert-Https $url

    Step 'Zabezpieczam czytniki ekranu i dźwięk.' 'Securing the screen readers and sound.'
    Protect-Speech $Before
    $target = Join-Path $CharonData 'profile-script'
    if ($DryRun) {
        Write-Host ('[dry-run] would: download {0}' -f $url)
        if ($isDefault) { Write-Host ('[dry-run] would: compare its SHA-256 with {0} (refuse on mismatch)' -f $expected) }
        else { Write-Host '[dry-run] would: log its SHA-256 (no published hash for a caller''s own URL)' }
        Write-Host ('[dry-run] would: empty and refill {0}' -f $target)
        Write-Host ('[dry-run] would: run powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}\{1}" {2}' -f $target, $(if ($entry) { $entry } else { '<the only .ps1>' }), $arguments)
        Write-Host '[dry-run] would: restore the screen reader settings, start the reader again if the script stopped it'
        return
    }

    Step 'Pobieram skrypt.' 'Downloading the script.'
    Wait-Internet
    $work = New-WorkFolder 'script'
    $leaf = ([uri]$url).Segments[-1]
    $isPs1 = $leaf -like '*.ps1'
    $file = Join-Path $work $(if ($isPs1) { 'script.ps1' } else { 'script.zip' })
    # r409: a pinned file whose SHA-256 differs is refused inside Get-Download (said and logged);
    # nothing has been changed at that point.
    if ($isPs1) { [void](Get-Download $url $file $expected @()) } else { [void](Get-Download $url $file $expected @('zip')) }

    Step 'Rozpakowuję skrypt.' 'Unpacking the script.'
    # Only Charon's own folder is emptied.
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    if ($isPs1) { if (-not $entry) { $entry = $leaf }; Copy-Item -LiteralPath $file -Destination (Join-Path $target $entry) -Force }
    else { Expand-Archive -LiteralPath $file -DestinationPath $target -Force }
    $entryFile = $null
    if ($entry) { $entryFile = Get-ChildItem -LiteralPath $target -Recurse -Filter $entry | Select-Object -First 1 }
    else {
        $candidates = @(Get-ChildItem -LiteralPath $target -Filter '*.ps1')
        if ($candidates.Count -eq 0) { $inner = @(Get-ChildItem -LiteralPath $target -Directory); if ($inner.Count -eq 1) { $candidates = @(Get-ChildItem -LiteralPath $inner[0].FullName -Filter '*.ps1') } }
        if ($candidates.Count -eq 1) { $entryFile = $candidates[0] }
    }
    if (-not $entryFile) { Fail 'Nie wiadomo, który plik uruchomić; podaj -ScriptEntry. Nic nie zmieniono.' 'It is not clear which file to run; give -ScriptEntry. Nothing was changed.' }
    Save-Snapshot $Before @{ Profile = $ProfileName; DefenderChanged = $false }

    Step 'Uruchamiam skrypt.' 'Running the script.'
    if ($isDefault -and $selection -and -not $ScriptArgumentsGiven) {
        $selectionFile = Join-Path $entryFile.DirectoryName 'charon-selection.json'
        [IO.File]::WriteAllText($selectionFile, (ConvertTo-Json @($selection)), (New-Object Text.UTF8Encoding($false)))
        $arguments = $arguments -f $selectionFile
    }
    $commandLine = '-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $entryFile.FullName
    if ($arguments) { $commandLine = $commandLine + ' ' + $arguments }
    $code = Start-Tool (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') $commandLine $entryFile.DirectoryName
    if ($code -ne 0) { $script:Failures++; Announce "Skrypt zakończył pracę z kodem $code." "The script ended with code $code." }

    Step 'Przywracam ustawienia czytników ekranu.' 'Restoring the screen reader settings.'
    Protect-Speech $Before
    Restore-Readers $Before

    Step 'Sprawdzam mowę i dźwięk.' 'Checking speech and sound.'
    $after = Get-SpeechState
    Show-SpeechState $after 'after:'
    Compare-SpeechState $Before $after
}

# =============================================================================
# Profile: ServerDesktop (r409, Charon's own code; Windows Server only)
# =============================================================================
# A Windows Server installed "with Desktop Experience" is a desktop a blind person
# can use, except for three defaults, each changed here the way Microsoft documents:
#  - sound: on older Windows Server the Windows Audio services do not start by
#    themselves, so nothing speaks; Protect-Speech sets Audiosrv and
#    AudioEndpointBuilder to Automatic and starts them. (Measured r409: on Windows
#    Server 2025, build 26100.32230, Standard with Desktop Experience, both are
#    already Automatic and running after installation, so there this is a check.)
#  - Server Manager opens at every sign-in and takes the focus: the policy "Do not
#    display Server Manager automatically at logon" (ServerManager.admx), value
#    DoNotOpenAtLogon under HKLM\SOFTWARE\Policies\Microsoft\Windows\Server\ServerManager.
#  - shutting down asks for a reason in a dialogue of its own: the policy "Display
#    Shutdown Event Tracker" disabled (Reliability), values ShutdownReasonOn = 0 and
#    ShutdownReasonUI = 0 under HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Reliability.
# Left alone on purpose: Ctrl+Alt+Delete at sign-in (a security setting), roles and
# features (Wi-Fi needs the Wireless-Networking feature and a restart: not installed
# by Charon), Windows Update, Defender, the firewall.
$ServerDesktopRegistry = @(
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Server\ServerManager'; Name = 'DoNotOpenAtLogon'; Value = 1 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Reliability'; Name = 'ShutdownReasonOn'; Value = 0 },
    @{ Path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Reliability'; Name = 'ShutdownReasonUI'; Value = 0 }
)
function Test-WindowsServer {
    # InstallationType: "Client" on Windows 10/11, "Server" (or "Server Core") on Windows Server.
    return ("$(Get-RegValue 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' 'InstallationType')" -like 'Server*')
}
function Invoke-ServerDesktop($Before) {
    $script:StepTotal = 4
    if (-not (Test-WindowsServer)) {
        $script:ExitCode = 2
        Announce 'Charon: profil stacji roboczej jest tylko dla Windows Server, a to nie jest Windows Server. Nic nie zmieniono.' `
            'Charon: the workstation profile is for Windows Server only, and this is not Windows Server. Nothing was changed.'
        return
    }
    Step 'Włączam dźwięk i zabezpieczam czytniki ekranu.' 'Turning sound on and securing the screen readers.'
    Protect-Speech $Before

    Step 'Wyłączam otwieranie Menedżera serwera przy logowaniu.' 'Turning off Server Manager at sign-in.'
    Set-RegValue $ServerDesktopRegistry[0].Path $ServerDesktopRegistry[0].Name $ServerDesktopRegistry[0].Value

    Step 'Wyłączam pytanie o powód zamknięcia systemu.' 'Turning off the shutdown reason question.'
    Set-RegValue $ServerDesktopRegistry[1].Path $ServerDesktopRegistry[1].Name $ServerDesktopRegistry[1].Value
    Set-RegValue $ServerDesktopRegistry[2].Path $ServerDesktopRegistry[2].Name $ServerDesktopRegistry[2].Value

    Step 'Sprawdzam mowę i dźwięk.' 'Checking speech and sound.'
    Protect-Speech $Before
    Restore-Readers $Before
    $after = Get-SpeechState
    Show-SpeechState $after 'after:'
    Compare-SpeechState $Before $after
}

# =============================================================================
# -RestoreNarrator: the logon task after a playbook's restart
# =============================================================================
function Invoke-RestoreOnly {
    $snapshot = $null
    if (Test-Path -LiteralPath $SnapshotPath) { try { $snapshot = Get-Content -LiteralPath $SnapshotPath -Raw | ConvertFrom-Json } catch { Write-Log ('snapshot unreadable: {0}' -f $_.Exception.Message) } }
    if (-not $snapshot) { Note 'Brak zapisanych ustawień Narratora; ustawiam tylko wartości obowiązkowe.' 'No saved Narrator settings; setting only the mandatory values.' }
    Protect-Speech $snapshot
    if ($snapshot -and $snapshot.DefenderChanged) { Restore-SecurityToggles $snapshot.Maps $snapshot.Samples }
    $state = Get-SpeechState
    Show-SpeechState $state 'restored:'
    if ($snapshot) { Compare-SpeechState $snapshot $state }
    # Narrator was running before the profile, or is configured to start: start it now.
    $wanted = (Test-HasToken $state.UserNarrator 'narrator') -or ($snapshot -and $snapshot.NarratorRunning)
    if ($wanted) { Start-NarratorIfSilent }
    Remove-RestoreTask
    Announce 'Charon: ustawienia Narratora przywrócone po zmianie profilu Windows. Narratora włącza Control, Windows, Enter.' `
        'Charon: Narrator settings restored after the Windows profile. Control, Windows, Enter turns Narrator on.'
}

# =============================================================================
# Main
# =============================================================================
function Invoke-Main {
    if ($DryRun) { Write-Host '[dry-run] Read-only queries only. Nothing is downloaded, written, removed or started; no log file.' }
    Write-Log ('---- Charon-WindowsProfile: profile {0}, restore {1}, user {2}, build {3}' -f $ProfileName, [bool]$RestoreNarrator, [Environment]::UserName, (Get-WindowsBuild))
    $admin = Test-Admin
    if ($RestoreNarrator) {
        if (-not $admin) { Note 'Bez uprawnień administratora część ustawień może się nie zapisać.' 'Without administrator rights some settings may not be written.' }
        Invoke-RestoreOnly
        return
    }
    if ($ProfileName -eq 'None') {
        Note 'Profil: brak. Nic nie zmieniam.' 'Profile: none. Nothing is changed.'
        if ($DryRun) { Show-SpeechState (Get-SpeechState) 'now:' }
        return
    }
    if (-not $admin) {
        if ($DryRun) { Note 'Uwaga: bez uprawnień administratora część odczytów się nie uda (pakiety w obrazie).' 'Note: without administrator rights some queries fail (provisioned packages).' }
        else { $script:ExitCode = 2; Fail 'Uruchom jako administrator. Nic nie zmieniono.' 'Run as administrator. Nothing was changed.' }
    }
    if (-not $Yes -and -not $DryRun) {
        Announce ("Charon zmieni ten Windows według profilu {0}. Aby kontynuować, wpisz TAK i naciśnij Enter." -f (ProfileLabel $true)) ("Charon will change this Windows according to the {0} profile. Type YES and press Enter to continue." -f (ProfileLabel $false))
        $answer = Read-Host $(if ($UsePolish) { 'Aby kontynuować, wpisz TAK' } else { 'Type YES to continue' })
        if ($answer -notin @('TAK', 'tak', 'YES', 'yes')) { $script:ExitCode = 2; Fail 'Nic nie zmieniono.' 'Nothing was changed.' }
    }
    $before = Get-SpeechState
    Show-SpeechState $before 'before:'
    $script:Started = $true
    switch ($ProfileName) {
        'Slim' { Invoke-Slim $before }
        'ReviOS' { Invoke-Playbook $before }
        'AtlasOS' { Invoke-Playbook $before }
        'Script' { Invoke-ScriptProfile $before }
        'DebloatLite' { Invoke-ScriptProfile $before }
        'NoAI' { Invoke-ScriptProfile $before }
        'WinUtil' { Invoke-ScriptProfile $before }
        'WinUtilMinimal' { Invoke-ScriptProfile $before }
        'ServerDesktop' { Invoke-ServerDesktop $before }
    }
    if ($script:ExitCode -ne 0) { return }
    if ($DryRun) { Write-Host '[dry-run] End of the plan. Nothing was changed.'; return }
    if ($script:Failures -gt 0) {
        Announce ("Charon: profil {0} zastosowany, ale {1} kroków się nie udało. Szczegóły są w dzienniku {2}." -f (ProfileLabel $true), $script:Failures, $LogPath) `
            ("Charon: the {0} profile was applied, but {1} steps failed. Details are in the log {2}." -f (ProfileLabel $false), $script:Failures, $LogPath)
    } else {
        Announce ("Charon: profil {0} zastosowany." -f (ProfileLabel $true)) ("Charon: the {0} profile was applied." -f (ProfileLabel $false))
    }
    if ($Restart) {
        Announce 'Komputer uruchomi się ponownie za minutę.' 'The computer restarts in one minute.'
        Invoke-Change 'restart in 60 seconds' { & (Join-Path $env:SystemRoot 'System32\shutdown.exe') /r /t 60 /c 'Charon' | Out-Null }
    } else {
        Announce 'Uruchom komputer ponownie, żeby zmiany zadziałały. Charon sam go nie restartuje.' 'Restart the computer for the changes to take effect. Charon does not restart it by itself.'
    }
}

Initialize-Speech
try {
    Invoke-Main | Out-Null
} catch {
    if ($script:ExitCode -eq 0) { $script:ExitCode = 1 }
    Write-Log ('ERROR: {0}' -f $_.Exception.Message)
    Write-Host ('Charon: {0}' -f $_.Exception.Message)
    Speak ('Charon: błąd. {0}' -f $_.Exception.Message) ('Charon: error. {0}' -f $_.Exception.Message)
    # Whatever went wrong, the reader's hotkey and the audio services are put back.
    if (-not $DryRun -and $script:Started) { try { Protect-Speech $null } catch { Write-Log ('protect after error failed: {0}' -f $_.Exception.Message) } }
} finally {
    Write-Log ('exit code {0}, failed steps {1}' -f $script:ExitCode, $script:Failures)
    if ($script:Synth) { try { $script:Synth.Dispose() } catch { } }
}
exit $script:ExitCode
