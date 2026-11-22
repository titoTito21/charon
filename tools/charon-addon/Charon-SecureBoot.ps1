<#
Charon: turning Secure Boot off with the computer maker's own interface (r408). GPL-2.0-or-later.
Dot-sourced by Install-Charon.ps1 and by other Charon tools; defines functions only, does nothing by itself.

  Get-CharonSecureBootState [-Facts HASHTABLE]
      Read-only. Maker, model, whether Secure Boot is on, and whether THIS computer has a maker's interface
      this file knows how to use (Supported), with the reason when it has not.
  Disable-CharonSecureBoot [-Polish] [-NoFirmware] [-WhatIf] [-State OBJECT]
      Explains what will happen and the risks, asks for a typed word and for the firmware (BIOS)
      supervisor password of the owner, suspends BitLocker for one restart, calls the maker's interface,
      reads the setting back and reports. Returns an object (Status, Changed, Detail).
      -NoFirmware and -WhatIf go through the whole dialogue but call neither BitLocker nor the firmware.

What this is, and what it never does
- A blind owner cannot use the firmware setup screen, and Charon is not signed by Microsoft. The makers of
  business computers publish an interface for changing firmware settings from Windows with the firmware
  supervisor password. This file uses that interface, and nothing else, to change the ONE setting "Secure
  Boot" from on to off: the same thing the owner could do in firmware setup.
- It is not a way around Secure Boot. It never writes the key databases (PK, KEK, db, dbx), never puts the
  firmware into setup or custom mode, never makes unsigned code "trusted", never sets, changes or guesses a
  password, and never acts without the typed word and the password. There is no switch that skips them.
- Without the supervisor password of the owner the firmware refuses the change; that is the maker's rule.
- Secure Boot can be switched on again the same way (or in firmware setup). With Secure Boot off the
  computer no longer checks who signed what it starts.

Makers (what their own documentation says; read 2026-11-22)
- Lenovo ThinkPad, ThinkCentre, ThinkStation: IMPLEMENTED. WMI classes in root\wmi, built into the firmware
  ("No software installation ... required": nothing is downloaded).
  https://docs.lenovocdrt.com/ref/bios/wmi/wmi_guide/
    Lenovo_BiosSetting (CurrentSetting "Item,Value"), Lenovo_BiosPasswordSettings (PasswordState, bit 1: a
    supervisor password is installed), and to change a setting, on models of 2020 and later (ThinkCentre and
    ThinkStation: 2017): Lenovo_SetBiosSetting("Item,Value"), Lenovo_WmiOpcodeInterface(
    "WmiOpcodePasswordAdmin:PASSWORD;"), Lenovo_SaveBiosSettings(); on older models
    Lenovo_SetBiosSetting("Item,Value,PASSWORD,ascii,us"), Lenovo_SaveBiosSettings("PASSWORD,ascii,us").
    "Some security-related settings can only be disabled when a Supervisor password exists. For example ...
    SecureBoot"; "A password cannot be set using this method when one does not already exist."
  https://docs.lenovocdrt.com/ref/bios/settings/thinkpad/secureboot/     SecureBoot: Disable, Enable
  https://docs.lenovocdrt.com/ref/bios/settings/thinkcentre/secure_boot/ SecureBoot: Disabled, Enabled
  https://docs.lenovocdrt.com/ref/bios/settings/thinkstation/secureboot/ SecureBoot: Enabled, Disabled
  The item's name and its values are read from the computer first (older ThinkCentre: "Secure Boot").
- Lenovo IdeaPad, Legion, Yoga, LOQ, ThinkBook and other lines: DECLINED. Lenovo documents the interface
  for the three Think lines above only.
- Dell: DECLINED, because Dell's own tool cannot do it. Dell Command | PowerShell Provider, User's Guide,
  Frequently asked questions: "How can I disable the SecureBoot feature? The SecureBoot feature can be
  disabled only from the BIOS setup screen."
  https://dl.dell.com/topicspdf/command-powershell-provider_users-guide_en-us.pdf
  So DellBIOSProvider is neither downloaded nor called.
- HP: STUB, DECLINED. HP publishes the Client Management Script Library (module HP.ClientManagement,
  https://www.powershellgallery.com/packages/HP.ClientManagement/ ; cmdlet Set-HPBIOSSettingValue -Name
  -Value -Password), but HP's developer pages could not be read on 2026-11-22, so the name and values of
  the Secure Boot setting per model, and whether HP's firmware then asks for a confirmation on its own
  screen at the next start, are NOT confirmed from HP's documentation. Not implemented until they are.
- ASUS, Acer, MSI and others: DECLINED, no documented interface known.
Because no maker's path needs a downloaded tool today, nothing is downloaded. For the day one does (HP):
Test-CharonVendorSignature checks that a downloaded file carries a valid Authenticode signature of the
maker before anything in it is used; a tool must come from the maker's own address and pass that check.

Not tried on a computer: the Lenovo calls themselves. Detection and the dialogue were tried with stand-ins.
#>

function Write-CsbText($Say, [bool]$Polish, [string]$TextPl, [string]$TextEn) {
    if ($Say) { & $Say $TextPl $TextEn } elseif ($Polish) { Write-Host $TextPl } else { Write-Host $TextEn }
}

# Read-only facts about this computer. Every query may fail (no rights, class missing): the fact then stays unknown.
function Get-CsbFacts {
    $f = @{ Manufacturer = ''; Model = ''; Family = ''; BiosVersion = ''; SecureBoot = $null; Administrator = $false
            LenovoSettings = $null; LenovoPasswordState = $null; LenovoOpcode = $false }
    try { $f.Administrator = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) } catch { }
    try { $cs = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop; $f.Manufacturer = "$($cs.Manufacturer)".Trim(); $f.Model = "$($cs.Model)".Trim(); $f.Family = "$($cs.SystemFamily)".Trim() } catch { }
    # Lenovo: Model is the machine type (21AH...); the name of the model ("ThinkPad T14 Gen 3") is the product's Version.
    try { $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop; $f.Family = ("$($f.Family) $($product.Version)").Trim() } catch { }
    try { $bios = Get-CimInstance -ClassName Win32_BIOS -ErrorAction Stop; $f.BiosVersion = "$($bios.SMBIOSBIOSVersion)".Trim(); if (-not $f.Manufacturer) { $f.Manufacturer = "$($bios.Manufacturer)".Trim() } } catch { }
    try { $f.SecureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) } catch { }
    if ($f.Manufacturer -match '(?i)lenovo') {
        try { $f.LenovoSettings = @(Get-CimInstance -Namespace 'root\wmi' -ClassName Lenovo_BiosSetting -ErrorAction Stop | ForEach-Object { "$($_.CurrentSetting)" } | Where-Object { $_ }) } catch { }
        try { $f.LenovoPasswordState = [int](Get-CimInstance -Namespace 'root\wmi' -ClassName Lenovo_BiosPasswordSettings -ErrorAction Stop | Select-Object -First 1).PasswordState } catch { }
        try { $f.LenovoOpcode = [bool](Get-CimClass -Namespace 'root\wmi' -ClassName Lenovo_WmiOpcodeInterface -ErrorAction Stop) } catch { }
    }
    return $f
}

# "SecureBoot,Enable" or "SecureBoot,Enable;[Optional:Disable,Enable]" -> the Secure Boot item, or $null.
# Only the item whose name is exactly Secure Boot (with or without the space) is ever taken: not
# "SecureBootMode", not the key or setup-mode items.
function Find-CsbLenovoItem($Settings) {
    foreach ($line in @($Settings)) {
        $main = "$line".Split(';')[0]; $parts = $main.Split(',')
        if ($parts.Count -lt 2) { continue }
        $name = $parts[0].Trim(); $value = $parts[1].Trim()
        if ($name -notmatch '^(?i)Secure ?Boot$') { continue }
        $choices = @()
        if ("$line" -match '\[Optional:([^\]]*)\]') { $choices = @($Matches[1].Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
        $off = ''
        if ($value -match '^(?i)(Enable|Disable)$') { $off = 'Disable' } elseif ($value -match '^(?i)(Enabled|Disabled)$') { $off = 'Disabled' }
        if ($choices.Count) { $listed = @($choices | Where-Object { $_ -match '^(?i)Disabled?$' }); if ($listed.Count) { $off = $listed[0] } }
        return [pscustomobject]@{ Name = $name; Value = $value; DisableValue = $off; IsOn = ($value -match '^(?i)Enabled?$') }
    }
    return $null
}

function Get-CharonSecureBootState {
    [CmdletBinding()]
    param([hashtable]$Facts)
    $f = if ($Facts) { $Facts } else { Get-CsbFacts }
    $names = "$($f.Family) $($f.Model)"
    $state = [pscustomobject]@{
        Vendor = "$($f.Manufacturer)"; Model = ("$($f.Family) $($f.Model)").Trim(); BiosVersion = "$($f.BiosVersion)"
        Line = 'Other'; SecureBoot = $f.SecureBoot; Administrator = [bool]$f.Administrator
        Supported = $false; Mechanism = 'None'; SettingName = ''; SettingValue = ''; DisableValue = ''
        SupervisorPassword = $null; UsesOpcode = [bool]$f.LenovoOpcode
        ReasonPl = ''; ReasonEn = ''
    }
    if ($state.Vendor -match '(?i)lenovo') {
        if ($names -match '(?i)Think(Pad|Centre|Station)') {
            $state.Line = 'LenovoThink'
            $item = Find-CsbLenovoItem $f.LenovoSettings
            if (-not $item -or -not $item.DisableValue) {
                $state.ReasonPl = 'Ten komputer Lenovo nie udostępnia w Windows ustawienia Secure Boot przez interfejs WMI firmy Lenovo (albo brak uprawnień administratora do jego odczytu).'
                $state.ReasonEn = 'This Lenovo computer does not offer the Secure Boot setting to Windows through Lenovo''s WMI interface (or administrator rights are missing to read it).'
            } else {
                $state.Supported = $true; $state.Mechanism = 'LenovoWmi'
                $state.SettingName = $item.Name; $state.SettingValue = $item.Value; $state.DisableValue = $item.DisableValue
                if ($null -ne $f.LenovoPasswordState) { $state.SupervisorPassword = (([int]$f.LenovoPasswordState -band 2) -ne 0) }
                if ($null -eq $state.SecureBoot) { $state.SecureBoot = [bool]$item.IsOn }
            }
        } else {
            $state.Line = 'LenovoOther'
            $state.ReasonPl = 'Lenovo opisuje interfejs do zmiany ustawień firmware z Windows tylko dla linii ThinkPad, ThinkCentre i ThinkStation. Ten model (IdeaPad, Legion, Yoga, LOQ, ThinkBook i podobne) go nie ma.'
            $state.ReasonEn = 'Lenovo documents its interface for changing firmware settings from Windows for ThinkPad, ThinkCentre and ThinkStation only. This model (IdeaPad, Legion, Yoga, LOQ, ThinkBook and the like) does not have it.'
        }
    } elseif ($state.Vendor -match '(?i)^dell') {
        $state.Line = 'Dell'
        $state.ReasonPl = 'Dell podaje w dokumentacji swojego narzędzia (Dell Command PowerShell Provider), że Secure Boot można wyłączyć tylko na ekranie ustawień BIOS. Narzędzie Della tego nie zrobi, więc go nie pobieram ani nie uruchamiam.'
        $state.ReasonEn = 'Dell states in the documentation of its own tool (Dell Command PowerShell Provider) that Secure Boot can be disabled only from the BIOS setup screen. Dell''s tool cannot do it, so it is neither downloaded nor run.'
    } elseif ($state.Vendor -match '(?i)^(hp|hewlett)') {
        $state.Line = 'HP'
        $state.ReasonPl = 'Dla komputerów HP ta funkcja nie jest jeszcze zrobiona: nazwy ustawienia w narzędziach HP i tego, czy firmware HP żąda potem potwierdzenia na własnym ekranie, nie udało się potwierdzić w dokumentacji HP.'
        $state.ReasonEn = 'For HP computers this is not implemented yet: the setting names in HP''s tools, and whether HP''s firmware then asks for a confirmation on its own screen, could not be confirmed from HP''s documentation.'
    } else {
        if ($state.Vendor -match '(?i)asus') { $state.Line = 'Asus' }
        $state.ReasonPl = 'Producent tego komputera nie udostępnia udokumentowanego sposobu wyłączenia Secure Boot z Windows.'
        $state.ReasonEn = 'The maker of this computer offers no documented way to turn Secure Boot off from Windows.'
    }
    return $state
}

# One call of a Lenovo WMI method, as in Lenovo's guide for Windows PowerShell 5.1:
#   (Get-WmiObject -Class CLASS -Namespace root\wmi).METHOD("ARGUMENT")   -> .return is "Success" or the reason.
# The ONLY place in this file that changes anything in firmware. Tests replace this function with a stand-in.
function Invoke-CsbLenovoMethod([string]$Class, [string]$Method, [string]$Argument, [bool]$WithArgument = $true) {
    $object = Get-WmiObject -Namespace 'root\wmi' -Class $Class -ErrorAction Stop | Select-Object -First 1
    if (-not $object) { throw "WMI class $Class has no instance on this computer." }
    if ($WithArgument) { $result = $object.$Method.Invoke($Argument) } else { $result = $object.$Method.Invoke() }
    return "$($result.return)"
}

# Lenovo's documented sequence for one setting. Returns @{ Ok; Steps }: Steps never holds the password.
function Invoke-CsbLenovoChange([string]$Name, [string]$Value, [string]$Password, [bool]$UsesOpcode) {
    if ($Name -notmatch '^(?i)Secure ?Boot$' -or $Value -notmatch '^(?i)Disabled?$') { throw 'Internal error: only the Secure Boot item may be set, and only to Disable.' }
    $steps = @()
    if ($UsesOpcode) {
        $r = Invoke-CsbLenovoMethod 'Lenovo_SetBiosSetting' 'SetBiosSetting' "$Name,$Value"; $steps += "SetBiosSetting($Name,$Value): $r"
        if ($r -ne 'Success') { return @{ Ok = $false; Steps = $steps } }
        $r = Invoke-CsbLenovoMethod 'Lenovo_WmiOpcodeInterface' 'WmiOpcodeInterface' "WmiOpcodePasswordAdmin:$Password;"; $steps += "WmiOpcodeInterface(WmiOpcodePasswordAdmin): $r"
        if ($r -ne 'Success') { return @{ Ok = $false; Steps = $steps } }
        $r = Invoke-CsbLenovoMethod 'Lenovo_SaveBiosSettings' 'SaveBiosSettings' '' $false; $steps += "SaveBiosSettings(): $r"
        return @{ Ok = ($r -eq 'Success'); Steps = $steps }
    }
    # Older models: the password travels inside both calls, and their format has no way to carry a comma or a semicolon.
    if ($Password -match '[,;]') { return @{ Ok = $false; Steps = @('the legacy interface cannot carry a password that contains a comma or a semicolon') } }
    $r = Invoke-CsbLenovoMethod 'Lenovo_SetBiosSetting' 'SetBiosSetting' "$Name,$Value,$Password,ascii,us"; $steps += "SetBiosSetting($Name,$Value,password): $r"
    if ($r -ne 'Success') { return @{ Ok = $false; Steps = $steps } }
    $r = Invoke-CsbLenovoMethod 'Lenovo_SaveBiosSettings' 'SaveBiosSettings' "$Password,ascii,us"; $steps += "SaveBiosSettings(password): $r"
    return @{ Ok = ($r -eq 'Success'); Steps = $steps }
}

# BitLocker on the Windows drive: $true protection on, $false off, $null cannot tell.
function Get-CsbBitLockerProtection {
    try { return ("$((Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop).ProtectionStatus)" -eq 'On') } catch { }
    try {
        $volume = Get-CimInstance -Namespace 'root\CIMV2\Security\MicrosoftVolumeEncryption' -ClassName Win32_EncryptableVolume -Filter ("DriveLetter='{0}'" -f $env:SystemDrive) -ErrorAction Stop
        return [bool]($volume -and ([int]$volume.ProtectionStatus -eq 1))
    } catch { }
    return $null
}
function Suspend-CsbBitLocker {
    try { Suspend-BitLocker -MountPoint $env:SystemDrive -RebootCount 1 -ErrorAction Stop | Out-Null; return $true } catch { }
    try {
        & (Join-Path $env:SystemRoot 'System32\manage-bde.exe') -protectors -disable $env:SystemDrive -RebootCount 1 | Out-Null
        return ($LASTEXITCODE -eq 0)
    } catch { return $false }
}

# True when FILE has a valid Authenticode signature whose signer's subject matches SIGNER (a regular
# expression, e.g. '^CN=HP Inc\.'). For a maker's tool that had to be downloaded: nothing in the file is
# used before this says true. (No maker's path in this file downloads anything today.)
function Test-CharonVendorSignature([string]$Path, [string]$Signer) {
    try {
        $signature = Get-AuthenticodeSignature -LiteralPath $Path -ErrorAction Stop
        return ("$($signature.Status)" -eq 'Valid') -and $signature.SignerCertificate -and ($signature.SignerCertificate.Subject -match $Signer)
    } catch { return $false }
}

function Disable-CharonSecureBoot {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [switch]$Polish,
        [switch]$NoFirmware,           # the whole dialogue, but neither BitLocker nor the firmware is called
        $State,                        # from Get-CharonSecureBootState (tests pass their own)
        [scriptblock]$Say,             # { param($textPl, $textEn) } - where the texts go (default: the console)
        [scriptblock]$Ask,             # { param($prompt) } - returns the typed line (default: Read-Host)
        [scriptblock]$AskSecret        # { param($prompt) } - returns a SecureString (default: Read-Host -AsSecureString)
    )
    # (Not a two-letter "pl" name: this file is dot-sourced, and such a variable here would hide the caller's language flag.)
    $polishTexts = [bool]$Polish
    if (-not $Ask) { $Ask = { param($prompt) Read-Host $prompt } }
    if (-not $AskSecret) { $AskSecret = { param($prompt) Read-Host $prompt -AsSecureString } }
    if (-not $State) { $State = Get-CharonSecureBootState }
    $result = [pscustomobject]@{ Status = ''; Changed = $false; Vendor = $State.Vendor; Model = $State.Model; Setting = $State.SettingName; Detail = '' }
    $other = @('Zostaje ekran ustawień firmware (z pomocą osoby widzącej albo aplikacji czytającej ekran aparatem) albo start przez shim z własnym kluczem MOK; wtedy Secure Boot zostaje włączony.',
               'What remains is the firmware setup screen (with a sighted helper or an app that reads the screen through the camera), or starting through shim with your own MOK key, which keeps Secure Boot on.')

    if ($State.SecureBoot -eq $false) {
        Write-CsbText $Say $polishTexts 'Secure Boot jest już wyłączony. Nic nie zmieniam.' 'Secure Boot is already off. Nothing is changed.'
        $result.Status = 'AlreadyOff'; return $result
    }
    if (-not $State.Supported) {
        Write-CsbText $Say $polishTexts ("Nie wyłączę Secure Boot z Windows na tym komputerze ({0}). {1} {2}" -f $State.Model, $State.ReasonPl, $other[0]) ("Secure Boot will not be turned off from Windows on this computer ({0}). {1} {2}" -f $State.Model, $State.ReasonEn, $other[1])
        $result.Status = 'NotSupported'; $result.Detail = $State.ReasonEn; return $result
    }
    if ($State.SupervisorPassword -eq $false) {
        Write-CsbText $Say $polishTexts ("Ten komputer nie ma ustawionego hasła nadzorcy firmware (Supervisor Password). Lenovo pozwala wyłączyć Secure Boot z Windows tylko wtedy, gdy takie hasło istnieje, a z Windows nie da się go założyć: trzeba je ustawić raz na ekranie ustawień firmware. Nic nie zmieniam. " + $other[0]) `
            ("This computer has no firmware supervisor password. Lenovo allows Secure Boot to be turned off from Windows only when such a password exists, and it cannot be created from Windows: it has to be set once in the firmware setup screen. Nothing is changed. " + $other[1])
        $result.Status = 'NoSupervisorPassword'; return $result
    }
    if (-not $State.Administrator -and -not $NoFirmware) {
        Write-CsbText $Say $polishTexts 'Do zmiany ustawień firmware potrzebne są uprawnienia administratora. Nic nie zmieniam.' 'Changing firmware settings needs administrator rights. Nothing is changed.'
        $result.Status = 'NotAdministrator'; return $result
    }

    # ---- what will happen, and the risks, BEFORE anything is asked
    Write-CsbText $Say $polishTexts ("Mogę wyłączyć Secure Boot na tym komputerze ({0}) interfejsem firmy Lenovo, tym samym ustawieniem, które jest na ekranie ustawień firmware: {1} z {2} na {3}. Potrzebne jest twoje hasło nadzorcy firmware; bez niego firmware odmówi." -f $State.Model, $State.SettingName, $State.SettingValue, $State.DisableValue) `
        ("I can turn Secure Boot off on this computer ({0}) with Lenovo's interface, the same setting that is in the firmware setup screen: {1} from {2} to {3}. Your firmware supervisor password is needed; without it the firmware refuses." -f $State.Model, $State.SettingName, $State.SettingValue, $State.DisableValue)
    Write-CsbText $Say $polishTexts 'Zanim się zgodzisz: 1. Z wyłączonym Secure Boot komputer nie sprawdza, kto podpisał uruchamiane programy startowe; to twoja decyzja jako właściciela i można ją cofnąć w ten sam sposób. 2. Niektóre komputery żądają przy następnym uruchomieniu potwierdzenia na ekranie firmware, którego Charon ani czytnik ekranu jeszcze nie czytają; może być potrzebna osoba widząca. 3. Zmiana Secure Boot może sprawić, że BitLocker zażąda klucza odzyskiwania; zawieszę jego ochronę na jeden restart, ale miej klucz pod ręką (konto Microsoft: aka.ms/myrecoverykey). 4. Zmiana zadziała po ponownym uruchomieniu komputera.' `
        'Before you agree: 1. With Secure Boot off the computer does not check who signed the start-up programs it runs; that is your decision as the owner and it can be undone the same way. 2. Some computers ask for a confirmation on a firmware screen at the next start, which neither Charon nor a screen reader reads yet; a sighted person may be needed. 3. Changing Secure Boot can make BitLocker ask for the recovery key; its protection will be suspended for one restart, but have the key at hand (Microsoft account: aka.ms/myrecoverykey). 4. The change takes effect after the computer restarts.'
    $word = if ($polishTexts) { 'WYLACZ' } else { 'DISABLE' }
    $question = if ($polishTexts) { "Aby wyłączyć Secure Boot, wpisz $word. Cokolwiek innego: bez zmian" } else { "To turn Secure Boot off, type $word. Anything else: no change" }
    $typed = "$(& $Ask $question)".Trim()
    if ($typed -ne $word -and -not ($polishTexts -and $typed -eq 'WYŁĄCZ')) {
        Write-CsbText $Say $polishTexts 'Secure Boot zostaje włączony. Nic nie zmieniono.' 'Secure Boot stays on. Nothing was changed.'
        $result.Status = 'NotConfirmed'; return $result
    }
    $question = if ($polishTexts) { 'Hasło nadzorcy firmware (Supervisor Password)' } else { 'Firmware supervisor password' }
    $secure = & $AskSecret $question
    if (-not $secure -or $secure.Length -eq 0) {
        Write-CsbText $Say $polishTexts 'Nie podano hasła. Nic nie zmieniono.' 'No password was given. Nothing was changed.'
        $result.Status = 'NoPassword'; return $result
    }
    if ($NoFirmware -or -not $PSCmdlet.ShouldProcess(("{0} ({1})" -f $State.Model, $State.SettingName), ("set to {0} through the maker's interface" -f $State.DisableValue))) {
        Write-CsbText $Say $polishTexts ("Tryb próbny: nie wołam ani BitLockera, ani firmware. Wywołałbym interfejs Lenovo: {0} na {1}, hasło, zapis." -f $State.SettingName, $State.DisableValue) `
            ("Trial mode: neither BitLocker nor the firmware is called. The Lenovo interface would be called: {0} to {1}, password, save." -f $State.SettingName, $State.DisableValue)
        $result.Status = 'NotPerformed'; return $result
    }

    # ---- BitLocker first
    $protected = Get-CsbBitLockerProtection
    if ($protected) {
        if (Suspend-CsbBitLocker) { Write-CsbText $Say $polishTexts 'Ochrona BitLocker zawieszona na jeden restart.' 'BitLocker protection suspended for one restart.' }
        else {
            Write-CsbText $Say $polishTexts 'Nie udało się zawiesić BitLockera, więc nie zmieniam Secure Boot: Windows zażądałby klucza odzyskiwania. Nic nie zmieniono.' 'BitLocker could not be suspended, so Secure Boot is not changed: Windows would ask for the recovery key. Nothing was changed.'
            $result.Status = 'BitLockerNotSuspended'; return $result
        }
    } elseif ($null -eq $protected) {
        Write-CsbText $Say $polishTexts 'Uwaga: nie udało się odczytać stanu BitLockera. Jeśli dysk jest zaszyfrowany, miej klucz odzyskiwania pod ręką.' 'Note: the BitLocker state could not be read. If the drive is encrypted, have the recovery key at hand.'
    }

    # ---- the maker's interface, once. A refusal is reported as it is; nothing is tried again.
    $bstr = [IntPtr]::Zero; $plain = ''; $outcome = $null
    try {
        $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
        $outcome = Invoke-CsbLenovoChange $State.SettingName $State.DisableValue $plain ([bool]$State.UsesOpcode)
    } catch {
        $outcome = @{ Ok = $false; Steps = @("error: $($_.Exception.Message)") }
    } finally {
        if ($bstr -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
        $plain = ''
    }
    $result.Detail = ($outcome.Steps -join '; ')
    if (-not $outcome.Ok) {
        Write-CsbText $Say $polishTexts ("Firmware odmówiło zmiany Secure Boot: {0}. Przy złym haśle nadzorcy Lenovo podaje Access Denied. Nie próbuję ponownie. Secure Boot zostaje włączony." -f $result.Detail) `
            ("The firmware refused to change Secure Boot: {0}. With a wrong supervisor password Lenovo answers Access Denied. No second attempt is made. Secure Boot stays on." -f $result.Detail)
        $result.Status = 'Failed'; return $result
    }
    # ---- read back
    $now = $null
    try { $now = Find-CsbLenovoItem @(Get-CimInstance -Namespace 'root\wmi' -ClassName Lenovo_BiosSetting -ErrorAction Stop | ForEach-Object { "$($_.CurrentSetting)" }) } catch { }
    $windows = $null
    try { $windows = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) } catch { }
    $read = if ($now) { $now.Value } else { '?' }
    $result.Changed = $true; $result.Status = 'Saved'
    $result.Detail = "$($result.Detail); setting now reads $read; Windows reports Secure Boot $windows until the restart"
    Write-CsbText $Say $polishTexts ("Firmware przyjęło zmianę. Ustawienie {0} ma teraz wartość {1}. Secure Boot wyłączy się po ponownym uruchomieniu komputera; do tego czasu Windows nadal podaje, że jest włączony. Jeśli przy starcie firmware zapyta o potwierdzenie na swoim ekranie, będzie potrzebna osoba widząca." -f $State.SettingName, $read) `
        ("The firmware accepted the change. The setting {0} now reads {1}. Secure Boot goes off when the computer restarts; until then Windows still reports it as on. If the firmware asks for a confirmation on its own screen at start-up, a sighted person will be needed." -f $State.SettingName, $read)
    return $result
}
