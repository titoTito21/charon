<#
Charon network for the installed Windows (first logon, r409). GPL-2.0-or-later.

Gives Windows the network connection Charon itself used, so the user does not
set it up by hand. Run elevated, by the answer file's FirstLogonCommands,
BEFORE anything that downloads (readers, Windows profile).

  Charon-Network.ps1 [-Language pl|en] [-Source DIR] [-WaitSeconds 120]
      [-NoSpeech] [-DryRun]
  By hand / for tests:
      [-SsidHex HEX -Auth open|WPA2PSK [-KeyHex HEX]]     instead of the file
      [-Address A.B.C.D -PrefixLength 24 [-Gateway G] [-Dns D1,D2] [-InterfaceAlias NAME]]
      [-ValidateOnly -ProfileOut FILE]    write the WLAN profile XML and check it
                                           against Windows' own schema; no import

Wi-Fi: Charon writes DIR
etwork.conf (DIR = this script's folder,
%ProgramData%\Charon, copied from \charon on the install media), the same
NAME=value text it gives a Linux system (src/efi/CharonWsInstall.h, NetworkConf):
      CHARON_NET_TYPE=wifi
      CHARON_WIFI_SSID=<the network name>
      CHARON_WIFI_SECURITY=open | wpa-psk
      CHARON_WIFI_PSK=<the password>                 (wpa-psk only)
The file is read, then wiped and deleted here AND on the install media if it
is still plugged in and writable; its content is never logged or printed. A WLAN profile
(Microsoft's WLAN_profile v1 schema, connectionMode auto) is imported for all
users with "netsh wlan add profile" and the temporary XML is wiped and deleted
(Windows keeps the key itself, protected); then "netsh wlan connect".
Charon joins WPA2-PSK and open networks only, so only those are written; a
64-digit hex key is given as networkKey, anything else as passPhrase.
802.1X (enterprise) networks: refused with a message, nothing is imported.

Ethernet: nothing to do, Windows asks DHCP as Charon did. Charon has no static
address or proxy of its own, so the answer file never passes one; -Address
(New-NetIPAddress, Set-DnsClientServerAddress) is for use by hand.

Speech: through NVDA when it is running (its controller client), else with a
Windows voice (System.Speech), so that only one voice talks.
Exit codes: 0 done or nothing to do, 1 error, 2 refused (bad arguments,
enterprise network), 3 no Wi-Fi adapter with a driver, 4 profile imported but
no connection within -WaitSeconds.

Microsoft documentation relied on:
  https://learn.microsoft.com/windows/win32/nativewifi/wlan-profileschema-schema
  https://learn.microsoft.com/windows/win32/nativewifi/wpa2-personal-profile-sample
  https://learn.microsoft.com/windows/win32/nativewifi/wlan-profileschema-sharedkey-security-element
  https://learn.microsoft.com/windows-server/networking/technologies/netsh/netsh-wlan   (add profile, connect)
  https://learn.microsoft.com/powershell/module/nettcpip/new-netipaddress
  https://learn.microsoft.com/powershell/module/dnsclient/set-dnsclientserveraddress
#>
[CmdletBinding()]
param(
    [string]$Source = '',
    [ValidateSet('', 'pl', 'en')][string]$Language = '',
    [string]$SsidHex = '',
    [string]$KeyHex = '',
    [string]$Auth = '',
    [string]$Address = '',
    [int]$PrefixLength = 24,
    [string]$Gateway = '',
    [string[]]$Dns = @(),
    [string]$InterfaceAlias = '',
    [int]$WaitSeconds = 120,
    [switch]$ValidateOnly,
    [string]$ProfileOut = '',
    [switch]$NoSpeech,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if (-not $Source) { $Source = $PSScriptRoot }
$CharonData = Join-Path $env:ProgramData 'Charon'
$LogPath = Join-Path $CharonData 'network.log'
$UsePolish = if ($Language) { $Language -eq 'pl' } else { (Get-UICulture).Name -like 'pl*' }
$script:Synth = $null
$script:Nvda = $false

function Write-Log([string]$Text) {
    if ($DryRun) { return }
    try {
        if (-not (Test-Path -LiteralPath $CharonData)) { New-Item -ItemType Directory -Path $CharonData -Force | Out-Null }
        Add-Content -LiteralPath $LogPath -Value ('{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Text) -Encoding UTF8
    } catch { }
}
# ---- speech: NVDA when it runs, else a Windows voice ---------------------------
function Initialize-NvdaSpeech {
    # NVDA's controller client (nvdaControllerClient.dll, in NVDA's own folder) lets a program
    # hand text to the running NVDA. Elevated or not, it reaches the NVDA of this desktop.
    if ($NoSpeech -or -not (Get-Process -Name nvda -ErrorAction SilentlyContinue)) { return $false }
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
                if (-not $root) { continue }
                $dir = Join-Path $root 'NVDA'
                if (-not (Test-Path -LiteralPath $dir)) { continue }
                $names = if ([IntPtr]::Size -eq 8) { 'nvdaControllerClient.dll', 'nvdaControllerClient64.dll' } else { 'nvdaControllerClient32.dll', 'nvdaControllerClient.dll' }
                $dll = Get-ChildItem -LiteralPath $dir -Recurse -Include $names -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch '(?i)arm64|x86' -or [IntPtr]::Size -ne 8 } | Select-Object -First 1
                if ($dll) { break }
            }
            if (-not $dll) { return $false }
            $path = $dll.FullName.Replace('\', '\\')
            Add-Type -Namespace Charon -Name NvdaClient -MemberDefinition @"
[DllImport("$path", CharSet = CharSet.Unicode)] public static extern int nvdaController_testIfRunning();
[DllImport("$path", CharSet = CharSet.Unicode)] public static extern int nvdaController_speakText(string text);
"@
        }
        return ([Charon.NvdaClient]::nvdaController_testIfRunning() -eq 0)
    } catch { Write-Log ('NVDA controller client not usable: {0}' -f $_.Exception.Message); return $false }
}
function Initialize-Speech {
    if ($NoSpeech) { return }
    $script:Nvda = Initialize-NvdaSpeech
    if ($script:Nvda) { return }
    try {
        Add-Type -AssemblyName System.Speech -ErrorAction Stop
        $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
        $voices = @($synth.GetInstalledVoices() | Where-Object { $_.Enabled })
        if ($voices.Count -eq 0) { $synth.Dispose(); return }
        $polish = $voices | Where-Object { $_.VoiceInfo.Culture.Name -like 'pl*' } | Select-Object -First 1
        $english = $voices | Where-Object { $_.VoiceInfo.Culture.Name -like 'en*' } | Select-Object -First 1
        $script:SpeakPolish = $false
        if ($UsePolish -and $polish) { $synth.SelectVoice($polish.VoiceInfo.Name); $script:SpeakPolish = $true } elseif ($english) { $synth.SelectVoice($english.VoiceInfo.Name) }
        $synth.SetOutputToDefaultAudioDevice(); $script:Synth = $synth
    } catch { $script:Synth = $null; Write-Log ('speech unavailable: {0}' -f $_.Exception.Message) }
}
function Announce([string]$TextPl, [string]$TextEn) {
    if ($UsePolish) { Write-Host $TextPl } else { Write-Host $TextEn }
    Write-Log $TextEn
    try {
        if ($script:Nvda) { [void][Charon.NvdaClient]::nvdaController_speakText($(if ($UsePolish) { $TextPl } else { $TextEn })) }
        elseif ($script:Synth) { if ($script:SpeakPolish) { $script:Synth.Speak($TextPl) } else { $script:Synth.Speak($TextEn) } }
    } catch { Write-Log ('speech failed: {0}' -f $_.Exception.Message) }
}
# ---- helpers --------------------------------------------------------------------
function ConvertFrom-Hex([string]$Hex) {
    if ($Hex.Length % 2 -or $Hex -notmatch '^[0-9a-fA-F]*$') { throw 'not hex' }
    $bytes = New-Object byte[] ($Hex.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [Convert]::ToByte($Hex.Substring($i * 2, 2), 16) }
    return , $bytes
}
function Remove-Secret([string]$Path) {
    # Overwrites, then deletes. (On flash media the old blocks may survive; the file is gone from the file system.)
    try {
        if (-not (Test-Path -LiteralPath $Path)) { return }
        $length = (Get-Item -LiteralPath $Path).Length
        if ($length -gt 0) { [IO.File]::WriteAllBytes($Path, (New-Object byte[] $length)) }
        Remove-Item -LiteralPath $Path -Force
        Write-Log ('removed {0}' -f $Path)
    } catch { Write-Log ('could not remove {0}: {1}' -f $Path, $_.Exception.Message) }
}
function Escape-Xml([string]$Text) { return [Security.SecurityElement]::Escape($Text) }
function Get-ProfileName([byte[]]$Ssid) {
    # The profile is named like the network when the name is plain text; otherwise by its hex.
    $text = $null
    try { $text = (New-Object Text.UTF8Encoding($false, $true)).GetString($Ssid) } catch { }
    if ($text -and $text -notmatch '[\x00-\x1f]' -and $text.Trim() -eq $text) { return $text }
    return ('Charon-' + (($Ssid | ForEach-Object { $_.ToString('x2') }) -join ''))
}
function New-WlanProfileXml([byte[]]$Ssid, [string]$AuthKind, [byte[]]$Key) {
    $name = Get-ProfileName $Ssid
    $hex = (($Ssid | ForEach-Object { $_.ToString('X2') }) -join '')
    $security = '<authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption>'
    if ($AuthKind -eq 'WPA2PSK') {
        $material = [Text.Encoding]::UTF8.GetString($Key)
        $keyType = if ($material -match '^[0-9a-fA-F]{64}$') { 'networkKey' } else { 'passPhrase' }
        $security = '<authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>' +
            ('<sharedKey><keyType>{0}</keyType><protected>false</protected><keyMaterial>{1}</keyMaterial></sharedKey>' -f $keyType, (Escape-Xml $material))
    }
    return ('<?xml version="1.0"?>' + "`r`n" +
        '<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">' +
        ('<name>{0}</name><SSIDConfig><SSID><hex>{1}</hex><name>{0}</name></SSID></SSIDConfig>' -f (Escape-Xml $name), $hex) +
        '<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>' +
        ('<MSM><security>{0}</security></MSM></WLANProfile>' -f $security) + "`r`n")
}
function Test-WlanProfileXml([string]$Xml) {
    # Against the schema files Windows itself ships (%SystemRoot%\L2Schemas). Returns the list of problems.
    $problems = New-Object Collections.Generic.List[string]
    $folder = Join-Path $env:SystemRoot 'L2Schemas'
    $main = Join-Path $folder 'WLAN_profile_v1.xsd'
    if (-not (Test-Path -LiteralPath $main)) { $problems.Add("no schema file $main"); return $problems }
    $settings = New-Object Xml.XmlReaderSettings
    $settings.ValidationType = [Xml.ValidationType]::Schema
    # Warnings count too: an element no loaded schema knows is only a warning to the validator.
    $settings.ValidationFlags = [Xml.Schema.XmlSchemaValidationFlags]::ReportValidationWarnings -bor [Xml.Schema.XmlSchemaValidationFlags]::ProcessIdentityConstraints
    foreach ($file in Get-ChildItem -LiteralPath $folder -Filter 'WLAN_profile_v*.xsd') { try { [void]$settings.Schemas.Add([NullString]::Value, $file.FullName) } catch { Write-Log ('schema {0} not loaded: {1}' -f $file.Name, $_.Exception.Message) } }
    try { $settings.Schemas.Compile() } catch { $problems.Add('schemas do not compile: ' + $_.Exception.Message) }
    if ($settings.Schemas.Count -eq 0) { $problems.Add('no schema could be loaded'); return $problems }
    $list = $problems
    $settings.add_ValidationEventHandler({ param($s, $e) $list.Add($e.Message) }.GetNewClosure())
    $reader = [Xml.XmlReader]::Create((New-Object IO.StringReader($Xml)), $settings)
    try { while ($reader.Read()) { } } catch { $problems.Add($_.Exception.Message) } finally { $reader.Close() }
    return $problems
}
function Test-Online {
    # Microsoft's own connectivity test address (the one Windows uses for its network status).
    try { return ((Invoke-WebRequest -Uri 'http://www.msftconnecttest.com/connecttest.txt' -UseBasicParsing -TimeoutSec 10 -ErrorAction Stop).Content -like 'Microsoft Connect Test*') } catch { return $false }
}
function Wait-Online([int]$Seconds) {
    $end = (Get-Date).AddSeconds($Seconds)
    do { if (Test-Online) { return $true }; Start-Sleep -Seconds 5 } while ((Get-Date) -lt $end)
    return $false
}
function Get-WifiAdapters { return @(Get-NetAdapter -Physical -ErrorAction SilentlyContinue | Where-Object { "$($_.PhysicalMediaType)" -match '802\.11' }) }

# ---- static address (by hand) ------------------------------------------------------
function Set-StaticAddress {
    $adapter = $null
    if ($InterfaceAlias) { $adapter = Get-NetAdapter -Name $InterfaceAlias -ErrorAction Stop }
    else { $adapter = Get-NetAdapter -Physical | Where-Object { $_.Status -eq 'Up' -and "$($_.PhysicalMediaType)" -notmatch '802\.11' } | Sort-Object ifIndex | Select-Object -First 1 }
    if (-not $adapter) { throw 'no connected Ethernet adapter' }
    Write-Log ('static address {0}/{1} gateway {2} dns {3} on {4}' -f $Address, $PrefixLength, $Gateway, ($Dns -join ','), $adapter.Name)
    if ($DryRun) { Write-Host ('[dry-run] would: set {0}/{1} on {2}' -f $Address, $PrefixLength, $adapter.Name); return }
    Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -Dhcp Disabled
    Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Remove-NetIPAddress -Confirm:$false -ErrorAction SilentlyContinue
    Get-NetRoute -InterfaceIndex $adapter.ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue
    $arguments = @{ InterfaceIndex = $adapter.ifIndex; IPAddress = $Address; PrefixLength = $PrefixLength }
    if ($Gateway) { $arguments.DefaultGateway = $Gateway }
    New-NetIPAddress @arguments | Out-Null
    if ($Dns.Count) { Set-DnsClientServerAddress -InterfaceIndex $adapter.ifIndex -ServerAddresses $Dns }
    Announce ("Charon: ustawiłem stały adres sieciowy {0}." -f $Address) ("Charon: the fixed network address {0} is set." -f $Address)
}

# ---- main ---------------------------------------------------------------------------
$exit = 0
Initialize-Speech
try {
    Write-Log '---- Charon-Network'
    # 1. The settings: arguments, else the file. The file goes away as soon as it has been read.
    $ssid = $null; $key = $null
    $file = Join-Path $Source 'network.conf'
    if (-not $SsidHex -and (Test-Path -LiteralPath $file)) {
        # NAME=value lines, the value being everything up to the end of the line, as bytes (Latin-1 keeps
        # every byte of a network name as it is).
        $latin = [Text.Encoding]::GetEncoding(28591)
        foreach ($line in ($latin.GetString([IO.File]::ReadAllBytes($file)) -split "`r?`n")) {
            if ($line -match '^CHARON_WIFI_SSID=(.+)$') { $SsidHex = (($latin.GetBytes($Matches[1]) | ForEach-Object { $_.ToString('x2') }) -join '') }
            elseif ($line -match '^CHARON_WIFI_PSK=(.+)$') { $KeyHex = (($latin.GetBytes($Matches[1]) | ForEach-Object { $_.ToString('x2') }) -join '') }
            elseif ($line -match '^CHARON_WIFI_SECURITY=(\S+)\s*$') { $Auth = $(if ($Matches[1] -eq 'wpa-psk') { 'WPA2PSK' } else { $Matches[1] }) }
            elseif ($line -match '^CHARON_NET_TYPE=(\S+)\s*$' -and $Matches[1] -ne 'wifi') { Write-Log ('network type {0}: nothing to set up' -f $Matches[1]) }
        }
        if (-not $DryRun) {
            Remove-Secret $file
            foreach ($letter in 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N') { Remove-Secret ('{0}:\charon\network.conf' -f $letter) }
        }
    }
    if ($Address) { Set-StaticAddress }
    if (-not $SsidHex) {
        Write-Log 'no Wi-Fi settings: nothing to do (Ethernet uses DHCP)'
    } else {
        if ($Auth -notin 'open', 'WPA2PSK') {
            $exit = 2
            Announce 'Charon: sieci Wi-Fi z logowaniem firmowym 802.1X nie potrafię ustawić. Połącz się z siecią ręcznie w ustawieniach Windows.' `
                'Charon: I cannot set up a Wi-Fi network with enterprise 802.1X sign-in. Connect to the network by hand in Windows Settings.'
            throw 'refused: unsupported authentication'
        }
        $ssid = ConvertFrom-Hex $SsidHex
        if ($ssid.Length -lt 1 -or $ssid.Length -gt 32) { $exit = 2; throw 'refused: the network name must have 1 to 32 bytes' }
        if ($Auth -eq 'WPA2PSK') {
            $key = ConvertFrom-Hex $KeyHex
            if ($key.Length -lt 8 -or $key.Length -gt 64) { $exit = 2; throw 'refused: the password must have 8 to 64 characters' }
        }
        $name = Get-ProfileName $ssid
        $xml = New-WlanProfileXml $ssid $Auth $key
        $problems = @(Test-WlanProfileXml $xml)
        Write-Log ('WLAN profile "{0}" ({1}): schema check: {2}' -f $name, $Auth, $(if ($problems.Count) { $problems -join ' | ' } else { 'valid' }))
        if ($ValidateOnly) {
            if ($ProfileOut) { [IO.File]::WriteAllText($ProfileOut, $xml, (New-Object Text.UTF8Encoding($false))) }
            Write-Host ('schema check: {0}' -f $(if ($problems.Count) { $problems -join ' | ' } else { 'valid' }))
            if ($problems.Count) { $exit = 1 }
        } elseif ($DryRun) {
            Write-Host ('[dry-run] would: import the WLAN profile "{0}" ({1}) for all users and connect' -f $name, $Auth)
        } else {
            # 2. The Wi-Fi service and an adapter with a driver. Drivers may still be installing at the first sign-in.
            $service = $null
            try { $service = Get-Service -Name WlanSvc -ErrorAction Stop } catch { }
            if (-not $service) {
                $exit = 3
                Announce 'Charon: ten Windows nie ma usługi sieci bezprzewodowej, więc nie mogę ustawić Wi-Fi. W Windows Server trzeba najpierw dodać funkcję Wireless LAN Service. Na razie użyj kabla.' `
                    'Charon: this Windows has no wireless network service, so I cannot set up Wi-Fi. On Windows Server the feature Wireless LAN Service must be added first. Use a cable for now.'
                throw 'no WlanSvc'
            }
            if ("$($service.Status)" -ne 'Running') { try { Start-Service -Name WlanSvc -ErrorAction Stop } catch { Write-Log ('WlanSvc did not start: {0}' -f $_.Exception.Message) } }
            $adapters = Get-WifiAdapters
            for ($i = 0; $i -lt 12 -and $adapters.Count -eq 0; $i++) { Start-Sleep -Seconds 5; $adapters = Get-WifiAdapters }
            if ($adapters.Count -eq 0) {
                $exit = 3
                Announce ("Charon: Windows nie ma sterownika do karty Wi-Fi tego komputera, więc nie mogę połączyć się z siecią {0}. Podłącz kabel sieciowy albo zainstaluj sterownik karty; potem połącz się z siecią w ustawieniach Windows. Hasła sieci nie zapisałem." -f $name) `
                    ("Charon: Windows has no driver for this computer's Wi-Fi adapter, so I cannot connect to the network {0}. Plug in a network cable or install the adapter's driver; then connect to the network in Windows Settings. I did not keep the network password." -f $name)
                throw 'no Wi-Fi adapter with a driver'
            }
            # 3. Import, wipe the XML, connect.
            $temp = Join-Path ([IO.Path]::GetTempPath()) ('charon-wlan-{0}.xml' -f [Guid]::NewGuid().ToString('N'))
            try {
                [IO.File]::WriteAllText($temp, $xml, (New-Object Text.UTF8Encoding($false)))
                $output = (& netsh.exe wlan add profile "filename=$temp" user=all 2>&1 | Out-String).Trim()
                $code = $LASTEXITCODE
            } finally { Remove-Secret $temp }
            Write-Log ('netsh wlan add profile: exit {0}: {1}' -f $code, $output)
            if ($code -ne 0) {
                $exit = 1
                Announce ("Charon: Windows nie przyjął ustawień sieci Wi-Fi {0}. Połącz się z nią ręcznie w ustawieniach Windows." -f $name) ("Charon: Windows did not accept the settings of the Wi-Fi network {0}. Connect to it by hand in Windows Settings." -f $name)
                throw 'netsh wlan add profile failed'
            }
            $output = (& netsh.exe wlan connect "name=$name" 2>&1 | Out-String).Trim()
            Write-Log ('netsh wlan connect: exit {0}: {1}' -f $LASTEXITCODE, $output)
            Announce ("Charon: łączę Windows z siecią Wi-Fi {0}." -f $name) ("Charon: connecting Windows to the Wi-Fi network {0}." -f $name)
            if (Wait-Online $WaitSeconds) {
                Announce 'Charon: Windows jest połączony z siecią.' 'Charon: Windows is connected to the network.'
            } else {
                $exit = 4
                Announce ("Charon: ustawienia sieci {0} są zapisane, ale po {1} sekundach nadal nie ma połączenia. Windows będzie próbował dalej sam." -f $name, $WaitSeconds) `
                    ("Charon: the settings of the network {0} are saved, but after {1} seconds there is still no connection. Windows keeps trying by itself." -f $name, $WaitSeconds)
            }
        }
    }
} catch {
    if ($exit -eq 0) { $exit = 1 }
    Write-Log ('ERROR: {0}' -f $_.Exception.Message)
    Write-Host ('Charon-Network: {0}' -f $_.Exception.Message)
} finally {
    $key = $null; $KeyHex = ''; $xml = $null
    Write-Log ('exit code {0}' -f $exit)
    if ($script:Synth) { try { $script:Synth.Dispose() } catch { } }
}
exit $exit
