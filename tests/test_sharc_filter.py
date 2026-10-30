# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded synth/filter pairing; synthetic kernel instructions are never executed."""
import copy
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from elekloader import devices, elemod, formats, link, sharc
from elekloader.devices.digitone_mk2 import DEVICE
from elekloader.sdk import build as sdk
from elekloader.sdk.sharc import filter_artifact, machine_layout, voice_artifact

ROOT = Path(__file__).resolve().parents[1]
PROFILE = DEVICE.releases['1.12'].sharc
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


def artifact():
    return filter_artifact(bytes(4), bytes(4), 0, 6, 20)


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

    def test_filter_layout_and_producer_bounds(self):
        payload = sharc.module_payload(artifact(), PROFILE)
        self.assertEqual(payload['owner'], (0x200a0000, 0x200b0000))
        self.assertEqual(payload['machine_kind'], 'filter')
        self.assertEqual(payload['machine_id'], 6)
        self.assertEqual(payload['entries'], {'render_offset_bytes': 2048})
        self.assertEqual(machine_layout('filter'), {
            'code_pc16': 0xbd0400, 'code_bytes': 30720,
            'data_address': 0x200a8000, 'data_bytes': 31744})
        self.assertEqual(machine_layout('voice'), {
            'code_pc16': 0xbc8400, 'code_bytes': 30720,
            'data_address': 0x20098000, 'data_bytes': 31744})
        for code, data in ((bytes(6), bytes(4)), (bytes(30656), bytes(31744)), (bytes(4), b'')):
            doc = filter_artifact(code, data, 0, 31, 12)
            # The code ends at its NOP guard, rounded to whole words; zeros fill the rest.
            self.assertEqual(len(bytes.fromhex(doc['segments'][1]['hex'])), len(code) + 64 + len(code) % 4)
            self.assertEqual(doc['zero'][-1], {'offset_bytes': 64512, 'size_bytes': 1024})
        for code, data in ((b'', bytes(4)), (bytes(3), bytes(4)), (bytes(30658), bytes(4)),
                           (bytes(4), bytes(31748)), (bytes(4), bytes(3))):
            with self.subTest(code=len(code), data=len(data)), self.assertRaises(ValueError):
                filter_artifact(code, data, 0, 6, 12)

    def test_machine_producers_require_entry_in_supplied_code(self):
        code = bytes(12)
        for producer in (voice_artifact, filter_artifact):
            for entry in (0, len(code) - 2):
                with self.subTest(producer=producer.__name__, entry=entry):
                    doc = producer(code, b'', entry, 6, 0)
                    self.assertEqual(doc['entries'], {'render_offset_bytes': 2048 + entry})
            for entry in (-2, 1, len(code), len(code) + 2, 6078, True, False, 0.0, '0', None):
                with self.subTest(producer=producer.__name__, entry=entry), self.assertRaises(ValueError):
                    producer(code, b'', entry, 6, 0)

    def test_filter_refuses_invalid_identity_entry_and_owner(self):
        cases = [
            (lambda d: d.update(machine_id=True), 'machine_id'),
            (lambda d: d.update(machine_id=5), 'machine_id'),
            (lambda d: d.update(machine_id=32), 'machine_id'),
            (lambda d: d.update(stack_bytes=2049), 'filter stack'),
            (lambda d: d.update(entries=None), 'render entry'),
            (lambda d: d['entries'].update(render_offset_bytes=0), 'adapter reservation'),
            (lambda d: d['entries'].update(render_offset_bytes=2052), 'read-ahead guard'),
            (lambda d: d['entries'].update(render_offset_bytes=2116), 'not in owned code'),
            (lambda d: d['segments'][0].update(hex='0000' + d['segments'][0]['hex'][4:]), 'qualified prefix'),
            (lambda d: d['segments'][1].update(hex=d['segments'][1]['hex'][:-4] + '0000'), 'read-ahead guard'),
            (lambda d: d['segments'][2].update(offset_bytes=65536), 'exceeds its owner'),
            (lambda d: d['zero'][0].update(offset_bytes=32768), 'gap or overlap'),
            (lambda d: d['zero'][-1].update(offset_bytes=64516, size_bytes=1020), 'gap or overlap'),
        ]
        for change, error in cases:
            doc = artifact()
            change(doc)
            with self.subTest(error=error), self.assertRaisesRegex(elemod.ModError, error):
                sharc.module_payload(doc, PROFILE)

    def test_adapter_state_cannot_be_replaced_with_author_data(self):
        # Preserve complete owner coverage, but write inside the adapter's state.
        doc = artifact()
        doc['segments'].append({'offset_bytes': 64512, 'kind': 'data', 'hex': '00000000'})
        doc['zero'][-1] = {'offset_bytes': 64516, 'size_bytes': 1020}
        with self.assertRaisesRegex(elemod.ModError, 'adapter reservation'):
            sharc.module_payload(doc, PROFILE)

    def test_filter_code_can_change_without_allowlist(self):
        doc = artifact()
        doc['segments'][1]['hex'] = '01000100' + doc['segments'][1]['hex'][8:]
        sharc.module_payload(doc, PROFILE)


@unittest.skipUnless(STOCK and os.environ.get('ELEKLOADER_CROSS'), 'needs owner stock and cross compiler')
class PairLinkTests(unittest.TestCase):
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
        for kind, producer, native in (('voice', voice_artifact, 'synth'), ('filter', filter_artifact, 'filter')):
            for machine_id in (6, 7):
                name = '%s-%d' % (kind, machine_id)
                symbol = '%s_%d' % (kind, machine_id)
                p = root / name
                p.mkdir()
                (p / 'machine.c').write_text('#include "mk2/machines.h"\n'
                    'const struct mk2_%s %s = {%d,"PAIR","PAIR"};\n' % (native, symbol, machine_id))
                (p / 'kernel.json').write_text(json.dumps(producer(bytes(4), bytes(4), 0, machine_id,
                                                                  128 if kind == 'voice' else 12,
                                                                  voice_bytes=8 if machine_id == 7 else 0)))
                (p / 'mod.json').write_text(json.dumps({
                    'id': name, 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                    'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'},
                    'resources': {'core': '0.1'}, 'requires': ['core', 'machines-dn2'],
                    'contribute': [{'to': PROFILE[kind + '_table'], 'data': '00000000',
                                    'relocs': [[0, 'abs32', 'sym:' + symbol, 0]]}]}))
                cls.mods[name] = elemod.load_any(sdk.build(str(p), STOCK, str(p / 'build'))[0])

    def linked(self, *mods):
        return link.link(self.base + list(mods), self.image, sharc_stream=self.stream)

    def test_refuses_an_invalid_g_and_links_a_valid_one(self):
        refusal = 'g-%s 0.1: DN2 filter G needs a label of 1..5 printable ASCII characters and a default of 0..127'
        cases = {'valid': ('"MODE", 0', True), 'top': ('"HIGH5", 127', True), 'none': (None, True),
                 'default': ('"MODE", 128', False), 'long': ('"TOOWIDE", 1', False),
                 'empty': ('"", 1', False), 'unprintable': ('"M\\tX", 1', False), 'nolabel': ('0, 1', False)}
        for name, (param, ok) in cases.items():
            with self.subTest(name=name):
                p = Path(self.tmp.name) / ('g-' + name)
                p.mkdir()
                g = 'static const struct mk2_param g = {%s};\n' % param if param else ''
                (p / 'machine.c').write_text('#include "mk2/machines.h"\n' + g +
                    'const struct mk2_filter f = {8, "G", "G", 0, %s};\n' % ('&g' if param else '0'))
                (p / 'kernel.json').write_text(json.dumps(filter_artifact(bytes(4), bytes(4), 0, 8, 12)))
                (p / 'mod.json').write_text(json.dumps({
                    'id': 'g-' + name, 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                    'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'},
                    'resources': {'core': '0.1'}, 'requires': ['core', 'machines-dn2'],
                    'contribute': [{'to': PROFILE['filter_table'], 'data': '00000000',
                                    'relocs': [[0, 'abs32', 'sym:f', 0]]}]}))
                mod = elemod.load_any(sdk.build(str(p), STOCK, str(p / 'build'))[0])
                errors = link.check(self.base + [mod], self.image)
                self.assertEqual(errors == [], ok, errors)
                if not ok:
                    self.assertEqual(errors, [refusal % name])

    def at(self, stream, address, size=6):
        rec = next(r for r in sharc.parse(stream, PROFILE) if r.flags == sharc.LOAD
                   and r.target <= address and address + size <= r.target + r.count)
        offset = 16 + address - rec.target
        return rec.raw[offset:offset + size]

    def owned_records(self, stream, kind):
        lo, hi = PROFILE[kind + '_range']
        return [r.raw for r in sharc.parse(stream, PROFILE)
                if r.flags in (sharc.LOAD, sharc.FILL) and lo <= r.target < hi]

    def test_pair_binds_both_hooks_and_is_order_independent(self):
        voice, filt = self.mods['voice-6'], self.mods['filter-6']
        self.assertEqual(link.check(self.base + [voice, filt], self.image), [])
        out = self.linked(voice, filt)
        reverse = link.link([filt, voice] + list(reversed(self.base)), self.image, sharc_stream=self.stream)
        self.assertEqual(out.image, reverse.image)
        self.assertEqual(out.sharc_stream, reverse.sharc_stream)
        self.assertEqual(out.sharc_stored, reverse.sharc_stored)
        self.assertEqual(out.layout, reverse.layout)
        records = sharc.parse(out.sharc_stream, PROFILE)
        for kind, binding, pc in (('voice', 0x2009fe00, 0xbc8400), ('filter', 0x200afe00, 0xbd0400)):
            self.assertEqual(next(r.raw[16:] for r in records if r.target == binding), struct.pack('<II', pc, 6))
            site, _old, new = PROFILE[kind + '_hook']
            self.assertEqual(self.at(out.sharc_stream, site), bytes.fromhex(new))
        self.assertEqual(self.at(out.sharc_stream, PROFILE['hook'][0]), self.at(self.stream, PROFILE['hook'][0]))
        site, _old, new = PROFILE['filter_capture']
        self.assertEqual(self.at(out.sharc_stream, site, 4), bytes.fromhex(new))
        self.assertEqual(out.layout['sharc']['mods'], [
            {'id': 'filter-6', 'owner': list(PROFILE['filter_range']), 'hook': PROFILE['filter_hook'][0]},
            {'id': 'voice-6', 'owner': list(PROFILE['voice_range']), 'hook': PROFILE['voice_hook'][0]}])
        self.assertNotIn('app', out.layout['sharc'])

    def test_a_sharc_build_carries_core_dn2s_dsp_peek(self):
        out = self.linked(self.mods['voice-6'])
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
        mods = self.base + [self.mods['voice-6'], self.busy]
        self.assertEqual(link.check(mods, self.image), [])
        plain = self.linked(self.mods['voice-6'])
        busy = self.linked(self.mods['voice-6'], self.busy)
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

    def test_each_machine_gets_its_own_guarded_stack_in_l1(self):
        for kind, other in (('voice', 'filter'), ('filter', 'voice')):
            lo, hi = PROFILE[kind + '_stack']
            with self.subTest(kind=kind):
                records = sharc.parse(self.linked(self.mods[kind + '-6']).sharc_stream, PROFILE)
                self.assertEqual(self.at(self.linked(self.mods[kind + '-6']).sharc_stream, lo, 4),
                                 struct.pack('<I', PROFILE['stack_guard']))
                self.assertTrue(any(r.flags == sharc.FILL and r.target == lo + 4 and r.count == hi - lo - 4
                                    for r in records))
                self.assertFalse(any(r.flags in (sharc.LOAD, sharc.FILL) and r.target < PROFILE[other + '_stack'][1]
                                     and PROFILE[other + '_stack'][0] < r.target + r.count for r in records))

    def test_the_binding_names_the_voice_state(self):
        for kind in ('voice', 'filter'):
            lo, binding = PROFILE[kind + '_range'][0], PROFILE[kind + '_range'][0] + PROFILE[kind + '_binding_offset']
            with self.subTest(kind=kind):
                stateful = self.linked(self.mods[kind + '-7']).sharc_stream
                self.assertEqual(self.at(stateful, binding + 0x20, 8), struct.pack('<II', lo + 32776, 8))
                self.assertEqual(self.at(self.linked(self.mods[kind + '-6']).sharc_stream, binding + 0x20, 8), bytes(8))

    def test_each_role_is_unchanged_when_the_other_is_absent(self):
        pair = self.linked(self.mods['voice-6'], self.mods['filter-6'])
        for kind, other in (('voice', 'filter'), ('filter', 'voice')):
            with self.subTest(kind=kind):
                out = self.linked(self.mods[kind + '-6'])
                self.assertEqual(self.owned_records(out.sharc_stream, kind), self.owned_records(pair.sharc_stream, kind))
                self.assertEqual(self.owned_records(out.sharc_stream, other), self.owned_records(self.stream, other))
                self.assertEqual(self.at(out.sharc_stream, PROFILE[kind + '_hook'][0]), bytes.fromhex(PROFILE[kind + '_hook'][2]))
                self.assertEqual(self.at(out.sharc_stream, PROFILE[other + '_hook'][0]), self.at(self.stream, PROFILE[other + '_hook'][0]))
                site, old, new = PROFILE['filter_capture']
                self.assertEqual(self.at(out.sharc_stream, site, 4), bytes.fromhex(new if kind == 'filter' else old))

    def test_the_capture_entry_is_in_the_adapter_and_continues_to_the_stock_arm(self):
        _site, old, new = PROFILE['filter_capture']
        stock_arm, entry = (int.from_bytes(bytes.fromhex(h), 'little') for h in (old, new))
        offset = 2 * (entry - sharc.pc16(PROFILE, PROFILE['filter_range'][0]))
        adapter = bytes.fromhex(json.loads((Path(__file__).resolve().parents[1]
                                            / 'elekloader/sdk/include/digitone-mk2/filter-bridge.json').read_text())['hex'])
        self.assertTrue(0 < offset < PROFILE['filter_adapter_bytes'] - PROFILE['code_guard_bytes'])
        def word(value):  # a 32-bit operand: high half first, each half little-endian
            return (value >> 16).to_bytes(2, 'little') + (value & 0xffff).to_bytes(2, 'little')
        jump = adapter.find(bytes.fromhex('3e06') + word(stock_arm), offset)
        self.assertTrue(offset < jump < offset + 80)  # JUMP to the arm the stock table named
        # Before it: ASTATX kept, the capture arrays' base loaded (the stores), ASTATX back.
        at = offset
        for address in (0x20097d38, 0x20097d40, 0x20097d38):
            at = adapter.find(word(address), at + 1, jump)
            self.assertNotEqual(at, -1, hex(address))

    def test_changed_filter_code_links_without_an_allowlist_update(self):
        doc = copy.deepcopy(self.mods['filter-6'].doc)
        doc['sharc']['segments'][1]['hex'] = '01000100' + doc['sharc']['segments'][1]['hex'][8:]
        changed = link.Mod2(doc, 'changed-filter')
        out = self.linked(self.mods['voice-6'], changed)
        record = next(r for r in sharc.parse(out.sharc_stream, PROFILE) if r.target == 0x200a0800)
        self.assertEqual(record.raw[16:20], bytes.fromhex('01000100'))

    def test_refuses_second_role_and_master_mix(self):
        master = copy.deepcopy(self.mods['voice-6'].doc)
        master.update(id='master-test', contribute=[])
        master['sharc'] = {'abi': PROFILE['l2_abi'],
            'segments': [{'offset_bytes': 0, 'kind': 'code', 'hex': '0100' * 34}],
            'zero': [{'offset_bytes': 68, 'size_bytes': 32700}],
            'entries': {'init_offset_bytes': 0, 'render_offset_bytes': 2}, 'stack_bytes': 12}
        master = link.Mod2(master, 'master')
        self.assertEqual(link.check(self.base + [master], self.image), [])
        out = self.linked(master)
        self.assertEqual(out.layout['sharc']['app'], 'master-test')
        self.assertEqual(out.layout['sharc']['owner'], list(PROFILE['l2_app_range']))
        self.assertNotIn('mods', out.layout['sharc'])
        for mod in self.mods.values():
            self.assertEqual(link.check(self.base + [mod], self.image), [])
        cases = [[self.mods['voice-6'], self.mods['voice-7']],
                 [self.mods['filter-6'], self.mods['filter-7']],
                 [self.mods['voice-6'], self.mods['filter-6'], self.mods['filter-7']],
                 [master, self.mods['voice-6']], [master, self.mods['filter-6']]]
        for mods in cases:
            with self.subTest(mods=[m.id for m in mods]):
                self.assertIn('SHARC mods may combine only one DN2 voice and one DN2 filter',
                              link.check(self.base + mods, self.image))

    def test_refuses_wrong_missing_and_cross_role_registration(self):
        for kind, other in (('voice', 'filter'), ('filter', 'voice')):
            original = self.mods[kind + '-6']
            missing = '%s: DN2 %s registration requires a matching SHARC %s payload' % (original.label(), kind, kind)
            mismatch = '%s: SHARC %s machine_id must match its single native %s registration' % (original.label(), kind, kind)
            cases = [
                (lambda d: d.pop('sharc'), missing),
                (lambda d: d['sharc'].update(machine_id=7), mismatch),
                (lambda d: d.update(contribute=[]), mismatch),
                (lambda d: d['contribute'][0].update(to=PROFILE[other + '_table']), mismatch),
            ]
            for change, error in cases:
                doc = copy.deepcopy(original.doc)
                change(doc)
                with self.subTest(kind=kind, error=error):
                    errors = link.check(self.base + [link.Mod2(doc, 'invalid')], self.image)
                    self.assertIn(error, errors)
                    if doc['contribute'] and doc['contribute'][0]['to'] == PROFILE[other + '_table']:
                        self.assertIn('%s: DN2 %s registration requires a matching SHARC %s payload'
                                      % (original.label(), other, other), errors)
            # The old registration tables are gone: the generic check refuses an entry there.
            legacy = copy.deepcopy(original.doc)
            table = 'mk2_synths' if kind == 'voice' else 'mk2_filters'
            legacy['contribute'].append(dict(legacy['contribute'][0], to=table))
            self.assertIn('%s adds to table %s, which no given mod declares' % (original.label(), table),
                          link.check(self.base + [link.Mod2(legacy, 'legacy')], self.image))
        errors = link.check([self.base[0], self.mods['filter-6']], self.image)
        self.assertTrue(any('requires machines-dn2' in s for s in errors), errors)
