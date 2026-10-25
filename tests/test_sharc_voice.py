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
        def malformed(code):
            offsets = []
            # Conservative Type 6b LSHIFT scan of this fixed adapter, not a decoder.
            for offset in range(0, len(code) - 5, 2):
                a, b, c = struct.unpack_from('<3H', code, offset)
                word = (a << 32) | (b << 16) | c
                if word & 0xffc187ff0000 == 0x020000000000:
                    expected = 15 if word & 0x8000 else 0
                    if (word >> 27) & 15 != expected:
                        offsets.append(offset)
            return offsets

        # Type 6b's negative count needs DATAEX=0xf; missing it halts real DSPs.
        good = bytes.fromhex('3e0200002210 3e02007822f0')
        bad = bytes.fromhex('3e02000022f0')
        self.assertEqual(malformed(good), [])
        self.assertEqual(malformed(bad), [0])
        for doc in (artifact(), filter_artifact(bytes(4), bytes(4), 0, 6, 12)):
            code = bytes.fromhex(doc['segments'][0]['hex'])
            self.assertEqual(malformed(code), [])
            self.assertIn(0, malformed(bad + code[6:]))

    def test_voice_envelope_and_mutations(self):
        doc = artifact()
        payload = sharc.module_payload(doc, PROFILE)
        self.assertEqual(payload['machine_id'], 5)
        self.assertEqual(payload['machine_kind'], 'voice')
        self.assertEqual(payload['owner'], (0x20090000, 0x20094000))
        self.assertEqual(payload['entries'], {'render_offset_bytes': 2048})
        cases = [
            (lambda d: d.update(machine_id=True), 'machine_id'),
            (lambda d: d.update(machine_id=4), 'machine_id'),
            (lambda d: d.update(machine_id=32), 'machine_id'),
            (lambda d: d.update(stack_bytes=129), 'voice stack'),
            (lambda d: d.update(entries=None), 'render entry'),
            (lambda d: d['entries'].update(render_offset_bytes=0), 'adapter reservation'),
            (lambda d: d['entries'].update(render_offset_bytes=8128), 'read-ahead guard'),
            (lambda d: d['segments'][0].update(hex='0000'+d['segments'][0]['hex'][4:]), 'qualified prefix'),
            (lambda d: d['segments'][1].update(hex=d['segments'][1]['hex'][:-4]+'0000'), 'read-ahead guard'),
            (lambda d: d['zero'][-1].update(offset_bytes=15364, size_bytes=1020), 'gap or overlap'),
        ]
        for change, error in cases:
            bad = copy.deepcopy(doc)
            change(bad)
            with self.subTest(error=error), self.assertRaisesRegex(elemod.ModError, error):
                sharc.module_payload(bad, PROFILE)
        # Author code may change without a per-kernel allowlist update.
        doc['segments'][1]['hex'] = '0000' + doc['segments'][1]['hex'][4:]
        sharc.module_payload(doc, PROFILE)

    def test_producer_bounds(self):
        # SHARC instructions are short-word aligned; padding makes the load four-byte aligned.
        doc = voice_artifact(bytes(6), bytes(4), 0, 5, 128)
        self.assertEqual(len(bytes.fromhex(doc['segments'][1]['hex'])), 6144)
        voice_artifact(bytes(6080), bytes(7168), 0, 31, 128)
        for code, data in ((bytes(6082), bytes(4)), (bytes(4), bytes(7172)), (bytes(3), bytes(4))):
            with self.assertRaises(ValueError):
                voice_artifact(code, data, 0, 5, 128)


@unittest.skipUnless(STOCK and os.environ.get('ELEKLOADER_CROSS'), 'needs owner stock and cross compiler')
class LinkTests(unittest.TestCase):
    def test_matching_native_registration_and_checked_hook(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'voice.c').write_text('#include "mk2/machines.h"\n'
                                      'const struct mk2_synth sine = {5,0,"SINE","SINE"};\n'
                                      'const struct mk2_filter filter = {6,0,"CLONE FILTER","CLONE"};\n')
            (p / 'voice.json').write_text(json.dumps(artifact()))
            manifest = {'id': 'voice-test', 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                        'sources': ['voice.c'], 'sharc': {'artifact': 'voice.json'},
                        'resources': {'core': '0.1'}, 'requires': ['core', 'machines-dn2'],
                        'contribute': [{'to': 'mk2_voice_synths', 'data': '00000000',
                                        'relocs': [[0,'abs32','sym:sine',0]]},
                                       {'to': 'mk2_filters', 'data': '00000000',
                                        'relocs': [[0,'abs32','sym:filter',0]]}]}
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
                if s.get('op') == 'jmp' and (s.get('target', '').startswith('dn2_filter_')
                    or s.get('target') == 'dn2_synth_parameter_type')}
            self.assertEqual(len(detours), 9)
            self.assertEqual(detours['0x40036478'], 'dn2_synth_parameter_type')
            self.assertEqual(detours['0x4003649c'], 'dn2_filter_parameter_type')
            for addr, target in detours.items():
                off = int(addr, 0) - dev.main_load
                expected = bytes.fromhex('4ef9') + struct.pack('>I', out.map['machines-dn2:' + target])
                self.assertEqual(out.image[off:off+6], expected, target)
            records = sharc.parse(out.sharc_stream, rel.sharc)
            binding = next(r for r in records if r.target == 0x20093e00)
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
            clone = copy.deepcopy(bad['contribute'][0])
            clone['to'] = 'mk2_synths'
            bad['contribute'].append(clone)
            self.assertIn('voice-test 0.1: SHARC voice machine_id must match its single native voice registration',
                          link.check([core,machines,link.Mod2(bad,'dual-role')], image))
            bad = copy.deepcopy(voice.doc)
            del bad['sharc']
            self.assertIn('voice-test 0.1: DN2 voice registration requires a matching SHARC voice payload',
                          link.check([core,machines,link.Mod2(bad,'bad')], image))
            self.assertTrue(any('requires machines-dn2' in s for s in link.check([core,voice], image)))
