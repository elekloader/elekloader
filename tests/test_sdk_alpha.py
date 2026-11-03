# SPDX-License-Identifier: GPL-2.0-or-later
"""The SDK alpha builder and linker: what a registration may declare, what is refused, and what the
build adds for each device. No emulator; the behaviour on the device is in test_sdk_alpha_*.py.

Build checks need ELEKLOADER_STOCK_154, ELEKLOADER_STOCK_DN1_144 (Digitone mk1 OS 1.44) and a cross
compiler (ELEKLOADER_CROSS)."""
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
from types import ModuleType
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from elekloader import elemod, formats, link        # noqa: E402
from elekloader.sdk import build                    # noqa: E402
from elekloader.sdk.sdk_alpha_build import sdk_alpha_build, sdk_alpha_main, sdk_alpha_registrations  # noqa: E402
import sdk_alpha_harness                          # noqa: E402
from sdk_alpha_devices import CannotYet, Device, Digitakt1, Digitone1  # noqa: E402
from sdk_alpha_harness import SdkMachine, sdk_expected_failure, sdk_tests  # noqa: E402

# Machines the build tests use, as the C an author writes. Not registered for the emulator tests.
EMPTY = SdkMachine("Empty", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static const struct elek_machine empty = { .id = @ID@, .name = "Empty", .short_name = "EMPT" };
    ELEK_REGISTER_MACHINE(empty);
"""))
ONE_PAGE = SdkMachine("One page", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static const struct elek_ui_knob knob = { .id = 0, .label = "KNOB" };
    static const struct elek_ui_page pages[] = { { .label = "ONE", .knob = { &knob } } };
    static const struct elek_machine one = {
        .id = @ID@, .name = "One page", .short_name = "ONE", .pages = pages, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(one);
"""))
LONG_LABEL = SdkMachine("Long label", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static const struct elek_ui_knob knob = { .id = 0, .label = "TOOLONG" };
    static const struct elek_ui_page pages[] = { { .label = "ONE", .knob = { &knob } } };
    static const struct elek_machine long_label = {
        .id = @ID@, .name = "Long label", .short_name = "LONG", .pages = pages, .page_count = 1,
    };
    ELEK_REGISTER_MACHINE(long_label);
"""))
HELLO = ROOT / 'examples/sdk-hello-world'       # two pages
GLYPHS = ROOT / 'examples/sdk-glyphs'


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


def sdk_alpha_test_controls_document(glyphs=False, v5=False):
    doc, raw = sdk_alpha_test_document()
    raw.extend(bytes(1200))
    doc['relocs'][-1][3] = ('sym:sdk_alpha_dt1_ui_v5' if v5 else 'sym:sdk_alpha_dt1_ui_v4' if glyphs else
                            'sym:sdk_alpha_dt1_ui_v3')
    stride, pages = (36, 640) if v5 else (28, 600) if glyphs else (24, 512)
    struct.pack_into('>I', raw, 56, 2)
    doc['relocs'] += [['.run', 52, 'abs32', 'sec:.run', pages]]
    for i in range(15):
        struct.pack_into('>I', raw, 96 + stride * i, i)
        doc['relocs'].append(['.run', 100 + stride * i, 'abs32', 'sec:.run', 72])
    for page in range(2):
        doc['relocs'].append(['.run', pages + 36 * page, 'abs32', 'sec:.run', 72])
        for encoder in range(8):
            # Logical zero is D on page 0; page 1 shares it at H.
            logical = ([2, 3, 4, 0, 5, 6, 7, 1] if page == 0 else
                       [8, 9, 10, 11, 12, 13, 14, 0])[encoder]
            doc['relocs'].append(['.run', pages + 4 + 36 * page + 4 * encoder,
                                  'abs32', 'sec:.run', 96 + stride * logical])
    return doc, raw


class SdkAlphaTests(unittest.TestCase):
    def test_digitone_destinations_follow_the_logical_knob(self):
        # Knob 0 sits on encoder D, knob 1 on A and knob 13 on the second page: each keeps its own
        # parameter (machines-dn1's sdk_pos), whatever encoder or page shows it.
        panel = Digitone1(None, {'fw_params': '0x1000'}, [100], {
            100: {'pages': [{'knobs': {0: {'id': 1}, 3: {'id': 0}}}, {'knobs': {0: {'id': 13}}}]},
        })
        panel.machine = lambda track: 100
        panel.peek32 = lambda at: {0x1000 + 60 * 119 + 4: 27, 0x1000 + 60 * 120 + 4: 28,
                                   0x1000 + 60 * 117 + 4: 52}[at]
        self.assertEqual(panel.destination_for(0, track=2), 27)
        self.assertEqual(panel.destination_for(1, track=2), 28)
        self.assertEqual(panel.destination_for(13, track=2), 52)

    def test_waiting_for_ui_state_returns_early_and_has_a_deadline(self):
        class Session:
            ms, halted = 0, None

            def run_ms(self, ms):
                self.ms += ms

        session = Session()
        panel = Device(session, {}, [])
        self.assertTrue(panel.wait_until(lambda: session.ms >= 100))
        self.assertEqual(session.ms, 100)
        self.assertTrue(panel.wait_until(lambda: True))
        self.assertEqual(session.ms, 100)
        self.assertFalse(panel.wait_until(lambda: False, limit_ms=75))
        self.assertEqual(session.ms, 175)
        session.halted = 'stopped'
        self.assertFalse(panel.wait_until(lambda: False))
        self.assertEqual(session.ms, 175)

    def test_known_gaps_are_expected_only_on_the_named_device(self):
        class Probe:
            def setUp(self):
                self.failure = None

            def tearDown(self):
                pass

            @sdk_expected_failure('digitone-mk1')
            def test_observation_gap(self):
                with self.subTest(observation='label'):
                    raise CannotYet('labels')

            @sdk_expected_failure('digitone-mk1')
            def test_builder_refusal(self):
                self.fail('the builder refused the machine')

            @sdk_expected_failure('digitone-mk1')
            def test_fixed_gap(self):
                pass

            def test_new_regression(self):
                raise CannotYet('a new gap must still fail')

        module = ModuleType('sdk_harness_probe')
        Probe.__module__ = module.__name__
        with patch.dict(sys.modules, {module.__name__: module}), \
                patch.object(sdk_alpha_harness, '_devices', return_value=(Digitakt1, Digitone1)):
            sdk_tests(Probe)
        for device, counts in ((Digitakt1, (0, 0, 3)), (Digitone1, (2, 1, 1))):
            with self.subTest(device=device.key):
                cls = getattr(module, 'Probe_' + device.key.replace('-', '_'))
                result = unittest.TestResult()
                unittest.defaultTestLoader.loadTestsFromTestCase(cls).run(result)
                self.assertEqual(result.testsRun, 4)
                self.assertEqual(result.errors, [])
                self.assertEqual((len(result.expectedFailures), len(result.unexpectedSuccesses),
                                  len(result.failures)), counts)
                self.assertFalse(result.wasSuccessful())

    def test_malformed_devices_gets_a_build_message_without_a_traceback(self):
        with tempfile.TemporaryDirectory() as tmp:
            for devices in (['digitone-mk1'], 'digitone-mk1', True, 42):
                with self.subTest(devices=devices):
                    Path(tmp, 'mod.json').write_text(json.dumps({'devices': devices}))
                    output = io.StringIO()
                    with redirect_stdout(output):
                        code = sdk_alpha_main([tmp, '--stock', 'unused.syx'])
                    self.assertEqual(code, 1)
                    self.assertEqual(output.getvalue(),
                                     'BUILD FAILED: mod.json: "devices" maps each device to its OS versions\n')

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

    def test_sdk_alpha_pages_logical_identity_and_capacity(self):
        doc, raw = sdk_alpha_test_controls_document()
        self.assertEqual(sdk_alpha_registrations(doc, {'.run': raw}),
                         [('sdk_alpha_registration_empty', 120)])
        # A blank position cannot alias logical zero. Zero can appear on both pages.
        doc['relocs'] = [r for r in doc['relocs'] if r[1] != 580]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        for offset, value, message in ((56, 3, 'at most 2 pages'),
                                       (96, 15, '15 knobs'),
                                       (96, 0xffffffff, '15 knobs'),
                                       (120, 0, 'cannot share'),
                                       (108, 2, 'supplied'),
                                       (104, 128, 'min exceeds max')):
            with self.subTest(offset=offset):
                doc, raw = sdk_alpha_test_controls_document()
                struct.pack_into('>I', raw, offset, value)
                if offset == 104:
                    struct.pack_into('>I', raw, 108, 1)
                with self.assertRaisesRegex(build.BuildError, message):
                    sdk_alpha_registrations(doc, {'.run': raw})
        # A unique descriptor may live outside any common array or section.
        doc, raw = sdk_alpha_test_controls_document()
        descriptor = raw[96:120]
        label = next(r for r in doc['relocs'] if r[1] == 100)
        doc['relocs'].remove(label)
        doc['relocs'].append(['.other', 4, 'abs32', 'sec:.run', 72])
        for r in doc['relocs']:
            if r[4] == 96:
                r[3:5] = ['sec:.other', 0]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw, '.other': descriptor})), 1)
        for target in (97, len(raw) - 20):
            doc, raw = sdk_alpha_test_controls_document()
            next(r for r in doc['relocs'] if r[1] == 516)[4] = target
            with self.assertRaises(build.BuildError):
                sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_controls_document()
        next(r for r in doc['relocs'] if r[1] == 516)[3] = 'sym:external_knob'
        with self.assertRaisesRegex(build.BuildError, 'own mod'):
            sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_controls_document()
        doc['relocs'] = [r for r in doc['relocs'] if r[1] != 516]
        with self.assertRaisesRegex(build.BuildError, 'contiguous'):
            sdk_alpha_registrations(doc, {'.run': raw})
        # Label-only pages need no knob list; stale page records must be rebuilt.
        doc, raw = sdk_alpha_test_controls_document()
        doc['relocs'] = [r for r in doc['relocs'] if not 516 <= r[1] <= 580 or r[1] == 548]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        next(r for r in doc['relocs'] if r[1] == 32)[3] = 'sym:sdk_alpha_dt1_ui_v2'
        with self.assertRaisesRegex(build.BuildError, 'ABI'):
            sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_glyph_registration(self):
        for supplied in (False, True):
            doc, raw = sdk_alpha_test_controls_document(glyphs=True)
            if supplied:
                doc['relocs'].append(['.run', 120, 'abs32', 'sec:.run', 1000])
            self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        for kind in ('truncated', 'external', 'unrelocated', 'duplicate', 'unaligned'):
            with self.subTest(kind=kind):
                doc, raw = sdk_alpha_test_controls_document(glyphs=True)
                doc['relocs'].append(['.run', 120, 'abs32', 'sec:.run', 1000])
                if kind == 'truncated':
                    raw = raw[:1031]
                elif kind == 'external':
                    doc['relocs'][-1][3] = 'sym:external_glyph'
                elif kind == 'unrelocated':
                    doc['relocs'].pop()
                    struct.pack_into('>I', raw, 120, 1000)
                elif kind == 'duplicate':
                    doc['relocs'].append(doc['relocs'][-1][:])
                elif kind == 'unaligned':
                    doc['relocs'][-1][4] += 1
                with self.assertRaises(build.BuildError):
                    sdk_alpha_registrations(doc, {'.run': raw})

    def test_a_knobs_default_is_within_its_bounds(self):
        # Knob 0 runs -64..63 (min at +8, max at +16), its .def at +28 and whether it is supplied at +32.
        def document(default=None, supplied=1):
            doc, raw = sdk_alpha_test_controls_document(v5=True)
            struct.pack_into('>iIiI', raw, 104, -64, 1, 63, 1)
            if default is not None:
                struct.pack_into('>iI', raw, 124, default, supplied)
            return doc, raw

        def knob0(doc, raw):
            layouts = {}
            sdk_alpha_registrations(doc, {'.run': raw}, layouts)
            return next(k for k in layouts[120]['pages'][0]['knobs'].values() if k['id'] == 0)
        self.assertEqual(knob0(*document())['def'], -64)        # omitted: its min, raw 0 as before
        for value in (-64, 0, 63):
            self.assertEqual(knob0(*document(value))['def'], value)
        for value, supplied, message in ((64, 1, 'within'), (-65, 1, 'within'), (0, 2, 'supplied')):
            with self.subTest(value=value, supplied=supplied):
                doc, raw = document(value, supplied)
                with self.assertRaisesRegex(build.BuildError, message):
                    sdk_alpha_registrations(doc, {'.run': raw})

    def test_a_callbacks_input_is_read_only(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        result = subprocess.run([cc, '-std=c99', '-fsyntax-only', '-x', 'c',
                                 '-I', str(ROOT / 'elekloader/sdk/include'), '-'],
                                input='#include "elekloader/sdk_alpha_machine.h"\n'
                                      'void bad(const struct elek_ui_context *ctx) {'
                                      'ctx->knob[0]->value = 1;}\n',
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0, 'a callback can write its input')

    def test_sine_example_retriggers_without_a_waveform_jump(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        source = textwrap.dedent('''
            #include <assert.h>
            #include "examples/sdk-sine/sine.c"
            const char sdk_alpha_dt1_ui_v5[] = "test";
            uint32_t elek_phase_inc(int32_t pitch) { return 23410327u; }
            int main(void) {
                const struct elek_ui_value value = { .value = 0 };
                const struct elek_ui_value *knobs[] = { &value };
                const struct elek_ui_context ctx = { .knob_count = 1, .knob = knobs };
                struct elek_voice voice = { .pitch = 60 << 16, .gate = 1 };
                int32_t out[ELEK_VOICE_FRAMES], last = 0;
                for (int block = 0; block < 50; ++block) {
                    voice.fresh = block % 7 == 0;
                    render(&ctx, &voice, out);
                    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i) {
                        int64_t step = (int64_t)out[i] - last;
                        assert(step > -(1 << 26) && step < (1 << 26));
                        last = out[i];
                    }
                }
                voice.gate = 0;
                voice.fresh = 0;
                render(&ctx, &voice, out);
                voice.gate = voice.fresh = 1;
                render(&ctx, &voice, out);
                assert(out[0] == 0); /* A note after release still starts at zero. */
            }
        ''')
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp, 'sine-retrigger'))
            result = subprocess.run([cc, '-std=c99', '-I', str(ROOT),
                                     '-I', str(ROOT / 'elekloader/sdk/include'),
                                     '-x', 'c', '-', '-o', binary],
                                    input=source, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([binary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def stocks(self):
        stocks = {'digitakt-mk1': os.environ.get('ELEKLOADER_STOCK_154', ''),
                  'digitone-mk1': os.environ.get('ELEKLOADER_STOCK_DN1_144', '')}
        cross = os.environ.get('ELEKLOADER_CROSS', 'm68k-linux-gnu-')
        if not all(Path(s).is_file() for s in stocks.values()) or not shutil.which(cross + 'gcc'):
            self.skipTest('needs ELEKLOADER_STOCK_154, ELEKLOADER_STOCK_DN1_144 and m68k cross compiler')
        return stocks

    def test_sdk_alpha_builds_link_for_the_digitakt_and_keep_slot_conflicts(self):
        stock = self.stocks()['digitakt-mk1']
        with tempfile.TemporaryDirectory() as tmp:
            core_path, core = build.build(str(ROOT / 'mods/core'), stock, tmp)
            pages_path, pages = build.build(str(ROOT / 'mods/machine-pages'), stock, tmp)
            firmware, dev, _ = formats.load(stock)
            image = formats.main_image(firmware, dev)
            built = {}
            for name, folder in (('empty', EMPTY.write(tmp)), ('one page', ONE_PAGE.write(tmp)),
                                 ('hello', str(HELLO)), ('glyphs', str(GLYPHS))):
                path, mod = sdk_alpha_build(folder, stock, tmp)
                built[name] = path, mod
                result = subprocess.run([sys.executable, '-m', 'elekloader.lint', path, '--stock', stock,
                                         '--with', core_path, '--with', pages_path, '--json'],
                                        cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, name + '\n' + result.stdout + result.stderr)
                link.link([core, pages, mod], image)
                self.assertEqual(mod.requires, ['core', 'machine-pages'])
            self.assertEqual(built['empty'][1].names, ['machine:%d' % EMPTY.id])
            self.assertEqual(built['hello'][1].names, ['machine:122'])
            with open(built['empty'][0]) as fh:
                duplicate = json.load(fh)
            duplicate['id'], duplicate['exports'] = 'sdk-alpha-conflict', []   # keep the refusal to the slot
            with self.assertRaisesRegex(elemod.ModError, 'machine:%d' % EMPTY.id):
                link.link([core, pages, built['empty'][1], link.Mod2(duplicate, 'conflict')], image)
            with open(pages_path) as fh:
                older_pages = json.load(fh)
            older_pages['exports'].remove('sdk_alpha_dt1_ui_v4')
            with self.assertRaisesRegex(elemod.ModError, 'sdk_alpha_dt1_ui_v4'):
                link.link([core, link.Mod2(older_pages, 'older-pages'), built['empty'][1]], image)

    def test_sdk_alpha_one_source_builds_for_both_devices(self):
        stocks = self.stocks()
        dn = stocks['digitone-mk1']
        with tempfile.TemporaryDirectory() as tmp:
            _, dt_mod = sdk_alpha_build(EMPTY.write(tmp + '/dt'), stocks['digitakt-mk1'], tmp + '/dt-out')
            self.assertEqual((dt_mod.names, dt_mod.requires), (['machine:%d' % EMPTY.id], ['core', 'machine-pages']))
            path, dn_mod = sdk_alpha_build(EMPTY.write(tmp + '/dn'), dn, tmp + '/dn-out')
            self.assertEqual((dn_mod.names, dn_mod.requires), (['dnmachine:%d' % EMPTY.id], ['core', 'machines']))
            with open(path) as fh:
                doc = json.load(fh)
            self.assertEqual([c['to'] for c in doc['contribute']], ['sdk_alpha_machines'])
            core = build.build(str(ROOT / 'mods/core-dn1'), dn, tmp)[1]
            machines = build.build(str(ROOT / 'mods/machines-dn1'), dn, tmp)[1]
            firmware, dev, _ = formats.load(dn)
            image = formats.main_image(firmware, dev)
            link.link([core, machines, dn_mod], image)
            doc['id'], doc['exports'] = 'sdk-alpha-conflict', []
            with self.assertRaisesRegex(elemod.ModError, 'dnmachine:%d' % EMPTY.id):
                link.link([core, machines, dn_mod, link.Mod2(doc, 'conflict')], image)
            link.link([core, machines, sdk_alpha_build(ONE_PAGE.write(tmp + '/one'), dn, tmp)[1]], image)
            link.link([core, machines, sdk_alpha_build(str(HELLO), dn, tmp)[1]], image)   # two pages
            for stock in stocks.values():
                with self.assertRaisesRegex(build.BuildError, '1..5 printable ASCII characters'):
                    sdk_alpha_build(LONG_LABEL.write(tmp + '/long'), stock, tmp)
                shutil.rmtree(tmp + '/long')

            def edited(machine, name, edit):
                folder = machine.write(tmp + '/' + name)
                with open(folder + '/mod.json') as fh:
                    mod = json.load(fh)
                edit(mod)
                with open(folder + '/mod.json', 'w') as fh:
                    json.dump(mod, fh)
                return folder
            only_dt = edited(EMPTY, 'only-dt', lambda m: m.update(devices={'digitakt-mk1': ['1.54']}))
            with self.assertRaisesRegex(build.BuildError, 'mod.json is for digitakt-mk1 1.54; the stock file is digitone-mk1'):
                sdk_alpha_build(only_dt, dn, tmp)
            both = edited(EMPTY, 'both', lambda m: m.update(device='digitone-mk1'))
            with self.assertRaisesRegex(build.BuildError, '"devices" replaces "device" and "os"'):
                sdk_alpha_build(both, dn, tmp)


if __name__ == '__main__':
    unittest.main()
