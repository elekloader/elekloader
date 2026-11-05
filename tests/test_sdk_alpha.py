# SPDX-License-Identifier: GPL-2.0-or-later
"""The SDK alpha builder and linker: what a registration may declare, what is refused, and what the
build adds for each device. No emulator; the behaviour on the device is in test_sdk_alpha_*.py.

Build checks need ELEKLOADER_STOCK_154, ELEKLOADER_STOCK_DN1_144 (Digitone mk1 OS 1.44) and a cross
compiler (ELEKLOADER_CROSS); the Digitone II's need ELEKLOADER_DN2_SYX (OS 1.12) and the cross compiler."""
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
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from elekloader import elemod, formats, link        # noqa: E402
from elekloader.sdk import build                    # noqa: E402
from elekloader.sdk.sdk_alpha_build import sdk_alpha_build, sdk_alpha_main, sdk_alpha_registrations  # noqa: E402
from sdk_alpha_devices import Device, Digitone1  # noqa: E402
from sdk_alpha_harness import SdkMachine  # noqa: E402

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
# A machine with a sound: Digitakt renders it; the other targets refuse it.
RENDERS = SdkMachine("Renders", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static void render(const struct elek_audio_context *ctx, const struct elek_voice *v, elek_sample *out)
    {
        (void)ctx; (void)v;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = ELEK_SAMPLE(0);
    }
    static const struct elek_machine renders = {
        .id = @ID@, .name = "Renders", .short_name = "REND", .render = render, .voice_state = 4,
    };
    ELEK_REGISTER_MACHINE(renders);
"""))
DEFAULTS = SdkMachine("Defaults", register=False, source=ONE_PAGE.source.replace(
    '.label = "KNOB"', '.label = "KNOB", .def = ELEK_UI_BOUND(0)'))
HELLO = ROOT / 'examples/sdk-hello-world'       # two pages
GLYPHS = ROOT / 'examples/sdk-glyphs'


def sdk_alpha_test_document():
    return sdk_alpha_test_render_document(render=False)


def sdk_alpha_test_render_document(render=True, voice_state=0):
    """A registration: the machine at 40 is 40 bytes, its render (an rts) at 88."""
    raw = bytearray(112)
    struct.pack_into('>i', raw, 0, 120)
    struct.pack_into('>iii', raw, 16, 3, 120, 0x55493330)
    struct.pack_into('>i', raw, 40, 120)
    struct.pack_into('>I', raw, 76, voice_state)
    raw[88:90] = b'\x4e\x75'
    raw[96:102] = b'Empty\0'
    raw[104:109] = b'EMPT\0'
    doc = {'exports': ['sdk_alpha_registration_empty'],
           'symbols': {'sdk_alpha_registration_empty': ['.run', 0]},
           'relocs': [['.run', o, 'abs32', 'sec:.run', a]
                      for o, a in ((4, 96), (8, 104), (28, 32), (36, 40), (44, 96), (48, 104))] +
                     [['.run', 32, 'abs32', 'sym:sdk_alpha_dt1_ui_v1', 0]]}
    if render:
        doc['relocs'].append(['.run', 72, 'abs32', 'sec:.run', 88])
    return doc, raw


def sdk_alpha_test_controls_document():
    doc, raw = sdk_alpha_test_document()
    raw.extend(bytes(1200))
    stride, pages = 36, 768
    struct.pack_into('>I', raw, 56, 2)
    doc['relocs'] += [['.run', 52, 'abs32', 'sec:.run', pages]]
    for i in range(15):
        struct.pack_into('>I', raw, 128 + stride * i, i)
        doc['relocs'].append(['.run', 132 + stride * i, 'abs32', 'sec:.run', 96])
    for page in range(2):
        doc['relocs'].append(['.run', pages + 36 * page, 'abs32', 'sec:.run', 96])
        for encoder in range(8):
            # Logical zero is D on page 0; page 1 shares it at H.
            logical = ([2, 3, 4, 0, 5, 6, 7, 1] if page == 0 else
                       [8, 9, 10, 11, 12, 13, 14, 0])[encoder]
            doc['relocs'].append(['.run', pages + 4 + 36 * page + 4 * encoder,
                                  'abs32', 'sec:.run', 128 + stride * logical])
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
                    raw[96] = 0
                elif kind == 'truncated':
                    raw = raw[:50]
                elif kind == 'pages':
                    doc['relocs'].append(['.run', 52, 'abs32', 'sec:.run', 96])
                with self.assertRaises(build.BuildError):
                    sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_pages_logical_identity_and_capacity(self):
        doc, raw = sdk_alpha_test_controls_document()
        self.assertEqual(sdk_alpha_registrations(doc, {'.run': raw}),
                         [('sdk_alpha_registration_empty', 120)])
        # A blank position cannot alias logical zero. Zero can appear on both pages.
        doc['relocs'] = [r for r in doc['relocs'] if r[1] != 836]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        for offset, value, message in ((56, 3, 'at most 2 pages'),
                                       (128, 15, '15 knobs'),
                                       (128, 0xffffffff, '15 knobs'),
                                       (164, 0, 'cannot share'),
                                       (140, 2, 'supplied'),
                                       (136, 128, 'min exceeds max')):
            with self.subTest(offset=offset):
                doc, raw = sdk_alpha_test_controls_document()
                struct.pack_into('>I', raw, offset, value)
                if offset == 136:
                    struct.pack_into('>I', raw, 140, 1)
                with self.assertRaisesRegex(build.BuildError, message):
                    sdk_alpha_registrations(doc, {'.run': raw})
        # A unique descriptor may live outside any common array or section.
        doc, raw = sdk_alpha_test_controls_document()
        descriptor = raw[128:164]
        label = next(r for r in doc['relocs'] if r[1] == 132)
        doc['relocs'].remove(label)
        doc['relocs'].append(['.other', 4, 'abs32', 'sec:.run', 96])
        for r in doc['relocs']:
            if r[4] == 128:
                r[3:5] = ['sec:.other', 0]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw, '.other': descriptor})), 1)
        for target in (129, len(raw) - 20):
            doc, raw = sdk_alpha_test_controls_document()
            next(r for r in doc['relocs'] if r[1] == 772)[4] = target
            with self.assertRaises(build.BuildError):
                sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_controls_document()
        next(r for r in doc['relocs'] if r[1] == 772)[3] = 'sym:external_knob'
        with self.assertRaisesRegex(build.BuildError, 'own mod'):
            sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_controls_document()
        doc['relocs'] = [r for r in doc['relocs'] if r[1] != 772]
        with self.assertRaisesRegex(build.BuildError, 'contiguous'):
            sdk_alpha_registrations(doc, {'.run': raw})
        # Label-only pages need no knob list; stale page records must be rebuilt.
        doc, raw = sdk_alpha_test_controls_document()
        doc['relocs'] = [r for r in doc['relocs'] if not 772 <= r[1] <= 836 or r[1] == 804]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        next(r for r in doc['relocs'] if r[1] == 32)[3] = 'sym:unknown_sdk_abi'
        with self.assertRaisesRegex(build.BuildError, 'ABI'):
            sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_glyph_registration(self):
        for supplied in (False, True):
            doc, raw = sdk_alpha_test_controls_document()
            if supplied:
                doc['relocs'].append(['.run', 152, 'abs32', 'sec:.run', 1000])
            self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        for kind in ('truncated', 'external', 'unrelocated', 'duplicate', 'unaligned'):
            with self.subTest(kind=kind):
                doc, raw = sdk_alpha_test_controls_document()
                doc['relocs'].append(['.run', 152, 'abs32', 'sec:.run', 1000])
                if kind == 'truncated':
                    raw = raw[:1031]
                elif kind == 'external':
                    doc['relocs'][-1][3] = 'sym:external_glyph'
                elif kind == 'unrelocated':
                    doc['relocs'].pop()
                    struct.pack_into('>I', raw, 152, 1000)
                elif kind == 'duplicate':
                    doc['relocs'].append(doc['relocs'][-1][:])
                elif kind == 'unaligned':
                    doc['relocs'][-1][4] += 1
                with self.assertRaises(build.BuildError):
                    sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_defaults(self):
        for low, high, default, supplied, expected in (
                (-50, 50, 0, 1, 0), (0, 127, 100, 1, 100),
                (-50, 50, 0, 0, -50), (0, 127, 0, 1, 0)):
            with self.subTest(low=low, high=high, default=default, supplied=supplied):
                doc, raw = sdk_alpha_test_controls_document()
                struct.pack_into('>iIiI', raw, 136, low, 1, high, 1)
                struct.pack_into('>iI', raw, 156, default, supplied)
                layouts = {}
                sdk_alpha_registrations(doc, {'.run': raw}, layouts)
                knob = layouts[120]['pages'][0]['knobs'][3]
                self.assertEqual((knob['def'], knob['has_default']), (expected, bool(supplied)))
        for default, supplied, message in ((-51, 1, 'within min..max'),
                                             (51, 1, 'within min..max'), (0, 2, 'supplied')):
            doc, raw = sdk_alpha_test_controls_document()
            struct.pack_into('>iIiI', raw, 136, -50, 1, 50, 1)
            struct.pack_into('>iI', raw, 156, default, supplied)
            with self.assertRaisesRegex(build.BuildError, message):
                sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_render_registration(self):
        layouts = {}
        doc, raw = sdk_alpha_test_render_document(voice_state=16)
        self.assertEqual(sdk_alpha_registrations(doc, {'.run': raw}, layouts),
                         [('sdk_alpha_registration_empty', 120)])
        self.assertEqual([layouts[120][k] for k in ('render', 'voice_state')], [True, 16])
        doc, raw = sdk_alpha_test_render_document(render=False)
        sdk_alpha_registrations(doc, {'.run': raw}, layouts)
        self.assertEqual([layouts[120][k] for k in ('render', 'voice_state')], [False, 0])
        for kwargs, message in (({'voice_state': 257}, 'voice_state is at most 256'),
                                ({'render': False, 'voice_state': 4}, 'needs a render')):
            with self.subTest(**kwargs):
                doc, raw = sdk_alpha_test_render_document(**kwargs)
                with self.assertRaisesRegex(build.BuildError, message):
                    sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_render_document(render=False)
        struct.pack_into('>I', raw, 72, 0x40001000)        # an address, not a function of the mod
        with self.assertRaisesRegex(build.BuildError, 'function in the mod'):
            sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_render_document()
        doc['relocs'][-1][3] = 'sym:external_render'
        with self.assertRaisesRegex(build.BuildError, 'own mod'):
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
                                 ('hello', str(HELLO)), ('glyphs', str(GLYPHS)),
                                 ('renders', RENDERS.write(tmp))):
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

    def test_the_ordinary_build_command_runs_the_sdk_step(self):
        stock = self.stocks()['digitakt-mk1']
        with tempfile.TemporaryDirectory() as tmp:
            with redirect_stdout(io.StringIO()):
                self.assertEqual(build.main([str(HELLO), '--stock', stock, '--out', tmp]), 0)
            built = sorted(Path(tmp).glob('*.elemod'))
            self.assertEqual(len(built), 1)
            mod = link.Mod2(json.loads(built[0].read_text()), 'hello')
            self.assertEqual((mod.names, mod.requires), (['machine:122'], ['core', 'machine-pages']))

    def test_a_build_for_several_targets_names_each_output(self):
        stocks = self.stocks()
        with tempfile.TemporaryDirectory() as tmp:
            for stock in stocks.values():
                sdk_alpha_build(str(HELLO), stock, tmp)
            self.assertEqual(sorted(p.name for p in Path(tmp).glob('*.elemod')),
                             ['sdk-hello-world-0.1-digitakt-mk1-os1.54.elemod',
                              'sdk-hello-world-0.1-digitone-mk1-os1.44.elemod'])
            one = EMPTY.write(tmp)                  # a single target keeps the plain name
            mod = json.loads(Path(one, 'mod.json').read_text())
            mod['devices'] = {'digitakt-mk1': ['1.54']}
            Path(one, 'mod.json').write_text(json.dumps(mod))
            path, _ = sdk_alpha_build(one, stocks['digitakt-mk1'], tmp)
            self.assertEqual(Path(path).name, 'sdk-test-empty-0.1.elemod')

    def test_sdk_alpha_refuses_imports_no_companion_exports(self):
        stock = self.stocks()['digitakt-mk1']
        with tempfile.TemporaryDirectory() as tmp:
            sdk_alpha_build(RENDERS.write(tmp), stock, tmp)          # imports only the ABI symbol
            source = Path(tmp, 'renders', 'machine.c')
            plain = source.read_text()
            for sample, refusal in (
                    ('ELEK_SAMPLE(ctx->knob[0] / 127.0)',
                     r'uses floating point \(.*__divdf3.*\): the ColdFire has none; .* use elek_level'),
                    ('helper()', 'imports helper, which no companion exports')):
                with self.subTest(sample=sample):
                    source.write_text(plain.replace('(void)ctx;', 'extern int helper(void);').replace(
                        'ELEK_SAMPLE(0)', sample))
                    with self.assertRaisesRegex(build.BuildError, refusal):
                        sdk_alpha_build(str(source.parent), stock, tmp)

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
            sdk_alpha_build(RENDERS.write(tmp + '/renders-dt'), stocks['digitakt-mk1'], tmp)
            sdk_alpha_build(DEFAULTS.write(tmp + '/defaults-dt'), stocks['digitakt-mk1'], tmp)
            for machine in (DEFAULTS, RENDERS):
                mod = sdk_alpha_build(machine.write(tmp + '/dn-' + machine.name), dn, tmp)[1]
                link.link([core, machines, mod], image)
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

    def test_sdk_alpha_builds_link_for_the_digitone_ii_and_keep_synth_conflicts(self):
        stock = os.environ.get('ELEKLOADER_DN2_SYX', '')
        cross = os.environ.get('ELEKLOADER_CROSS', 'm68k-linux-gnu-')
        if not Path(stock).is_file() or not shutil.which(cross + 'gcc'):
            self.skipTest('needs ELEKLOADER_DN2_SYX and m68k cross compiler')
        with tempfile.TemporaryDirectory() as tmp:
            core = build.build(str(ROOT / 'mods/core-dn2'), stock, tmp)[1]
            machines = build.build(str(ROOT / 'mods/machines-dn2'), stock, tmp)[1]
            firmware, dev, _ = formats.load(stock)
            image = formats.main_image(firmware, dev)
            built = {}
            for name, folder in (('empty', EMPTY.write(tmp)), ('one page', ONE_PAGE.write(tmp)),
                                 ('hello', str(HELLO)), ('glyphs', str(GLYPHS))):
                path, mod = sdk_alpha_build(folder, stock, tmp)
                built[name] = path, mod
                self.assertEqual(mod.requires, ['core', 'machines-dn2'])
                link.link([core, machines, mod], image)
            # The SDK's IDs reach 127: the synths' claims, which native SHARC voices share.
            self.assertEqual(built['empty'][1].names, ['mk2-synth:%d' % EMPTY.id])
            self.assertEqual(built['glyphs'][1].names, ['mk2-synth:127'])
            with open(built['empty'][0]) as fh:
                duplicate = json.load(fh)
            duplicate['id'], duplicate['exports'] = 'sdk-alpha-conflict', []
            duplicate['resources']['names'] = []  # descriptor inference also protects hand-written mods
            with self.assertRaisesRegex(elemod.ModError, 'mk2-synth:%d' % EMPTY.id):
                link.link([core, machines, built['empty'][1], link.Mod2(duplicate, 'conflict')], image)
            with self.assertRaisesRegex(build.BuildError, 'render is not supported on digitone-mk2'):
                sdk_alpha_build(RENDERS.write(tmp + '/renders'), stock, tmp)
            with self.assertRaisesRegex(build.BuildError, 'defaults are not supported on digitone-mk2'):
                sdk_alpha_build(DEFAULTS.write(tmp + '/defaults'), stock, tmp)
            midi = SdkMachine("Midi slot", register=False, source=EMPTY.source)
            midi.id = 4                                 # 4..127 elsewhere; MIDI's type on the Digitone II
            with self.assertRaisesRegex(build.BuildError, r'invalid machine slot 4 \(expected 5\.\.127\)'):
                sdk_alpha_build(midi.write(tmp + '/midi'), stock, tmp)


if __name__ == '__main__':
    unittest.main()
