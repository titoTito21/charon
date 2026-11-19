function New-CharonMenuBcd([string]$source, [string]$target) {
    # Windows' DVD template BCD with a boot menu (see work/make-bcd-menu.py):
    # displayorder lists Windows Setup and Windows Memory Diagnostic, and
    # bootmenupolicy is Legacy, so Windows Boot Manager draws its own menu.
    $h = [IO.File]::ReadAllBytes($source)
    if ([Text.Encoding]::ASCII.GetString($h, 0, 4) -ne 'regf') { throw 'BCD: not a registry hive' }
    $cells = New-Object System.Collections.Generic.List[int[]]
    $pos = 4096
    while ($pos + 32 -le $h.Length -and [Text.Encoding]::ASCII.GetString($h, $pos, 4) -eq 'hbin') {
        $size = [BitConverter]::ToInt32($h, $pos + 8); $cell = $pos + 32
        while ($cell -lt $pos + $size) {
            $s = [BitConverter]::ToInt32($h, $cell)
            if ($s -eq 0) { throw 'BCD: zero cell' }
            $cells.Add(@($cell, $s)); $cell += [Math]::Abs($s)
        }
        $pos += $size
    }
    $setup = '{7619dcc9-fafe-11d9-b411-000476eba25f}'; $memdiag = '{b2721d73-1db4-4c62-bf78-c548a880142d}'
    $old = [Text.Encoding]::Unicode.GetBytes($setup + [char]0 + [char]0)
    $new = [Text.Encoding]::Unicode.GetBytes($setup + [char]0 + $memdiag + [char]0 + [char]0)
    $target_vk = -1
    foreach ($c in $cells) {
        $cell = $c[0]; $s = $c[1]
        if ($s -lt 0 -and $h[$cell + 4] -eq 0x76 -and $h[$cell + 5] -eq 0x6b) {
            $length = [BitConverter]::ToUInt32($h, $cell + 8); $offset = [BitConverter]::ToUInt32($h, $cell + 12); $kind = [BitConverter]::ToInt32($h, $cell + 16)
            if ($kind -eq 7 -and $length -eq $old.Length) {
                $data = 4096 + $offset + 4; $match = $true
                for ($i = 0; $i -lt $old.Length; $i++) { if ($h[$data + $i] -ne $old[$i]) { $match = $false; break } }
                if ($match) { if ($target_vk -ge 0) { throw 'BCD: displayorder twice' }; $target_vk = $cell }
            }
        }
    }
    if ($target_vk -lt 0) { throw 'BCD: displayorder not found' }
    $need = (4 + $new.Length + 7) -band (-bnot 7)
    $done = $false
    foreach ($c in $cells) {
        $cell = $c[0]; $s = $c[1]
        if ($s -ge $need) {
            $rest = $s - $need
            if ($rest -gt 0 -and $rest -lt 8) { continue }
            [BitConverter]::GetBytes([int](-$need)).CopyTo($h, $cell)
            $new.CopyTo($h, $cell + 4)
            if ($rest -gt 0) { [BitConverter]::GetBytes([int]$rest).CopyTo($h, $cell + $need) }
            [BitConverter]::GetBytes([uint32]$new.Length).CopyTo($h, $target_vk + 8)
            [BitConverter]::GetBytes([uint32]($cell - 4096)).CopyTo($h, $target_vk + 12)
            $done = $true; break
        }
    }
    if (-not $done) { throw 'BCD: no free cell' }
    $patched = 0
    foreach ($c in $cells) {
        $cell = $c[0]; $s = $c[1]
        if ($s -lt 0 -and $h[$cell + 4] -eq 0x6e -and $h[$cell + 5] -eq 0x6b) {
            $nameLength = [BitConverter]::ToUInt16($h, $cell + 4 + 0x48)
            if ($nameLength -ne 8 -or [Text.Encoding]::ASCII.GetString($h, $cell + 4 + 0x4c, 8) -ne '250000c2') { continue }
            $count = [BitConverter]::ToUInt32($h, $cell + 4 + 0x24); $list = [BitConverter]::ToUInt32($h, $cell + 4 + 0x28)
            for ($i = 0; $i -lt $count; $i++) {
                $vk = 4096 + [BitConverter]::ToUInt32($h, 4096 + $list + 4 + 4 * $i)
                $length = [BitConverter]::ToUInt32($h, $vk + 8); $offset = [BitConverter]::ToUInt32($h, $vk + 12); $kind = [BitConverter]::ToInt32($h, $vk + 16)
                if ($kind -ne 3 -or $length -ne 8 -or $h[4096 + $offset + 4] -ne 1) { throw 'BCD: unexpected bootmenupolicy' }
                $h[4096 + $offset + 4] = 0; $patched++
            }
        }
    }
    if ($patched -ne 1) { throw 'BCD: bootmenupolicy not found' }
    [IO.File]::WriteAllBytes($target, $h)
}
