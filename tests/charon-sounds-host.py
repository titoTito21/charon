"""Actual cue formats, rational resampling, bounded outputs and source identity."""
from array import array
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('charon_sounds', root/'tools/prepare-charon-sounds.py')
sound = importlib.util.module_from_spec(spec);spec.loader.exec_module(sound)
checks = 0
def check(condition):
    global checks
    assert condition
    checks += 1
def rejected(action):
    try:action()
    except ValueError:check(True)
    else:raise AssertionError('Invalid source accepted')
def wav(pcm):
    fmt = struct.pack('<HHIIHH', 1, 2, 44100, 176400, 4, 16)
    body = b'WAVEfmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(pcm))+pcm
    return b'RIFF'+struct.pack('<I',len(body))+body

for frames in (1, 2, 17, 441):
    source = struct.pack('<hh', 32767, -32768)*frames
    converted = sound.resample(source)
    count = (frames*48000+44099)//44100
    check(len(converted) == count*4)
    check(converted == struct.pack('<hh',32767,-32768)*count)
    check(sound.decode_wav(wav(source)) == source)
for frames in (3, 17, 200):
    source = b''.join(struct.pack('<hh', i*100-10000, 10000-i*100) for i in range(frames))
    converted = sound.resample(source)
    values = list(struct.iter_unpack('<hh', converted))
    check(values[0] == (-10000,10000))
    check(all(left == -right for left,right in values))
    check(all(-10000 <= left <= (frames-1)*100-10000 for left,_ in values))
    check(converted == sound.resample(source))
valid = wav(struct.pack('<hh',1,-1)*10)
for data in (b'', valid[:20], valid+b'extra', valid[:-1], valid.replace(b'RIFF', b'RIFX',1)):
    rejected(lambda data=data:sound.decode_wav(data))
for offset,value,form in ((20,3,'H'),(22,1,'H'),(24,48000,'I'),(28,1,'I'),(32,2,'H'),(34,24,'H')):
    invalid = bytearray(valid);struct.pack_into('<'+form, invalid, offset, value)
    rejected(lambda:sound.decode_wav(bytes(invalid)))
for pcm in (b'', b'\0', b'\0'*6, b'\0'*(4*(sound.MAX_SOURCE_FRAMES+1))):
    rejected(lambda pcm=pcm:sound.resample(pcm))

source_dir = root/'src/sfx/boot_manager'
originals = {name:(source_dir/name).read_bytes() for name in sound.CUES}
with tempfile.TemporaryDirectory(prefix='charon-cues-test-') as directory:
    destination = Path(directory)/'prepared'
    manifest = sound.prepare(source_dir, destination)
    check(set(manifest['cues']) == set(sound.CUES))
    check(manifest['sample_rate'] == 48000 and manifest['channels'] == 2 and manifest['sample_bits'] == 16)
    check(manifest['speech_cache'] is False and manifest['source_files_modified'] is False)
    check(json.loads((destination/'manifest.json').read_text()) == manifest)
    for name, record in manifest['cues'].items():
        original = originals[name];native = sound.decode_wav(original)
        check(record['source_sha256'] == hashlib.sha256(original).hexdigest())
        check(record['source_frames'] == len(native)//4 and record['source_bytes'] == len(original))
        pcm = (destination/record['file']).read_bytes()
        check(len(pcm) == record['bytes'] == record['frames']*4)
        check(record['frames'] == (record['source_frames']*48000+44099)//44100)
        check(hashlib.sha256(pcm).hexdigest() == record['sha256'])
        original_samples = [v[0] for v in struct.iter_unpack('<h', native)]
        minimum,maximum = min(original_samples),max(original_samples)
        check(all(minimum <= v[0] <= maximum for v in struct.iter_unpack('<h', pcm)))
        check((source_dir/name).read_bytes() == original)
    check(manifest['cues']['popUp.wav']['file'] == 'popUp.pcm')
    check(sum(r['bytes'] for r in manifest['cues'].values()) <= sound.MAX_TOTAL_OUTPUT)
    rejected(lambda:sound.prepare(source_dir,destination))
    empty = Path(directory)/'empty';empty.mkdir()
    rejected(lambda:sound.prepare(empty,Path(directory)/'missing'))
    check(not (Path(directory)/'missing').exists())
print('PASS actual twelve WAVs, PCM48k frame counts/hashes/resampling bounds, exact popUp case, preserved sources and invalid-input rejection', checks)
