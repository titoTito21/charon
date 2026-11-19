# Copies Winre.wim from the recovery partition (read only) for the Charon WinPE test.
# Changes nothing: no reagentc, no drive letter, no recovery settings.
# Run as administrator.
$ErrorActionPreference = 'Stop'
$target = '<local path>'
New-Item -ItemType Directory -Force -Path $target | Out-Null
$part = Get-Partition | Where-Object { $_.Offset -eq 1023050514432 } | Select-Object -First 1
if (-not $part) { throw 'Recovery partition (offset 1023050514432) not found.' }
$volume = ($part | Get-Volume).Path
$source = $volume + 'Recovery\WindowsRE\Winre.wim'
Copy-Item -LiteralPath $source -Destination (Join-Path $target 'winre.wim')
$a = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$b = (Get-FileHash -LiteralPath (Join-Path $target 'winre.wim') -Algorithm SHA256).Hash
if ($a -ne $b) { throw 'Copy hash mismatch.' }
Set-Content -LiteralPath (Join-Path $target 'done.txt') -Value $b -Encoding ascii
Write-Host "Skopiowano Winre.wim, SHA256 $b"
Start-Sleep -Seconds 3
