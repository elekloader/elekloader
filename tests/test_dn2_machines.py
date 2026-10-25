# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the clone registry and optional native MIDI dispatch."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which('cc')
CF_CALL = os.environ.get('ELEKLOADER_CF_CALL')
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


@unittest.skipUnless(CC, 'native C compiler is unavailable')
class RegistryTests(unittest.TestCase):
    def test_stock_registered_and_missing_ids(self):
        for flags in ([], ['-DEMPTY']):
            with self.subTest(flags=flags), tempfile.TemporaryDirectory() as out:
                executable = Path(out) / 'registry'
                result = subprocess.run([CC, '-std=c99', '-Wall', '-Werror', *flags,
                                         '-I', str(ROOT / 'elekloader/sdk/include'),
                                         str(ROOT / 'tests/test_dn2_machines.c'), '-o', str(executable)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(executable)], check=True)


@unittest.skipUnless(CF_CALL and STOCK and os.environ.get('ELEKLOADER_CROSS'),
                     'needs DN2 stock, cross compiler and ELEKLOADER_CF_CALL')
class ParameterDispatchTests(unittest.TestCase):
    def test_parameter_lookup_maps_clones_but_identity_reads_keep_ids(self):
        from elekloader import patch, syx
        from elekloader.devices.digitone_mk2 import DEVICE
        from elekloader.sdk import build as sdk
        from elekloader.sdk.sharc import filter_artifact, voice_artifact

        stock = syx.Syx.load(STOCK)
        image = stock.section(3)
        for address, expected in ((0x40036478, '712800de4e75'), (0x4003649c, '712800df4e75')):
            offset = address - DEVICE.main_load
            self.assertEqual(image[offset:offset + 6], bytes.fromhex(expected))
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            base = [sdk.build(str(ROOT / 'mods' / source), STOCK, str(work / source))[0]
                    for source in ('core-dn2', 'machines-dn2')]
            for kind, first, getter_pc, type_offset, producer in (
                    ('synth', 5, 0x40036462, 0xde, voice_artifact),
                    ('filter', 6, 0x40036486, 0xdf, filter_artifact)):
                with self.subTest(kind=kind):
                    stage = work / kind
                    stage.mkdir()
                    (stage / 'machine.c').write_text('#include "mk2/machines.h"\n'
                        'const struct mk2_%s clone={%d,0,"CLONE","CLONE"};\n'
                        'const struct mk2_%s custom={8,0,"CUSTOM","CUSTOM"};\n' % (kind, first, kind))
                    (stage / 'kernel.json').write_text(json.dumps(producer(bytes(4), bytes(4), 0, 8, 20)))
                    (stage / 'mod.json').write_text(json.dumps({
                        'id': kind + '-dispatch-test', 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                        'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'},
                        'resources': {'core': '0.1'}, 'requires': ['core', 'machines-dn2'],
                        'contribute': [{'to': table, 'data': '00000000',
                                        'relocs': [[0, 'abs32', 'sym:' + symbol, 0]]}
                                       for table, symbol in [('mk2_' + kind + 's', 'clone'),
                                                             ('mk2_voice_' + kind + 's', 'custom')]]}))
                    mod = sdk.build(str(stage), STOCK, str(stage / 'build'))[0]
                    outputs, manifest = patch.build(STOCK, base + [mod], 'TEST', log=lambda *args: None)
                    firmware = stage / 'test.syx'
                    firmware.write_bytes(outputs['syx'])
                    vtable, getter = 0x47f3a000, 0x47f3a100
                    pokes = [{'addr': hex(vtable + 0x28), 'hex': getter.to_bytes(4, 'big').hex()},
                             {'addr': hex(getter), 'hex': '202f00044e75'}]
                    calls, expected = [], []
                    reader = 'dn2_real_reader' if kind == 'synth' else 'dn2_filter_real_reader'
                    for i, machine_id in enumerate((0, 1, 2, 3, 4, 5, 6, 8, 9, 31, 32)):
                        address = 0x47f3b000 + i * 0x100
                        sound = bytearray(0xe0)
                        sound[:4] = vtable.to_bytes(4, 'big')
                        sound[type_offset] = machine_id
                        pokes.append({'addr': hex(address), 'hex': sound.hex()})
                        owner = 0x47f3d000 + i * 0x20
                        pokes.append({'addr': hex(owner + 0x10), 'hex': address.to_bytes(4, 'big').hex()})
                        for parameter in (False, True):
                            calls.append({'pc': hex(getter_pc if parameter else manifest['_map'][reader]),
                                          'args': [hex(owner if parameter else address)]})
                            mapped = (0 if machine_id == first else (4 if kind == 'synth' else 0))
                            expected.append(mapped if parameter and first <= machine_id < 32 and machine_id != 8
                                            else machine_id)
                    # Native MIDI handlers must reach the setter through the real owner getters.
                    owner_vtable, setter, allowed = 0x47f3a200, 0x47f3a320, 0x47f3a300
                    table = bytearray(0x7c)
                    for offset, address in ((0x34, setter), (0x38, allowed),
                                            (0x74, 0x40036462), (0x78, 0x40036486)):
                        table[offset:offset + 4] = address.to_bytes(4, 'big')
                    pokes += [{'addr': hex(owner_vtable), 'hex': table.hex()},
                              {'addr': hex(allowed), 'hex': '70014e75'},
                              {'addr': hex(setter), 'hex': '202f000823c047f3f0004e75'}]
                    for i, machine_id in enumerate((0, first)):
                        sound_address, owner = 0x47f3e000 + i * 0x100, 0x47f3e200 + i * 0x20
                        sound = bytearray(0xe0)
                        sound[:4] = vtable.to_bytes(4, 'big')
                        sound[type_offset] = machine_id
                        owner_data = bytearray(0x20)
                        owner_data[:4] = owner_vtable.to_bytes(4, 'big')
                        owner_data[0x10:0x14] = sound_address.to_bytes(4, 'big')
                        pokes += [{'addr': hex(sound_address), 'hex': sound.hex()},
                                  {'addr': hex(owner), 'hex': owner_data.hex()}]
                        cc, nrpn = (40, 201) if kind == 'synth' else (16, 148)
                        for pc, key, value in ((0x40038bc6, cc, 7), (0x400374f6, nrpn, 896)):
                            calls.append({'pc': hex(pc), 'args': [hex(owner), str(key), str(value), '0', '0'],
                                          'reads': [{'addr': '0x47f3f000', 'len': 4}]})
                    request = stage / 'request.json'
                    request.write_text(json.dumps({'pokes': pokes, 'calls': calls}))
                    result = subprocess.run([CF_CALL, str(firmware), str(request), '--base-sha', stock.sha256],
                                            capture_output=True, text=True, timeout=300)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
                    self.assertEqual(len(rows), len(calls))
                    for i, (row, value) in enumerate(zip(rows, expected)):
                        self.assertTrue(row['ok'], row)
                        self.assertEqual(row['d'][0], value)
                        self.assertEqual(row['d'][1:], row['d_in'][1:])
                        self.assertEqual(row['a'][2:7], row['a_in'][2:7])
                        self.assertEqual(row['a'][1], vtable if i % 2 else row['a_in'][1])
                        self.assertEqual(row['a'][7], row['sp0'] + 4)
                    dispatched = rows[len(expected):]
                    self.assertNotEqual(dispatched[0]['reads'], ['00000000'])
                    for row in dispatched:
                        self.assertTrue(row['ok'], row)
                        self.assertEqual(row['d'][0], 1, row)
                        self.assertEqual(row['reads'], dispatched[0]['reads'])


if __name__ == '__main__':
    unittest.main()
