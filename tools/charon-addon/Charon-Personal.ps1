<#
Charon r410: applies the answers of the detailed install mode in the installed Windows. GPL-2.0-or-later.

  Charon-Personal.ps1 -Answers C:\ProgramData\Charon\answers.ini -Yes     (first logon, elevated)
  Charon-Personal.ps1 -Answers FILE -DryRun                               prints what would be done

answers.ini holds only the answers that differ from the defaults (key=value lines written by Charon).
Every key has one function Apply-<key>; tools/charon-install/charon_questions.py refuses to build when a
question has no function here or a function here has no question. The result of every key is written to
%ProgramData%\Charon\personal-report.txt (key=applied|skipped|failed: text). Nothing is ignored in silence.
Per-user values are written for the signed-in user and into the default user's hive, so that accounts made
later start with them too.
#>
param([string]$Answers = "$env:ProgramData\Charon\answers.ini", [switch]$Yes, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Dir = Join-Path $env:ProgramData 'Charon'
$Report = Join-Path $Dir 'personal-report.txt'
$Log = Join-Path $Dir 'personal.log'
$script:Status = ''; $script:Text = ''
$script:NvdaChanges = @()

function Say([string]$t) { if ($DryRun) { Write-Host $t } else { Add-Content -LiteralPath $Log -Value $t -Encoding UTF8 } }
function Ok([string]$t) { $script:Status = 'applied'; $script:Text = $t }
function Skip([string]$t) { $script:Status = 'skipped'; $script:Text = $t }
function Fail([string]$t) { $script:Status = 'failed'; $script:Text = $t }
function Set-Reg([string]$Path, [string]$Name, $Value, [string]$Type = 'DWord') {
    if ($DryRun) { Say "REG $Path $Name=$Value ($Type)"; return }
    if (-not (Test-Path -LiteralPath $Path)) { New-Item -Path $Path -Force | Out-Null }
    New-ItemProperty -LiteralPath $Path -Name $Name -Value $Value -PropertyType $Type -Force | Out-Null
}
# A per-user value: for the signed-in user and for the default user's hive (loaded as HKU\CharonDefault).
function Set-UserReg([string]$SubKey, [string]$Name, $Value, [string]$Type = 'DWord') {
    Set-Reg "HKCU:\$SubKey" $Name $Value $Type
    if ($DryRun) { Say "REG (default user) $SubKey $Name=$Value"; return }
    if ($script:DefaultHive) { Set-Reg "Registry::HKEY_USERS\CharonDefault\$SubKey" $Name $Value $Type }
}
function Run-Exe([string]$File, [string[]]$Arguments) {
    if ($DryRun) { Say ("RUN $File " + ($Arguments -join ' ')); return 0 }
    $p = Start-Process -FilePath $File -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden
    Say ("$File " + ($Arguments -join ' ') + " -> " + $p.ExitCode)
    return $p.ExitCode
}
function Powercfg([string[]]$Arguments, [string]$What) {
    $code = Run-Exe "$env:SystemRoot\System32\powercfg.exe" $Arguments
    if ($code -eq 0) { Ok $What } else { Fail "powercfg $($Arguments -join ' ') ended with $code" }
}
function Nvda([string]$Section, [string]$Key, [string]$Value) {
    $script:NvdaChanges += , @($Section, $Key, $Value); Ok "nvda.ini [$Section] $Key = $Value"
}
# Writes the collected NVDA values into the user's nvda.ini. NVDA is closed first (it writes its
# configuration when it exits) and started again afterwards.
function Write-NvdaIni {
    if ($script:NvdaChanges.Count -eq 0) { return }
    $ini = Join-Path $env:APPDATA 'nvda\nvda.ini'
    if ($DryRun) { foreach ($c in $script:NvdaChanges) { Say "NVDA $ini [$($c[0])] $($c[1]) = $($c[2])" }; return }
    $exe = @("${env:ProgramFiles(x86)}\NVDA\nvda.exe", "$env:ProgramFiles\NVDA\nvda.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    $running = @(Get-Process -Name nvda -ErrorAction SilentlyContinue).Count -gt 0
    if ($running -and $exe) { Start-Process -FilePath $exe -ArgumentList '-q' -Wait; Start-Sleep -Seconds 3 }
    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path -LiteralPath $ini) { foreach ($l in [IO.File]::ReadAllLines($ini)) { $lines.Add($l) } }
    else { New-Item -ItemType Directory -Force -Path (Split-Path $ini) | Out-Null }
    foreach ($c in $script:NvdaChanges) {
        $section = $c[0]; $key = $c[1]; $value = $c[2]
        $start = -1
        for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq "[$section]") { $start = $i; break } }
        if ($start -lt 0) { $lines.Add("[$section]"); $lines.Add("`t$key = $value"); continue }
        $end = $lines.Count; $found = -1
        for ($i = $start + 1; $i -lt $lines.Count; $i++) {
            $t = $lines[$i].Trim()
            if ($t -match '^\[[^\[]') { $end = $i; break }               # the next top-level section
            if ($t -match '^\[\[') { if ($found -lt 0) { $end = $i }; break }   # a subsection: plain keys stand before it
            if ($t -match ('^' + [regex]::Escape($key) + '\s*=')) { $found = $i; break }
        }
        if ($found -ge 0) { $lines[$found] = "`t$key = $value" } else { $lines.Insert($start + 1, "`t$key = $value") }
    }
    [IO.File]::WriteAllLines($ini, $lines, (New-Object Text.UTF8Encoding($false)))
    if ($running -and $exe) { Start-Process -FilePath $exe }
}
$Advanced = 'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced'
$AU = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'

function Apply-wtimezone($v) { $c = Run-Exe "$env:SystemRoot\System32\tzutil.exe" @('/s', "`"$v`""); if ($c -eq 0) { Ok "time zone $v" } else { Fail "tzutil /s ended with $c" } }
function Apply-w_bootmenu($v) {
    $policy = 'legacy'; if ($v -eq 'tile') { $policy = 'standard' }
    $c = Run-Exe "$env:SystemRoot\System32\bcdedit.exe" @('/set', '{current}', 'bootmenupolicy', $policy)
    if ($c -eq 0) { Ok "bootmenupolicy $policy" } else { Fail "bcdedit ended with $c" }
}
function Apply-w_hibernate($v) { Powercfg @('/hibernate', $v) "hibernation $v" }
function Apply-fast_startup($v) { Set-Reg 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' 'HiberbootEnabled' 0; Ok 'HiberbootEnabled=0' }
function Apply-w_punct($v) { Nvda 'speech' 'symbolLevel' $v }
function Apply-w_layout($v) { Nvda 'keyboard' 'keyboardLayout' $v }
function Apply-w_modifier($v) {
    $mask = 0; foreach ($k in $v.Split(',')) { switch ($k) { 'capslock' { $mask = $mask -bor 1 } 'numpad' { $mask = $mask -bor 2 } 'extended' { $mask = $mask -bor 4 } } }
    if ($mask -eq 0) { Fail 'no NVDA key was chosen; NVDA needs at least one, so its default stays'; return }
    Nvda 'keyboard' 'NVDAModifierKeys' "$mask"
}
function Apply-w_typed_chars($v) { Nvda 'keyboard' 'speakTypedCharacters' $v }
function Apply-w_typed_words($v) { Nvda 'keyboard' 'speakTypedWords' $v }
function Apply-w_autolang($v) { Nvda 'speech' 'autoLanguageSwitching' $(if ($v -eq 'yes') { 'True' } else { 'False' }) }
function Apply-w_braille($v) { Nvda 'braille' 'display' $v }
function Apply-w_ducking($v) { Nvda 'audio' 'audioDuckingMode' $v }
function Apply-w_nvda_update($v) { Nvda 'update' 'autoCheck' $(if ($v -eq 'yes') { 'True' } else { 'False' }) }
function Apply-w_nvda_stats($v) { Nvda 'update' 'allowUsageStats' $(if ($v -eq 'yes') { 'True' } else { 'False' }); Nvda 'update' 'askedAllowUsageStats' 'True' }
function Apply-w_nvda_welcome($v) { Nvda 'general' 'showWelcomeDialogAtStartup' $(if ($v -eq 'yes') { 'True' } else { 'False' }) }
function Apply-w_taskbar_left($v) { if ($v -ne 'yes') { Skip 'default'; return }; Set-UserReg $Advanced 'TaskbarAl' 0; Ok 'TaskbarAl=0' }
# ---- r410, user decision 2026-11-24: six settings Microsoft does not document (sources: Win11Debloat, WinUtil).
# Each stays in the build only when a virtual-machine run shows its effect; see tests/charon-r410-install-results.txt.
function Apply-w_ext($v) {
    if ($v -ne 'yes') { Skip 'default'; return }
    Set-UserReg 'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'HideFileExt' 0; Ok 'HideFileExt=0 (unofficial setting)'
}
function Apply-w_hidden($v) {
    if ($v -ne 'yes') { Skip 'default'; return }
    Set-UserReg 'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'Hidden' 1; Ok 'Hidden=1 (unofficial setting)'
}
function Apply-w_explorer_start($v) {
    if ($v -ne 'pc') { Skip 'default'; return }
    Set-UserReg 'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' 'LaunchTo' 1; Ok 'LaunchTo=1 (unofficial setting)'
}
function Apply-w_start_sound($v) {
    if ($v -ne 'on' -and $v -ne 'off') { Skip 'default'; return }
    $d = if ($v -eq 'off') { 1 } else { 0 }
    Set-Reg 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\LogonUI\BootAnimation' 'DisableStartupSound' $d
    Set-Reg 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\EditionOverrides' 'UserSetting_DisableStartupSound' $d
    Ok "DisableStartupSound=$d, UserSetting_DisableStartupSound=$d (unofficial setting)"
}
function Apply-w_numlock($v) {
    if ($v -ne 'on' -and $v -ne 'off') { Skip 'default'; return }
    $d = if ($v -eq 'on') { '2' } else { '0' }
    Set-Reg 'Registry::HKEY_USERS\.DEFAULT\Control Panel\Keyboard' 'InitialKeyboardIndicators' $d 'String'
    Set-UserReg 'Control Panel\Keyboard' 'InitialKeyboardIndicators' $d 'String'
    Ok "InitialKeyboardIndicators=$d (unofficial setting)"
}
function Apply-w_onedrive($v) {
    if ($v -ne 'remove') { Skip 'default'; return }
    $setup = @("$env:SystemRoot\System32\OneDriveSetup.exe", "$env:SystemRoot\SysWOW64\OneDriveSetup.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $setup) { if ($DryRun) { Say 'RUN OneDriveSetup.exe /uninstall'; Ok 'dry'; return }; Skip 'this Windows has no OneDrive setup program (OneDriveSetup.exe)'; return }
    Get-Process -Name OneDrive -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $code = Run-Exe $setup @('/uninstall')
    if ($code -eq 0) { Ok 'OneDriveSetup.exe /uninstall ended with 0 (unofficial setting)' } else { Fail "OneDriveSetup.exe /uninstall ended with $code" }
}
function Apply-w_dark($v) {
    if ($v -ne 'yes') { Skip 'default'; return }
    $k = 'Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
    Set-UserReg $k 'AppsUseLightTheme' 0; Set-UserReg $k 'SystemUsesLightTheme' 0; Ok 'AppsUseLightTheme=0, SystemUsesLightTheme=0'
}
function Apply-w_ssh($v) {
    if ($v -ne 'yes') { Skip 'default'; return }
    if ($DryRun) { Say 'RUN Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0; Set-Service sshd Automatic; Start-Service sshd'; Ok 'dry'; return }
    Add-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0' | Out-Null
    Set-Service -Name sshd -StartupType Automatic; Start-Service sshd
    Ok 'OpenSSH server installed, sshd automatic and started'
}
function Apply-w_netprofile($v) {
    if ($DryRun) { Say 'RUN Get-NetConnectionProfile | Set-NetConnectionProfile -NetworkCategory Private'; Ok 'dry'; return }
    $p = @(Get-NetConnectionProfile -ErrorAction SilentlyContinue)
    if ($p.Count -eq 0) { Fail 'no network is connected, so there is no profile to mark as private'; return }
    $p | Set-NetConnectionProfile -NetworkCategory Private; Ok "network category Private for: $(($p | ForEach-Object Name) -join ', ')"
}
# r410: the note about Windows Home is written on Windows Home only (it stood in every report, on
# Enterprise too). The edition is the registry's EditionID: Core, CoreN, CoreSingleLanguage and
# CoreCountrySpecific are the Home editions; Professional, Enterprise, Education ... are not.
function Get-HomeNote {
    try { $e = [string](Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name EditionID -ErrorAction Stop).EditionID } catch { return '' }
    if ($e -match '^Core(N|SingleLanguage|CountrySpecific)?$') { return " (not honoured by Windows Home; this is edition $e)" }
    return ''
}
function Apply-w_update($v) {
    if ($v -eq 'off') { Set-Reg $AU 'NoAutoUpdate' 1; Ok "NoAutoUpdate=1$(Get-HomeNote)" }
    else { Set-Reg $AU 'NoAutoUpdate' 0; Set-Reg $AU 'AUOptions' ([int]$v); Ok "AUOptions=$v$(Get-HomeNote)" }
}
function Apply-w_update_restart($v) { if ($v -ne 'yes') { Skip 'default'; return }; Set-Reg $AU 'NoAutoRebootWithLoggedOnUsers' 1; Ok 'NoAutoRebootWithLoggedOnUsers=1' }
function Apply-w_adid($v) { if ($v -ne 'yes') { Skip 'default'; return }; Set-Reg 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\AdvertisingInfo' 'DisabledByGroupPolicy' 1; Ok 'DisabledByGroupPolicy=1' }
function Apply-w_recall($v) { if ($v -ne 'yes') { Skip 'default'; return }; Set-Reg 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsAI' 'DisableAIDataAnalysis' 1; Ok 'DisableAIDataAnalysis=1' }
function Apply-w_widgets($v) { if ($v -ne 'yes') { Skip 'default'; return }; Set-Reg 'HKLM:\SOFTWARE\Policies\Microsoft\Dsh' 'AllowNewsAndInterests' 0; Ok 'AllowNewsAndInterests=0' }
function Apply-w_sleep_ac($v) { Powercfg @('/change', 'standby-timeout-ac', $v) "standby-timeout-ac $v" }
function Apply-w_sleep_dc($v) { Powercfg @('/change', 'standby-timeout-dc', $v) "standby-timeout-dc $v" }
function Apply-w_display_off($v) { Powercfg @('/change', 'monitor-timeout-ac', $v) "monitor-timeout-ac $v"; if ($script:Status -eq 'applied') { Powercfg @('/change', 'monitor-timeout-dc', $v) "monitor-timeout-ac and -dc $v" } }
function Apply-w_lid($v) {
    Powercfg @('/setacvalueindex', 'SCHEME_CURRENT', 'SUB_BUTTONS', 'LIDACTION', $v) 'ac'
    if ($script:Status -eq 'applied') { Powercfg @('/setdcvalueindex', 'SCHEME_CURRENT', 'SUB_BUTTONS', 'LIDACTION', $v) 'dc' }
    if ($script:Status -eq 'applied') { Powercfg @('/setactive', 'SCHEME_CURRENT') "LIDACTION $v on mains and on battery" }
}
function Apply-w_power_plan($v) { Powercfg @('/setactive', $v) "power plan $v" }
function Apply-w_features($v) {
    $names = @(); foreach ($k in $v.Split(',')) { switch ($k) {
        'wsl' { $names += 'Microsoft-Windows-Subsystem-Linux', 'VirtualMachinePlatform' } 'netfx3' { $names += 'NetFx3' }
        'sandbox' { $names += 'Containers-DisposableClientVM' } 'hyperv' { $names += 'Microsoft-Hyper-V' } } }
    $bad = @()
    foreach ($n in $names) { $c = Run-Exe "$env:SystemRoot\System32\dism.exe" @('/online', '/enable-feature', "/featurename:$n", '/all', '/norestart'); if ($c -ne 0 -and $c -ne 3010) { $bad += "$n ($c)" } }
    if ($bad.Count) { Fail ("not enabled: " + ($bad -join ', ') + "; enabled: the others of " + ($names -join ', ')) } else { Ok ("enabled (in force after a restart): " + ($names -join ', ')) }
}

# ---- software sets (the catalogue: EFI\HAK\catalogue\charon.ini, copied with Charon to %ProgramData%\Charon) ----
function Read-Catalogue {
    $dirs = @($env:CHARON_CATALOGUE, (Join-Path $Dir 'EFI\HAK\catalogue'), (Join-Path $Dir 'EFI\catalogue'), (Join-Path $Dir 'catalogue')) | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'charon.ini')) }
    if (-not $dirs) { return $null }
    $items = @{}
    foreach ($name in 'charon.ini', 'site.ini') {
        $file = Join-Path @($dirs)[0] $name
        if (-not (Test-Path -LiteralPath $file)) { continue }
        $id = $null
        foreach ($line in [IO.File]::ReadAllLines($file)) {
            if ($line.StartsWith('[')) { $id = $null; if ($line.StartsWith('[item ')) { $id = $line.Substring(6).TrimEnd(']'); if (-not $items.ContainsKey($id)) { $items[$id] = @{} } }; continue }
            if ($id -and -not $line.StartsWith(';')) { $eq = $line.IndexOf('='); if ($eq -gt 0) { $items[$id][$line.Substring(0, $eq)] = $line.Substring($eq + 1) } }
        }
    }
    return $items
}
function Speak([string]$t) { Say $t; if ($DryRun) { return }; try { (New-Object -ComObject SAPI.SpVoice).Speak($t) | Out-Null } catch { } }
function Apply-w_sets($v) {
    $items = Read-Catalogue
    if (-not $items) { Fail 'the software catalogue (EFI\HAK\catalogue\charon.ini) was not found in the installed system'; return }
    $list = @()
    foreach ($set in $v.Split(',')) {
        $pick = $script:All["w_set_$set"]
        if ($pick -eq 'none') { continue }
        if ($pick) { $list += $pick.Split(',') }
        else { $list += @($items.Keys | Where-Object { $items[$_]['set'] -eq $set -and $items[$_]['default'] -eq 'yes' -and ($items[$_]['systems'] -split ',') -contains 'windows' } | Sort-Object) }
    }
    $winget = $null
    if (-not $DryRun) {
        $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
        if (-not $winget) { try { Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.DesktopAppInstaller_8wekyb3d8bbwe } catch { }; $winget = Get-Command winget.exe -ErrorAction SilentlyContinue }
        if (-not $winget) { Fail 'winget is not available in this Windows yet, so no program was installed'; return }
    }
    Speak "Charon: installing programs: $($list.Count)."
    $good = @(); $bad = @(); $other = @(); $i = 0
    foreach ($item in $list) {
        $i++; $id = $null; if ($items.ContainsKey($item)) { $id = $items[$item]['winget'] }
        Speak "$i of $($list.Count): $(if ($items.ContainsKey($item) -and $items[$item]['name.en']) { $items[$item]['name.en'] } else { $item })"
        if ($items.ContainsKey($item) -and $items[$item]['systems'] -and -not (($items[$item]['systems'] -split ',') -contains 'windows')) { $other += $item; continue }
        if (-not $id) { $bad += "$item (not available here)"; continue }
        $c = Run-Exe $(if ($winget) { $winget.Source } else { 'winget.exe' }) @('install', '--id', $id, '-e', '--silent', '--accept-package-agreements', '--accept-source-agreements', '--source', 'winget')
        if ($c -eq 0) { $good += $item } else { $bad += "$item (winget ended with $c)" }
    }
    if ($bad.Count) { Speak "Charon: some programs were not installed."; Fail ("installed: " + ($good -join ', ') + "; NOT installed: " + ($bad -join ', ')) }
    else { Speak 'Charon: the programs are installed.'; Ok ("installed: " + ($good -join ', ') + $(if ($other.Count) { '; for another system, not installed here: ' + ($other -join ', ') } else { '' })) }
}
function Apply-w_set_everyday($v) { Skip 'used by the answer w_sets' }
function Apply-w_set_blind($v) { Skip 'used by the answer w_sets' }
function Apply-w_set_tools($v) { Skip 'used by the answer w_sets' }
function Apply-w_set_create($v) { Skip 'used by the answer w_sets' }

# ---- r410 (helper es): the remaining easy rows and block E, the administrator layer ----
# Answers whose values are never written to the log or a report (said as "set"); their lines leave answers.ini after use.
$Secrets = @('w_key')
# Runs a program without writing its arguments anywhere (for a product key).
function Run-Quiet([string]$File, [string[]]$Arguments, [string]$Shown) {
    if ($DryRun) { Say "RUN $File $Shown"; return 0 }
    $p = Start-Process -FilePath $File -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden
    Say "$File $Shown -> $($p.ExitCode)"; return $p.ExitCode
}
function Get-Winget {
    if ($DryRun) { return 'winget.exe' }
    $w = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $w) { try { Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.DesktopAppInstaller_8wekyb3d8bbwe } catch { }; $w = Get-Command winget.exe -ErrorAction SilentlyContinue }
    if ($w) { return $w.Source } else { return $null }
}
function Winget-Install([string]$Id) {
    $w = Get-Winget; if (-not $w) { return -1 }
    return (Run-Exe $w @('install', '--id', $Id, '-e', '--silent', '--accept-package-agreements', '--accept-source-agreements', '--source', 'winget'))
}
function Site-Dir {
    $d = @($env:CHARON_SITE, (Join-Path $Dir 'EFI\HAK\site'), (Join-Path $Dir 'EFI\site'), (Join-Path $Dir 'site')) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } | Select-Object -First 1
    return $d
}
function Plain-Name([string]$n) { return ($n -and $n -notmatch '[\\/:]' -and $n -notmatch '\.\.') }
$Slmgr = "$env:SystemRoot\System32\slmgr.vbs"
$Cscript = "$env:SystemRoot\System32\cscript.exe"
function Apply-w_key($v) {
    if (-not $v) { Skip 'default'; return }
    if ($v -notmatch '^[A-Za-z0-9]{5}(-[A-Za-z0-9]{5}){4}$') { Fail 'not a product key of the form XXXXX-XXXXX-XXXXX-XXXXX-XXXXX'; return }
    $c = Run-Quiet $Cscript @('//NoLogo', $Slmgr, '/ipk', $v) '//NoLogo slmgr.vbs /ipk (the key)'
    if ($c -ne 0) { Fail "slmgr.vbs /ipk ended with $c"; return }
    $null = Run-Quiet $Cscript @('//NoLogo', $Slmgr, '/cpky') '//NoLogo slmgr.vbs /cpky'
    Ok 'product key installed (slmgr /ipk), then removed from the registry (slmgr /cpky)'
}
function Apply-w_ssh_key($v) {
    if (-not $v) { Skip 'default'; return }
    if ($v -notmatch '^(ssh-ed25519|ssh-rsa|ecdsa-sha2-\S+|sk-\S+) \S+') { Fail "not a public SSH key: $($v.Split(' ')[0]) ..."; return }
    $f = Join-Path $env:ProgramData 'ssh\administrators_authorized_keys'
    if ($DryRun) { Say "APPEND to $f; icacls /inheritance:r /grant *S-1-5-32-544:F /grant *S-1-5-18:F"; Ok 'dry'; return }
    New-Item -ItemType Directory -Force -Path (Split-Path $f) | Out-Null
    $have = @(); if (Test-Path -LiteralPath $f) { $have = [IO.File]::ReadAllLines($f) }
    if ($have -notcontains $v) { [IO.File]::WriteAllLines($f, @($have + $v), (New-Object Text.UTF8Encoding($false))) }
    # Microsoft's command names the groups; their SIDs work in every language of Windows
    $c = Run-Exe "$env:SystemRoot\System32\icacls.exe" @("`"$f`"", '/inheritance:r', '/grant', '*S-1-5-32-544:F', '/grant', '*S-1-5-18:F')
    if ($c -eq 0) { Ok "public key in $f (Administrators and SYSTEM only)" } else { Fail "icacls ended with $c" }
}
function Apply-w_help_tool($v) {
    if ($v -ne 'yes') { Skip 'default'; return }
    $c = Winget-Install 'BearWare.dk.TeamTalk5'
    if ($c -eq 0) { Ok 'TeamTalk 5 installed (winget BearWare.dk.TeamTalk5)' } elseif ($c -eq -1) { Fail 'winget is not available in this Windows yet' } else { Fail "winget ended with $c" }
}
function Apply-w_soft_file($v) {
    if ($v -ne 'yes') { Skip 'default'; return }
    $dirs = @($env:CHARON_CATALOGUE, (Join-Path $Dir 'EFI\HAK\catalogue'), (Join-Path $Dir 'EFI\catalogue'), (Join-Path $Dir 'catalogue')) | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ 'software.txt')) }
    if (-not $dirs) { Fail 'the list file EFI\HAK\catalogue\software.txt was not found in the installed system'; return }
    $items = Read-Catalogue; $good = @(); $bad = @(); $other = @()
    foreach ($raw in [IO.File]::ReadAllLines((Join-Path @($dirs)[0] 'software.txt'))) {
        $line = $raw.Trim(); if (-not $line -or $line.StartsWith('#')) { continue }
        if ($line -match '^winget:\s*(\S+)') { $id = $Matches[1] }
        elseif ($line -match '^(apt|dnf|pacman|aur|flatpak):') { $other += $line; continue }
        elseif ($line -match '[:/]') { $bad += "$line (not supported by this build)"; continue }
        elseif ($items -and $items.ContainsKey($line) -and $items[$line]['systems'] -and -not (($items[$line]['systems'] -split ',') -contains 'windows')) { $other += $line; continue }
        elseif ($items -and $items.ContainsKey($line) -and $items[$line]['winget']) { $id = $items[$line]['winget'] }
        else { $bad += "$line (no catalogue id for Windows)"; continue }
        $c = Winget-Install $id
        if ($c -eq 0) { $good += $id } else { $bad += "$id (winget ended with $c)" }
    }
    if ($bad.Count) { Fail ("installed: " + ($good -join ', ') + "; NOT installed: " + ($bad -join '; ')) }
    else { Ok ("installed: " + ($good -join ', ') + $(if ($other.Count) { '; for another system: ' + ($other -join '; ') } else { '' })) }
}
function Apply-w_net_static($v) {
    if (-not $v) { Skip 'default'; return }
    $p = $v.Trim() -split '\s+'
    if ($p.Count -lt 3 -or $p[0] -notmatch '^(\d+\.\d+\.\d+\.\d+)/(\d+)$') { Fail "expected ADDRESS/PREFIX GATEWAY DNS, got: $v"; return }
    $ip = $Matches[1]; $len = [int]$Matches[2]; $gw = $p[1]; $dns = $p[2].Split(',')
    if ($DryRun) { Say "RUN New-NetIPAddress $ip/$len gateway $gw; Set-DnsClientServerAddress $($dns -join ',')"; Ok 'dry'; return }
    $a = Get-NetAdapter -Physical | Where-Object Status -eq 'Up' | Select-Object -First 1
    if (-not $a) { Fail 'no connected network adapter'; return }
    Set-NetIPInterface -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -Dhcp Disabled
    Get-NetIPAddress -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Remove-NetIPAddress -Confirm:$false -ErrorAction SilentlyContinue
    Get-NetRoute -InterfaceIndex $a.ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue
    New-NetIPAddress -InterfaceIndex $a.ifIndex -IPAddress $ip -PrefixLength $len -DefaultGateway $gw | Out-Null
    Set-DnsClientServerAddress -InterfaceIndex $a.ifIndex -ServerAddresses $dns
    Ok "adapter $($a.Name): $ip/$len, gateway $gw, DNS $($dns -join ', ')"
}
function Apply-w_proxy($v) {
    if (-not $v) { Skip 'default'; return }
    if ($v -notmatch '^[A-Za-z0-9.-]+:\d+$') { Fail "expected host:port, got: $v"; return }
    $c = Run-Exe "$env:SystemRoot\System32\netsh.exe" @('winhttp', 'set', 'proxy', "proxy-server=`"$v`"")
    if ($c -eq 0) { Ok "WinHTTP proxy $v" } else { Fail "netsh winhttp set proxy ended with $c" }
}
function Apply-w_ntp_server($v) {
    if (-not $v) { Skip 'default'; return }
    if ($v -notmatch '^[A-Za-z0-9.:-]+$') { Fail "not a server name: $v"; return }
    if (-not $DryRun) { Set-Service -Name w32time -StartupType Automatic; Start-Service -Name w32time -ErrorAction SilentlyContinue }
    $c = Run-Exe "$env:SystemRoot\System32\w32tm.exe" @('/config', "/manualpeerlist:`"$v`"", '/syncfromflags:manual', '/update')
    if ($c -eq 0) { Ok "time server $v (w32tm manual peer list)" } else { Fail "w32tm /config ended with $c" }
}
$WU = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate'
function Apply-wsus($v) {
    if (-not $v) { Skip 'default'; return }
    if ($v -notmatch '^https?://[A-Za-z0-9.-]+(:\d+)?/?$') { Fail "expected http://host:port, got: $v"; return }
    Set-Reg $WU 'WUServer' $v 'String'; Set-Reg $WU 'WUStatusServer' $v 'String'; Set-Reg $AU 'UseWUServer' 1
    Ok "WSUS server $v (WUServer, WUStatusServer, UseWUServer=1)"
}
function Apply-no_wu_drivers($v) { if ($v -ne 'yes') { Skip 'default'; return }; Set-Reg $WU 'ExcludeWUDriversInQualityUpdate' 1; Ok 'ExcludeWUDriversInQualityUpdate=1' }
function Apply-kms($v) {
    if (-not $v) { Skip 'default'; return }
    if ($v -notmatch '^[A-Za-z0-9.-]+(:\d+)?$') { Fail "expected host or host:port, got: $v"; return }
    $c = Run-Exe $Cscript @('//NoLogo', $Slmgr, '/skms', $v)
    if ($c -eq 0) { Ok "KMS host $v (slmgr /skms)" } else { Fail "slmgr.vbs /skms ended with $c" }
}
function Apply-telemetry_dest($v) {
    if ($v -ne 'yes') { Skip 'default'; return }
    Set-Reg 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DataCollection' 'AllowTelemetry' 0
    $e = ''; try { $e = [string](Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name EditionID -ErrorAction Stop).EditionID } catch { }
    if ($e -and $e -notmatch 'Enterprise|Education|Server') { Ok "AllowTelemetry=0 (edition $e honours only 1: Windows treats 0 as required data)" } else { Ok "AllowTelemetry=0 (edition $e)" }
}
function Apply-w_asset_tag($v) { if (-not $v) { Skip 'default'; return }; Ok 'asset tag recorded in the applied-settings report' }
function Apply-w_root_ca($v) {
    if (-not $v) { Skip 'default'; return }
    $site = Site-Dir; if (-not $site -and -not $DryRun) { Fail 'EFI\HAK\site was not found in the installed system'; return }
    $good = @(); $bad = @()
    foreach ($c in $v.Split(',')) {
        $c = $c.Trim(); if (-not (Plain-Name $c)) { $bad += "$c (not a plain file name)"; continue }
        if ($DryRun) { Say "RUN Import-Certificate $c -CertStoreLocation Cert:\LocalMachine\Root"; $good += $c; continue }
        $f = Join-Path $site $c; if (-not (Test-Path -LiteralPath $f)) { $bad += "$c (not found)"; continue }
        try { $r = Import-Certificate -FilePath $f -CertStoreLocation 'Cert:\LocalMachine\Root'; $good += "$c ($($r.Thumbprint))" } catch { $bad += "$c ($($_.Exception.Message))" }
    }
    if ($bad.Count) { Fail ("trusted: " + ($good -join ', ') + "; not: " + ($bad -join ', ')) } else { Ok ("certificates trusted: " + ($good -join ', ')) }
}
function Apply-w_reader_cfg_file($v) {
    if (-not $v) { Skip 'default'; return }
    if (-not (Plain-Name $v)) { Fail "not a plain folder name: $v"; return }
    $target = Join-Path $env:APPDATA 'nvda'
    if ($DryRun) { Say "COPY site folder $v into $target (NVDA closed and started again)"; $script:NvdaChanges = @(); Ok 'dry'; return }
    $site = Site-Dir; $src = if ($site) { Join-Path $site $v } else { $null }
    if (-not $src -or -not (Test-Path -LiteralPath $src -PathType Container)) { Fail "the folder $v was not found in EFI\HAK\site"; return }
    $exe = @("${env:ProgramFiles(x86)}\NVDA\nvda.exe", "$env:ProgramFiles\NVDA\nvda.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    $running = @(Get-Process -Name nvda -ErrorAction SilentlyContinue).Count -gt 0
    if ($running -and $exe) { Start-Process -FilePath $exe -ArgumentList '-q' -Wait; Start-Sleep -Seconds 3 }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $src '*') -Destination $target -Recurse -Force
    $n = @(Get-ChildItem -LiteralPath $src -Recurse -File).Count
    if ($running -and $exe) { Start-Process -FilePath $exe }
    $script:NvdaChanges = @()   # the ready configuration replaces the NVDA answers of the speech group
    Ok "NVDA configuration $v deployed: $n files in $target (the NVDA answers of the speech group are not applied over it)"
}
function Apply-w_post_script($v) {
    if (-not $v) { Skip 'default'; return }
    $p = $v.Trim() -split '\s+'; $name = $p[0]; $want = if ($p.Count -gt 1) { $p[1] -replace '^sha256=', '' } else { '' }
    if (-not (Plain-Name $name) -or $name -notmatch '\.ps1$') { Fail "expected FILE.ps1 sha256=HASH, got: $v"; return }
    if ($want -notmatch '^[0-9A-Fa-f]{64}$') { Fail 'the SHA-256 of the script is missing (FILE.ps1 sha256=HASH)'; return }
    if ($DryRun) { Say "RUN $name elevated when its SHA-256 is $want"; Ok 'dry'; return }
    $site = Site-Dir; $f = if ($site) { Join-Path $site $name } else { $null }
    if (-not $f -or -not (Test-Path -LiteralPath $f)) { Fail "the script $name was not found in EFI\HAK\site"; return }
    $copy = Join-Path $Dir "post\$name"; New-Item -ItemType Directory -Force -Path (Split-Path $copy) | Out-Null; Copy-Item -LiteralPath $f -Destination $copy -Force
    if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $want.ToUpper()) { Fail "the SHA-256 of $name does not match: not run"; return }
    # r410 (es): with -File a script that ends in a NON-terminating error exits 0. The script now runs with
    # $ErrorActionPreference = Stop (inherited by the script's scope), so any error ends it with status 1; its own
    # "exit N" is kept. -EncodedCommand avoids quoting the path through the command line.
    $quoted = $copy -replace "'", "''"
    $cmd = "`$ErrorActionPreference = 'Stop'; try { & '$quoted'; if (`$LASTEXITCODE) { exit `$LASTEXITCODE } } catch { [Console]::Error.WriteLine(`$_.ToString()); exit 1 }"
    $enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($cmd))
    $c = Run-Exe "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $enc)
    if ($c -eq 0) { Ok "script $name ran, exit status 0" } else { Fail "script $name ended with exit status $c" }
}
# runs after every other answer, with the applied-settings report (main part below)
function Apply-w_phone_home($v) {
    if (-not $v) { Skip 'default'; return }
    if ($v -notmatch '^https?://') { Fail "expected an http or https address, got: $v"; return }
    if ($DryRun) { Say "POST the applied-settings report to $v"; Ok 'dry'; return }
    try { $r = Invoke-WebRequest -UseBasicParsing -Method Post -Uri $v -InFile $Installed -ContentType 'text/plain' -TimeoutSec 30; Ok "report sent to $v (HTTP $($r.StatusCode))" }
    catch { Fail "the report could not be sent to ${v}: $($_.Exception.Message)" }
}

# ---- main ----
if (-not (Test-Path -LiteralPath $Answers)) { Write-Output "No answers file ${Answers}: nothing to do."; exit 0 }
if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $Dir | Out-Null }
$script:DefaultHive = $false
if (-not $DryRun) {
    & reg.exe load 'HKU\CharonDefault' "$env:SystemDrive\Users\Default\NTUSER.DAT" 2>&1 | Out-Null
    $script:DefaultHive = ($LASTEXITCODE -eq 0)
}
# every answer first (the sets read the answers about their single programs)
$script:All = @{}
foreach ($line in [IO.File]::ReadAllLines($Answers)) { $t = $line.Trim(); $eq = $t.IndexOf('='); if ($eq -gt 0 -and -not $t.StartsWith(';')) { $script:All[$t.Substring(0, $eq).Trim()] = $t.Substring($eq + 1) } }
$lines = @()
# r410 (es): the applied-settings report of the installed system (specification 6.5): one line per answer, secrets as "set"
$Installed = Join-Path $Dir 'install-report.txt'
if ($DryRun) { $Installed = Join-Path $env:TEMP "charon-install-report-dry-$PID.txt" }
$instLines = @('# Charon: settings of the detailed mode applied in this Windows: key=value -> applied | skipped | failed: text',
    "# written $([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')); computer $env:COMPUTERNAME")
# The power plan first: the display, sleep and lid answers are written into the ACTIVE plan, so a plan chosen after
# them would leave them behind in the old one (r410 VM proof: 1 minute asked, 15 minutes in force).
$ordered = [IO.File]::ReadAllLines($Answers)
$ordered = @($ordered | Where-Object { $_.Trim().StartsWith('w_power_plan=') }) + @($ordered | Where-Object { -not $_.Trim().StartsWith('w_power_plan=') })
foreach ($line in $ordered) {
    $t = $line.Trim()
    if (-not $t -or $t.StartsWith(';') -or $t.StartsWith('#') -or $t.StartsWith('[')) { continue }
    $eq = $t.IndexOf('='); if ($eq -lt 1) { continue }
    $key = $t.Substring(0, $eq).Trim(); $value = $t.Substring($eq + 1)
    if ($key -eq 'w_phone_home') { continue }   # after everything else, with the report
    $script:Status = 'failed'; $script:Text = 'the function gave no result'
    $command = Get-Command -Name "Apply-$key" -CommandType Function -ErrorAction SilentlyContinue
    if (-not $command) { $script:Text = 'this version of Charon-Personal.ps1 does not know this answer' }
    else { try { & $command $value } catch { Fail $_.Exception.Message } }
    $shown = if ($Secrets -contains $key) { 'set' } else { $value }
    $lines += "$key=$($script:Status): $($script:Text)"
    $instLines += "$key=$shown -> $($script:Status): $($script:Text)"
    Say "$key=$shown -> $($script:Status): $($script:Text)"
}
try { Write-NvdaIni } catch { $lines += "nvda_ini=failed: $($_.Exception.Message)"; $instLines += "nvda_ini -> failed: $($_.Exception.Message)" }
[IO.File]::WriteAllLines($Installed, $instLines, (New-Object Text.UTF8Encoding($false)))
if ($script:All.ContainsKey('w_phone_home')) {
    $v = $script:All['w_phone_home']; $script:Status = 'failed'; $script:Text = 'the function gave no result'
    try { Apply-w_phone_home $v } catch { Fail $_.Exception.Message }
    $lines += "w_phone_home=$($script:Status): $($script:Text)"; $instLines += "w_phone_home=$v -> $($script:Status): $($script:Text)"
    Say "w_phone_home=$v -> $($script:Status): $($script:Text)"
    [IO.File]::WriteAllLines($Installed, $instLines, (New-Object Text.UTF8Encoding($false)))
}
if ($DryRun) { Remove-Item -LiteralPath $Installed -ErrorAction SilentlyContinue }
# a secret leaves answers.ini once it was used
if (-not $DryRun -and @($script:All.Keys | Where-Object { $Secrets -contains $_ }).Count) {
    $keep = @([IO.File]::ReadAllLines($Answers) | Where-Object { $k = ($_ -split '=', 2)[0].Trim(); -not ($Secrets -contains $k) })
    [IO.File]::WriteAllLines($Answers, $keep, (New-Object Text.UTF8Encoding($false)))
}
if ($script:DefaultHive) { [GC]::Collect(); Start-Sleep -Milliseconds 500; & reg.exe unload 'HKU\CharonDefault' 2>&1 | Out-Null }
if ($DryRun) { $lines | ForEach-Object { Write-Output $_ } } else { [IO.File]::WriteAllLines($Report, $lines, (New-Object Text.UTF8Encoding($false))) }
exit 0
