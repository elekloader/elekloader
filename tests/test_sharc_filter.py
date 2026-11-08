# SPDX-License-Identifier: GPL-2.0-or-later
"""DN2 SHARC builds with the real core-dn2, machines-dn2 and dspbusy-dn2: the qualified data site, the DSP
peek, the capture entry and several synths and filters side by side. Synthetic kernels are never run."""
import copy
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
from elekloader import devices, elemod, formats, link, sharc   # noqa: E402
from elekloader.devices.digitone_mk2 import DEVICE   # noqa: E402
from elekloader.sdk import build as sdk   # noqa: E402
from test_sharc_kernels import DISPATCHER, INERT, kernel   # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROFILE = DEVICE.releases['1.12'].sharc
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


class EnvelopeTests(unittest.TestCase):
    def test_only_the_exact_profiled_sharc_data_site_is_accepted(self):
        address, (old, new) = next(iter(PROFILE['data_sites'].items()))
        doc = {'elemod': 2, 'id': 'busy', 'version': '0.1',
               'target': devices.target_of(DEVICE, DEVICE.releases['1.12']), 'sections': {},
               'sites': [{'addr': address, 'len': len(bytes.fromhex(old)), 'kind': 'data',
                          'stock_sha256': elemod.sha(bytes.fromhex(old)), 'new': new}]}
        self.assertEqual(len(link.Mod2(doc).sites), 1)
        for change in ({'new': old}, {'kind': 'code'}, {'stock_sha256': '0' * 64},
                       {'relocs': [[0, 'abs32', 'abs', 0]]}, {'len': 4, 'new': new[:8]}):
            bad = copy.deepcopy(doc)
            bad['sites'][0].update(change)
            with self.subTest(change=change), self.assertRaisesRegex(elemod.ModError, 'unqualified SHARC data site'):
                link.Mod2(bad)
        bad = copy.deepcopy(doc)
        bad['sites'][0]['addr'] += 2
        with self.assertRaisesRegex(elemod.ModError, 'outside the stock image'):
            link.Mod2(bad)
        with self.assertRaisesRegex(elemod.ModError, 'outside the stock image'):
            elemod.parse_sites(doc, 'format-1', DEVICE, DEVICE.releases['1.12'])

    def test_the_capture_entry_keeps_the_slot_and_continues_to_the_stock_arm(self):
        _site, stock_arm = PROFILE['filter_capture']
        code = bytes.fromhex(DISPATCHER['code'])              # the executable's order: whole instructions
        entry = 2 * DISPATCHER['exports']['dn2_filter_capture.'][1]
        jump = code.find(bytes.fromhex('063e00') + int.from_bytes(bytes.fromhex(stock_arm), 'little').to_bytes(3, 'big'),
                         entry)
        self.assertTrue(entry < jump < entry + 80)           # JUMP to the arm the stock table named


@unittest.skipUnless(STOCK and os.environ.get('ELEKLOADER_CROSS'), 'needs owner stock and cross compiler')
class BuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        root = Path(cls.tmp.name)
        cls.stock, cls.dev, cls.rel = formats.load(STOCK)
        cls.image = formats.main_image(cls.stock, cls.dev)
        cls.stream = cls.stock.section(7)
        cls.base = [elemod.load_any(sdk.build(str(ROOT / 'mods' / src), STOCK, str(root / src))[0])
                    for src in ('core-dn2', 'machines-dn2')]
        cls.busy = elemod.load_any(sdk.build(str(ROOT / 'mods/dspbusy-dn2'), STOCK, str(root / 'busy'))[0])
        cls.mods = {}
        for name, machines in (('synths', [('synth', 6), ('synth', 7)]), ('filters', [('filter', 6), ('filter', 7)]),
                               ('synth-9', [('synth', 9)])):
            p = root / name
            p.mkdir()
            (p / 'machine.c').write_text('#include "mk2/machines.h"\n' + ''.join(
                'const struct mk2_%s %s%d = {%d,"M%d","M%d"};\n' % (kind, kind, mid, mid, mid, mid) for kind, mid in machines))
            (p / 'kernel.json').write_text(json.dumps(kernel(*[('mk2-voice' if kind == 'synth' else 'mk2-filter', mid,
                                                                {'state_bytes': 8}) for kind, mid in machines])))
            (p / 'mod.json').write_text(json.dumps({
                'id': name, 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'},
                'resources': {'core': '0.1'}, 'requires': ['core', 'machines-dn2'],
                'contribute': [{'to': 'mk2_voice_synths' if kind == 'synth' else 'mk2_voice_filters', 'data': '00000000',
                                'relocs': [[0, 'abs32', 'sym:%s%d' % (kind, mid), 0]]} for kind, mid in machines]}))
            cls.mods[name] = elemod.load_any(sdk.build(str(p), STOCK, str(p / 'build'))[0])

    def linked(self, *mods):
        return link.link(self.base + list(mods), self.image, sharc_stream=self.stream)

    def at(self, stream, address, size=6):
        rec = next(r for r in sharc.parse(stream, PROFILE) if r.flags == sharc.LOAD
                   and r.target <= address and address + size <= r.target + r.count)
        offset = 16 + address - rec.target
        return rec.raw[offset:offset + size]

    def test_several_synths_and_filters_from_several_mods(self):
        mods = [self.mods['synths'], self.mods['filters'], self.mods['synth-9']]
        self.assertEqual(link.check(self.base + mods, self.image), [])
        out = self.linked(*mods)
        reverse = link.link(list(reversed(mods)) + list(reversed(self.base)), self.image, sharc_stream=self.stream)
        self.assertEqual((out.image, out.sharc_stream, out.layout), (reverse.image, reverse.sharc_stream, reverse.layout))
        self.assertEqual([m['id'] for m in out.layout['sharc']['kernels']['mods']],
                         ['machines-dn2', 'filters', 'synth-9', 'synths'])
        self.assertNotIn('app', out.layout['sharc'])
        tables = out.layout['sharc']['kernels']['mods'][0]['data'][0]
        for table, mid in ((0, 6), (0, 7), (0, 9), (2048, 6), (2048, 7)):
            self.assertNotEqual(struct.unpack('<I', self.at(out.sharc_stream, tables + table + 16 * mid, 4))[0], 0)
        self.assertEqual(self.at(out.sharc_stream, tables + 16 * 8, 4), bytes(4))

    def test_kernels_and_a_master_mod_do_not_combine(self):
        master = copy.deepcopy(self.mods['synth-9'].doc)
        master.update(id='master-test', contribute=[], sharc=INERT)
        master = link.Mod2(master, 'master')
        self.assertEqual(link.check(self.base + [master], self.image), [])
        out = self.linked(master)
        self.assertEqual(out.layout['sharc']['app'], 'master-test')
        self.assertIn('DN2 machine kernels do not combine with master-test 0.1',
                      link.check(self.base + [master, self.mods['synths']], self.image))

    def test_a_sharc_build_carries_core_dn2s_dsp_peek(self):
        out = self.linked(self.mods['synth-9'])
        lo, hi = PROFILE['peek_range']
        code = bytes.fromhex(PROFILE['peek_code'])
        self.assertEqual(self.at(out.sharc_stream, lo, len(code)), code)
        site, old, new = PROFILE['peek_hook']
        self.assertEqual(self.at(self.stream, site), bytes.fromhex(old))
        self.assertEqual(self.at(out.sharc_stream, site), bytes.fromhex(new))
        for site, (old, _new) in PROFILE['data_sites'].items():
            self.assertEqual(self.at(out.sharc_stream, site), bytes.fromhex(old))
        records = sharc.parse(out.sharc_stream, PROFILE)
        self.assertTrue(any(r.flags == sharc.FILL and r.target == lo + len(code) and r.count == hi - lo - len(code)
                            for r in records))

    def test_busy_companion_requires_a_sharc_mod_and_owns_only_its_site(self):
        self.assertEqual(self.busy.sections, {})
        self.assertIsNone(self.busy.sharc)
        self.assertEqual(link.check(self.base + [self.busy], self.image),
                         ['dspbusy-dn2 0.1: SHARC data site requires a SHARC mod'])
        mods = self.base + [self.mods['synth-9'], self.busy]
        self.assertEqual(link.check(mods, self.image), [])
        plain = self.linked(self.mods['synth-9'])
        busy = self.linked(self.mods['synth-9'], self.busy)
        self.assertEqual(plain.image, busy.image)
        address, (old, new) = next(iter(PROFILE['data_sites'].items()))
        self.assertEqual(self.at(plain.sharc_stream, address), bytes.fromhex(old))
        self.assertEqual(self.at(busy.sharc_stream, address), bytes.fromhex(new))
        changed = [i for i, (a, b) in enumerate(zip(plain.sharc_stream, busy.sharc_stream)) if a != b]
        record = next(r for r in sharc.parse(self.stream, PROFILE)
                      if r.flags == sharc.LOAD and r.target <= address < r.target + r.count)
        offset = record.offset + 16 + address - record.target
        self.assertEqual(changed, [offset + i for i, (a, b) in enumerate(zip(bytes.fromhex(old), bytes.fromhex(new))) if a != b])
        duplicate = copy.deepcopy(self.busy.doc)
        duplicate['id'] = 'other-busy'
        self.assertTrue(any('overlap' in e for e in link.check(mods + [link.Mod2(duplicate)], self.image)))


if __name__ == '__main__':
    unittest.main()
