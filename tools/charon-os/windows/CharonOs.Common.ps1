# Charon OS companion programs for Windows: shared code. GPL-2.0-or-later.
# Dot-sourced by Charon-Settings.ps1 and Charon-BootManager.ps1 (Windows PowerShell 5.1).
#
# The caller sets before dot-sourcing:
#   $script:CharonEspPath      folder standing in for the EFI system partition ('' = the real one)
#   $script:CharonFirmwareMode 'real' | 'dir' | 'none'
#   $script:CharonFirmwareDir  folder with efivarfs-style files (mode 'dir', tests only)
#   $script:CharonLanguage     'en' | 'pl' | '' (automatic)
#   $script:CharonYes          $true: confirmations are answered with yes (non-interactive runs)

$script:CharonGlobalGuid = '{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}'
$script:CharonVarSuffix = '-8be4df61-93ca-11d2-aa0d-00e098032b8c'
$script:CharonEspType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
$script:CharonVoiceName = 'Charon voice'                 # BootMenu.h firmwareVoiceName
$script:CharonVoiceFile = '\EFI\HAK\CHARONVOICE.EFI'     # BootMenu.h firmwareVoiceFile
$script:CharonRelease = 'r409'
$script:CharonEspRoot = $null
$script:CharonEspMount = $null
$script:CharonEspPartition = $null
$script:CharonGui = $false
$script:CharonPl = $false

# ---- language and plain output ------------------------------------------------
function T([string]$En, [string]$Pl) { if ($script:CharonPl) { return $Pl } else { return $En } }
function Out-Line([string]$Text) { [Console]::Out.WriteLine($Text) }
function Initialize-CharonConsole {
    # Redirected output (tests, logs) is written as UTF-8; a real console keeps its own code page.
    try { if ([Console]::IsOutputRedirected) { [Console]::OutputEncoding = New-Object Text.UTF8Encoding $false } } catch { }
}
function Set-CharonLanguage([string]$Hak) {
    if ($script:CharonLanguage -eq 'pl') { $script:CharonPl = $true; return }
    if ($script:CharonLanguage -eq 'en') { $script:CharonPl = $false; return }
    $pl = $false
    try { if ((Get-UICulture).Name -like 'pl*') { $pl = $true } } catch { }
    if ($Hak) {
        # \EFI\HAK\audio-language: the language Charon itself was installed with (BootMenu.h InstallationLocale).
        $file = [IO.Path]::Combine($Hak, 'audio-language')
        try { if ([IO.File]::Exists($file)) { $text = [IO.File]::ReadAllText($file); if ($text -match '^(?i)pl') { $pl = $true } } } catch { }
    }
    $script:CharonPl = $pl
}
function Read-CharonLine([string]$Prompt) {
    [Console]::Out.Write($Prompt + ' ')
    $line = [Console]::In.ReadLine()
    if ($null -eq $line) { Out-Line ''; return $null }
    if ([Console]::IsInputRedirected) { Out-Line $line }      # keeps transcripts of piped sessions readable
    return $line.Trim()
}
# A numbered question. Returns the number typed, 0 for back, -1 at the end of input.
function Read-CharonNumber([string]$Prompt, [int]$Maximum) {
    while ($true) {
        $line = Read-CharonLine $Prompt
        if ($null -eq $line) { return -1 }
        $n = 0
        if ([int]::TryParse($line, [ref]$n) -and $n -ge 0 -and $n -le $Maximum) { return $n }
        Out-Line (T "Type a number from 0 to $Maximum." "Wpisz liczbę od 0 do $Maximum.")
    }
}
# A menu question. Returns the number typed, 0 for back, -1 at the end of input, -2 when the
# items are to be listed again (an empty line or L). Menus are listed once; after a change
# only the changed line is printed, so a screen reader does not repeat the whole list.
function Read-CharonMenu([string]$Prompt, [int]$Maximum) {
    while ($true) {
        $line = Read-CharonLine $Prompt
        if ($null -eq $line) { return -1 }
        if ($line -eq '' -or $line -ieq 'l') { return -2 }
        $n = 0
        if ([int]::TryParse($line, [ref]$n) -and $n -ge 0 -and $n -le $Maximum) { return $n }
        Out-Line (T "Type a number from 0 to $Maximum, or L to hear the list again." "Wpisz liczbę od 0 do $Maximum albo L, aby usłyszeć listę ponownie.")
    }
}
function Test-CharonYesWord([string]$Line) { return ($Line -match '^(?i)(yes|y|tak|t)$') }
# Every change of a firmware variable is asked first. GUI: a message box whose default button is No.
function Confirm-Charon([string]$Question) {
    if ($script:CharonYes) { Out-Line $Question; Out-Line (T 'Confirmed by the -Yes switch.' 'Potwierdzone przełącznikiem -Yes.'); return $true }
    if ($script:CharonGui) {
        $r = [Windows.Forms.MessageBox]::Show($Question, (T 'Charon: confirm' 'Charon: potwierdzenie'), [Windows.Forms.MessageBoxButtons]::YesNo, [Windows.Forms.MessageBoxIcon]::Question, [Windows.Forms.MessageBoxDefaultButton]::Button2)
        return ($r -eq [Windows.Forms.DialogResult]::Yes)
    }
    Out-Line $Question
    $line = Read-CharonLine (T 'Type yes to confirm, anything else cancels:' 'Wpisz tak, aby potwierdzić; cokolwiek innego anuluje:')
    if ($null -eq $line) { return $false }
    return (Test-CharonYesWord $line)
}
# A one-line question in the dialog programs: a label, a text box (masked for a password),
# OK and Cancel. Returns the text, or $null when cancelled.
function Read-CharonGuiLine([string]$Prompt, [bool]$Secret) {
    $form = New-Object Windows.Forms.Form
    $form.Text = 'Charon'; $form.StartPosition = 'CenterScreen'; $form.ClientSize = New-Object Drawing.Size 560, 150
    $form.FormBorderStyle = 'FixedDialog'; $form.MaximizeBox = $false; $form.MinimizeBox = $false; $form.Font = [Drawing.SystemFonts]::MessageBoxFont
    $label = New-Object Windows.Forms.Label
    $label.Text = $Prompt; $label.UseMnemonic = $false; $label.Location = New-Object Drawing.Point 12, 12; $label.Size = New-Object Drawing.Size 536, 48; $label.TabIndex = 0
    $box = New-Object Windows.Forms.TextBox
    $box.Location = New-Object Drawing.Point 12, 66; $box.Size = New-Object Drawing.Size 536, 24; $box.TabIndex = 1; $box.AccessibleName = $Prompt
    if ($Secret) { $box.UseSystemPasswordChar = $true }
    $ok = New-Object Windows.Forms.Button
    $ok.Text = 'OK'; $ok.Location = New-Object Drawing.Point 352, 106; $ok.Size = New-Object Drawing.Size 92, 30; $ok.TabIndex = 2; $ok.DialogResult = [Windows.Forms.DialogResult]::OK
    $cancel = New-Object Windows.Forms.Button
    $cancel.Text = T 'Cancel' 'Anuluj'; $cancel.Location = New-Object Drawing.Point 456, 106; $cancel.Size = New-Object Drawing.Size 92, 30; $cancel.TabIndex = 3; $cancel.DialogResult = [Windows.Forms.DialogResult]::Cancel
    $form.Controls.AddRange(@($label, $box, $ok, $cancel)); $form.AcceptButton = $ok; $form.CancelButton = $cancel
    try { if ($form.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) { return $box.Text } else { return $null } } finally { $form.Dispose() }
}
function ConvertTo-CharonSecureString([string]$Text) {
    $secure = New-Object Security.SecureString
    if ($Text) { foreach ($ch in $Text.ToCharArray()) { $secure.AppendChar($ch) } }
    return $secure
}
function Show-CharonMessage([string]$Text) {
    if ($script:CharonGui) { [void][Windows.Forms.MessageBox]::Show($Text, 'Charon', [Windows.Forms.MessageBoxButtons]::OK, [Windows.Forms.MessageBoxIcon]::Information) }
    else { Out-Line $Text }
}
function Test-CharonAdministrator {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# ---- the EFI system partition, without a drive letter -------------------------
# The partition is reached through its volume GUID path (\\?\Volume{...}\), which needs
# no mount at all. Only when Windows does not offer that path is the partition attached
# to an empty folder under %ProgramData%\Charon, and that access path is removed at exit.
function Open-CharonEsp {
    if ($script:CharonEspRoot) { return $script:CharonEspRoot }
    if ($script:CharonEspPath) {
        $full = [IO.Path]::GetFullPath($script:CharonEspPath)
        if (-not [IO.Directory]::Exists($full)) { throw (T "The folder $full does not exist." "Folder $full nie istnieje.") }
        $script:CharonEspRoot = $full; return $full
    }
    if (-not (Test-CharonAdministrator)) { throw (T 'The EFI system partition needs administrator rights. Start the program with its .cmd launcher.' 'Partycja systemowa EFI wymaga uprawnień administratora. Uruchom program plikiem .cmd.') }
    $all = @(Get-Partition | Where-Object { $_.GptType -eq $script:CharonEspType })
    if ($all.Count -eq 0) { throw (T 'No EFI system partition was found (GPT disk).' 'Nie znaleziono partycji systemowej EFI (dysk GPT).') }
    $ordered = @($all | Where-Object { $_.IsSystem }) + @($all | Where-Object { -not $_.IsSystem })
    $chosen = $null; $chosenRoot = $null; $firstRoot = $null; $firstPart = $null
    foreach ($p in $ordered) {
        $root = $null
        foreach ($a in @($p.AccessPaths)) { if ($a -like '\\?\Volume{*') { $root = $a } }
        if (-not $root) { try { $v = Get-Volume -Partition $p -ErrorAction Stop; if ($v.UniqueId -like '\\?\Volume{*') { $root = $v.UniqueId } } catch { } }
        $ok = $false
        if ($root) { try { $ok = [IO.Directory]::Exists($root) } catch { $ok = $false } }
        if (-not $ok) { continue }
        if (-not $firstRoot) { $firstRoot = $root; $firstPart = $p }
        if ([IO.Directory]::Exists([IO.Path]::Combine($root, 'EFI', 'HAK'))) { $chosen = $p; $chosenRoot = $root; break }
    }
    if (-not $chosen -and $firstRoot) { $chosen = $firstPart; $chosenRoot = $firstRoot }
    if (-not $chosen) {
        # No volume path: attach the system's own partition to an empty folder (no drive letter).
        $chosen = $ordered[0]
        $dir = Join-Path $env:ProgramData ('Charon\esp-' + $PID)
        New-Item -ItemType Directory -Force $dir | Out-Null
        Add-PartitionAccessPath -DiskNumber $chosen.DiskNumber -PartitionNumber $chosen.PartitionNumber -AccessPath $dir -ErrorAction Stop
        $script:CharonEspMount = $dir; $chosenRoot = $dir + '\'
    }
    $script:CharonEspPartition = $chosen
    $script:CharonEspRoot = $chosenRoot
    return $chosenRoot
}
function Close-CharonEsp {
    if ($script:CharonEspMount) {
        try { Remove-PartitionAccessPath -DiskNumber $script:CharonEspPartition.DiskNumber -PartitionNumber $script:CharonEspPartition.PartitionNumber -AccessPath $script:CharonEspMount -ErrorAction Stop } catch { }
        try { [IO.Directory]::Delete($script:CharonEspMount) } catch { }
        $script:CharonEspMount = $null
    }
    $script:CharonEspRoot = $null
}
# \EFI\HAK under the partition root; $null when Charon is not there. FAT keeps no case, an
# ordinary test folder on another file system may: the names are matched without case.
function Get-CharonHak([string]$Root, [switch]$Create) {
    $at = $Root
    foreach ($name in 'EFI', 'HAK') {
        $next = $null
        if ([IO.Directory]::Exists($at)) { foreach ($d in [IO.Directory]::GetDirectories($at)) { if ([IO.Path]::GetFileName($d.TrimEnd('\', '/')) -ieq $name) { $next = $d } } }
        if (-not $next) {
            if (-not $Create) { return $null }
            $next = [IO.Path]::Combine($at, $name); [void][IO.Directory]::CreateDirectory($next)
        }
        $at = $next
    }
    return $at
}
function Write-CharonFileDurably([string]$Path, [byte[]]$Bytes) {
    $stream = New-Object IO.FileStream($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($Bytes, 0, $Bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
}
function Test-CharonBytesEqual([byte[]]$A, [byte[]]$B) {
    if ($null -eq $A -or $null -eq $B -or $A.Length -ne $B.Length) { return $false }
    for ($i = 0; $i -lt $A.Length; $i++) { if ($A[$i] -ne $B[$i]) { return $false } }
    return $true
}
function ConvertTo-CharonHex([byte[]]$Bytes) { return (($Bytes | ForEach-Object { $_.ToString('x2') }) -join '') }

# ---- firmware variables -----------------------------------------------------------
# 'real': kernel32 Get/SetFirmwareEnvironmentVariableExW with SeSystemEnvironmentPrivilege,
#         the method of tools/charon-addon/Install-CharonAddon.ps1.
# 'dir' : a folder of files named like Linux efivarfs entries (4 attribute bytes, then the
#         data); used by the tests, never touches firmware.
# 'none': every call fails with a plain message.
function Initialize-CharonFirmware {
    if ($script:CharonFirmwareMode -eq 'dir') {
        if (-not [IO.Directory]::Exists($script:CharonFirmwareDir)) { throw (T "The folder $($script:CharonFirmwareDir) does not exist." "Folder $($script:CharonFirmwareDir) nie istnieje.") }
        return
    }
    if ($script:CharonFirmwareMode -ne 'real') { return }
    if (-not ('CharonOs.Native' -as [type])) {
        Add-Type -Namespace CharonOs -Name Native -MemberDefinition @'
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
    }
    if (-not (Test-CharonAdministrator)) { throw (T 'Firmware variables need administrator rights. Start the program with its .cmd launcher.' 'Zmienne firmware wymagają uprawnień administratora. Uruchom program plikiem .cmd.') }
    $type = 0; [void][CharonOs.Native]::GetFirmwareType([ref]$type)
    if ($type -ne 2) { throw (T 'This Windows did not start in UEFI mode (BIOS or CSM); there are no UEFI variables to read.' 'Ten Windows nie wystartował w trybie UEFI (BIOS lub CSM); nie ma zmiennych UEFI.') }
    $token = [IntPtr]::Zero; $luid = 0
    if (-not [CharonOs.Native]::OpenProcessToken([CharonOs.Native]::GetCurrentProcess(), 0x28, [ref]$token)) { throw (T 'Cannot open the process token.' 'Nie można otworzyć tokenu procesu.') }
    if (-not [CharonOs.Native]::LookupPrivilegeValueW($null, 'SeSystemEnvironmentPrivilege', [ref]$luid)) { throw (T 'SeSystemEnvironmentPrivilege not found.' 'Brak uprawnienia SeSystemEnvironmentPrivilege.') }
    $state = New-Object CharonOs.Native+TokenPrivileges
    $state.Count = 1; $state.Luid = $luid; $state.Attributes = 2
    [void][CharonOs.Native]::AdjustTokenPrivileges($token, $false, [ref]$state, 0, [IntPtr]::Zero, [IntPtr]::Zero)
    if ([Runtime.InteropServices.Marshal]::GetLastWin32Error() -ne 0) { throw (T 'Windows did not grant the firmware variable privilege (run as administrator).' 'Windows nie przyznał uprawnienia do zmiennych firmware (uruchom jako administrator).') }
}
function Test-CharonFirmware { return ($script:CharonFirmwareMode -eq 'real' -or $script:CharonFirmwareMode -eq 'dir') }
function Get-CharonNoFirmwareText { return (T 'Firmware variables are not available in this mode (-NoFirmware).' 'Zmienne firmware są niedostępne w tym trybie (-NoFirmware).') }
function Get-FwVar([string]$Name) {
    if ($script:CharonFirmwareMode -eq 'dir') {
        $file = [IO.Path]::Combine($script:CharonFirmwareDir, $Name + $script:CharonVarSuffix)
        if (-not [IO.File]::Exists($file)) { return $null }
        $all = [IO.File]::ReadAllBytes($file)
        if ($all.Length -lt 4) { return $null }
        $data = New-Object byte[] ($all.Length - 4)
        [Array]::Copy($all, 4, $data, 0, $data.Length)
        return , $data
    }
    if ($script:CharonFirmwareMode -ne 'real') { throw (Get-CharonNoFirmwareText) }
    $buffer = New-Object byte[] 65536; $attr = 0
    $n = [CharonOs.Native]::GetFirmwareEnvironmentVariableExW($Name, $script:CharonGlobalGuid, $buffer, 65536, [ref]$attr)
    if ($n -eq 0) {
        $e = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($e -eq 203) { return $null }                   # ERROR_ENVVAR_NOT_FOUND
        throw (T "Reading the firmware variable $Name failed (error $e)." "Odczyt zmiennej firmware $Name nie powiódł się (błąd $e).")
    }
    $data = New-Object byte[] $n
    [Array]::Copy($buffer, 0, $data, 0, $n)
    return , $data
}
# $Data = $null deletes the variable. Attributes 7: non-volatile, boot service, runtime.
function Set-FwVar([string]$Name, [byte[]]$Data) {
    if ($script:CharonFirmwareMode -eq 'dir') {
        $file = [IO.Path]::Combine($script:CharonFirmwareDir, $Name + $script:CharonVarSuffix)
        if ($null -eq $Data -or $Data.Length -eq 0) { if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }; return }
        $all = New-Object byte[] ($Data.Length + 4)
        $all[0] = 7; [Array]::Copy($Data, 0, $all, 4, $Data.Length)
        [IO.File]::WriteAllBytes($file, $all); return
    }
    if ($script:CharonFirmwareMode -ne 'real') { throw (Get-CharonNoFirmwareText) }
    $size = 0; if ($null -ne $Data) { $size = $Data.Length }
    if (-not [CharonOs.Native]::SetFirmwareEnvironmentVariableExW($Name, $script:CharonGlobalGuid, $Data, [uint32]$size, 7)) {
        $e = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw (T "Firmware rejected writing $Name (error $e). Nothing else was changed." "Firmware odrzuciło zapis $Name (błąd $e). Nic więcej nie zmieniono.")
    }
}
function ConvertFrom-CharonOrder([byte[]]$Bytes) {
    $ids = @()
    if ($null -ne $Bytes) { for ($i = 0; $i + 1 -lt $Bytes.Length; $i += 2) { $ids += [int][BitConverter]::ToUInt16($Bytes, $i) } }
    return , $ids
}
function ConvertTo-CharonOrder([int[]]$Ids) {
    $bytes = New-Object byte[] (2 * $Ids.Count)
    for ($i = 0; $i -lt $Ids.Count; $i++) { $bytes[2 * $i] = [byte]($Ids[$i] -band 0xff); $bytes[2 * $i + 1] = [byte](($Ids[$i] -shr 8) -band 0xff) }
    return , $bytes
}
function Get-CharonOrder([string]$Name) { return , (ConvertFrom-CharonOrder (Get-FwVar $Name)) }
function Get-CharonOptionName([string]$Prefix, [int]$Id) { return ('{0}{1:X4}' -f $Prefix, $Id) }

# ---- load options (UEFI 2.10 section 3.1.3; the checks of src/efi/BootOption.h Parse) ----
function Read-CharonLoadOption([byte[]]$Data) {
    $o = [pscustomobject]@{ Valid = $false; Active = $false; Attributes = [uint32]0; Description = ''; File = ''; PartitionGuid = ''; HardDriveNode = $null; PathOffset = 0; PathBytes = 0; FirmwareFile = $false }
    if ($null -eq $Data -or $Data.Length -lt 8 -or $Data.Length -gt 65536) { return $o }
    $o.Attributes = [BitConverter]::ToUInt32($Data, 0)
    $o.Active = (($o.Attributes -band 1) -ne 0)
    $end = 6; $bad = $false
    while ($end + 2 -le $Data.Length -and [BitConverter]::ToUInt16($Data, $end) -ne 0) {
        $unit = [int][BitConverter]::ToUInt16($Data, $end); $end += 2
        if ($unit -ge 0xd800 -and $unit -le 0xdbff) {
            if ($end + 2 -gt $Data.Length) { $bad = $true; break }
            $low = [int][BitConverter]::ToUInt16($Data, $end)
            if ($low -lt 0xdc00 -or $low -gt 0xdfff) { $bad = $true; break }
            $end += 2
        } elseif ($unit -ge 0xdc00 -and $unit -le 0xdfff) { $bad = $true; break }
    }
    if ($end + 2 -gt $Data.Length) { $bad = $true }
    if ($end -gt 6 -and $end -le $Data.Length) { $o.Description = [Text.Encoding]::Unicode.GetString($Data, 6, $end - 6) }
    if ($bad) { return $o }
    $start = $end + 2; $length = [int][BitConverter]::ToUInt16($Data, 4)
    $o.PathOffset = $start; $o.PathBytes = $length
    if ($length -lt 4 -or $length -gt $Data.Length - $start) { return $o }
    $at = $start; $finished = $false; $ok = $true
    while ($at -lt $start + $length) {
        if ($start + $length - $at -lt 4) { $ok = $false; break }
        $node = [int][BitConverter]::ToUInt16($Data, $at + 2)
        if ($node -lt 4 -or $node -gt $start + $length - $at) { $ok = $false; break }
        $type = $Data[$at]; $sub = $Data[$at + 1]
        if ($type -eq 0x7f) {
            if ($node -ne 4) { $ok = $false; break }
            if ($sub -eq 0xff) { if ($at + 4 -ne $start + $length) { $ok = $false; break }; $finished = $true }
            elseif ($sub -ne 1 -or $at + 4 -eq $start + $length) { $ok = $false; break }
        }
        if ($type -eq 4 -and $sub -eq 1 -and $node -eq 42) {
            $hd = New-Object byte[] 42; [Array]::Copy($Data, $at, $hd, 0, 42); $o.HardDriveNode = $hd
            if ($Data[$at + 41] -eq 2) { $g = New-Object byte[] 16; [Array]::Copy($Data, $at + 24, $g, 0, 16); $o.PartitionGuid = (New-Object Guid (, $g)).ToString() }
        }
        if ($type -eq 4 -and $sub -eq 4 -and $node -gt 4) { $o.File += [Text.Encoding]::Unicode.GetString($Data, $at + 4, $node - 4).TrimEnd([char]0) }
        if ($type -eq 4 -and $sub -eq 6) { $o.FirmwareFile = $true }
        $at += $node
    }
    # BootOption.h Parse: active, boot category (no application/hidden category bits), path closed by an end node.
    $o.Valid = ($ok -and $finished -and $o.Active -and (($o.Attributes -band 0x1f00) -eq 0) -and $Data.Length -ge 12)
    return $o
}
function Test-CharonOwnEntry($Option) {
    if ($Option.Description -eq 'Charon' -or $Option.Description -eq 'Charon czytnik') { return $true }
    return ($Option.File -match '(?i)\\EFI\\HAK\\(HAKBOOT|shimx64|grubx64)\.efi$')
}

# ---- the reader add-on: driver option "Charon voice" ---------------------------
# Found exactly as BootMenu.h FindFirmwareVoice and Install-CharonAddon.ps1 Find-Charon do:
# a Driver#### listed in DriverOrder whose description is "Charon voice".
function Find-CharonVoice {
    $want = [Text.Encoding]::Unicode.GetBytes($script:CharonVoiceName + [char]0)
    foreach ($id in (Get-CharonOrder 'DriverOrder')) {
        $o = Get-FwVar (Get-CharonOptionName 'Driver' $id)
        if ($null -eq $o -or $o.Length -lt 6 + $want.Length) { continue }
        $same = $true
        for ($i = 0; $i -lt $want.Length; $i++) { if ($o[6 + $i] -ne $want[$i]) { $same = $false; break } }
        if ($same) { return $id }
    }
    return $null
}
# The hard-drive node of the partition Charon lies on. Taken from an existing load option of
# the same partition when there is one (firmware's or the installer's own bytes), otherwise
# built from the partition table like Install-CharonAddon.ps1 New-DriverOption.
function Get-CharonHardDriveNode {
    $guid = $null; $number = 0; $start = [uint64]0; $size = [uint64]0
    if ($script:CharonEspPartition) {
        $p = $script:CharonEspPartition
        $sector = [uint64](Get-Disk -Number $p.DiskNumber).LogicalSectorSize
        $guid = ([guid]$p.Guid).ToString(); $number = [uint32]$p.PartitionNumber
        $start = [uint64]($p.Offset / $sector); $size = [uint64]($p.Size / $sector)
    } elseif ($env:CHARON_TEST_ESP_PART) {
        $t = $env:CHARON_TEST_ESP_PART.Split(',')
        $number = [uint32]$t[0]; $start = [uint64]$t[1]; $size = [uint64]$t[2]; $guid = ([guid]$t[3]).ToString()
    }
    foreach ($prefix in 'Boot', 'Driver') {
        foreach ($id in (Get-CharonOrder ($prefix + 'Order'))) {
            $o = Read-CharonLoadOption (Get-FwVar (Get-CharonOptionName $prefix $id))
            if ($o.HardDriveNode -and (($guid -and $o.PartitionGuid -eq $guid) -or (-not $guid -and (Test-CharonOwnEntry $o)))) { return , $o.HardDriveNode }
        }
    }
    if (-not $guid) { throw (T 'The partition of Charon could not be identified; the driver entry was not created.' 'Nie udało się ustalić partycji Charona; wpis sterownika nie został utworzony.') }
    $hd = New-Object byte[] 42
    $hd[0] = 4; $hd[1] = 1; $hd[2] = 42; $hd[3] = 0
    [BitConverter]::GetBytes([uint32]$number).CopyTo($hd, 4)
    [BitConverter]::GetBytes([uint64]$start).CopyTo($hd, 8)
    [BitConverter]::GetBytes([uint64]$size).CopyTo($hd, 16)
    ([guid]$guid).ToByteArray().CopyTo($hd, 24)
    $hd[40] = 2; $hd[41] = 2
    return , $hd
}
function New-CharonVoiceOption {
    $hd = Get-CharonHardDriveNode
    $name = [Text.Encoding]::Unicode.GetBytes($script:CharonVoiceFile + [char]0)
    $file = New-Object byte[] (4 + $name.Length)
    $file[0] = 4; $file[1] = 4; [BitConverter]::GetBytes([uint16]$file.Length).CopyTo($file, 2); $name.CopyTo($file, 4)
    $path = [byte[]]($hd + $file + [byte[]](0x7f, 0xff, 4, 0))
    $desc = [Text.Encoding]::Unicode.GetBytes($script:CharonVoiceName + [char]0)
    # LOAD_OPTION_ACTIVE | LOAD_OPTION_FORCE_RECONNECT, as BootMenu.h InstallFirmwareVoice
    return , ([byte[]]([BitConverter]::GetBytes([uint32]3) + [BitConverter]::GetBytes([uint16]$path.Length) + $desc + $path))
}
function Enable-CharonVoice {
    $existing = Find-CharonVoice
    if ($null -ne $existing) { return $existing }
    $option = New-CharonVoiceOption
    $id = $null
    for ($c = 0; $c -lt 0x1000; $c++) { if ($null -eq (Get-FwVar (Get-CharonOptionName 'Driver' $c))) { $id = $c; break } }
    if ($null -eq $id) { throw (T 'No free Driver#### number.' 'Brak wolnego numeru Driver####.') }
    Set-FwVar (Get-CharonOptionName 'Driver' $id) $option
    $order = Get-CharonOrder 'DriverOrder'
    try { Set-FwVar 'DriverOrder' (ConvertTo-CharonOrder ([int[]](@($id) + $order))) }
    catch { Set-FwVar (Get-CharonOptionName 'Driver' $id) $null; throw }
    $check = Get-FwVar (Get-CharonOptionName 'Driver' $id)
    if (-not (Test-CharonBytesEqual $check $option) -or (Find-CharonVoice) -ne $id) { throw (T 'The driver entry read back does not match what was written.' 'Odczyt kontrolny wpisu sterownika nie zgadza się z zapisem.') }
    return $id
}
function Disable-CharonVoice {
    $id = Find-CharonVoice
    if ($null -eq $id) { return $null }
    $kept = @((Get-CharonOrder 'DriverOrder') | Where-Object { $_ -ne $id })
    if ($kept.Count) { Set-FwVar 'DriverOrder' (ConvertTo-CharonOrder ([int[]]$kept)) } else { Set-FwVar 'DriverOrder' $null }
    Set-FwVar (Get-CharonOptionName 'Driver' $id) $null
    if ($null -ne (Find-CharonVoice)) { throw (T 'The driver entry is still there after removing it.' 'Wpis sterownika nadal istnieje po usunięciu.') }
    return $id
}

# ---- Secure Boot and BitLocker (state only) ------------------------------------
function Get-CharonSecureBoot {
    if ($script:CharonFirmwareMode -eq 'none') { return 'unknown' }
    if ($script:CharonFirmwareMode -eq 'real') { try { if (Confirm-SecureBootUEFI -ErrorAction Stop) { return 'on' } else { return 'off' } } catch { } }
    try { $v = Get-FwVar 'SecureBoot'; if ($null -ne $v -and $v.Length -ge 1) { if ($v[0] -eq 1) { return 'on' } else { return 'off' } } } catch { }
    return 'unknown'
}
function Test-CharonBitLockerOn {
    if ($script:CharonFirmwareMode -ne 'real') { return $false }
    try { $v = Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop; return ($v.ProtectionStatus -eq 'On') } catch { return $false }
}
# Asked before a change that alters what firmware starts or loads. Returns $false to cancel.
function Confirm-CharonBitLocker {
    if (-not (Test-CharonBitLockerOn)) { return $true }
    $text = T ("BitLocker protects this Windows. A change of what firmware starts or loads can make Windows ask for the BitLocker recovery key at the next start. Suspend BitLocker for one restart now? Yes: suspend for one restart and go on. No: go on without suspending (have the recovery key at hand). Cancel: change nothing.") `
              ("Ten Windows jest chroniony funkcją BitLocker. Zmiana tego, co firmware uruchamia lub ładuje, może spowodować, że Windows przy następnym starcie zażąda klucza odzyskiwania funkcji BitLocker. Zawiesić BitLocker na jeden restart? Tak: zawieś na jeden restart i kontynuuj. Nie: kontynuuj bez zawieszania (miej pod ręką klucz odzyskiwania). Anuluj: nic nie zmieniaj.")
    $answer = 'cancel'
    if ($script:CharonYes) { Out-Line $text; $answer = 'no' }
    elseif ($script:CharonGui) {
        $r = [Windows.Forms.MessageBox]::Show($text, 'Charon: BitLocker', [Windows.Forms.MessageBoxButtons]::YesNoCancel, [Windows.Forms.MessageBoxIcon]::Warning, [Windows.Forms.MessageBoxDefaultButton]::Button3)
        if ($r -eq [Windows.Forms.DialogResult]::Yes) { $answer = 'yes' } elseif ($r -eq [Windows.Forms.DialogResult]::No) { $answer = 'no' }
    } else {
        Out-Line $text
        $n = Read-CharonNumber (T '1 suspend and go on, 2 go on without suspending, 0 cancel:' '1 zawieś i kontynuuj, 2 kontynuuj bez zawieszania, 0 anuluj:') 2
        if ($n -eq 1) { $answer = 'yes' } elseif ($n -eq 2) { $answer = 'no' }
    }
    if ($answer -eq 'cancel') { return $false }
    if ($answer -eq 'yes') {
        Suspend-BitLocker -MountPoint $env:SystemDrive -RebootCount 1 | Out-Null
        Show-CharonMessage (T 'BitLocker is suspended for one restart.' 'BitLocker zawieszony na jeden restart.')
    }
    return $true
}

# ---- what is installed on the partition ------------------------------------------
function Get-CharonFileLine([string]$Hak, [string]$Name) {
    $file = [IO.Path]::Combine($Hak, $Name)
    if (-not [IO.File]::Exists($file)) { return $null }
    $info = New-Object IO.FileInfo $file
    return ('{0}: {1} {2}, {3:yyyy-MM-dd HH:mm}' -f $Name, $info.Length, (T 'bytes' 'bajtów'), $info.LastWriteTime)
}
function Get-CharonFirstLine([string]$Hak, [string]$Name) {
    $file = [IO.Path]::Combine($Hak, $Name)
    if (-not [IO.File]::Exists($file)) { return $null }
    try { $text = [IO.File]::ReadAllText($file); return ($text -split "`r?`n")[0].Trim() } catch { return $null }
}
# The settings format the installed boot manager writes: its EncodeSettings carries the
# magic "CHARS00" followed by the version character (checked on r407 builds: one hit).
function Get-CharonBinaryFormat([string]$Hak) {
    $found = $null
    foreach ($name in 'HAKBOOT.EFI', 'grubx64.efi') {
        $file = [IO.Path]::Combine($Hak, $name)
        if (-not [IO.File]::Exists($file)) { continue }
        try {
            $text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($file))
            $at = 0
            while ($true) {
                $at = $text.IndexOf('CHARS00', $at, [StringComparison]::Ordinal)
                if ($at -lt 0 -or $at + 7 -ge $text.Length) { break }
                $c = [string]$text[$at + 7]
                if ($c -cmatch '^[0-9A-Z]$') { if ($null -eq $found -or [int][char]$c -gt [int][char]$found) { $found = $c } }
                $at += 7
            }
        } catch { }
    }
    return $found
}
