"""charon-wpa-psk-host.py: host test of src/efi/CharonWpaPsk.h (the WPA2 key Charon hands to an installed system).

Compiles the header with the tree's Clang and compares it with
  - the three passphrase-to-PSK test vectors of IEEE 802.11i-2004, Annex H.4.3,
  - Python's hashlib.pbkdf2_hmac for passphrases of 8 and 63 bytes, a 32-byte network name and a name with
    bytes above 127,
and checks that out-of-range input (7 or 64 bytes of passphrase, empty or 33-byte name) is refused.
No arguments. Environment: CHARON_LLVM as for charon-install-answers-host.py."""
import hashlib, os, shutil, subprocess, sys, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
VECTORS = [
    ('password', 'IEEE', 'f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e'),
    ('ThisIsAPassword', 'ThisIsASSID', '0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af'),
    ('a' * 32, 'Z' * 32, 'becb93866bb8c3832cb777c2f559807c8c59afcb6eae734885001300a981cc62'),
]
EXTRA = [(b'12345678', b'x'), (b'p' * 63, b'network name of thirty-two bytes'), (b'za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85', b'Sie\xc4\x87 domowa')]
REFUSED = [(b'1234567', b'x'), (b'p' * 64, b'x'), (b'12345678', b''), (b'12345678', b'y' * 33)]
SOURCE = r'''
#include "CharonWpaPsk.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
static size_t unhex(const char* s,unsigned char* out) {size_t n=strlen(s)/2;for(size_t i=0;i<n;++i) {char b[3]={s[i*2],s[i*2+1],0};out[i]=(unsigned char)strtoul(b,nullptr,16);}return n;}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    unsigned char pass[200],ssid[200];const size_t p=unhex(argv[1],pass),s=unhex(argv[2],ssid);char hex[65];
    if(!hakefi::wpapsk::KeyHex((const char*)pass,p,ssid,s,hex)) {puts("refused");return 0;}
    puts(hex);return 0;
}
'''

def find_clang():
    for candidate in (os.environ.get('CHARON_LLVM'), str(root.parent / 'llvm-23.1.5' / 'bin')):
        for name in ('clang++.exe', 'clang++'):
            if candidate and (Path(candidate) / name).exists():
                return str(Path(candidate) / name)
    return shutil.which('clang++')

def main():
    clang = find_clang()
    if not clang:
        raise SystemExit('clang++ not found (set CHARON_LLVM)')
    passed = failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / 'wpa.cpp'
        source.write_text(SOURCE)
        exe = Path(tmp) / ('wpa.exe' if os.name == 'nt' else 'wpa')
        build = subprocess.run([clang, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src' / 'efi'), str(source), '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise SystemExit('compile failed:\n' + build.stderr)
        def run(p, s):
            return subprocess.run([str(exe), p.hex(), s.hex()], capture_output=True, text=True).stdout.strip()
        cases = [('IEEE 802.11i H.4.3 vector %d' % (i + 1), p.encode(), s.encode(), want) for i, (p, s, want) in enumerate(VECTORS)]
        cases += [('hashlib %d-byte passphrase, %d-byte name' % (len(p), len(s)), p, s, hashlib.pbkdf2_hmac('sha1', p, s, 4096, 32).hex()) for p, s in EXTRA]
        cases += [('refused: %d-byte passphrase, %d-byte name' % (len(p), len(s)), p, s, 'refused') for p, s in REFUSED]
        for name, p, s, want in cases:
            got = run(p, s)
            ok = got == want
            passed += ok
            failed += not ok
            print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else ': got %s, want %s' % (got, want)))
    print('%d passed, %d failed' % (passed, failed))
    sys.exit(1 if failed else 0)

if __name__ == '__main__':
    main()
