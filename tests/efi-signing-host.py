"""Signing orchestration/PE invariants; mock tools are NOT cryptographic proof."""
from pathlib import Path
import hashlib
import importlib.util
import json
import ssl
import struct
import tempfile
from unittest.mock import patch

source = Path(__file__).resolve().parents[1]/'tools/hak_efi_sign.py'
spec = importlib.util.spec_from_file_location('hak_efi_sign', source)
signing = importlib.util.module_from_spec(spec); spec.loader.exec_module(signing)
checks = 0
def check(condition):
    global checks
    assert condition
    checks += 1

def unsigned(subsystem=10):
    data = bytearray(1024); data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128); data[128:132] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 132, 0x8664, 1, 0, 0, 0, 240, 2)
    struct.pack_into('<H', data, 152, 0x20b)
    struct.pack_into('<I', data, 168, 0x1000)
    struct.pack_into('<I', data, 212, 512)
    struct.pack_into('<H', data, 220, subsystem)
    struct.pack_into('<I', data, 260, 16)
    struct.pack_into('<8sIIIIIIHHI', data, 392, b'.text', 512, 0x1000, 512, 512, 0, 0, 0, 0, 0x60000020)
    data[512] = 0xc3
    return bytes(data)

def structured_signature(data):
    # Intentionally not a real Authenticode signature: external verification
    # success/failure is injected separately to test publication boundaries.
    result = bytearray(data)
    offset = (len(result)+7)&~7
    result.extend(bytes(offset-len(result)))
    record = struct.pack('<IHH', 16, 0x200, 2) + b'\x30FAKE123'
    struct.pack_into('<II', result, 296, offset, len(record))
    struct.pack_into('<I', result, 216, 0x12345)
    return bytes(result)+record

original = unsigned()
signed = structured_signature(original)
check(signing.preserved_image(original, signed)['signatures'] == 1)
check(signing.pe_image(unsigned(11))['subsystem'] == 11)
for mutate in (
    lambda d:d.__setitem__(512, 0x90),
    lambda d:struct.pack_into('<I', d, 296, 513),
    lambda d:struct.pack_into('<I', d, 300, 1),
    lambda d:struct.pack_into('<I', d, 1024, 500),
    lambda d:struct.pack_into('<H', d, 1030, 1),
    lambda d:struct.pack_into('<H', d, 220, 3),
    lambda d:struct.pack_into('<I', d, 412, 0xffffff00),
):
    damaged = bytearray(signed); mutate(damaged)
    try:signing.preserved_image(original, bytes(damaged))
    except ValueError:check(True)
    else:raise AssertionError('Damaged signed artifact accepted')
for data in (b'', original[:300], b'not PE', signed+b'extra'):
    try:signing.pe_image(data)
    except ValueError:check(True)
    else:raise AssertionError('Malformed artifact accepted')

with tempfile.TemporaryDirectory(prefix='hak-sign-contract-') as directory:
    directory = Path(directory)
    image, key, cert, output = [directory/name for name in ('input.efi', 'secret.key', 'public.pem', 'signed.efi')]
    image.write_bytes(original); key.write_bytes(b'PRIVATE TEST MARKER NEVER REPORT')
    # The PEM is a deliberately non-X509 fixture; sbverify is mocked below.
    # Real cryptographic verification requires the external integration test.
    public = ssl.DER_cert_to_PEM_cert(b'public test certificate').encode('ascii')
    cert.write_bytes(public)
    executable = directory/'test-tool'; executable.write_bytes(b'EXTERNAL TOOL FIXTURE')
    calls, behavior = [], ['ok']
    def runner(command):
        calls.append(command)
        if '--key' in command:
            check(command[1] == '--key' and command[3] == '--cert' and command[5] == '--output')
            check(Path(command[2]) == key.resolve())
            check(Path(command[4]).read_bytes() == public)
            check(Path(command[7]).read_bytes() == original)
            if behavior[0] == 'sign-fail':raise RuntimeError('signer failed')
            value = structured_signature(original)
            if behavior[0] == 'body-change':value = value[:512]+b'\x90'+value[513:]
            if behavior[0] == 'unsigned':value = original
            Path(command[6]).write_bytes(value)
            if behavior[0] == 'cert-change':Path(command[4]).write_bytes(b'wrong certificate')
        else:
            check(command[1] == '--cert' and len(command) == 4)
            if behavior[0] == 'verify-fail':raise RuntimeError('wrong signer or invalid cryptographic signature')
            if behavior[0] == 'after-verify-change':Path(command[3]).write_bytes(original)
    with patch.object(signing, 'tool_path', return_value=str(executable)), patch.object(signing, 'run_tool', side_effect=runner):
        for failure in ('sign-fail', 'body-change', 'unsigned', 'cert-change', 'verify-fail', 'after-verify-change'):
            behavior[0] = failure
            try:signing.sign_image(image, key, cert, output)
            except (ValueError, RuntimeError):check(True)
            else:raise AssertionError('Failed signing operation published success')
            check(not output.exists() and not output.with_name(output.name+'.signing.json').exists())
            check(image.read_bytes() == original and cert.read_bytes() == public)
        behavior[0] = 'ok'
        result = signing.sign_image(image, key, cert, output)
        check(output.read_bytes() == signed)
        check(result['output_sha256'] == hashlib.sha256(signed).hexdigest())
        check(result['certificate_der_sha256'] == hashlib.sha256(b'public test certificate').hexdigest())
        check(result['firmware_trust_verified'] is False)
        check('PRIVATE TEST MARKER' not in json.dumps(result) and str(key) not in json.dumps(result))
        verified = signing.verify_signed_image(image, output, cert)
        check(verified['output_sha256'] == result['output_sha256'])
        before = len(calls)
        for target in (output, image, key):
            try:signing.sign_image(image, key, cert, target)
            except ValueError:check(len(calls) == before)
            else:raise AssertionError('Existing file overwritten')
        try:signing.sign_image(output, key, cert, directory/'twice.efi')
        except ValueError:check(len(calls) == before)
        else:raise AssertionError('Pre-signed input accepted')

print('PASS EFI signing command, PE identity, pinned certificate, failure/publication and secret-free provenance contracts', checks)
print('External tools mocked: this result is not Authenticode or firmware trust verification.')
