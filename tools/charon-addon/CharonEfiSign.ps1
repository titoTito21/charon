# Authenticode signing of UEFI images (PE32+) with Windows' own .NET
# (SignedCms), r385. GPL-2.0-or-later. Dot-sourced by Install-CharonAddon.ps1.
# Set-AuthenticodeSignature refuses EFI subsystems ("not a valid Win32
# application"), so the image hash and SpcIndirectDataContent are built here
# as the PE/COFF Authenticode specification and edk2's DxeImageVerificationLib
# expect: SHA-256 over the headers without CheckSum and the certificate
# directory entry, the sections by file offset, then any extra data; the
# WIN_CERTIFICATE (revision 2.0, PKCS_SIGNED_DATA) is appended 8-aligned.
Add-Type -AssemblyName System.Security

function Get-DerLength([int]$n) {
    if ($n -lt 0x80) { return [byte[]]@($n) }
    if ($n -lt 0x100) { return [byte[]]@(0x81, $n) }
    if ($n -lt 0x10000) { return [byte[]]@(0x82, ($n -shr 8), ($n -band 0xff)) }
    return [byte[]]@(0x83, ($n -shr 16), (($n -shr 8) -band 0xff), ($n -band 0xff))
}
function New-Der([byte]$Tag, [byte[]]$Body) { return [byte[]](@($Tag) + (Get-DerLength $Body.Length) + $Body) }

function Get-PeLayout([byte[]]$Image) {
    if ($Image.Length -lt 0x40 -or $Image[0] -ne 0x4d -or $Image[1] -ne 0x5a) { throw 'Not a PE image (no MZ).' }
    $pe = [BitConverter]::ToInt32($Image, 0x3c)
    if ([BitConverter]::ToUInt32($Image, $pe) -ne 0x4550) { throw 'Not a PE image (no PE signature).' }
    $coff = $pe + 4; $sections = [BitConverter]::ToUInt16($Image, $coff + 2); $optSize = [BitConverter]::ToUInt16($Image, $coff + 16)
    $opt = $coff + 20
    if ([BitConverter]::ToUInt16($Image, $opt) -ne 0x20b) { throw 'Only PE32+ (x64) images are supported.' }
    $dirs = $opt + 112
    if ([BitConverter]::ToUInt32($Image, $opt + 108) -lt 5) { throw 'The image has no certificate directory entry.' }
    $table = $opt + $optSize; $list = @()
    for ($i = 0; $i -lt $sections; $i++) {
        $s = $table + 40 * $i
        $list += [pscustomobject]@{ Size = [BitConverter]::ToUInt32($Image, $s + 16); Offset = [BitConverter]::ToUInt32($Image, $s + 20) }
    }
    return [pscustomobject]@{
        CheckSum = $opt + 64; CertEntry = $dirs + 4 * 8; Headers = [BitConverter]::ToUInt32($Image, $opt + 60)
        CertOffset = [BitConverter]::ToUInt32($Image, $dirs + 32); CertSize = [BitConverter]::ToUInt32($Image, $dirs + 36)
        Sections = @($list | Where-Object { $_.Size -gt 0 } | Sort-Object Offset)
    }
}

function Get-AuthenticodeDigest([byte[]]$Image) {
    $l = Get-PeLayout $Image
    $sha = [Security.Cryptography.SHA256]::Create()
    $add = { param($from, $count) if ($count -gt 0) { [void]$sha.TransformBlock($Image, $from, $count, $null, 0) } }
    & $add 0 $l.CheckSum
    & $add ($l.CheckSum + 4) ($l.CertEntry - $l.CheckSum - 4)
    & $add ($l.CertEntry + 8) ($l.Headers - $l.CertEntry - 8)
    $hashed = [long]$l.Headers
    foreach ($s in $l.Sections) { & $add $s.Offset $s.Size; $hashed += $s.Size }
    $end = if ($l.CertSize) { [long]$l.CertOffset } else { [long]$Image.Length }
    if ($end -gt $hashed) { & $add $hashed ($end - $hashed) }
    [void]$sha.TransformFinalBlock((New-Object byte[] 0), 0, 0)
    return $sha.Hash
}

function Invoke-CharonEfiSign([string]$Path, [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate) {
    [byte[]]$image = [IO.File]::ReadAllBytes($Path)
    $l = Get-PeLayout $image
    if ($l.CertSize) {                                  # replace an earlier signature
        if ($l.CertOffset + $l.CertSize -ne $image.Length) { throw 'Existing certificate table is not at the end of the image.' }
        $image = [byte[]]$image[0..($l.CertOffset - 1)]
    }
    $pad = (8 - ($image.Length % 8)) % 8
    if ($pad) { $image = [byte[]]($image + (New-Object byte[] $pad)) }
    [Array]::Clear($image, $l.CertEntry, 8)
    $digest = Get-AuthenticodeDigest $image
    # SpcIndirectDataContent { SpcAttributeTypeAndOptionalValue { SPC_PE_IMAGE_DATAOBJ,
    #   SpcPeImageData { flags '', file [0] { [2] { [0] "<<<Obsolete>>>" } } } }, DigestInfo { sha256, digest } }
    $obsolete = [Text.Encoding]::BigEndianUnicode.GetBytes('<<<Obsolete>>>')
    $link = New-Der 0xa0 (New-Der 0xa2 (New-Der 0x80 $obsolete))
    $peData = New-Der 0x30 ([byte[]](@(0x03, 0x01, 0x00) + $link))
    $peOid = [byte[]](0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x01, 0x0f)
    $sha256 = New-Der 0x30 ([byte[]](@(0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01) + @(0x05, 0x00)))
    $spc = New-Der 0x30 ([byte[]]((New-Der 0x30 ([byte[]]($peOid + $peData))) + (New-Der 0x30 ([byte[]]($sha256 + (New-Der 0x04 $digest))))))
    $content = New-Object Security.Cryptography.Pkcs.ContentInfo((New-Object Security.Cryptography.Oid '1.3.6.1.4.1.311.2.1.4'), $spc)
    $cms = New-Object Security.Cryptography.Pkcs.SignedCms($content, $false)
    $signer = New-Object Security.Cryptography.Pkcs.CmsSigner($Certificate)
    $signer.DigestAlgorithm = New-Object Security.Cryptography.Oid '2.16.840.1.101.3.4.2.1'
    $signer.IncludeOption = [Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
    # SpcSpOpusInfo (empty), as signtool adds; it also makes the signed attributes explicit.
    [void]$signer.SignedAttributes.Add((New-Object Security.Cryptography.AsnEncodedData((New-Object Security.Cryptography.Oid '1.3.6.1.4.1.311.2.1.12'), [byte[]](0x30, 0x00))))
    $cms.ComputeSignature($signer, $true)
    [byte[]]$pkcs7 = $cms.Encode()
    $length = 8 + $pkcs7.Length; $certPad = (8 - ($length % 8)) % 8
    $win = [byte[]]([BitConverter]::GetBytes([uint32]($length + $certPad)) + [BitConverter]::GetBytes([uint16]0x0200) + [BitConverter]::GetBytes([uint16]0x0002) + $pkcs7 + (New-Object byte[] $certPad))
    [BitConverter]::GetBytes([uint32]$image.Length).CopyTo($image, $l.CertEntry)
    [BitConverter]::GetBytes([uint32]$win.Length).CopyTo($image, $l.CertEntry + 4)
    $signed = [byte[]]($image + $win)
    [IO.File]::WriteAllBytes($Path, $signed)
    return [BitConverter]::ToString($digest).Replace('-', '').ToLowerInvariant()
}

function Get-CharonSigningCertificate([switch]$Create) {
    $subject = 'CN=Charon UEFI add-on (this computer)'
    $c = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } | Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $c -and $Create) {
        $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 `
            -KeyExportPolicy NonExportable -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(30)
    }
    return $c
}
