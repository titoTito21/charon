#!/usr/bin/env python3
"""Give the Windows files their release form: *.ps1 as UTF-8 with BOM and CRLF (Windows
PowerShell 5.1 reads a script without BOM in the ANSI code page), *.cmd as ASCII with CRLF.
Usage: python normalize-windows.py [--check]"""
import sys
from pathlib import Path
folder = Path(__file__).resolve().parent.parent / 'windows'
bad = 0
for path in sorted(folder.iterdir()):
    if path.suffix.lower() not in ('.ps1', '.cmd'):
        continue
    raw = path.read_bytes()
    text = raw.decode('utf-8-sig').replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r\n')
    if path.suffix.lower() == '.cmd':
        text.encode('ascii')
        wanted = text.encode('ascii')
    else:
        wanted = b'\xef\xbb\xbf' + text.encode('utf-8')
    if raw != wanted:
        bad += 1
        if '--check' in sys.argv:
            print('not normalized:', path.name)
        else:
            path.write_bytes(wanted)
            print('normalized:', path.name)
sys.exit(1 if bad and '--check' in sys.argv else 0)
