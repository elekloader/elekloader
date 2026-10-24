# SPDX-License-Identifier: GPL-2.0-or-later
"""Inspect the pinned compiler output for the DN2 sine and core artifacts."""
import hashlib
import math
import re
import struct
import subprocess

PIN = '2b26d3b75c53063575bc5c820fa0d38879335187'
TOOLS = ('selcc', 'selas', 'seld', 'seldump')
FLAGS = ['-proc', 'ADSP-21569', '-char-size-8', '-O1']


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def command(args, cwd=None, text_output=False):
    result = subprocess.run([str(a) for a in args], cwd=cwd, text=True,
                            capture_output=True, check=False)
    require(result.returncode == 0, f'{args[0]} failed:\n{result.stdout}{result.stderr}')
    require(not result.stderr.strip(), f'{args[0]} diagnostics:\n{result.stderr}')
    require(text_output or not result.stdout.strip(),
            f'{args[0]} unexpected diagnostics:\n{result.stdout}')
    require(not re.search(r'(?im)\b(error|warning|unimplemented)\b', result.stdout),
            f'{args[0]} diagnostics:\n{result.stdout}')
    return result.stdout


def section_rows(dump, raw):
    # Read the pinned inspector's table, not a new general-purpose ELF reader.
    rows = {}
    for match in re.finditer(
            r"(?m)^([0-9a-f]{4})\s+(\w+)\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+"
            r"([0-9a-f]{8})\s+([0-9a-f]{8}):B\s+\d+\s+\d+\s+\d+\s+\d+\s+'([^']*)'", dump):
        index, kind, flags, address, offset, size, name = match.groups()
        row = dict(index=int(index, 16), kind=kind, flags=int(flags, 16),
                   address=int(address, 16), offset=int(offset, 16), size=int(size, 16))
        require(kind != 'nobits', 'unexpected BSS: this probe initializes both state words')
        require(row['offset'] + row['size'] <= len(raw), f'truncated section {name}')
        row['bytes'] = raw[row['offset']:row['offset'] + row['size']]
        require(name not in rows, f'duplicate section {name}')
        rows[name] = row
    require(rows, 'inspector returned no section table')
    return rows


def validate_data(data):
    require(len(data) == 152, 'expected 128 table + 16 width + 8 state bytes')
    require(struct.unpack_from('<4I', data, 128) == (1, 4, 4, 4),
            'ineffective char-size-8 or wrong integer/float/pointer widths')
    expected = b''.join(struct.pack('<f', float(f'{0.125 * math.sin(i * math.pi / 32):.9f}'))
                        for i in range(32))
    require(data[:128] == expected, 'sine table differs from independently rounded values')
    require(data[144:] == bytes(8), 'phase/increment initializers are not zero')
