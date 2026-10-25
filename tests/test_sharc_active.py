# SPDX-License-Identifier: GPL-2.0-or-later
"""Active DN2 sine integration; requires owner stock and a ColdFire cross compiler."""
import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader import elemod, formats, link, sharc  # noqa: E402
from elekloader.sdk import build as sdk  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


@unittest.skipUnless(STOCK and os.environ.get('ELEKLOADER_CROSS'),
                     'needs ELEKLOADER_DN2_SYX and ELEKLOADER_CROSS')
class ActiveSineTests(unittest.TestCase):
    def test_stock_bound_bridge_and_qualified_sine(self):
        with tempfile.TemporaryDirectory() as work:
            core_path, _ = sdk.build(str(ROOT / 'mods/core-dn2'), STOCK, work)
            app_path, _ = sdk.build(str(ROOT / 'examples/sharc-sine'), STOCK, work)
            core, app = elemod.load_any(core_path), elemod.load_any(app_path)
            asm_path, _ = sdk.build(str(ROOT / 'examples/sharc-sine-asm'), STOCK, work)
            asm = elemod.load_any(asm_path)
            stock, dev, rel = formats.load(STOCK)
            image = formats.main_image(stock, dev)
            self.assertIsNone(link.link([core], image).sharc_stream)
            self.assertEqual(link.check([core, app], image), [])
            self.assertEqual(link.check([core, asm], image), [])
            # Packaging acceptance does not qualify these synthetic bytes for execution.
            for selected in (app, asm):
                for mutation in ('code', 'data', 'entry', 'stack'):
                    changed = copy.deepcopy(selected.doc)
                    artifact = changed['sharc']
                    if mutation in ('code', 'data'):
                        segment = artifact['segments'][1 if mutation == 'data' else 0]
                        data = bytearray.fromhex(segment['hex'])
                        data[4] ^= 1
                        segment['hex'] = data.hex()
                    elif mutation == 'entry':
                        artifact['entries']['render_offset_bytes'] += 2
                    else:
                        artifact['stack_bytes'] -= 4
                    self.assertEqual(link.check([core, link.Mod2(changed, mutation)], image), [])

            old_core = copy.deepcopy(core.doc)
            old_core.pop('sharc')
            self.assertIn('active SHARC mod requires a qualified matching core bridge',
                          link.check([link.Mod2(old_core, 'old-core'), app], image))
            mutated = copy.deepcopy(app.doc)
            code = mutated['sharc']['segments'][0]
            code['hex'] = ('00' if code['hex'][:2] != '00' else '01') + code['hex'][2:]
            self.assertEqual(link.check([core, link.Mod2(mutated, 'mutated')], image), [])
            for mutation in ('entry', 'stack'):
                changed = copy.deepcopy(core.doc)
                if mutation == 'entry':
                    changed['sharc']['entries']['hook_offset_bytes'] += 2
                else:
                    changed['sharc']['stack_bytes'] -= 4
                self.assertTrue(any('unqualified active SHARC core bytes or entry' in p for p in
                                    link.check([link.Mod2(changed, mutation), app], image)))
            for change, message in (
                    (lambda a: a['entries'].update(render_offset_bytes=16384), 'entry is not in owned code'),
                    (lambda a: a['entries'].update(init_offset_bytes=1), 'entry is not in owned code'),
                    (lambda a: a['segments'][0].update(offset_bytes=4), 'gap or overlap'),
                    (lambda a: a['zero'][0].update(size_bytes=a['zero'][0]['size_bytes'] + 4), 'exceeds its owner')):
                changed = copy.deepcopy(app.doc)
                change(changed['sharc'])
                with self.assertRaisesRegex(elemod.ModError, message):
                    link.Mod2(changed, 'invalid-envelope')
            bad_core = copy.deepcopy(core.doc)
            code = bad_core['sharc']['segments'][0]
            code['hex'] = ('00' if code['hex'][:2] != '00' else '01') + code['hex'][2:]
            large_stack = copy.deepcopy(app.doc)
            large_stack['sharc']['stack_bytes'] = rel.sharc['stack_max']
            for message, mods in (
                ('SHARC mods may combine only one DN2 voice and one DN2 filter', [core, app, asm]),
                ('active SHARC mod requires a qualified matching core bridge',
                 [link.Mod2(old_core, 'old-core'), app]),
                ('unqualified active SHARC core bytes or entry',
                 [link.Mod2(bad_core, 'bad-core'), app]),
                ('active SHARC stack exceeds the profile limit',
                 [core, link.Mod2(large_stack, 'large-stack')]),
            ):
                with self.subTest(refusal=message), self.assertRaisesRegex(elemod.ModError, message):
                    link.link(mods, image, sharc_stream=stock.section(7))
            with self.assertRaisesRegex(elemod.ModError, 'SHARC mod needs the stock section-7 stream'):
                link.link([core, app], image)
            relocated = copy.deepcopy(app.doc)
            relocated['sharc']['zero'][0]['offset_bytes'] += 4
            relocated['sharc']['zero'][0]['size_bytes'] -= 4
            with self.assertRaisesRegex(elemod.ModError, 'gap or overlap'):
                link.Mod2(relocated, 'relocated')

            for selected in (app, asm):
                linked = link.link([core, selected], image, sharc_stream=stock.section(7))
                profile = rel.sharc
                with patch.dict(profile, stored_max=len(linked.sharc_stored) - 1):
                    with self.assertRaisesRegex(elemod.ModError, 'SHARC stored section exceeds the profile limit'):
                        link.link([core, selected], image, sharc_stream=stock.section(7))
                records = sharc.parse(linked.sharc_stream, profile)
                self.assertEqual(len(records), profile['record_count'] + 9)
                self.assertEqual(linked.layout['sharc']['hook'], profile['hook'][0])
                self.assertEqual(linked.layout['sharc']['core'], list(profile['core_range']))
                self.assertEqual(linked.layout['sharc']['owner'], [0x20090000, 0x20098000])
                binding = next(r for r in records if r.target == profile['core_range'][0])
                import struct
                expected = (0xbc80a0, 0xbc80bf, 0) if selected is app else (0xbc8058, 0xbc8077, 0)
                self.assertEqual(binding.raw[16:], struct.pack('<III', *expected))
                table_at, table = profile['keyed_pitch_table']
                table_record = next(r for r in records if r.target == profile['pitch_range'][0] + table_at)
                self.assertEqual(table_record.raw[16:].hex(), table)
                outputs = formats.write(stock, formats.pack_main(linked.image, dev), dev,
                                        stored_sharc=linked.sharc_stored)
                facts = formats.verify(outputs, stock, linked.image, dev,
                                       want_sharc=linked.sharc_stream)
                self.assertIn('sections 2, 4, 5, 8, byte for byte', facts['untouched'])


if __name__ == '__main__':
    unittest.main()
