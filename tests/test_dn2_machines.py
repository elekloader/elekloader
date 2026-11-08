# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the machine registry, its host types and native MIDI dispatch."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
CC = shutil.which('cc')
CF_CALL = os.environ.get('ELEKLOADER_CF_CALL')
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


def kernel(*kernels):
    """A dn2-kernel-v1 artifact (elekloader/sharc.py) of one 48-bit NOP, a kernel for each (kind, id): the
    ColdFire side does not run it."""
    from elekloader.sharc import KERNEL_ABI
    return {'abi': KERNEL_ABI, 'code': '00' * 6, 'data': '', 'code_pc16': 0xbc8400, 'data_address': 0x20098000,
            'data_align': 4, 'relocs': [], 'exports': {},
            'kernels': [{'kind': kind, 'id': mid, 'entry': 0, **({'knobs': []} if kind == 'sdk' else {})}
                        for kind, mid in kernels]}


def mk2_mod(work, name, source, contribute, kernels):
    """Build an mk2 mod: its C, its contributions {table: symbol} and its kernels."""
    from elekloader.sdk import build as sdk
    stage = work / name
    stage.mkdir()
    (stage / 'machine.c').write_text('#include "mk2/machines.h"\n' + source)
    (stage / 'kernel.json').write_text(json.dumps(kernel(*kernels)))
    (stage / 'mod.json').write_text(json.dumps({
        'id': name, 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
        'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'},
        'resources': {'core': '0.1'}, 'requires': ['core', 'machines-dn2'],
        'contribute': [{'to': table, 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:' + symbol, 0]]}
                       for table, symbol in contribute]}))
    return sdk.build(str(stage), STOCK, str(stage / 'build'))[0]


def cf_calls(firmware, calls, pokes=()):
    """Run calls [(pc, args)] in DigiKit's ColdFire: one result row each."""
    from elekloader import syx
    request = Path(firmware).with_suffix('.json')
    request.write_text(json.dumps({'pokes': list(pokes), 'calls': [{'pc': hex(pc), 'args': [str(a) for a in args],
                                                                  **({'reads': reads} if reads else {})}
                                                                 for pc, args, *reads in calls]}))
    result = subprocess.run([CF_CALL, str(firmware), str(request), '--base-sha', syx.Syx.load(STOCK).sha256],
                            capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, result.stderr
    return [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]


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


class ParameterTableSitesTests(unittest.TestCase):
    @unittest.skipUnless(STOCK, 'needs DN2 stock')
    def test_syn_view_contract_matches_stock(self):
        from elekloader import formats
        firmware, device, _ = formats.load(STOCK)
        image = formats.main_image(firmware, device)

        def data(addr, n):
            return image[addr - device.main_load:addr - device.main_load + n]

        # The title accessor keeps its live view in a2 and passes view +144's page to the getter.
        manifest = json.loads((ROOT / 'mods/machines-dn2/mod.json').read_text())
        ret = int(manifest['defsym']['SYN_TITLE_RETURN'], 0)
        self.assertEqual(data(ret - 12, 12), bytes.fromhex('2f2a00902f004eb9400c2652'))
        # 60 primary methods, then a secondary-base table. Method 4 paints, method 19 names it.
        self.assertEqual(int(manifest['defsym']['SYN_METHODS'], 0), 0x401d4db0)   # SYN's view at power-on
        self.assertEqual(data(0x401d4db0 + 4 * 4, 4), bytes.fromhex('40018118'))
        self.assertEqual(data(0x401d4db0 + 19 * 4, 4), bytes.fromhex('400177b2'))
        self.assertEqual(data(0x401d4db0 + 60 * 4, 4), bytes.fromhex('fffffffc'))
        # The Brain's pointer, which the SDK reads for the page stack: stock tests it there.
        self.assertEqual(int(manifest['defsym']['BRAIN'], 0), 0x44648900)
        self.assertEqual(data(0x4003b7c6, 6), bytes.fromhex('4ab944648900'))
        # The UI loop sets Brain +0xe8 to 2 when it adds the screen saver's view (the SDK hides glyphs).
        self.assertEqual(data(0x4002e3aa, 6), bytes.fromhex('7002254000e8'))
        # A key zeroes the idle count (+0xe4); the same routine then clears +0xe8 before its tick and
        # draw sites, so the first frame after waking draws the glyphs again.
        self.assertEqual(data(0x4002c54e, 4), bytes.fromhex('42a800e4'))
        self.assertEqual(data(0x4002e334, 10), bytes.fromhex('4aaa00e4663e42aa00e8'))
        self.assertEqual(data(0x4002e464, 6), bytes.fromhex('4eb94011d5d4'))

    def test_bounds_and_references_match_the_appended_rows(self):
        source = ((ROOT / 'mods/machines-dn2/dn2.h').read_text()
                  + (ROOT / 'mods/machines-dn2/machines.c').read_text())
        define = {name: int(value, 0) for name, value in
                  (line.split()[1:3] for line in source.splitlines()
                   if line.startswith('#define ') and len(line.split()) > 2)}
        mod = json.loads((ROOT / 'mods/machines-dn2/mod.json').read_text())
        self.assertEqual(int(mod['defsym']['T1_ROWS']), define['ROWS'])
        self.assertEqual(define['G_CODE'], define['ROWS'] + define['ADDED'])  # 0.1's G row: projects keep it
        self.assertEqual(define['SDK_ROW'], define['ADDED'] + 1)  # SDK rows follow the five native pages and G
        self.assertEqual(define['T1_END'], define['ROWS'] + define['SDK_ROW'] + define['SDK_KNOBS'])
        bounds = [s for s in mod['sites'] if s['op'] == 'bytes' and len(s['stock']) == 12]
        self.assertEqual(len(bounds), 54)
        for site in bounds:
            stock, new = bytes.fromhex(site['stock']), bytes.fromhex(site['new'])
            self.assertEqual(stock[:2], new[:2], site['addr'])
            self.assertIn(int.from_bytes(stock[2:], 'big'), (define['ROWS'], define['ROWS'] - 1))
            self.assertEqual(int.from_bytes(new[2:], 'big') - int.from_bytes(stock[2:], 'big'),
                             define['T1_END'] - define['ROWS'], site['addr'])
        self.assertEqual(sum(s.get('target') == 'dn2_t1' for s in mod['sites']), 56)


@unittest.skipUnless(CF_CALL and STOCK and os.environ.get('ELEKLOADER_CROSS'),
                     'needs DN2 stock, cross compiler and ELEKLOADER_CF_CALL')
class VoicePagesTests(unittest.TestCase):
    def test_stock_readers_see_the_pages_and_their_rows(self):
        from elekloader import patch, syx
        from elekloader.sdk import build as sdk

        stock = syx.Syx.load(STOCK)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            base = [sdk.build(str(ROOT / 'mods' / source), STOCK, str(work / source))[0]
                    for source in ('core-dn2', 'machines-dn2')]
            mod = mk2_mod(work, 'paged',
                          'const struct mk2_synth voice = {8, "PAGED", "PAGED"};\n'
                          'static const struct mk2_param a = {"ALPH", 100}, b = {"BETA", 3};\n'
                          'static const struct mk2_page two[2] = {{{&a, 0, &b}}, {{0}}};\n'
                          'const struct mk2_voice_pages pages = {8, 2, two};\n',
                          (('mk2_voice_synths', 'voice'), ('mk2_voice_pages', 'pages')), [('mk2-voice', 8)])
            stage = work / 'paged'
            outputs, _manifest = patch.build(STOCK, base + [mod], 'TEST', log=lambda *args: None)
            firmware = stage / 'test.syx'
            firmware.write_bytes(outputs['syx'])
            page_count, slot_code, row_slot, row_cc, value_class, cc_code, nrpn_code = (
                0x400c2636, 0x400dc18e, 0x400dbe28, 0x400dc406, 0x400c257c, 0x400dc978, 0x400dca5a)
            # Stock synths keep their counts (FM TONE 4, WAVETONE 3, FM DRUM 4, SWARMER 1, MIDI 1);
            # a missing synth has MIDI's.
            checks = [(page_count, [8], 2), (page_count, [0], 4), (page_count, [1], 3),
                      (page_count, [2], 4), (page_count, [3], 1), (page_count, [4], 1),
                      (page_count, [5], 1),
                      (slot_code, [25, 8, 0], 321), (slot_code, [26, 8, 0], 0),
                      (slot_code, [27, 8, 0], 323), (slot_code, [33, 8, 0], 0),
                      (slot_code, [41, 8, 0], 0),
                      (row_slot, [321], 25), (row_slot, [323], 27), (row_slot, [0xd5], 36),
                      (row_cc, [321], 40), (row_cc, [0xd5], 49),
                      # CC/NRPN by position: FM TONE's CC 40/41/42 and NRPN 201/202/203 are slots
                      # 25/26/27, ALPH, empty and BETA on the host FM TONE; stock FM TONE unchanged.
                      (cc_code, [0, 8, 0, 40], 321), (cc_code, [0, 8, 0, 41], 0),
                      (cc_code, [0, 8, 0, 42], 323), (cc_code, [0, 0, 0, 40], 200),
                      (nrpn_code, [0, 8, 0, 201], 321), (nrpn_code, [0, 8, 0, 202], 0),
                      (nrpn_code, [0, 8, 0, 203], 323), (nrpn_code, [0, 0, 0, 201], 200),
                      (row_cc, [323], 42),
                      # A missing synth (9) keeps its type: MIDI's CC 70 (slot 33) is not its parameter.
                      (cc_code, [0, 4, 0, 70], 168), (cc_code, [0, 9, 0, 70], 0),
                      (value_class, [321], None), (value_class, [0xd5], None),
                      (value_class, [377], None), (value_class, [0], None)]
            request = stage / 'request.json'
            request.write_text(json.dumps({'calls': [{'pc': hex(pc), 'args': [str(a) for a in args]}
                                                     for pc, args, _ in checks]}))
            result = subprocess.run([CF_CALL, str(firmware), str(request), '--base-sha', stock.sha256],
                                    capture_output=True, text=True, timeout=300)
            self.assertEqual(result.returncode, 0, result.stderr)
            rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            self.assertEqual(len(rows), len(checks))
            for row, (pc, args, want) in zip(rows, checks):
                self.assertTrue(row['ok'], row)
                if want is not None:
                    self.assertEqual(row['d'][0], want, (hex(pc), args))
            classes = [row['d'][0] for row in rows[-4:]]
            self.assertEqual(classes[0], classes[1])          # an added row shares its template's object
            self.assertEqual(classes[2], classes[3])          # past the added rows: the stock clamp to 0


@unittest.skipUnless(CF_CALL and STOCK and os.environ.get('ELEKLOADER_CROSS'),
                     'needs DN2 stock, cross compiler and ELEKLOADER_CF_CALL')
class ParameterDispatchTests(unittest.TestCase):
    def test_parameter_lookup_maps_custom_filters_to_the_host_but_identity_reads_keep_ids(self):
        from elekloader import patch, syx
        from elekloader.devices.digitone_mk2 import DEVICE
        from elekloader.sdk import build as sdk

        stock = syx.Syx.load(STOCK)
        image = stock.section(3)
        for address, expected in ((0x40036478, '712800de4e75'), (0x4003649c, '712800df4e75')):
            offset = address - DEVICE.main_load
            self.assertEqual(image[offset:offset + 6], bytes.fromhex(expected))
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            base = [sdk.build(str(ROOT / 'mods' / source), STOCK, str(work / source))[0]
                    for source in ('core-dn2', 'machines-dn2')]
            for kind, first, getter_pc, type_offset, kernel_kind, dispatch in (
                    ('synth', 5, 0x40036462, 0xde, 'mk2-voice', (0,)),
                    ('filter', 6, 0x40036486, 0xdf, 'mk2-filter', (1, 8))):
                with self.subTest(kind=kind):
                    mod = mk2_mod(work, kind + '-dispatch-test',
                                  'const struct mk2_%s custom={8,"CUSTOM","CUSTOM"};\n' % kind,
                                  [('mk2_voice_' + kind + 's', 'custom')], [(kernel_kind, 8)])
                    stage = work / (kind + '-dispatch-test')
                    outputs, manifest = patch.build(STOCK, base + [mod], 'TEST', log=lambda *args: None)
                    firmware = stage / 'test.syx'
                    firmware.write_bytes(outputs['syx'])
                    vtable, getter = 0x47f3a000, 0x47f3a100
                    pokes = [{'addr': hex(vtable + 0x28), 'hex': getter.to_bytes(4, 'big').hex()},
                             {'addr': hex(getter), 'hex': '202f00044e75'}]
                    calls, expected, modes = [], [], []
                    identity = manifest['_map']['dn2_real_reader' if kind == 'synth' else 'dn2_filter_real_reader']
                    # Page and default callers reach the stock reader directly; it maps like the getter.
                    pcs = {'identity': identity, 'parameter': getter_pc,
                           'reader': 0x4004b7f2 if kind == 'synth' else 0x4004b860}
                    for i, machine_id in enumerate((0, 1, 2, 3, 4, 5, 6, 8, 9, 31, 32)):
                        address = 0x47f3b000 + i * 0x100
                        sound = bytearray(0xe0)
                        sound[:4] = vtable.to_bytes(4, 'big')
                        sound[type_offset] = machine_id
                        pokes.append({'addr': hex(address), 'hex': sound.hex()})
                        owner = 0x47f3d000 + i * 0x20
                        pokes.append({'addr': hex(owner + 0x10), 'hex': address.to_bytes(4, 'big').hex()})
                        for mode, pc in pcs.items():
                            calls.append({'pc': hex(pc), 'args': [hex(owner if mode == 'parameter' else address)]})
                            modes.append(mode)
                            # A custom machine keeps its ID for MIDI, whose lookups map it, and the
                            # stock reader sees a custom filter as LOWPASS 4, its host. A missing synth
                            # keeps its own type for MIDI (no stock synth map has it) but the stock
                            # reader sees MIDI's; a missing filter falls back to MULTI-MODE. Synth IDs
                            # reach 127 (SDK machines), filter IDs 31.
                            if mode == 'identity' or not first <= machine_id < (128 if kind == 'synth' else 32):
                                expected.append(machine_id)
                            elif kind == 'synth':
                                expected.append(machine_id if machine_id == 8 or mode == 'parameter' else 4)
                            else:
                                expected.append(0 if machine_id != 8 else 8 if mode == 'parameter' else 1)
                    # Native MIDI handlers must reach the setter through the real owner getters; a
                    # custom filter's FREQ CC and NRPN set the parameter LOWPASS 4's set.
                    owner_vtable, setter, allowed = 0x47f3a200, 0x47f3a320, 0x47f3a300
                    table = bytearray(0x7c)
                    for offset, address in ((0x34, setter), (0x38, allowed),
                                            (0x74, 0x40036462), (0x78, 0x40036486)):
                        table[offset:offset + 4] = address.to_bytes(4, 'big')
                    pokes += [{'addr': hex(owner_vtable), 'hex': table.hex()},
                              {'addr': hex(allowed), 'hex': '70014e75'},
                              {'addr': hex(setter), 'hex': '202f000823c047f3f0004e75'}]
                    for i, machine_id in enumerate(dispatch):
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
                    for row, value, mode in zip(rows, expected, modes):
                        self.assertTrue(row['ok'], row)
                        self.assertEqual(row['d'][0], value, (mode, row))
                        self.assertEqual(row['d'][1:], row['d_in'][1:])
                        self.assertEqual(row['a'][2:7], row['a_in'][2:7])
                        self.assertEqual(row['a'][1], vtable if mode == 'parameter' else row['a_in'][1])
                        self.assertEqual(row['a'][7], row['sp0'] + 4)
                    dispatched = rows[len(expected):]
                    self.assertNotEqual(dispatched[0]['reads'], ['00000000'])
                    for row in dispatched:
                        self.assertTrue(row['ok'], row)
                        self.assertEqual(row['d'][0], 1, row)
                        self.assertEqual(row['reads'], dispatched[0]['reads'])

    def test_g_takes_the_row_after_a_full_synth_and_leaves_stock_filters_alone(self):
        from elekloader import patch, syx
        from elekloader.sdk import build as sdk

        stock = syx.Syx.load(STOCK)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            mods = [sdk.build(str(ROOT / 'mods' / source), STOCK, str(work / source))[0]
                    for source in ('core-dn2', 'machines-dn2')]
            knobs = ', '.join('{"K%d", %d}' % (n, n) for n in range(40))
            mods.append(mk2_mod(work, 'full', 'static const struct mk2_param k[40] = {%s};\n'
                                'static const struct mk2_page five[5] = {%s};\n'
                                'const struct mk2_synth voice = {8, "FULL", "FULL"};\n'
                                'const struct mk2_voice_pages pages = {8, 5, five};\n'
                                % (knobs, ', '.join('{{%s}}' % ', '.join('&k[%d]' % (8 * p + i) for i in range(8))
                                                    for p in range(5))),
                                (('mk2_voice_synths', 'voice'), ('mk2_voice_pages', 'pages')), [('mk2-voice', 8)]))
            mods.append(mk2_mod(work, 'g', 'static const struct mk2_param g = {"MODE", 9};\n'
                                'const struct mk2_filter filter = {9, "GEE", "GEE", 0, &g};\n',
                                [('mk2_voice_filters', 'filter')], [('mk2-filter', 9)]))
            outputs, _manifest = patch.build(STOCK, mods, 'TEST', log=lambda *args: None)
            firmware = work / 'test.syx'
            firmware.write_bytes(outputs['syx'])
            slot_code, row_slot, row_cc, value_class, cc_code, nrpn_code = (
                0x400dc18e, 0x400dbe28, 0x400dc406, 0x400c257c, 0x400dc978, 0x400dca5a)
            # The synth's 40 rows are 321..360 and G's is 361, after them. Slot 66 is G's only for
            # the custom filter: MULTI-MODE keeps TYPE (38), as a missing filter (6) does, and
            # LOWPASS 4 has none; CC 18 and NRPN 1:22 follow.
            checks = [(slot_code, [25, 8, 0], 321), (slot_code, [64, 8, 0], 360),
                      (slot_code, [66, 8, 9], 361), (slot_code, [66, 0, 9], 361),
                      (slot_code, [66, 0, 0], 38), (slot_code, [66, 0, 1], 0), (slot_code, [66, 8, 6], 38),
                      (slot_code, [67, 0, 9], 39),
                      (row_slot, [361], 66), (row_slot, [360], 64), (row_cc, [361], 18),
                      (cc_code, [0, 8, 9, 18], 361), (nrpn_code, [0, 8, 9, 150], 361),
                      (cc_code, [0, 0, 1, 18], 0), (cc_code, [0, 0, 0, 18], 38), (cc_code, [0, 0, 9, 16], 39),
                      (cc_code, [0, 8, 9, 40], 321),
                      (value_class, [361], None), (value_class, [38], None)]
            request = work / 'request.json'
            request.write_text(json.dumps({'calls': [{'pc': hex(pc), 'args': [str(a) for a in args]}
                                                     for pc, args, _ in checks]}))
            result = subprocess.run([CF_CALL, str(firmware), str(request), '--base-sha', stock.sha256],
                                    capture_output=True, text=True, timeout=300)
            self.assertEqual(result.returncode, 0, result.stderr)
            rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            self.assertEqual(len(rows), len(checks))
            for row, (pc, args, want) in zip(rows, checks):
                self.assertTrue(row['ok'], row)
                if want is not None:
                    self.assertEqual(row['d'][0], want, (hex(pc), args))
            self.assertEqual(rows[-2]['d'][0], rows[-1]['d'][0])  # G's value class is TYPE's

    def test_several_voices_and_filters_from_several_mods(self):
        from elekloader import patch
        from elekloader.sdk import build as sdk

        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            mods = [sdk.build(str(ROOT / 'mods' / source), STOCK, str(work / source))[0]
                    for source in ('core-dn2', 'machines-dn2')]
            tables = (('mk2_voice_synths', 'voice'), ('mk2_voice_pages', 'pages'), ('mk2_voice_filters', 'filter'))
            mods.append(mk2_mod(work, 'two', 'const struct mk2_synth voice = {8, "PAGED", "PAGED"};\n'
                                'static const struct mk2_param a = {"ALPH", 100}, b = {"BETA", 3}, g = {"MODE", 9};\n'
                                'static const struct mk2_page two[2] = {{{&a, 0, &b}}, {{0}}};\n'
                                'const struct mk2_voice_pages pages = {8, 2, two};\n'
                                'const struct mk2_filter filter = {9, "GEE", "GEE", 0, &g};\n',
                                tables, [('mk2-voice', 8), ('mk2-filter', 9)]))
            mods.append(mk2_mod(work, 'five', 'const struct mk2_synth voice5 = {10, "WIDE", "WIDE"};\n'
                                'static const struct mk2_param o = {"OTHER", 7}, l = {"LAST", 50}, g = {"TONE", 20};\n'
                                'static const struct mk2_page five[5] = {{{&o}}, {{0}}, {{0}}, {{0}}, {{[7] = &l}}};\n'
                                'const struct mk2_voice_pages pages5 = {10, 5, five};\n'
                                'const struct mk2_filter filter5 = {11, "TONE", "TONE", 0, &g};\n',
                                [(t, n + '5') for t, n in tables], [('mk2-voice', 10), ('mk2-filter', 11)]))
            outputs, manifest = patch.build(STOCK, mods, 'TEST', log=lambda *args: None)
            firmware = work / 'test.syx'
            firmware.write_bytes(outputs['syx'])
            page_count, slot_code, cc_code, name = 0x400c2636, 0x400dc18e, 0x400dc978, 0x400dc496
            # Seven pages in all, past the five of 0.2's pool. A slot is one row for every voice; G's is
            # one row for every filter with a G; MIDI follows.
            rows = cf_calls(firmware, [
                (page_count, [8]), (page_count, [10]),
                (slot_code, [25, 8, 0]), (slot_code, [25, 10, 0]), (slot_code, [27, 8, 0]), (slot_code, [27, 10, 0]),
                (slot_code, [64, 10, 0]), (slot_code, [64, 8, 0]),
                (slot_code, [66, 0, 9]), (slot_code, [66, 0, 11]), (cc_code, [0, 10, 11, 18]),
                (cc_code, [0, 10, 0, 40]), (cc_code, [0, 8, 9, 42])])
            self.assertTrue(all(r['ok'] for r in rows), rows)
            self.assertEqual([r['d'][0] for r in rows], [2, 5, 321, 321, 323, 0, 360, 0, 361, 361, 361, 321, 323])
            # The shared rows' defaults are the active track's machine's (the range accessor's dn2_range).
            mod = json.loads((ROOT / 'mods/machines-dn2/mod.json').read_text())['defsym']
            sound, out = int(mod['SOUNDS'], 16), 0x47f3a000
            for synth, filt, want in ((8, 9, [100, None, 9]), (10, 11, [7, 50, 20])):
                rows = cf_calls(firmware, [(manifest['_map']['dn2_range'], [hex(out), code],
                                            {'addr': hex(out), 'len': 12}) for code in (321, 360, 361)],
                                pokes=[{'addr': mod['ACTIVE_TRACK'], 'hex': '00'},
                                       {'addr': hex(sound + 0xde), 'hex': '%02x%02x' % (synth, filt)}])
                defaults = [int(r['reads'][0][16:24], 16) >> 8 for r in rows]
                self.assertEqual([d if w is not None else None for d, w in zip(defaults, want)], want, synth)
                self.assertEqual({r['reads'][0][:16] for r in rows}, {'00000000' '00007f00'})

    def test_an_sdk_machine_with_a_render_sends_its_own_type(self):
        from unittest import mock
        from elekloader import patch
        from elekloader.sdk import build as sdk
        from elekloader.sdk.sdk_alpha_build import sdk_alpha_build

        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            mods = [sdk.build(str(ROOT / 'mods' / source), STOCK, str(work / source))[0]
                    for source in ('core-dn2', 'machines-dn2')]
            stage = work / 'renders'
            stage.mkdir()
            (stage / 'machine.c').write_text(
                '#include "elekloader/sdk_alpha_machine.h"\n'
                'void tone_render(const struct elek_voice *v, elek_sample *out);\n'
                'static const struct elek_machine tone = {.id = 40, .name = "Tone", .short_name = "TONE",'
                ' .render = tone_render}, quiet = {.id = 41, .name = "Quiet", .short_name = "QUIE"};\n'
                'ELEK_REGISTER_MACHINE(tone);\nELEK_REGISTER_MACHINE(quiet);\n')
            (stage / 'tone.s').write_text('.SECTION/SW seg_swco;\n.GLOBAL tone_render.;\ntone_render.:\n')
            (stage / 'mod.json').write_text(json.dumps({
                'id': 'renders', 'version': '0.1', 'devices': {'digitone-mk2': ['1.12']}, 'sources': ['machine.c'],
                'sharc': {'sources': ['tone.s']}}))
            with mock.patch('elekloader.sdk.sharc.kernel_artifact', lambda sources, work, kernels:
                            kernel(*[(k[0], k[1]) for k in kernels])):
                mods.append(sdk_alpha_build(str(stage), STOCK, str(stage / 'build'))[0])
            outputs, manifest = patch.build(STOCK, mods, 'TEST', log=lambda *args: None)
            firmware = work / 'test.syx'
            firmware.write_bytes(outputs['syx'])
            # The frame's synth type (dn2_frame_type): the render's kernel plays 40; FM TONE plays 41.
            rows = cf_calls(firmware, [(manifest['_map']['dn2_dsp_type'], [t]) for t in (40, 41, 0)])
            self.assertTrue(all(r['ok'] for r in rows), rows)
            self.assertEqual([r['d'][0] for r in rows], [40, 0, 0])

    def test_curve_plotter_draws_only_inside_the_graph(self):
        from elekloader import patch, syx
        from elekloader.sdk import build as sdk

        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            base = [sdk.build(str(ROOT / 'mods' / source), STOCK, str(work / source))[0]
                    for source in ('core-dn2', 'machines-dn2')]
            # In range, above 16 (drawn at the top) and 0xff (no dot), across all 51 columns; g is 0
            # without a declared G.
            mod = mk2_mod(work, 'curve-test',
                          'static void curve(uint32_t freq, uint32_t reso, uint32_t g, uint8_t y[51]) {\n'
                          '    for (unsigned x = 0; x < 51; ++x) y[x] = x % 4 == 3 ? 0xff : x < 17 ? x : 200;\n'
                          '    y[0] = freq >> 8; y[1] = reso >> 8; y[2] = g; }\n'
                          'const struct mk2_filter custom = {8,"CURVE","CURVE",curve};\n',
                          [('mk2_voice_filters', 'custom')], [('mk2-filter', 8)])
            stage = work / 'curve-test'
            outputs, manifest = patch.build(STOCK, base + [mod], 'TEST', log=lambda *args: None)
            firmware = stage / 'test.syx'
            firmware.write_bytes(outputs['syx'])
            # A 128 x 64 canvas, stored by column, 2 words a column (the stock pixel writer's layout).
            canvas, data = 0x47f3a000, 0x47f3a100
            header = bytearray(0x18)
            header[4:8], header[8:12] = (128).to_bytes(4, 'big'), (64).to_bytes(4, 'big')
            header[12:16], header[16:20] = (2).to_bytes(4, 'big'), data.to_bytes(4, 'big')
            pokes = [{'addr': hex(canvas), 'hex': header.hex()}, {'addr': hex(data), 'hex': '00' * 1024}]
            draw = manifest['_map']['dn2_filter_draw_curve']
            calls = [{'pc': hex(draw), 'args': [str(t), hex(canvas), '0x0c00', '0x0500', '0'],
                      'reads': [{'addr': hex(data), 'len': 1024}]} for t in (8, 1, 6, 9)]
            request = stage / 'request.json'
            request.write_text(json.dumps({'pokes': pokes, 'calls': calls}))
            result = subprocess.run([CF_CALL, str(firmware), str(request), '--base-sha', syx.Syx.load(STOCK).sha256],
                                    capture_output=True, text=True, timeout=300)
            self.assertEqual(result.returncode, 0, result.stderr)
            rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            self.assertEqual(len(rows), 4)
            expected = set()
            for x in range(51):
                v = 12 if x == 0 else 5 if x == 1 else 0 if x == 2 else None if x % 4 == 3 else min(x, 16)
                if v is not None:
                    expected.add((21 + x, 7 + v))  # canvas y counts from the bottom
            # The canvas persists across the calls: only the custom ID draws, the others add nothing.
            for row in rows:
                self.assertTrue(row['ok'], row)
                self.assertEqual(row['d'][2:], row['d_in'][2:])
                self.assertEqual(row['a'][2:7], row['a_in'][2:7])
                self.assertEqual(row['a'][7], row['sp0'] + 4)
                bits = bytes.fromhex(row['reads'][0])
                drawn = {(x, y) for x in range(128) for y in range(64)
                         if int.from_bytes(bits[8 * x + 4 * (y >> 5):8 * x + 4 * (y >> 5) + 4], 'big')
                         & (0x80000000 >> (y & 31))}
                self.assertEqual(drawn, expected)
                self.assertTrue(all(21 <= x <= 71 and 7 <= y <= 23 for x, y in drawn))


if __name__ == '__main__':
    unittest.main()
