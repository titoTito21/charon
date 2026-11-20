#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic "cloud images" and target disks for tests/CharonSeedDiskTests.cpp.

Usage: python3.15 tests/charon-seeddisk-make-image.py <scratch directory>

Writes ordinary files only:
  cloud-512.img, cloud-4096.img    64 MiB GPT images laid out like Debian's
                                   generic cloud image: entry 0 = root (last
                                   on disk), entry 13 = BIOS boot, 14 = ESP
  disk-512.img, disk-512-after.img, disk-4096.img
                                   256 MiB "disks": the image at the start,
                                   zeros behind it (what Charon sees after it
                                   wrote the image to a larger disk)
"""
import hashlib, os, struct, sys, uuid, zlib

IMAGE = 64 << 20
DISK = 256 << 20
MIB = 1 << 20
TYPES = {
    'bios': uuid.UUID('21686148-6449-6E6F-744E-656564454649'),
    'esp': uuid.UUID('C12A7328-F81F-11D2-BA4B-00A0C93EC93B'),
    'root': uuid.UUID('0FC63DAF-8483-4772-8E79-3D69D8477DE4'),
}


def pattern(tag, size):
    out = bytearray()
    counter = 0
    while len(out) < size:
        out += hashlib.sha256(f'{tag}:{counter}'.encode()).digest()
        counter += 1
    return bytes(out[:size])


def make_image(path, bs):
    blocks = IMAGE // bs
    count, entry_size = 128, 128
    array_blocks = (count * entry_size + bs - 1) // bs
    first_usable = 2 + array_blocks
    last_usable = blocks - 2 - array_blocks
    layout = [  # entry index, kind, first byte, end byte (exclusive)
        (0, 'root', 20 * MIB, IMAGE - MIB),
        (13, 'bios', 1 * MIB, 4 * MIB),
        (14, 'esp', 4 * MIB, 20 * MIB),
    ]
    entries = bytearray(array_blocks * bs)
    data = bytearray(IMAGE)
    for index, kind, start, end in layout:
        unique = uuid.uuid5(uuid.NAMESPACE_DNS, f'charon-seeddisk-{bs}-{kind}')
        name = kind.encode('utf-16-le').ljust(72, b'\0')
        entries[index * entry_size:(index + 1) * entry_size] = struct.pack(
            '<16s16sQQQ72s', TYPES[kind].bytes_le, unique.bytes_le,
            start // bs, end // bs - 1, 0, name)
        data[start:start + 65536] = pattern(f'{bs}-{kind}-head', 65536)
        data[end - 65536:end] = pattern(f'{bs}-{kind}-tail', 65536)
    entries_crc = zlib.crc32(bytes(entries[:count * entry_size]))
    disk_guid = uuid.uuid5(uuid.NAMESPACE_DNS, f'charon-seeddisk-{bs}-disk')

    def header(my, alternate, array_lba):
        def fields(crc):
            return struct.pack(
                '<8sIIIIQQQQ16sQIII', b'EFI PART', 0x00010000, 92, crc, 0, my,
                alternate, first_usable, last_usable, disk_guid.bytes_le,
                array_lba, count, entry_size, entries_crc)
        return fields(zlib.crc32(fields(0))).ljust(bs, b'\0')

    mbr = bytearray(bs)
    mbr[446:462] = struct.pack('<B3sB3sII', 0, b'\x00\x02\x00', 0xEE,
                               b'\xff\xff\xff', 1, blocks - 1)
    mbr[510:512] = b'\x55\xaa'
    data[0:bs] = mbr
    data[bs:2 * bs] = header(1, blocks - 1, 2)
    data[2 * bs:2 * bs + len(entries)] = entries
    backup_array = blocks - 1 - array_blocks
    data[backup_array * bs:backup_array * bs + len(entries)] = entries
    data[(blocks - 1) * bs:] = header(blocks - 1, 1, backup_array)
    with open(path, 'wb') as f:
        f.write(data)
    return data


def make_disk(path, image):
    with open(path, 'wb') as f:
        f.write(image)
        f.truncate(DISK)


def main():
    scratch = sys.argv[1]
    os.makedirs(scratch, exist_ok=True)
    for bs, disks in ((512, ('disk-512.img', 'disk-512-after.img')),
                      (4096, ('disk-4096.img',))):
        image = make_image(os.path.join(scratch, f'cloud-{bs}.img'), bs)
        for name in disks:
            make_disk(os.path.join(scratch, name), image)
            print('made', name, DISK, 'bytes from', f'cloud-{bs}.img')
    print('PASS charon-seeddisk-make-image')


if __name__ == '__main__':
    main()
