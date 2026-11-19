"""Hash our unsigned, contiguous PE32+ fixture for a disposable Secure Boot db.

Deliberately refuses other PE layouts. This is not an installer or general
Authenticode verifier. Exclusions follow Microsoft PE Format, Authenticode
image hash (checksum and certificate directory).
"""
import hashlib
import struct
import uuid


def fixture_digest(data):
    def u16(at):
        return struct.unpack_from('<H', data, at)[0]
    def u32(at):
        return struct.unpack_from('<I', data, at)[0]
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('missing DOS header')
    pe = u32(60)
    if pe > len(data) - 264 or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('missing PE header')
    opt = pe + 24
    optional_size = u16(pe+20)
    if u16(pe+4) != 0x8664 or u16(opt) != 0x20b or optional_size < 240:
        raise ValueError('expected x64 PE32+')
    headers = u32(opt+60)
    count = u16(pe+6)
    table = opt + optional_size
    if not count or table + count*40 > headers or headers > len(data):
        raise ValueError('invalid section table')
    cert = opt + 112 + 4*8
    if u32(opt+108) < 5 or data[cert:cert+8] != bytes(8):
        raise ValueError('fixture must be unsigned')
    digest = hashlib.sha256()
    for start, end in ((0,opt+64),(opt+68,cert),(cert+8,headers)):
        digest.update(data[start:end])
    sections = sorted((u32(table+i*40+20), u32(table+i*40+16))
                      for i in range(count) if u32(table+i*40+16))
    end = headers
    for start, length in sections:
        if start != end or length > len(data)-start:
            raise ValueError('non-contiguous or truncated fixture section')
        end = start+length
        digest.update(data[start:end])
    # Unsigned COFF symbols/overlay are included in Authenticode's extra data.
    digest.update(data[end:])
    return digest.digest()


def trust_esl(data):
    kind = uuid.UUID('c1c41626-504c-4092-aca9-41f936934328').bytes_le
    owner = uuid.UUID('48616b11-8412-4610-ab11-431315713302').bytes_le
    return kind + struct.pack('<III',76,0,48) + owner + fixture_digest(data)
