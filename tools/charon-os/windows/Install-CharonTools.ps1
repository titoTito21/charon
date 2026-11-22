<#
.SYNOPSIS
Installs the Charon programs for Windows: "Charon screen reader settings" and "Charon
boot manager".

.DESCRIPTION
Copies the programs from the folder this script lies in to %ProgramFiles%\Charon and
creates two Start-menu shortcuts for all users, "Charon screen reader settings" and
"Charon boot manager". The shortcuts start the programs with administrator rights (the
EFI system partition and firmware variables need them) and with -ExecutionPolicy Bypass
for that one process; the execution policy of the system is not changed.

Running it again replaces the files and shortcuts with the same result. Nothing else is
touched: not the EFI system partition, not firmware variables, not the registry.

  -Uninstall          remove the files this script installs and the two shortcuts
  -Destination DIR    install folder (default %ProgramFiles%\Charon)
  -StartMenu DIR      folder for the shortcuts (default: Start menu, Programs, all users)
  -Language en|pl     language of the messages (default: Polish on a Polish Windows)

Called by the Charon installers when the folder os\windows exists beside them.
Needs administrator rights for the default folders. Windows PowerShell 5.1. GPL-2.0-or-later.
#>
param(
    [switch]$Uninstall,
    [string]$Destination = '',
    [string]$StartMenu = '',
    [string]$Language = ''
)
$ErrorActionPreference = 'Stop'
$source = Split-Path -Parent $MyInvocation.MyCommand.Path
$pl = if ($Language) { $Language -eq 'pl' } else { (Get-UICulture).Name -like 'pl*' }
function Say([string]$En, [string]$Polish) { if ($pl) { [Console]::Out.WriteLine($Polish) } else { [Console]::Out.WriteLine($En) } }
try { if ([Console]::IsOutputRedirected) { [Console]::OutputEncoding = New-Object Text.UTF8Encoding $false } } catch { }
if (-not $Destination) { $Destination = Join-Path $env:ProgramFiles 'Charon' }
if (-not $StartMenu) { $StartMenu = Join-Path ([Environment]::GetFolderPath('CommonStartMenu')) 'Programs' }
# The programs and what they load; Charon-SecureBoot.ps1 belongs to the Windows installer and is
# taken along only when it lies beside this script or in the installer's folder.
$files = @('Charon-Settings.ps1', 'Charon-BootManager.ps1', 'CharonOs.Common.ps1', 'CharonOs.Fields.ps1', 'Charon-Settings.cmd', 'Charon-BootManager.cmd', 'Install-CharonTools.ps1')
$optional = @('Charon-SecureBoot.ps1')
$shortcuts = @(
    @{ Name = 'Charon screen reader settings'; Script = 'Charon-Settings.ps1'; En = 'Settings of the Charon pre-boot screen reader'; Pl = 'Ustawienia czytnika ekranu Charon sprzed startu systemu' },
    @{ Name = 'Charon boot manager'; Script = 'Charon-BootManager.ps1'; En = 'Boot order and the Charon boot manager'; Pl = 'Kolejność rozruchu i menedżer rozruchu Charon' }
)
try {
    $default = (-not $PSBoundParameters.ContainsKey('Destination')) -or (-not $PSBoundParameters.ContainsKey('StartMenu'))
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($default -and -not $admin) { throw $(if ($pl) { 'Uruchom jako administrator (Program Files i menu Start wszystkich użytkowników).' } else { 'Run as administrator (Program Files and the Start menu of all users).' }) }
    if ($Uninstall) {
        foreach ($s in $shortcuts) { $link = Join-Path $StartMenu ($s.Name + '.lnk'); if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link -Force } }
        if (Test-Path -LiteralPath $Destination) {
            foreach ($name in ($files + $optional)) { $file = Join-Path $Destination $name; if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force } }
            if (-not (Get-ChildItem -LiteralPath $Destination -Force)) { Remove-Item -LiteralPath $Destination -Force }
            else { Say "Other files were left in $Destination." "W $Destination zostały inne pliki." }
        }
        Say 'The Charon programs and their Start-menu shortcuts were removed.' 'Programy Charona i ich skróty w menu Start zostały usunięte.'
        exit 0
    }
    foreach ($name in $files) { if (-not (Test-Path -LiteralPath (Join-Path $source $name))) { throw $(if ($pl) { "Brak pliku $name w $source." } else { "Missing $name in $source." }) } }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    New-Item -ItemType Directory -Force $StartMenu | Out-Null
    $same = ([IO.Path]::GetFullPath($source).TrimEnd('\') -ieq [IO.Path]::GetFullPath($Destination).TrimEnd('\'))
    if (-not $same) {
        foreach ($name in $files) { Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $Destination $name) -Force }
        foreach ($name in $optional) {
            foreach ($folder in @($source, (Join-Path $source '..\..\charon-addon'), (Join-Path $source '..'))) {
                $candidate = Join-Path $folder $name
                if (Test-Path -LiteralPath $candidate) { Copy-Item -LiteralPath $candidate -Destination (Join-Path $Destination $name) -Force; break }
            }
        }
    }
    $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $shell = New-Object -ComObject WScript.Shell
    foreach ($s in $shortcuts) {
        $link = Join-Path $StartMenu ($s.Name + '.lnk')
        $shortcut = $shell.CreateShortcut($link)
        $shortcut.TargetPath = $powershell
        $shortcut.Arguments = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + (Join-Path $Destination $s.Script) + '"'
        $shortcut.WorkingDirectory = $Destination
        $shortcut.Description = $(if ($pl) { $s.Pl } else { $s.En })
        $shortcut.Save()
        # "Run as administrator": bit 0x20 of byte 21 in the link header (SLDF_RUNAS_USER), so
        # that Windows asks for elevation when the shortcut is opened.
        $bytes = [IO.File]::ReadAllBytes($link)
        $bytes[21] = $bytes[21] -bor 0x20
        [IO.File]::WriteAllBytes($link, $bytes)
    }
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
    Say "Installed in $Destination. Start menu: Charon screen reader settings, Charon boot manager." "Zainstalowano w $Destination. Menu Start: Charon screen reader settings, Charon boot manager."
    exit 0
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
