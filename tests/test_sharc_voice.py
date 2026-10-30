# SPDX-License-Identifier: GPL-2.0-or-later
"""Voice ABI envelope and ownership; synthetic instructions are never executed."""
import copy
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from elekloader import elemod, formats, link, sharc
from elekloader.devices.digitone_mk2 import DEVICE
from elekloader.sdk import build as sdk
from elekloader.sdk.sharc import filter_artifact, voice_artifact

ROOT = Path(__file__).resolve().parents[1]
PROFILE = DEVICE.releases['1.12'].sharc
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


def artifact():
    return voice_artifact(b'\x01\x00' * 2, bytes(4), 0, 5, 128)


class EnvelopeTests(unittest.TestCase):
    def test_adapter_shift_counts_are_sign_extended(self):
        malformed = sharc.suspect_shifts
        # Type 6b's negative count needs DATAEX=0xf; missing it halts real DSPs.
        good = bytes.fromhex('3e0200002210 3e02007822f0')
        bad = bytes.fromhex('3e02000022f0')
        self.assertEqual(malformed(good), [])
        self.assertEqual(malformed(bad), [0])
        for doc in (artifact(), filter_artifact(bytes(4), bytes(4), 0, 6, 12)):
            code = bytes.fromhex(doc['segments'][0]['hex'])
            self.assertEqual(malformed(code), [])
            self.assertIn(0, malformed(bad + code[6:]))
        self.assertEqual(malformed(bytes.fromhex(PROFILE['peek_code'])), [])   # core-dn2's DSP peek

    def test_lint_notes_negative_shift_immediates_past_the_adapter(self):
        from types import SimpleNamespace
        from elekloader import lint
        doc = voice_artifact(b'\x01\x00' * 2 + bytes.fromhex('3e02000022f0'), bytes(4), 0, 5, 128)
        mod = SimpleNamespace(id='x', sharc=sharc.module_payload(doc, PROFILE), rel=SimpleNamespace(sharc=PROFILE))
        self.assertEqual(lint.sharc_shifts(mod), [{'mod': 'x', 'offset': 2048 + 4}])
        mod.sharc = sharc.module_payload(artifact(), PROFILE)
        self.assertEqual(lint.sharc_shifts(mod), [])

    def test_voice_envelope_and_mutations(self):
        doc = artifact()
        payload = sharc.module_payload(doc, PROFILE)
        self.assertEqual(payload['machine_id'], 5)
        self.assertEqual(payload['machine_kind'], 'voice')
        self.assertEqual(payload['owner'], (0x20090000, 0x200a0000))
        self.assertEqual(payload['entries'], {'render_offset_bytes': 2048})
        cases = [
            (lambda d: d.update(machine_id=True), 'machine_id'),
            (lambda d: d.update(machine_id=4), 'machine_id'),
            (lambda d: d.update(machine_id=32), 'machine_id'),
            (lambda d: d.update(stack_bytes=2049), 'voice stack'),
            (lambda d: d.update(entries=None), 'render entry'),
            (lambda d: d['entries'].update(render_offset_bytes=0), 'adapter reservation'),
            (lambda d: d['entries'].update(render_offset_bytes=2052), 'read-ahead guard'),
            (lambda d: d['segments'][0].update(hex='0000'+d['segments'][0]['hex'][4:]), 'qualified prefix'),
            (lambda d: d['segments'][1].update(hex=d['segments'][1]['hex'][:-4]+'0000'), 'read-ahead guard'),
            (lambda d: d['zero'][-1].update(offset_bytes=64516, size_bytes=1020), 'gap or overlap'),
        ]
        for change, error in cases:
            bad = copy.deepcopy(doc)
            change(bad)
            with self.subTest(error=error), self.assertRaisesRegex(elemod.ModError, error):
                sharc.module_payload(bad, PROFILE)
        # A machine has its own stack, so it may declare more than the master-output limit.
        self.assertGreater(PROFILE['voice_stack_max'], PROFILE['stack_max'])
        sharc.module_payload(dict(copy.deepcopy(doc), stack_bytes=PROFILE['voice_stack_max']), PROFILE)
        # Author code may change without a per-kernel allowlist update.
        doc['segments'][1]['hex'] = '0000' + doc['segments'][1]['hex'][4:]
        sharc.module_payload(doc, PROFILE)

    def test_voice_state(self):
        doc = voice_artifact(b'\x01\x00' * 2, bytes(4), 0, 5, 128, voice_bytes=12)
        self.assertEqual(doc['voice_state'], {'offset_bytes': 32776, 'voice_bytes': 12})
        self.assertEqual(sharc.module_payload(doc, PROFILE)['voice_state'], (32776, 12))
        self.assertIsNone(sharc.module_payload(artifact(), PROFILE)['voice_state'])
        shape, inside = 'needs exactly offset_bytes, voice_bytes', '16 zeroed voices inside the mod'
        for change, error in [
            (lambda v: None, shape), (lambda v: dict(v, extra=1), shape), (lambda v: [32776, 12], shape),
            (lambda v: dict(v, offset_bytes=32780), inside), (lambda v: dict(v, voice_bytes=0), inside),
            (lambda v: dict(v, voice_bytes=6), inside), (lambda v: dict(v, voice_bytes=1984), inside),
            (lambda v: dict(v, offset_bytes=64512, voice_bytes=4), inside),   # the adapter's state
            (lambda v: dict(v, offset_bytes=32768), inside)]:                 # the data
            bad = copy.deepcopy(doc)
            bad['voice_state'] = change(bad['voice_state'])
            with self.subTest(voice_state=bad['voice_state']), self.assertRaisesRegex(elemod.ModError, error):
                sharc.module_payload(bad, PROFILE)
        # The data and 16 voices of state share the 7 KiB data area.
        voice_artifact(bytes(4), bytes(31744 - 16 * 64), 0, 5, 128, voice_bytes=64)
        for data, each in ((bytes(31744 - 16 * 64 + 4), 64), (bytes(4), 6), (bytes(4), -4)):
            with self.assertRaises(ValueError):
                voice_artifact(bytes(4), data, 0, 5, 128, voice_bytes=each)

    def test_producer_bounds(self):
        # SHARC instructions are short-word aligned; padding makes the load four-byte aligned.
        doc = voice_artifact(bytes(6), bytes(4), 0, 5, 128)
        self.assertEqual(len(bytes.fromhex(doc['segments'][1]['hex'])), 72)
        voice_artifact(bytes(30656), bytes(31744), 0, 31, 128)
        for code, data in ((bytes(30658), bytes(4)), (bytes(4), bytes(31748)), (bytes(3), bytes(4))):
            with self.assertRaises(ValueError):
                voice_artifact(code, data, 0, 5, 128)


@unittest.skipUnless(STOCK and os.environ.get('ELEKLOADER_CROSS'), 'needs owner stock and cross compiler')
class LinkTests(unittest.TestCase):
    def test_matching_native_registration_and_checked_hook(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'voice.c').write_text('#include "mk2/machines.h"\n'
                                      'const struct mk2_synth sine = {5,"SINE","SINE"};\n')
            (p / 'voice.json').write_text(json.dumps(artifact()))
            manifest = {'id': 'voice-test', 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                        'sources': ['voice.c'], 'sharc': {'artifact': 'voice.json'},
                        'resources': {'core': '0.1'}, 'requires': ['core', 'machines-dn2'],
                        'contribute': [{'to': 'mk2_voice_synths', 'data': '00000000',
                                        'relocs': [[0,'abs32','sym:sine',0]]}]}
            (p / 'mod.json').write_text(json.dumps(manifest))
            paths = [sdk.build(str(src), STOCK, str(p / name))[0] for name, src in
                     (('core', ROOT/'mods/core-dn2'), ('machines', ROOT/'mods/machines-dn2'), ('voice',p))]
            core, machines, voice = [elemod.load_any(f) for f in paths]
            stock, dev, rel = formats.load(STOCK)
            image = formats.main_image(stock, dev)
            self.assertEqual(link.check([core, machines, voice], image), [])
            out = link.link([core, machines, voice], image, sharc_stream=stock.section(7))
            # Every filter/parameter detour must land on its own handler.
            detours = {s['addr']: s['target'] for s in json.loads(
                (ROOT/'mods/machines-dn2/mod.json').read_text())['sites']
                if s.get('op') == 'jmp' and s.get('target', '').startswith('dn2_filter_')}
            self.assertEqual(len(detours), 9)
            self.assertEqual(detours['0x4006eca0'], 'dn2_filter_curve')
            self.assertEqual(detours['0x4003649c'], 'dn2_filter_parameter_type')
            for addr, target in detours.items():
                off = int(addr, 0) - dev.main_load
                expected = bytes.fromhex('4ef9') + struct.pack('>I', out.map['machines-dn2:' + target])
                self.assertEqual(out.image[off:off+6], expected, target)
            records = sharc.parse(out.sharc_stream, rel.sharc)
            binding = next(r for r in records if r.target == 0x2009fe00)
            self.assertEqual(binding.raw[16:], struct.pack('<II', 0xbc8400, 5))
            # The stock master hook is untouched; only the voice helper changes.
            def at(stream, addr):
                rec = next(r for r in sharc.parse(stream, rel.sharc) if r.flags == sharc.LOAD
                           and r.target <= addr and addr + 6 <= r.target + r.count)
                i = 16 + addr - rec.target
                return rec.raw[i:i+6]
            self.assertEqual(at(out.sharc_stream, rel.sharc['hook'][0]), at(stock.section(7), rel.sharc['hook'][0]))
            self.assertEqual(at(out.sharc_stream, rel.sharc['voice_hook'][0]), bytes.fromhex(rel.sharc['voice_hook'][2]))
            bad = copy.deepcopy(voice.doc)
            bad['sharc']['machine_id'] = 6
            self.assertIn('voice-test 0.1: SHARC voice machine_id must match its single native voice registration',
                          link.check([core,machines,link.Mod2(bad,'bad')], image))
            bad = copy.deepcopy(voice.doc)
            bad['contribute'].append(dict(bad['contribute'][0], to='mk2_synths'))
            self.assertIn('voice-test 0.1 adds to table mk2_synths, which no given mod declares',
                          link.check([core,machines,link.Mod2(bad,'legacy')], image))
            bad = copy.deepcopy(voice.doc)
            del bad['sharc']
            self.assertIn('voice-test 0.1: DN2 voice registration requires a matching SHARC voice payload',
                          link.check([core,machines,link.Mod2(bad,'bad')], image))
            self.assertTrue(any('requires machines-dn2' in s for s in link.check([core,voice], image)))
