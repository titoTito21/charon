"""Prepare actual UI cues as 48 kHz PCM; preserve original WAV files unchanged.

Only sound effects, never speech utterances. Resampling is deterministic linear
interpolation with integer arithmetic, not a claim of lossless rate conversion.
The original source bytes are preserved and identified by SHA-256.
"""
from array import array
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

CUES = ('welcome.wav', 'focus.wav', 'edge.wav', 'wrap.wav', 'submenu_open.wav',
        'submenu_close.wav', 'popUp.wav', 'popup_close.wav', 'dialog_open.wav',
        'dialog_close.wav', 'select.wav', 'continue.wav',
        'system_will_launch.wav', 'boot_manager_loaded.wav')
# r361: sources may be 44.1 or 48 kHz, mono or stereo PCM16 (mono is
# duplicated to both channels); output stays 48 kHz stereo.
SOURCE_RATES, OUTPUT_RATE = (44100, 48000), 48000
SOURCE_RATE = 44100
MAX_SOURCE_BYTES = 4 * 1024 * 1024
MAX_SOURCE_FRAMES = 48000 * 20
MAX_TOTAL_OUTPUT = 24 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_source(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= MAX_SOURCE_BYTES:
        raise ValueError('Expected a bounded regular WAV source')
    data = path.read_bytes()
    if not 0 < len(data) <= MAX_SOURCE_BYTES:
        raise ValueError('WAV source changed size')
    return data


def decode_wav(data):
    if (len(data) < 44 or len(data) > MAX_SOURCE_BYTES or data[:4] != b'RIFF'
            or data[8:12] != b'WAVE' or struct.unpack_from('<I', data, 4)[0]+8 != len(data)):
        raise ValueError('Invalid exact RIFF/WAVE extent')
    position, fmt, pcm = 12, None, None
    while position < len(data):
        if position+8 > len(data):raise ValueError('Truncated WAV chunk')
        name, length = struct.unpack_from('<4sI', data, position)
        start, end = position+8, position+8+length
        position = end+(length&1)
        if position > len(data):raise ValueError('WAV chunk outside file')
        if name == b'fmt ':
            if fmt is not None or length not in (16, 18):raise ValueError('Unexpected WAV format chunk')
            fmt = struct.unpack_from('<HHIIHH', data, start)
            if length == 18 and data[end-2:end] != b'\0\0':raise ValueError('Unexpected WAV format extension')
        elif name == b'data':
            if pcm is not None:raise ValueError('Multiple WAV data chunks')
            pcm = data[start:end]
    if fmt is None or fmt[0] != 1 or fmt[1] not in (1, 2) or fmt[2] not in SOURCE_RATES or fmt[5] != 16 \
            or fmt[4] != fmt[1]*2 or fmt[3] != fmt[2]*fmt[4]:
        raise ValueError('Require PCM16 mono or stereo 44100/48000 Hz source')
    channels, rate = fmt[1], fmt[2]
    if pcm is None or not pcm or len(pcm)%(2*channels) or len(pcm)//(2*channels) > MAX_SOURCE_FRAMES:
        raise ValueError('Invalid or excessive source frame count')
    if channels == 1:
        mono = array('h'); mono.frombytes(pcm)
        if sys.byteorder != 'little':mono.byteswap()
        stereo = array('h', [0]) * (len(mono)*2)
        stereo[0::2] = mono; stereo[1::2] = mono
        if sys.byteorder != 'little':stereo.byteswap()
        pcm = stereo.tobytes()
    return pcm, rate, channels


def resample(pcm, rate=SOURCE_RATE):
    if not pcm or len(pcm)%4 or len(pcm)//4 > MAX_SOURCE_FRAMES:
        raise ValueError('Invalid bounded interleaved PCM16 source')
    samples = array('h'); samples.frombytes(pcm)
    if sys.byteorder != 'little':samples.byteswap()
    frames = len(samples)//2
    if rate == OUTPUT_RATE:
        if sys.byteorder != 'little':samples.byteswap()
        return samples.tobytes()
    output_frames = (frames*OUTPUT_RATE+rate-1)//rate
    output = array('h', [0]) * (output_frames*2)
    for frame in range(output_frames):
        first, remainder = divmod(frame*rate, OUTPUT_RATE)
        second = min(first+1, frames-1)
        for channel in (0, 1):
            value = samples[first*2+channel]*(OUTPUT_RATE-remainder)+samples[second*2+channel]*remainder
            output[frame*2+channel] = value//OUTPUT_RATE if value >= 0 else -((-value)//OUTPUT_RATE)
    if sys.byteorder != 'little':output.byteswap()
    return output.tobytes()


def prepare(source, destination):
    source, destination = Path(source), Path(destination)
    if source.is_symlink() or not source.is_dir():raise ValueError('Invalid cue directory')
    if destination.exists() or destination.is_symlink():raise ValueError('Destination must not exist')
    if not destination.parent.is_dir():raise ValueError('Destination parent must exist')
    found = {p.name for p in source.iterdir() if p.suffix.lower() in ('.wav', '.ogg')}
    if found != set(CUES):raise ValueError('Cue directory must contain exactly the supported %d WAV names' % len(CUES))
    payload, records, source_hashes = {}, {}, {}
    for name in CUES:
        original = read_source(source/name)
        pcm, rate, channels = decode_wav(original)
        converted = resample(pcm, rate)
        output_name = Path(name).with_suffix('.pcm').name
        payload[output_name] = converted
        source_hashes[name] = digest(original)
        records[name] = dict(source_sha256=digest(original), source_bytes=len(original),
            source_frames=len(pcm)//4, source_rate=rate, source_channels=channels, file=output_name,
            bytes=len(converted), frames=len(converted)//4, sha256=digest(converted))
    if sum(map(len, payload.values())) > MAX_TOTAL_OUTPUT:raise ValueError('Cue pack exceeds memory budget')
    if any(digest(read_source(source/name)) != value for name, value in source_hashes.items()):
        raise ValueError('Source cue changed during preparation')
    manifest = dict(format='CHARON-CUES-1', sample_rate=OUTPUT_RATE, channels=2,
        sample_bits=16, byte_order='little', resampler='linear-rational-truncate-zero-v1',
        source_files_modified=False, speech_cache=False, cues=records)
    destination.mkdir()
    for name, data in payload.items():
        with (destination/name).open('xb') as stream:stream.write(data)
    # Only a fully written, independently re-read pack receives a manifest.
    for name, data in payload.items():
        if (destination/name).read_bytes() != data:raise OSError('PCM output readback mismatch')
    with (destination/'manifest.json').open('x', encoding='utf-8') as stream:
        json.dump(manifest, stream, indent=2)
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    result = prepare(args.source, args.destination)
    print('Prepared', len(result['cues']), 'real UI cues as 48k stereo PCM; source WAVs unchanged; no speech cache.')
