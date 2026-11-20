#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent verification of what tests/CharonSeedDiskTests.cpp produced.

Usage: python3.15 tests/charon-seeddisk-verify.py <scratch directory>
           [--deps <directory holding iso-deps, fat-image-deps, fat-legacy-deps>]
           [--7z <7z.exe>]

For every line of <scratch>/manifest.txt:
  fat   own BPB / FAT / root directory parser (type by cluster count, both FAT
        copies, label, long-name checksums, cluster chains, contents), then
        pyfatfs (exact long names, byte-identical contents), then 7-Zip.
  disk  own GPT checker (both headers and arrays, CRCs, backup in the last
        LBA, protective MBR, no overlap, original partitions and their bytes
        unchanged, placement of the new partition), then the seed partition is
        cut out and checked like a FAT image.
"""
import argparse, os, struct, subprocess, sys, zlib

failures = 0


def check(ok, what):
    global failures
    print('PASS' if ok else 'FAIL', what)
    if not ok:
        failures += 1
    return ok


# ---- FAT ---------------------------------------------------------------

def lfn_checksum(short):
    total = 0
    for byte in short:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def parse_fat(image):
    """Returns (info dict, {long name: (short name, bytes)})."""
    sector, spc, reserved, fats, root_entries, total16, media, fat_size = \
        struct.unpack_from('<HBHBHHBH', image, 11)
    hidden, total32 = struct.unpack_from('<II', image, 28)
    total = total16 or total32
    assert image[510:512] == b'\x55\xaa', 'boot signature'
    assert image[0] in (0xEB, 0xE9), 'jump'
    assert image[38] == 0x29, 'extended boot signature'
    assert (total16 == 0) == (total >= 0x10000), 'TotSec16/32 use'
    assert root_entries * 32 % sector == 0, 'root directory fills whole sectors'
    root_sectors = root_entries * 32 // sector
    first_data = reserved + fats * fat_size + root_sectors
    clusters = (total - first_data) // spc
    bits = 12 if clusters < 4085 else 16 if clusters < 65525 else 32
    assert bits in (12, 16)
    fat_bytes = fat_size * sector
    fat1 = image[reserved * sector:reserved * sector + fat_bytes]
    fat2 = image[reserved * sector + fat_bytes:reserved * sector + 2 * fat_bytes]
    assert fat1 == fat2, 'FAT copies differ'
    assert fat_bytes * 8 >= (clusters + 2) * bits, 'FAT too short'

    def entry(n):
        if bits == 16:
            return struct.unpack_from('<H', fat1, n * 2)[0]
        pair = struct.unpack_from('<H', fat1, n * 3 // 2)[0]
        return pair >> 4 if n & 1 else pair & 0xFFF

    eoc = 0xFF8 if bits == 12 else 0xFFF8
    assert entry(0) == (0xF00 if bits == 12 else 0xFF00) | media, 'FAT[0]'
    assert entry(1) >= eoc, 'FAT[1]'
    root = reserved * sector + fats * fat_bytes
    files, label, pending, used = {}, None, [], set()
    for i in range(root_entries):
        e = image[root + i * 32:root + i * 32 + 32]
        if e[0] == 0:
            assert not pending
            break
        if e[11] == 0x0F:
            pending.append(e)
            continue
        if e[11] & 0x08:
            label = e[:11]
            assert not pending
            continue
        short = bytes(e[:11])
        name = (short[:8].rstrip() + (b'.' + short[8:].rstrip() if short[8:].strip() else b'')).decode('ascii')
        short_text = name
        if pending:
            assert pending[0][0] & 0x40, 'first long entry has the last flag'
            assert [p[0] & 0x3F for p in pending] == list(range(len(pending), 0, -1)), 'long entry order'
            units = b''
            for p in reversed(pending):
                assert p[13] == lfn_checksum(short), 'long name checksum'
                assert p[12] == 0 and p[26:28] == b'\0\0'
                units += p[1:11] + p[14:26] + p[28:32]
            codes = struct.unpack(f'<{len(units) // 2}H', units)
            assert len(codes) % 13 == 0
            if 0 in codes:
                cut = codes.index(0)
                assert set(codes[cut + 1:]) <= {0xFFFF}, 'long name padding'
                codes = codes[:cut]
            name = ''.join(map(chr, codes))
            pending = []
        first, size = struct.unpack_from('<HI', e, 26)
        data, cluster = b'', first
        cluster_bytes = spc * sector
        need = (size + cluster_bytes - 1) // cluster_bytes
        assert (first == 0) == (need == 0)
        for _ in range(need):
            assert 2 <= cluster < clusters + 2 and cluster not in used, 'cluster chain'
            used.add(cluster)
            at = (first_data + (cluster - 2) * spc) * sector
            data += image[at:at + cluster_bytes]
            cluster = entry(cluster)
        assert need == 0 or cluster >= eoc, 'chain ends with end-of-chain'
        time, date = struct.unpack_from('<HH', e, 22)
        assert 1 <= (date >> 5) & 15 <= 12 and 1 <= date & 31 <= 31 and (time >> 11) < 24, 'timestamp'
        files[name] = (short_text, bytes(data[:size]), (date, time))
    for n in range(2, clusters + 2):
        assert (entry(n) != 0) == (n in used), 'FAT marks exactly the used clusters'
    info = dict(sector=sector, spc=spc, clusters=clusters, bits=bits, media=media,
                fats=fats, total=total, hidden=hidden, label=label,
                bpb_label=bytes(image[43:54]), type=bytes(image[54:62]),
                serial=struct.unpack_from('<I', image, 39)[0])
    return info, files


def expected_files(directory):
    out = {}
    for name in os.listdir(directory):
        with open(os.path.join(directory, name), 'rb') as f:
            out[name] = f.read()
    return out


def verify_fat(path, sector, bits, label, expected, args, tag, hidden=None, serial=None):
    with open(path, 'rb') as f:
        image = f.read()
    try:
        info, files = parse_fat(image)
    except AssertionError as error:
        check(False, f'{tag}: structure: {error}')
        return
    want_label = label.encode().ljust(11)
    check(info['sector'] == sector and info['bits'] == bits and info['fats'] == 2 and info['media'] == 0xF8
          and info['total'] * sector == len(image) and info['type'] == f'FAT{bits}   '.encode(),
          f"{tag}: FAT{info['bits']} by cluster count ({info['clusters']} clusters, {info['spc']} sectors each), 2 FATs, media F8, {sector}-byte sectors")
    check(info['label'] == want_label and info['bpb_label'] == want_label, f'{tag}: label {label} in the BPB and the root directory')
    check((hidden is None or info['hidden'] == hidden) and (serial is None or info['serial'] == serial), f'{tag}: hidden sectors / serial')
    check({n: v[1] for n, v in files.items()} == expected, f'{tag}: own parser: {len(files)} files, exact names, identical contents')
    stamp = (((2026 - 1980) << 9) | (11 << 5) | 20, (12 << 11) | (34 << 5) | 28)
    check(all(v[2] == stamp for v in files.values()), f'{tag}: every file dated 2026-11-20 12:34:56')
    for name in ('user-data', 'meta-data', 'network-config'):
        if name in expected:
            short = files.get(name, ('', b''))[0]
            check('~' in short and short == short.upper() and short != name.upper(), f'{tag}: {name} has the 8.3 alias {short}')
    shorts = [v[0] for v in files.values()]
    check(len(set(shorts)) == len(shorts), f'{tag}: 8.3 names unique')

    try:
        from pyfatfs.PyFatFS import PyFatFS
        fs = PyFatFS(path, read_only=True)
        try:
            names = sorted(fs.listdir('/'))
            contents = {n: fs.readbytes('/' + n) for n in names}
        finally:
            fs.close()
        check(names == sorted(expected) and contents == expected, f'{tag}: pyfatfs: {len(names)} files, exact names, identical contents')
    except Exception as error:  # report, never hide
        check(False, f'{tag}: pyfatfs: {type(error).__name__}: {error}')

    if args.sevenzip:
        run = subprocess.run([args.sevenzip, 'l', '-slt', path], capture_output=True, text=True, errors='replace')
        listed = {}
        current = None
        kind = ''
        for line in run.stdout.splitlines():
            if line.startswith('Type = '):
                kind = kind or line[7:]
            if line.startswith('Path = '):
                current = line[7:]
            elif line.startswith('Size = ') and current is not None:
                listed[current] = int(line[7:])
        listed.pop(path, None)
        check(run.returncode == 0 and kind == 'FAT' and {n: len(d) for n, d in expected.items()} == listed,
              f'{tag}: 7-Zip: type {kind}, {len(listed)} files, exact names and sizes')
        out = os.path.join(os.path.dirname(path), '7z-out', os.path.basename(path))
        run = subprocess.run([args.sevenzip, 'x', '-y', '-o' + out, path], capture_output=True, text=True, errors='replace')
        same = run.returncode == 0 and os.path.isdir(out) and expected_files(out) == expected
        check(same, f'{tag}: 7-Zip: extracted contents identical')


# ---- GPT ---------------------------------------------------------------

def read_table(f, bs, lba):
    f.seek(lba * bs)
    block = f.read(bs)
    assert block[:8] == b'EFI PART', f'signature at LBA {lba}'
    (revision, size, crc, reserved, my, alternate, first, last, guid, array, count, entry_size,
     array_crc) = struct.unpack_from('<IIIIQQQQ16sQIII', block, 8)
    assert revision == 0x00010000 and size == 92 and reserved == 0
    assert zlib.crc32(block[:16] + b'\0\0\0\0' + block[20:size]) == crc, f'header CRC at LBA {lba}'
    assert block[size:] == bytes(bs - size), 'rest of the header block is zero'
    assert my == lba, 'MyLBA'
    f.seek(array * bs)
    entries = f.read(count * entry_size)
    assert zlib.crc32(entries) == array_crc, f'entry array CRC (header at LBA {lba})'
    parts = {}
    for i in range(count):
        e = entries[i * entry_size:(i + 1) * entry_size]
        if e[:16] != bytes(16):
            first_lba, last_lba, attributes = struct.unpack_from('<QQQ', e, 32)
            parts[i] = dict(type=e[:16], unique=e[16:32], first=first_lba, last=last_lba, attributes=attributes,
                            name=e[56:128].decode('utf-16-le').rstrip('\0'))
    return dict(alternate=alternate, first=first, last=last, guid=guid, array=array, count=count,
                entry_size=entry_size, entries=entries, parts=parts)


def verify_disk(scratch, fields, args):
    name, bs, cloud, first, count, index, placement, guids, expected_dir, bits = fields
    bs, first, count, index, bits = int(bs), int(first), int(count), int(index), int(bits)
    path = os.path.join(scratch, name)
    tag = name
    size = os.path.getsize(path)
    blocks = size // bs
    with open(path, 'rb') as f, open(os.path.join(scratch, cloud), 'rb') as c:
        try:
            primary = read_table(f, bs, 1)
            backup = read_table(f, bs, blocks - 1)
            original = read_table(c, bs, 1)
        except AssertionError as error:
            check(False, f'{tag}: GPT: {error}')
            return
        array_blocks = (primary['count'] * primary['entry_size'] + bs - 1) // bs
        check(primary['alternate'] == blocks - 1 and backup['alternate'] == 1, f'{tag}: both headers valid (CRC), backup header in the last LBA {blocks - 1}')
        check(backup['array'] == blocks - 1 - array_blocks and primary['array'] == 2 and primary['entries'] == backup['entries'],
              f'{tag}: backup array directly before the backup header, identical to the primary array (both CRCs good)')
        check(primary['last'] == backup['last'] == backup['array'] - 1 and primary['first'] == backup['first'] == original['first']
              and primary['guid'] == backup['guid'], f"{tag}: LastUsableLBA {primary['last']} = backup array - 1, same in both headers")
        f.seek(0)
        mbr = f.read(512)
        slots = [mbr[446 + 16 * i:462 + 16 * i] for i in range(4)]
        check(mbr[510:] == b'\x55\xaa' and slots[0][4] == 0xEE and struct.unpack_from('<II', slots[0], 8) == (1, min(blocks - 1, 0xFFFFFFFF))
              and all(s == bytes(16) for s in slots[1:]), f'{tag}: protective MBR covers LBA 1..{blocks - 1}')
        parts = primary['parts']
        check(set(parts) == set(original['parts']) | {index}, f'{tag}: entries {sorted(parts)} = original entries + entry {index}')
        same = all(parts[i]['type'] == o['type'] and parts[i]['first'] == o['first'] and parts[i]['last'] == o['last']
                   and parts[i]['name'] == o['name'] and parts[i]['attributes'] == o['attributes'] for i, o in original['parts'].items())
        check(same, f'{tag}: original partitions keep type, range, name, attributes')
        if guids == '1':
            uniques = [p['unique'] for p in parts.values()]
            check(primary['guid'] != original['guid'] and all(parts[i]['unique'] != o['unique'] for i, o in original['parts'].items())
                  and len(set(uniques)) == len(uniques), f'{tag}: fresh disk GUID and partition GUIDs, all different')
        else:
            check(primary['guid'] == original['guid'] and all(parts[i]['unique'] == o['unique'] for i, o in original['parts'].items()),
                  f'{tag}: disk GUID and partition GUIDs kept')
        ranges = sorted((p['first'], p['last']) for p in parts.values())
        check(all(a[1] < b[0] for a, b in zip(ranges, ranges[1:])) and all(primary['first'] <= a <= b <= primary['last'] for a, b in ranges),
              f'{tag}: partitions inside the usable area, none overlap')
        seed = parts[index]
        highest = max(o['last'] for o in original['parts'].values())
        mib = (1 << 20) // bs
        check(seed['first'] == first and seed['last'] == first + count - 1 and seed['name'] == 'CIDATA'
              and seed['type'].hex() == 'af3dc60f838472478e793d69d8477de4' and seed['first'] % mib == 0,
              f"{tag}: seed partition CIDATA at LBA {seed['first']}..{seed['last']}, 1 MiB aligned, Linux data type")
        if placement == 'end':
            check(primary['last'] - seed['last'] < mib and seed['first'] - highest - 1 >= 100 * mib,
                  f"{tag}: seed at the end of the usable area; {(seed['first'] - highest - 1) // mib} MiB free directly behind the root partition for growpart")
        else:
            check(seed['first'] == highest + 1, f'{tag}: seed directly behind the last partition')
        ok = True
        for o in original['parts'].values():
            offset, length = o['first'] * bs, (o['last'] - o['first'] + 1) * bs
            f.seek(offset)
            c.seek(offset)
            ok &= f.read(length) == c.read(length)
        check(ok, f'{tag}: bytes of the original partitions unchanged')
        f.seek(seed['first'] * bs)
        volume = f.read(count * bs)
    seed_path = os.path.join(scratch, name.replace('.img', '-seed.img'))
    with open(seed_path, 'wb') as f:
        f.write(volume)
    verify_fat(seed_path, bs, bits, 'CIDATA', expected_files(os.path.join(scratch, expected_dir)), args, f'{tag}: seed', hidden=first)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('scratch')
    parser.add_argument('--deps', default=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')))
    parser.add_argument('--7z', dest='sevenzip', default=r'C:\Program Files\7-Zip\7z.exe')
    args = parser.parse_args()
    sys.path[:0] = [os.path.join(args.deps, d) for d in ('iso-deps', 'fat-image-deps', 'fat-legacy-deps')]
    if not os.path.isfile(args.sevenzip):
        print('NOTE 7-Zip not found, second opinion skipped:', args.sevenzip)
        args.sevenzip = None
    with open(os.path.join(args.scratch, 'manifest.txt'), encoding='utf-8') as f:
        lines = [line.rstrip('\n').split('\t') for line in f if line.strip()]
    for fields in lines:
        if fields[0] == 'fat':
            _, name, sector, bits, label, expected_dir, serial = fields
            verify_fat(os.path.join(args.scratch, name), int(sector), int(bits), label,
                       expected_files(os.path.join(args.scratch, expected_dir)), args, name, serial=int(serial))
        else:
            verify_disk(args.scratch, fields[1:], args)
    print(('PASS' if failures == 0 else 'FAIL'), f'charon-seeddisk-verify ({failures} failures, {len(lines)} manifest lines)')
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
