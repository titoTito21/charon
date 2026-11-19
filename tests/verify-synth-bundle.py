"""Verify staged dependency files after moving the application directory."""
import hashlib
import json
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve() / 'synthDrivers'
manifest = json.loads((root / 'native-synth.json').read_text(encoding='utf-8'))
assert manifest['async'] is False and manifest['installRpath'] == '$ORIGIN'
assert {'native/libespeak-ng.so', 'native/libsonic.so'} <= manifest['sha256'].keys()
for name, expected in manifest['sha256'].items():
    path = (root / name).resolve()
    assert path.is_relative_to(root), name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
for directory in ('espeak-ng-data', 'native-synth-licenses'):
    actual = {str(path.relative_to(root)) for path in (root / directory).rglob('*') if path.is_file()}
    recorded = {name for name in manifest['sha256'] if name.startswith(directory + '/')}
    assert actual == recorded, (directory, actual ^ recorded)
print(f'PASS {len(manifest["sha256"])} staged dependencies and license files')
