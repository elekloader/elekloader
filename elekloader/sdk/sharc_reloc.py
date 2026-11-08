# SPDX-License-Identifier: GPL-2.0-or-later
"""Relocatable DN2 machine kernels from a linker that writes no relocations.

`relocatable` links the same objects at a base and at two probes, one moving the code and one the
data, and reads every address field from the difference. `rebase` then places the kernel at any
base with field arithmetic alone, which the patcher can do without a compiler or linker."""

from .. import sharc
from ..elemod import ModError

PROBE_CODE = 0x1010101   # PC16; every byte of a field changes, and a 24-bit field wraps where a 32-bit one carries
PROBE_DATA = 0x1010100   # bytes, plus the data's alignment (the probes find it)
CHECK = (0x2a5, 0x1230)  # a further pair that the self-check links


def relocatable(link, objects, code_pc16=0xbc8400, data_address=0x20098000):
    """The kernel's code, data, address fields and symbols, for `rebase`.

    link(objects, code_pc16, data_address) -> (code, data, symbols): the sections' bytes as the
    executable stores them (sdk.sharc swaps the code's parcels for the loader afterwards) and
    {name: address}; it raises ValueError when a section does not start at the base given. A field
    is (section, byte offset, width in bytes, kind): big-endian in code, little-endian in data,
    holding a PC16 code address or a byte data address. Raises
    ValueError when the difference is not such fields, or a further link disagrees with `rebase`."""
    code, data, symbols = link(objects, code_pc16, data_address)
    by_code = link(objects, code_pc16 + PROBE_CODE, data_address)
    for align in (4, 8, 16, 32, 64, 128):             # the smallest step that keeps the data's layout
        try:
            by_data = link(objects, code_pc16, data_address + PROBE_DATA + align)
            break
        except ValueError:
            continue
    else:
        raise ValueError('SHARC kernel: data needs more than 128-byte alignment')
    if data_address % align:
        raise ValueError('SHARC kernel: data base 0x%x is not %d-byte aligned' % (data_address, align))
    relocs = []
    for kind, probe, delta in (('code', by_code, PROBE_CODE), ('data', by_data, PROBE_DATA + align)):
        for section, base, moved in (('code', code, probe[0]), ('data', data, probe[1])):
            relocs += [(section, at, width, kind) for at, width in _fields(section, base, moved, delta)]
    taken = [(s, at + i) for s, at, width, _ in relocs for i in range(width)]
    if len(taken) != len(set(taken)):
        raise ValueError('SHARC kernel: a field moves with both the code and the data')
    artifact = {'code': code, 'data': data, 'relocs': sorted(relocs), 'code_pc16': code_pc16,
                'data_address': data_address, 'data_align': align,
                'symbols': {name: _symbol(name, at, by_code[2], by_data[2], code_pc16, data_address)
                            for name, at in symbols.items()}}
    dc, dd = CHECK[0], CHECK[1] - CHECK[1] % align
    real = link(objects, code_pc16 + dc, data_address + dd)
    if rebase(artifact, code_pc16 + dc, data_address + dd) != (bytes(real[0]), bytes(real[1])):
        raise ValueError('SHARC kernel: rebasing does not reproduce a real link')
    return artifact


def rebase(artifact, code_base, data_base):
    """The artifact's (code, data) linked at PC16 code_base and byte data_base. Raises ValueError."""
    try:
        return sharc.rebase(artifact, code_base, data_base)
    except ModError as e:
        raise ValueError(str(e)) from e


def _fields(section, base, moved, delta):
    """(offset, width) of each field that moved by delta between base and moved."""
    if len(base) != len(moved):
        raise ValueError('SHARC kernel: %s changes size with its base' % section)
    changed = [i for i in range(len(base)) if base[i] != moved[i]]
    runs = []
    for i in changed:                                 # contiguous runs; data words are 4-aligned
        if runs and runs[-1][0] + runs[-1][1] == i and (section == 'code' or i % 4):
            runs[-1][1] += 1
        else:
            runs.append([i, 1])
    order = 'big' if section == 'code' else 'little'
    for at, width in runs:
        mask = (1 << 8 * width) - 1
        step = int.from_bytes(moved[at:at + width], order) - int.from_bytes(base[at:at + width], order)
        if width > 4 or (step - delta) & mask:
            raise ValueError('SHARC kernel: %s bytes 0x%x..0x%x are not an address field' % (section, at, at + width))
    return [tuple(run) for run in runs]


def _symbol(name, at, by_code, by_data, code_pc16, data_address):
    if by_code.get(name) != at:
        return ('code', at - code_pc16)
    if by_data.get(name) != at:
        return ('data', at - data_address)
    return ('abs', at)
