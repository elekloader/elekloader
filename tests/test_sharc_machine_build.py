# SPDX-License-Identifier: GPL-2.0-or-later
"""machine_linker_script and machine_from_executable on synthetic SHARC executables (no compiler)."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader.sdk.sharc import (CODE_SECTION, DATA_SECTION, machine_from_executable,  # noqa: E402
                                  machine_layout, machine_linker_script, voice_artifact)


def executable(kind='voice', code=b'\x00\x01' * 8, data=b'\x11\x22\x33\x44', entry_parcel=0, code_pc=None,
               data_address=None, symbols=None, extra=()):
    """A little-endian SHARC ELF32 laid out as machine_linker_script places it. `symbols` is
    [(name, value, section index)]; `extra` adds sections as (name, type, flags, bytes)."""
    layout = machine_layout(kind)
    code_pc = layout['code_pc16'] if code_pc is None else code_pc
    data_address = layout['data_address'] if data_address is None else data_address
    render = code_pc + entry_parcel
    if symbols is None:
        symbols = [('sharc_%s_render.' % kind, render, 1)]
    names = [CODE_SECTION, DATA_SECTION, '.symtab', '.strtab', '.shstrtab'] + [e[0] for e in extra]
    shstr = b'\0' + b''.join(n.encode() + b'\0' for n in names)
    strtab = b'\0' + b''.join(s[0].encode() + b'\0' for s in symbols)
    symtab = bytes(16)
    at = 1
    for name, value, index in symbols:
        symtab += struct.pack('<IIIBBH', at, value, 0, 0x12, 0, index)
        at += len(name) + 1
    bodies = [code, data, symtab, strtab, shstr] + [e[3] for e in extra]
    offsets, blob = [], b''
    for body in bodies:
        offsets.append(52 + len(blob))
        blob += body + b'\0' * (-len(body) % 4)
    shoff = 52 + len(blob)
    headers = [bytes(40)]
    name_at = {n: shstr.index(b'\0' + n.encode() + b'\0') + 1 for n in names}
    specs = [(CODE_SECTION, 1, 7, code_pc, 0, 0), (DATA_SECTION, 1, 3, data_address, 0, 0),
             ('.symtab', 2, 0, 0, 4, 1), ('.strtab', 3, 0, 0, 0, 0), ('.shstrtab', 3, 0, 0, 0, 0)]
    specs += [(e[0], e[1], e[2], 0, 0, 0) for e in extra]
    for i, (name, typ, flags, address, link, info) in enumerate(specs):
        headers.append(struct.pack('<10I', name_at[name], typ, flags, address, offsets[i], len(bodies[i]),
                                   link, info, 4, 16 if typ == 2 else 0))
    header = (b'\x7fELF\x01\x01\x01' + bytes(9) + struct.pack('<HHIIIIIHHHHHH', 2, 0x85, 1, render, 0, shoff,
                                                              0, 52, 0, 0, 40, len(headers), 5))
    return header + blob + b''.join(headers)


class MachineBuildTests(unittest.TestCase):
    def test_the_linker_script_places_code_and_data_where_the_area_says(self):
        for kind in ('voice', 'filter'):
            layout, script = machine_layout(kind), machine_linker_script(kind)
            self.assertIn('START(0x%x)' % (layout['code_pc16'] * 2), script)
            self.assertIn('START(0x%x)' % layout['data_address'], script)
            self.assertIn('END(0x%x)' % (layout['data_address'] + layout['data_bytes'] - 1), script)
            self.assertIn(CODE_SECTION + ' SW', script)
            self.assertIn(DATA_SECTION + ' BW', script)

    def test_an_executable_becomes_the_artifact_the_producer_would_have_made(self):
        code = bytes(range(16))
        artifact = machine_from_executable('voice', executable(code=code, entry_parcel=2), 6)
        swapped = b''.join(code[i:i + 2][::-1] for i in range(0, 16, 2))
        self.assertEqual(artifact, voice_artifact(swapped, b'\x11\x22\x33\x44', 4, 6, 2048))
        self.assertEqual(machine_from_executable('filter', executable('filter'), 8)['stack_bytes'], 2048)
        self.assertEqual(machine_from_executable('voice', executable(), 6, stack_bytes=64)['stack_bytes'], 64)
        self.assertNotIn('data', [s['kind'] for s in
                                  machine_from_executable('voice', executable(data=b''), 6)['segments'][1:]])

    def test_the_state_size_symbol_reserves_16_voices(self):
        layout = machine_layout('filter')
        symbols = [('sharc_filter_render.', layout['code_pc16'], 1),
                   ('sharc_filter_state_bytes.', layout['data_address'] + 4, 2)]
        raw = executable('filter', data=b'\x11\x22\x33\x44' + struct.pack('<I', 10), symbols=symbols)
        self.assertEqual(machine_from_executable('filter', raw, 8)['voice_state'],
                         {'offset_bytes': 32776, 'voice_bytes': 12})       # rounded up to whole words
        with self.assertRaisesRegex(ValueError, 'sharc_filter_state_bytes is not in machine_data'):
            machine_from_executable('filter', executable('filter', symbols=symbols), 8)
        with self.assertRaisesRegex(ValueError, 'more than one sharc_filter_state_bytes'):
            machine_from_executable('filter', executable('filter', data=bytes(8), symbols=symbols + symbols[1:]), 8)

    def test_refusals(self):
        layout = machine_layout('voice')
        cases = [
            (b'not an executable', 'expected a linked little-endian SHARC ELF32 executable'),
            (executable(code_pc=layout['code_pc16'] + 2), 'code linked at PC16'),
            (executable(data_address=layout['data_address'] + 4), 'data linked at'),
            (executable(symbols=[]), 'needs exactly one sharc_voice_render'),
            (executable(symbols=[('sharc_voice_render.', layout['code_pc16'], 1), ('memcpy', 0, 0)]),
             'unresolved symbols: memcpy'),
            (executable(extra=[('.rela.seg_swco', 4, 0, bytes(12))]), 'unresolved relocations'),
            (executable(extra=[('.bss', 8, 3, b'')]), 'only machine_code and machine_data may be loaded'),
            (executable(code=b'\x00\x01' * 15400), 'code must fit'),
        ]
        for raw, message in cases:
            with self.subTest(message=message):
                with self.assertRaises(ValueError) as caught:
                    machine_from_executable('voice', raw, 6)
                self.assertIn(message, str(caught.exception))


if __name__ == '__main__':
    unittest.main()
