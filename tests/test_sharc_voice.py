# SPDX-License-Identifier: GPL-2.0-or-later
"""mk2 synths on the DN2: shift leads in SHARC code, and a built synth's registration and hooks."""
import copy
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
from elekloader import elemod, formats, link, sharc   # noqa: E402
from elekloader.devices.digitone_mk2 import DEVICE   # noqa: E402
from elekloader.sdk import build as sdk   # noqa: E402
from test_sharc_kernels import DISPATCHER, kernel   # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROFILE = DEVICE.releases['1.12'].sharc
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


def artifact():
    return kernel(('mk2-voice', 5))


class EnvelopeTests(unittest.TestCase):
    def test_shift_counts_are_sign_extended(self):
        malformed = sharc.suspect_shifts
        # Type 6b's negative count needs DATAEX=0xf; missing it halts real DSPs.
        good = bytes.fromhex('3e0200002210 3e02007822f0')
        bad = bytes.fromhex('3e02000022f0')
        self.assertEqual(malformed(good), [])
        self.assertEqual(malformed(bad), [0])
        self.assertEqual(malformed(sharc._parcels(bytes.fromhex(DISPATCHER['code']))), [])   # machines-dn2's dispatcher
        self.assertEqual(malformed(bytes.fromhex(PROFILE['peek_code'])), [])                 # core-dn2's DSP peek

    def test_lint_notes_negative_shift_immediates_in_a_kernel(self):
        from types import SimpleNamespace
        from elekloader import lint
        doc = kernel(('mk2-voice', 5), code=bytes(4) + sharc._parcels(bytes.fromhex('3e02000022f0')))
        mod = SimpleNamespace(id='x', sharc=sharc.kernel_payload(doc, PROFILE), rel=SimpleNamespace(sharc=PROFILE))
        self.assertEqual(lint.sharc_shifts(mod), [{'mod': 'x', 'offset': 4}])
        mod.sharc = sharc.kernel_payload(artifact(), PROFILE)
        self.assertEqual(lint.sharc_shifts(mod), [])


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
            # The stock master hook is untouched; the voice helper jumps to the dispatcher.
            def at(stream, addr):
                rec = next(r for r in sharc.parse(stream, rel.sharc) if r.flags == sharc.LOAD
                           and r.target <= addr and addr + 6 <= r.target + r.count)
                i = 16 + addr - rec.target
                return rec.raw[i:i+6]
            self.assertEqual(at(out.sharc_stream, rel.sharc['hook'][0]), at(stock.section(7), rel.sharc['hook'][0]))
            hook = out.layout['sharc']['kernels']['mods'][0]['code'][0]
            entry = sharc.pc16(PROFILE, hook) + DISPATCHER['exports']['dn2_voice_hook.'][1]
            self.assertEqual(at(out.sharc_stream, rel.sharc['voice_hook'][0]), sharc._jump(entry))
            bad = copy.deepcopy(voice.doc)
            bad['sharc']['kernels'][0]['id'] = 6
            self.assertEqual(['voice-test 0.1: its mk2_voice_synths entry 5 needs a SHARC mk2-voice kernel with that id',
                              'voice-test 0.1: its SHARC mk2-voice kernel 6 has no registration with that id in its mod'],
                             link.check([core,machines,link.Mod2(bad,'bad')], image))
            bad = copy.deepcopy(voice.doc)
            bad['contribute'].append(dict(bad['contribute'][0], to='mk2_synths'))
            self.assertIn('voice-test 0.1 adds to table mk2_synths, which no given mod declares',
                          link.check([core,machines,link.Mod2(bad,'legacy')], image))
            bad = copy.deepcopy(voice.doc)
            del bad['sharc']
            self.assertIn('voice-test 0.1: its mk2_voice_synths entry 5 needs a SHARC mk2-voice kernel with that id',
                          link.check([core,machines,link.Mod2(bad,'bad')], image))
            self.assertTrue(any('requires machines-dn2' in s for s in link.check([core,voice], image)))


if __name__ == '__main__':
    unittest.main()
