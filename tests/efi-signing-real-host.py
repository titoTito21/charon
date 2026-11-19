"""Real signtool signing with ephemeral TEST credentials; no enrollment/store edits."""
from pathlib import Path
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs12
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--dependencies', required=True, type=Path)
parser.add_argument('--signtool', default='C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/signtool.exe')
args = parser.parse_args()
sys.path[:0] = [str(args.dependencies.resolve()), str(Path(__file__).resolve().parents[1]/'tools')]
from asn1crypto import cms
from hak_efi_sign import pe_image, preserved_image
from hak_efi_authenticode import verify_authenticode, signature_envelope

if args.output.exists():raise ValueError('Test output must not exist')
unsigned = args.image.read_bytes(); pe_image(unsigned)
def test_certificate():
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'HAK TEST ONLY NOT ENROLLED')])
    now = datetime.now(timezone.utc)
    certificate = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
        .serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(minutes=1))
        .not_valid_after(now+timedelta(days=1))
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CODE_SIGNING]), critical=False)
        .sign(key, hashes.SHA256()))
    return key, certificate

with tempfile.TemporaryDirectory(prefix='hak-test-sign-') as temporary:
    stage = Path(temporary)
    key, certificate = test_certificate()
    pfx, image = stage/'test-only.pfx', stage/'HAKBOOT-test.efi'
    pfx.write_bytes(pkcs12.serialize_key_and_certificates(b'HAK TEST ONLY', key, certificate, None, serialization.NoEncryption()))
    image.write_bytes(unsigned)
    result = subprocess.run([args.signtool, 'sign', '/fd', 'SHA256', '/f', str(pfx), str(image)],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if result.returncode:raise RuntimeError('Actual signtool signing failed: '+result.stderr.decode(errors='replace'))
    signed = image.read_bytes()
    pem = certificate.public_bytes(serialization.Encoding.PEM)
    preserved_image(unsigned, signed)
    report = verify_authenticode(signed, pem)
    negatives = []
    def rejected(label, artifact, pin):
        try:verify_authenticode(artifact, pin)
        except Exception:negatives.append(label)
        else:raise AssertionError('Expected rejection: '+label)
    _, wrong = test_certificate()
    rejected('wrong certificate', signed, wrong.public_bytes(serialization.Encoding.PEM))
    pe = struct.unpack_from('<I', signed, 60)[0]
    section = pe+24+struct.unpack_from('<H', signed, pe+20)[0]
    body = struct.unpack_from('<I', signed, section+20)[0]
    modified = bytearray(signed); modified[body] ^= 1
    rejected('modified executable body', bytes(modified), pem)
    info = pe_image(signed); offset = info['certificate_offset']
    length = struct.unpack_from('<I', signed, offset)[0]
    envelope = signature_envelope(signed)
    signature = envelope['content']['signer_infos'][0]['signature'].native
    at = signed.index(signature, offset+8)
    modified = bytearray(signed); modified[at] ^= 1
    rejected('modified RSA signature', bytes(modified), pem)
    rejected('unsigned image', unsigned, pem)
    args.output.mkdir(parents=False)
    (args.output/'HAKBOOT-test-signed.efi').write_bytes(signed)
    (args.output/'TEST-NOT-ENROLLED.pem').write_bytes(pem)
    report.update(test_only=True, signer='Windows signtool /fd SHA256', negative_cases=negatives,
        input_sha256=hashlib.sha256(unsigned).hexdigest(), private_key_retained=False,
        trust_store_modified=False)
    (args.output/'result.json').write_text(json.dumps(report, indent=2))
print('PASS real signtool + independent pinned RSA/SHA256 Authenticode including PE digest; four tampering/wrong-pin negatives; no enrollment')
