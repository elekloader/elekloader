# SPDX-License-Identifier: GPL-2.0-or-later
"""Package DN2 machine kernels; compilation belongs to the mod.

A mod's build compiles its kernel, links it with `machine_linker_script(kind)` and hands the
linked executable to `machine_from_executable`: the build never names an address."""
import json
from pathlib import Path
import struct

from ..devices.digitone_mk2 import DEVICE
from ..sharc import module_payload, pc16


def machine_layout(kind):
    """Linker bounds for a voice or filter; no compiler-specific linker syntax."""
    if kind not in ('voice', 'filter'):
        raise ValueError('DN2 machine kind must be voice or filter')
    profile = DEVICE.releases['1.12'].sharc
    lo, hi = profile[kind + '_range']
    middle = (hi - lo) // 2
    return {'code_pc16': pc16(profile, lo + profile[kind + '_adapter_bytes']),
            'code_bytes': middle - profile[kind + '_adapter_bytes'],
            'data_address': lo + middle,
            'data_bytes': middle - profile[kind + '_state_bytes']}


def voice_artifact(code, data, render_offset_bytes, machine_id, stack_bytes, voice_bytes=0):
    """Kernel PC16 0xbc8400, data 0x20098000; entry offset excludes the adapter. voice_bytes:
    one voice's state; the artifact reserves 16, zeroed, after the data."""
    return _machine_artifact('voice', code, data, render_offset_bytes, machine_id, stack_bytes, voice_bytes)


def filter_artifact(code, data, render_offset_bytes, machine_id, stack_bytes, voice_bytes=0):
    """Kernel PC16 0xbd0400, data 0x200a8000; entry offset excludes the adapter. voice_bytes:
    as voice_artifact's."""
    return _machine_artifact('filter', code, data, render_offset_bytes, machine_id, stack_bytes, voice_bytes)


def _machine_artifact(kind, code, data, render_offset_bytes, machine_id, stack_bytes, voice_bytes=0):
    profile = DEVICE.releases['1.12'].sharc
    layout = machine_layout(kind)
    prefix = profile[kind + '_adapter_bytes']
    data_offset = prefix + layout['code_bytes']
    state = data_offset + layout['data_bytes']
    if not isinstance(code, bytes) or not code or len(code) % 2 or len(code) > layout['code_bytes'] - 64:
        raise ValueError('DN2 %s code must fit %d KiB with its 64-byte guard' % (kind, layout['code_bytes'] // 1024))
    if type(render_offset_bytes) is not int or render_offset_bytes % 2 or not 0 <= render_offset_bytes < len(code):
        raise ValueError('DN2 %s entry must be even and inside the supplied code' % kind)
    if not isinstance(data, bytes) or len(data) % 4 or len(data) > layout['data_bytes']:
        raise ValueError('DN2 %s data must fit %d KiB and be four-byte aligned' % (kind, layout['data_bytes'] // 1024))
    voice_at = data_offset + -(-len(data) // 8) * 8          # 16 voices' state, after the data
    if type(voice_bytes) is not int or voice_bytes < 0 or voice_bytes % 4:
        raise ValueError('DN2 %s voice state must be a whole number of 32-bit words' % kind)
    if voice_bytes and voice_at + 16 * voice_bytes > state:
        raise ValueError('DN2 %s data (%d bytes) and 16 voices of %d bytes need %d bytes; the area has %d'
                         % (kind, len(data), voice_bytes, voice_at - data_offset + 16 * voice_bytes, layout['data_bytes']))
    adapter = json.loads((Path(__file__).parent / ('include/digitone-mk2/' + kind + '-bridge.json')).read_text())
    code += b'\x01\x00' * (32 + len(code) // 2 % 2)        # the read-ahead guard, to whole words
    gap, end = prefix + len(code), data_offset + len(data)   # zeroed words decode as NOP
    doc = {'abi': profile[kind + '_abi'], 'machine_id': machine_id,
           'segments': [adapter, {'offset_bytes': prefix, 'kind': 'code', 'hex': code.hex()},
                        *([{'offset_bytes': data_offset, 'kind': 'data', 'hex': data.hex()}] if data else [])],
           'zero': ([{'offset_bytes': gap, 'size_bytes': data_offset - gap}] if gap < data_offset else [])
                   + ([{'offset_bytes': end, 'size_bytes': state - end}] if end < state else [])
                   + [{'offset_bytes': state, 'size_bytes': profile[kind + '_state_bytes']}],
           'entries': {'render_offset_bytes': prefix + render_offset_bytes},
           'stack_bytes': stack_bytes}
    if voice_bytes:
        doc['voice_state'] = {'offset_bytes': voice_at, 'voice_bytes': voice_bytes}
    module_payload(doc, profile)
    return doc


# The executable's two loadable sections; the linker script names them.
CODE_SECTION, DATA_SECTION = 'machine_code', 'machine_data'


def machine_linker_script(kind):
    """The linker script (the ADI LDF form Selache's seld reads) that places a machine's code
    and data where its kind's area puts them. Code comes from seg_swco, data from seg_dmda;
    the entry is `sharc_<kind>_render.` (the C function `sharc_<kind>_render`)."""
    layout = machine_layout(kind)
    code, data = layout['code_pc16'] * 2, layout['data_address']
    return ("ARCHITECTURE(ADSP-21569)\n"
            "$OBJECTS = $COMMAND_LINE_OBJECTS;\n"
            "MEMORY {\n"
            " code { TYPE(BW RAM) START(0x%x) END(0x%x) WIDTH(8) }\n"
            " data { TYPE(BW RAM) START(0x%x) END(0x%x) WIDTH(8) }\n"
            "}\n"
            "PROCESSOR p0 { OUTPUT($COMMAND_LINE_OUTPUT_FILE) SECTIONS {\n"
            " %s SW { INPUT_SECTION_ALIGN(2) INPUT_SECTIONS($OBJECTS(seg_swco)) } > code\n"
            " %s BW { INPUT_SECTION_ALIGN(4) INPUT_SECTIONS($OBJECTS(seg_dmda)) } > data\n"
            "} }\n" % (code, code + layout['code_bytes'] - 1, data, data + layout['data_bytes'] - 1,
                        CODE_SECTION, DATA_SECTION))


def machine_from_executable(kind, executable, machine_id, stack_bytes=None):
    """The machine artifact for a linked SHARC executable (little-endian ELF32) built with
    machine_linker_script(kind): its code, data and render entry, checked against the area.
    stack_bytes defaults to the most the kind's callback may declare. Raises ValueError."""
    def need(ok, message):
        if not ok:
            raise ValueError('DN2 %s executable: %s' % (kind, message))
    layout = machine_layout(kind)
    raw = bytes(executable)
    need(len(raw) >= 52 and raw[:7] == b'\x7fELF\x01\x01\x01'
         and struct.unpack_from('<HH', raw, 16) == (2, 0x85), 'expected a linked little-endian SHARC ELF32 executable')
    entry, shoff = struct.unpack_from('<II', raw, 24)[0], struct.unpack_from('<I', raw, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 46)
    need(shentsize == 40 and shoff + 40 * shnum <= len(raw) and shstrndx < shnum, 'malformed section table')
    headers = [struct.unpack_from('<10I', raw, shoff + 40 * i) for i in range(shnum)]

    def text(table, at):
        start = headers[table][4] + at
        end = raw.index(b'\0', start)
        return raw[start:end].decode('latin-1')
    sections = {}
    for name, kind_, flags, address, offset, size, *_ in headers:
        label = text(shstrndx, name)
        need(kind_ not in (4, 9), 'unresolved relocations in %s' % label)          # SHT_RELA, SHT_REL
        if flags & 2:                                                               # SHF_ALLOC
            need(kind_ == 1 and label in (CODE_SECTION, DATA_SECTION),
                 'only %s and %s may be loaded, not %s' % (CODE_SECTION, DATA_SECTION, label))
            need(label not in sections and offset + size <= len(raw), 'duplicate or truncated %s' % label)
            sections[label] = (flags, address, raw[offset:offset + size])
    need(CODE_SECTION in sections, 'no %s section' % CODE_SECTION)
    code_flags, code_pc, code = sections[CODE_SECTION]
    data_flags, data_address, data = sections.get(DATA_SECTION, (3, layout['data_address'], b''))
    need(code_flags & 4 and not data_flags & 4, 'code and data sections swapped')
    need(code_pc == layout['code_pc16'], 'code linked at PC16 0x%x, not 0x%x' % (code_pc, layout['code_pc16']))
    need(data_address == layout['data_address'], 'data linked at 0x%x, not 0x%x' % (data_address, layout['data_address']))
    symbols = [h for h in headers if h[1] == 2]                                     # SHT_SYMTAB
    need(len(symbols) == 1, 'no symbol table')
    _n, _t, _f, _a, offset, size, strings, *_ = symbols[0]
    render, undefined, state = [], [], []
    for at in range(offset + 16, offset + size, 16):
        name, value, _size, _info, _other, index = struct.unpack_from('<IIIBBH', raw, at)
        label = text(strings, name)
        if label and index == 0:
            undefined.append(label)
        if label == 'sharc_%s_render.' % kind:
            render.append(value)
        if label == 'sharc_%s_state_bytes.' % kind:                # DN2_VOICE_STATE / DN2_FILTER_STATE
            state.append(value)
    need(not undefined, 'unresolved symbols: ' + ', '.join(undefined))
    need(len(render) == 1, 'needs exactly one sharc_%s_render' % kind)
    need(entry == render[0], 'its entry is not sharc_%s_render' % kind)
    need(len(code) % 2 == 0, 'code is not whole 16-bit parcels')
    need(len(state) <= 1, 'more than one sharc_%s_state_bytes' % kind)
    voice_bytes = 0
    if state:
        at = state[0] - data_address
        need(0 <= at and at + 4 <= len(data), 'sharc_%s_state_bytes is not in %s' % (kind, DATA_SECTION))
        voice_bytes = struct.unpack_from('<I', data, at)[0]
        voice_bytes += -voice_bytes % 4                                         # whole words
    # The executable stores each 16-bit parcel little-endian; the loader takes them the other way.
    code = b''.join(code[i:i + 2][::-1] for i in range(0, len(code), 2))
    profile = DEVICE.releases['1.12'].sharc
    return _machine_artifact(kind, code, data, (render[0] - code_pc) * 2, machine_id,
                             profile[kind + '_stack_max'] if stack_bytes is None else stack_bytes, voice_bytes)
