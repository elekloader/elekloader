# SPDX-License-Identifier: GPL-2.0-or-later
"""Private machine UI tests. Optional build checks need ELEKLOADER_STOCK_154 and a cross compiler."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import elemod, link                 # noqa: E402
from elekloader.sdk import build                    # noqa: E402
from elekloader.sdk.sdk_alpha_build import sdk_alpha_build, sdk_alpha_registrations  # noqa: E402


def sdk_alpha_test_document():
    raw = bytearray(96)
    struct.pack_into('>i', raw, 0, 120)
    struct.pack_into('>iii', raw, 16, 3, 120, 0x55493330)
    struct.pack_into('>i', raw, 40, 120)
    raw[72:78] = b'Empty\0'
    raw[80:85] = b'EMPT\0'
    doc = {'exports': ['sdk_alpha_registration_empty'],
           'symbols': {'sdk_alpha_registration_empty': ['.run', 0]},
           'relocs': [['.run', o, 'abs32', 'sec:.run', a]
                      for o, a in ((4, 72), (8, 80), (28, 32), (36, 40), (44, 72), (48, 80))] +
                     [['.run', 32, 'abs32', 'sym:sdk_alpha_dt1_empty_ui', 0]]}
    return doc, raw


def sdk_alpha_test_controls_document():
    doc, raw = sdk_alpha_test_document()
    raw.extend(bytes(1200))
    doc['relocs'][-1][3] = 'sym:sdk_alpha_dt1_ui_v2'
    struct.pack_into('>I', raw, 56, 2)
    struct.pack_into('>I', raw, 64, 15)
    doc['relocs'] += [['.run', 52, 'abs32', 'sec:.run', 400],
                      ['.run', 60, 'abs32', 'sec:.run', 96]]
    for i in range(15):
        doc['relocs'].append(['.run', 96 + 20 * i, 'abs32', 'sec:.run', 72])
    for page in range(2):
        doc['relocs'].append(['.run', 400 + 36 * page, 'abs32', 'sec:.run', 72])
        for encoder in range(8):
            # Logical zero is D on page 0; page 1 shares it at H.
            logical = ([2, 3, 4, 0, 5, 6, 7, 1] if page == 0 else
                       [8, 9, 10, 11, 12, 13, 14, 0])[encoder]
            doc['relocs'].append(['.run', 404 + 36 * page + 4 * encoder,
                                  'abs32', 'sec:.run', 96 + 20 * logical])
    return doc, raw


class SdkAlphaTests(unittest.TestCase):
    def test_sdk_alpha_discovery_and_empty_descriptor(self):
        doc, raw = sdk_alpha_test_document()
        self.assertEqual(sdk_alpha_registrations(doc, {'.run': raw}),
                         [('sdk_alpha_registration_empty', 120)])

    def test_sdk_alpha_invalid_registrations(self):
        for offset, value in ((0, 3), (0, 128), (40, 121), (56, 1), (64, 1), (24, 0)):
            with self.subTest(offset=offset, value=value):
                doc, raw = sdk_alpha_test_document()
                struct.pack_into('>I', raw, offset, value)
                with self.assertRaises(build.BuildError):
                    sdk_alpha_registrations(doc, {'.run': raw})
        for kind in ('duplicate', 'missing', 'pointer', 'abi', 'name', 'truncated', 'pages'):
            with self.subTest(kind=kind):
                doc, raw = sdk_alpha_test_document()
                if kind == 'duplicate':
                    doc['exports'].append('sdk_alpha_registration_second')
                    doc['symbols']['sdk_alpha_registration_second'] = ['.run', 0]
                elif kind == 'missing':
                    doc['exports'] = []
                elif kind == 'pointer':
                    doc['relocs'] = []
                elif kind == 'abi':
                    doc['relocs'][-1][3] = 'sym:cm_ui_v3'
                elif kind == 'name':
                    raw[72] = 0
                elif kind == 'truncated':
                    raw = raw[:50]
                elif kind == 'pages':
                    doc['relocs'].append(['.run', 52, 'abs32', 'sec:.run', 72])
                with self.assertRaises(build.BuildError):
                    sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_optional_callback_has_empty_context(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp) / 'present')
            subprocess.run([cc, '-std=c99', '-Wno-int-to-pointer-cast', '-Wno-pointer-to-int-cast',
                            '-I', str(ROOT / 'elekloader/sdk/include'),
                            str(ROOT / 'mods/machine-pages/sdk_alpha.c'),
                            str(ROOT / 'tests/fixtures/sdk-alpha-empty-machine/present-test.c'),
                            '-o', exe], check=True)
            subprocess.run([exe], check=True)

    def test_sdk_alpha_pages_logical_identity_and_capacity(self):
        doc, raw = sdk_alpha_test_controls_document()
        self.assertEqual(sdk_alpha_registrations(doc, {'.run': raw}),
                         [('sdk_alpha_registration_empty', 120)])
        # A blank position cannot alias logical zero. Zero can appear on both pages.
        doc['relocs'] = [r for r in doc['relocs'] if r[1] != 468]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        for offset, value, message in ((56, 3, 'at most 2 pages'),
                                       (64, 16, '15 knobs'),
                                       (104, 2, 'supplied'),
                                       (100, 128, 'min exceeds max')):
            with self.subTest(offset=offset):
                doc, raw = sdk_alpha_test_controls_document()
                struct.pack_into('>I', raw, offset, value)
                if offset == 100:
                    struct.pack_into('>I', raw, 104, 1)
                with self.assertRaisesRegex(build.BuildError, message):
                    sdk_alpha_registrations(doc, {'.run': raw})
        for target in (97, 396, 999):
            doc, raw = sdk_alpha_test_controls_document()
            next(r for r in doc['relocs'] if r[1] == 404)[4] = target
            with self.assertRaisesRegex(build.BuildError, 'must reference'):
                sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_controls_document()
        doc['relocs'] = [r for r in doc['relocs'] if r[1] != 404]
        with self.assertRaisesRegex(build.BuildError, 'each knob must appear'):
            sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_mapping_and_instance_snapshots(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp) / 'values')
            subprocess.run([cc, '-std=c99', '-Wno-int-to-pointer-cast', '-Wno-pointer-to-int-cast',
                            '-I', str(ROOT / 'elekloader/sdk/include'),
                            str(ROOT / 'mods/machine-pages/sdk_alpha.c'),
                            str(ROOT / 'tests/fixtures/sdk-alpha-controls-machine/values-test.c'),
                            '-o', exe], check=True)
            subprocess.run([exe], check=True)
            readonly = subprocess.run([cc, '-std=c99', '-fsyntax-only', '-x', 'c',
                                       '-I', str(ROOT / 'elekloader/sdk/include'), '-'],
                                      input='#include "elekloader/sdk_alpha_machine.h"\n'
                                            'void bad(const struct elek_ui_context *ctx) {'
                                            'ctx->knob[0]->value = 1;}\n',
                                      capture_output=True, text=True)
            self.assertNotEqual(readonly.returncode, 0, 'callback snapshot became writable')

    def test_sdk_alpha_build_links_and_keeps_slot_conflicts(self):
        stock = os.environ.get('ELEKLOADER_STOCK_154', '')
        cross = os.environ.get('ELEKLOADER_CROSS', 'm68k-linux-gnu-')
        if not stock or not Path(stock).is_file() or not shutil.which(cross + 'gcc'):
            self.skipTest('needs ELEKLOADER_STOCK_154 and m68k cross compiler')
        with tempfile.TemporaryDirectory() as tmp:
            core_path, core = build.build(str(ROOT / 'mods/core'), stock, tmp)
            pages_path, pages = build.build(str(ROOT / 'mods/machine-pages'), stock, tmp)
            path, empty = sdk_alpha_build(str(ROOT / 'tests/fixtures/sdk-alpha-empty-machine'), stock, tmp)
            hello_path, hello = sdk_alpha_build(str(ROOT / 'examples/sdk-hello-world'), stock, tmp)
            for fixture in ('sdk-alpha-controls-machine', 'sdk-alpha-one-page-machine',
                            'sdk-alpha-callback-machine'):
                fixture_path, _ = sdk_alpha_build(str(ROOT / 'tests/fixtures' / fixture), stock, tmp)
                result = subprocess.run([sys.executable, '-m', 'elekloader.lint', fixture_path,
                                         '--stock', stock, '--with', core_path, '--with', pages_path, '--json'],
                                        cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(empty.names, ['machine:120'])
            self.assertEqual(empty.requires, ['core', 'machine-pages'])
            self.assertEqual(hello.names, ['machine:122'])
            result = subprocess.run([sys.executable, '-m', 'elekloader.lint', path, '--stock', stock,
                                     '--with', core_path, '--with', pages_path, '--with', hello_path, '--json'],
                                    cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with open(path) as fh:
                duplicate = json.load(fh)
            duplicate['id'] = 'sdk-alpha-conflict'
            # Removing exports isolates the named-slot refusal from export collisions.
            duplicate['exports'] = []
            other = link.Mod2(duplicate, 'conflict')
            from elekloader import formats
            firmware, dev, _ = formats.load(stock)
            image = formats.main_image(firmware, dev)
            with self.assertRaisesRegex(elemod.ModError, 'machine:120'):
                link.link([core, pages, empty, other], image)


if __name__ == '__main__':
    unittest.main()
