# SPDX-License-Identifier: GPL-2.0-or-later
"""The SDK alpha builder and linker: what a registration may declare, what is refused, and what the
build adds for each device. No emulator; the behaviour on the device is in test_sdk_alpha_*.py.

Build checks need ELEKLOADER_STOCK_154, ELEKLOADER_STOCK_DN1_144 (Digitone mk1 OS 1.44) and a cross
compiler (ELEKLOADER_CROSS); the Digitone II's need ELEKLOADER_DN2_SYX (OS 1.12) and the cross compiler."""
from contextlib import redirect_stdout
import io
import json
import os
import re
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
from elekloader.sdk import sdk_alpha_build as B     # noqa: E402
from elekloader.sdk.sdk_alpha_build import sdk_alpha_build, sdk_alpha_main, sdk_alpha_registrations  # noqa: E402
from sdk_alpha_devices import Device, Digitakt1, Digitone1, Digitone2  # noqa: E402
from sdk_alpha_harness import SdkMachine  # noqa: E402

# Machines the build tests use, as the C an author writes. Not registered for the emulator tests.
EMPTY = SdkMachine("Empty", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static const struct elek_machine empty = { .id = @ID@, .name = "Empty", .short_name = "EMPT" };
    ELEK_REGISTER_MACHINE(empty);
"""))
ONE_PAGE = SdkMachine("One page", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static const struct elek_knob knobs[] = { { "KNOB", .max = 127 } };
    static const struct elek_machine one = {
        .id = @ID@, .name = "One page", .short_name = "ONE", ELEK_KNOBS(knobs),
        .pages = { { "ONE", { &knobs[0] } } },
    };
    ELEK_REGISTER_MACHINE(one);
"""))
LONG_LABEL = SdkMachine("Long label", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static const struct elek_knob knobs[] = { { "TOOLONG", .max = 127 } };
    static const struct elek_machine long_label = {
        .id = @ID@, .name = "Long label", .short_name = "LONG", ELEK_KNOBS(knobs),
        .pages = { { "ONE", { &knobs[0] } } },
    };
    ELEK_REGISTER_MACHINE(long_label);
"""))
# A machine with a sound: Digitakt renders it; the other targets refuse it.
RENDERS = SdkMachine("Renders", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    static void render(const struct elek_voice *v, elek_sample *out)
    {
        (void)v;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = ELEK_SAMPLE(0);
    }
    static const struct elek_machine renders = {
        .id = @ID@, .name = "Renders", .short_name = "REND", .render = render,
    };
    ELEK_REGISTER_MACHINE(renders);
"""))
# A render in SHARC assembly, as the Digitone II takes it: declared in C, defined in the mod's "sharc" sources.
SHARC_RENDER = SdkMachine("Sharc render", register=False, source=textwrap.dedent("""
    #include "elekloader/sdk_alpha_machine.h"
    void tone_render(const struct elek_voice *v, elek_sample *out);
    static const struct elek_knob knobs[] = { { "KNOB", .max = 127 } };
    static const struct elek_machine tone = {
        .id = @ID@, .name = "Sharc render", .short_name = "TONE", ELEK_KNOBS(knobs), .render = tone_render,
    };
    ELEK_REGISTER_MACHINE(tone);
"""))
# A knob that starts above its min.
DEFAULTS = SdkMachine("Defaults", register=False, source=ONE_PAGE.source.replace(
    '.max = 127', '.min = -50, .max = 50'))
# Sixteen machines in one mod: more than the Digitone mk1's MACHINES menu lists.
SIXTEEN = SdkMachine("Sixteen", register=False, source='#include "elekloader/sdk_alpha_machine.h"\n' + ''.join(
    'static const struct elek_machine m%d = { .id = %d, .name = "M", .short_name = "M" };\n'
    'ELEK_REGISTER_MACHINE(m%d);\n' % (i, 100 + i, i) for i in range(16)))
ELEK_STATE_BYTES = 256                          # ELEK_VOICE_STATE_MAX
HELLO = ROOT / 'examples/sdk-hello-world'       # two pages
GLYPHS = ROOT / 'examples/sdk-glyphs'


M = 8                   # the machine, after the registration's pointer to it and its ABI reference
RENDER, NAME, SNAME = M + B.MACHINE_SIZE, M + B.MACHINE_SIZE + 8, M + B.MACHINE_SIZE + 16
KNOBS, GLYPH = 400, 1000


def page_at(page, encoder=None):
    """The offset of a page's label, or of one of its encoders."""
    at = M + B.MACHINE_PAGES + B.PAGE_SIZE * page
    return at if encoder is None else at + B.PAGE_ENCODER + 4 * encoder


def knob_at(k, field=0):
    return KNOBS + B.KNOB_SIZE * k + field


def sdk_alpha_test_document():
    return sdk_alpha_test_render_document(render=False)


def sdk_alpha_test_render_document(render=True):
    """A registration of an empty machine at M; its render (an rts) at RENDER."""
    raw = bytearray(SNAME + 8)
    struct.pack_into('>i', raw, M, 120)
    raw[RENDER:RENDER + 2] = b'\x4e\x75'
    raw[NAME:NAME + 6] = b'Empty\0'
    raw[SNAME:SNAME + 5] = b'EMPT\0'
    doc = {'exports': ['sdk_alpha_registration_empty'], 'imports': [B.SDK_ALPHA_ABI],
           'symbols': {'sdk_alpha_registration_empty': ['.run', 0]},
           'relocs': [['.run', o, 'abs32', 'sec:.run', a]
                      for o, a in ((0, M), (M + B.MACHINE_NAME, NAME), (M + B.MACHINE_SHORT_NAME, SNAME))] +
                     [['.run', 4, 'abs32', 'sym:' + B.SDK_ALPHA_ABI, 0]]}
    if render:
        doc['relocs'].append(['.run', M + B.MACHINE_RENDER, 'abs32', 'sec:.run', RENDER])
    return doc, raw


def sdk_alpha_test_controls_document():
    """Fifteen knobs 0..127 labelled "Empty", on two pages."""
    doc, raw = sdk_alpha_test_document()
    raw.extend(bytes(GLYPH + 64 - len(raw)))
    struct.pack_into('>I', raw, M + B.MACHINE_KNOB_COUNT, 15)
    doc['relocs'].append(['.run', M + B.MACHINE_KNOBS, 'abs32', 'sec:.run', KNOBS])
    for k in range(15):
        struct.pack_into('>ii', raw, knob_at(k, B.KNOB_MIN), 0, 127)
        doc['relocs'].append(['.run', knob_at(k, B.KNOB_LABEL), 'abs32', 'sec:.run', NAME])
    for page in range(2):
        doc['relocs'].append(['.run', page_at(page), 'abs32', 'sec:.run', NAME])
        for encoder in range(8):
            # Knob zero is D on page 0; page 1 shares it at H.
            k = ([2, 3, 4, 0, 5, 6, 7, 1] if page == 0 else [8, 9, 10, 11, 12, 13, 14, 0])[encoder]
            doc['relocs'].append(['.run', page_at(page, encoder), 'abs32', 'sec:.run', knob_at(k)])
    return doc, raw


def reloc(doc, offset):
    return next(r for r in doc['relocs'] if r[1] == offset)


def without(doc, *offsets):
    doc['relocs'] = [r for r in doc['relocs'] if r[1] not in offsets]


class SdkAlphaTests(unittest.TestCase):
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
        self.assertEqual(list(sdk_alpha_registrations(doc, {'.run': raw})), [120])
        self.assertEqual(sdk_alpha_registrations(doc, {'.run': raw})[120]['at'], ('.run', M))

    def test_sdk_alpha_invalid_registrations(self):
        empty = r'machine "Empty" \(id 120\)'
        for offset, value, message in (
                (M, 3, r'machine "Empty" \(id 3\): id 3 is outside 4\.\.127$'),
                (M, 128, r'machine "Empty" \(id 128\): id 128 is outside 4\.\.127$'),
                (M + B.MACHINE_KNOB_COUNT, 1, empty + r': knob_count is 1 but knobs is null \(give the knobs '
                                                      r'with ELEK_KNOBS\)$'),
                (page_at(0, 0), 1, empty + r', pages\[0\], encoder A: the knob is 0x00000001, not null or an '
                                           r'address in this mod$')):
            with self.subTest(offset=offset, value=value):
                doc, raw = sdk_alpha_test_document()
                struct.pack_into('>I', raw, offset, value)
                with self.assertRaisesRegex(build.BuildError, message):
                    sdk_alpha_registrations(doc, {'.run': raw})
        for kind, message in (
                ('duplicate', empty + r': machine "Empty" has the same id: give each machine its own id$'),
                ('missing', r'no machine registered: add ELEK_REGISTER_MACHINE\(machine\)$'),
                ('pointer', r'ELEK_REGISTER_MACHINE\(empty\): registers no machine$'),
                ('abi', r'the mod does not use sdk_alpha_abi_v1: rebuild it with this SDK\'s header$'),
                ('name', r'machine id 120: name is empty \(give it 1\.\.16 printable ASCII characters\)$'),
                ('long name', r'machine id 120: name "Seventeen chars!!" has 17 characters \(at most 16\)$'),
                ('unprintable', r'machine "Empty" \(id 120\): short_name "E\\u0007PT" has 0x07, which is not '
                                r'printable ASCII$'),
                ('truncated', r'ELEK_REGISTER_MACHINE\(empty\): the machine runs past the end of its data'),
                ('pages', empty + r': pages\[1\] "Empty" is set but pages\[0\] has no label$')):
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
                    doc['imports'] = ['sdk_alpha_dt1_ui_v1']
                elif kind == 'name':
                    raw[NAME] = 0
                elif kind == 'long name':
                    raw.extend(b'Seventeen chars!!\0')
                    reloc(doc, M + B.MACHINE_NAME)[4] = len(raw) - 18
                elif kind == 'unprintable':
                    raw[SNAME + 1] = 7
                elif kind == 'truncated':
                    raw = raw[:M + 10]
                elif kind == 'pages':               # a second page without a first
                    doc['relocs'].append(['.run', page_at(1), 'abs32', 'sec:.run', NAME])
                with self.assertRaisesRegex(build.BuildError, '^sdk alpha: ' + message):
                    sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_pages_hold_the_machines_own_knobs(self):
        doc, raw = sdk_alpha_test_controls_document()
        layouts = sdk_alpha_registrations(doc, {'.run': raw})
        self.assertEqual([layouts[120]['pages'][p]['knobs'][e]['id'] for p, e in ((0, 3), (1, 7), (0, 0))],
                         [0, 0, 2])
        without(doc, page_at(0, 3))                 # a blank position
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        without(doc, *[page_at(p, e) for p in range(2) for e in range(8)])   # label-only pages
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        machine = r'^sdk alpha: machine "Empty" \(id 120\)'
        not_a_knob = r'does not point at one of the machine\'s knobs \(use &knobs\[ID\], ID below 15\)$'
        for edit, message in (
                (lambda doc, raw: struct.pack_into('>I', raw, M + B.MACHINE_KNOB_COUNT, 16),
                 machine + r': has 16 knobs \(at most 15\)$'),
                (lambda doc, raw: reloc(doc, page_at(0, 0)).__setitem__(4, knob_at(2) + 4),
                 machine + r', pages\[0\] "Empty", encoder A: ' + not_a_knob),
                (lambda doc, raw: reloc(doc, page_at(0, 0)).__setitem__(4, knob_at(15)),
                 machine + r', pages\[0\] "Empty", encoder A: ' + not_a_knob),
                (lambda doc, raw: reloc(doc, page_at(1, 0)).__setitem__(4, NAME),
                 machine + r', pages\[1\] "Empty", encoder A: ' + not_a_knob),
                (lambda doc, raw: reloc(doc, page_at(0, 0)).__setitem__(3, 'sym:external_knob'),
                 machine + r', pages\[0\] "Empty", encoder A: the knob points at external_knob, outside this mod'),
                (lambda doc, raw: struct.pack_into('>ii', raw, knob_at(4, B.KNOB_MIN), 5, 5),
                 machine + r', knobs\[4\] "Empty": max 5 is not above min 5$'),
                (lambda doc, raw: struct.pack_into('>ii', raw, knob_at(4, B.KNOB_MIN), 0, -1),
                 machine + r', knobs\[4\] "Empty": max -1 is not above min 0$'),
                (lambda doc, raw: without(doc, page_at(1)),
                 machine + r', pages\[1\]: has knobs but no label \(give the page a label\)$'),
                (lambda doc, raw: without(doc, page_at(0), *[page_at(0, e) for e in range(8)]),
                 machine + r': pages\[1\] "Empty" is set but pages\[0\] has no label$'),
                (lambda doc, raw: without(doc, knob_at(3)),
                 machine + r', knobs\[3\]: has no label \(give it 1\.\.5 printable ASCII characters\)$'),
                (lambda doc, raw: without(doc, page_at(0)) or doc['relocs'].append(
                    ['.run', page_at(0), 'abs32', 'sec:.run', len(raw)]) or raw.extend(b'SEVENTEEN-LETTERS\0'),
                 machine + r', pages\[0\]: label "SEVENTEEN-LETTERS" has 17 characters \(at most 16\)$'),
                (lambda doc, raw: without(doc, M + B.MACHINE_KNOBS),
                 machine + r': knob_count is 15 but knobs is null \(give the knobs with ELEK_KNOBS\)$'),
                (lambda doc, raw: doc.__setitem__('imports', ['unknown_sdk_abi']), 'sdk_alpha_abi_v1')):
            with self.subTest(message=message):
                doc, raw = sdk_alpha_test_controls_document()
                edit(doc, raw)
                with self.assertRaisesRegex(build.BuildError, message):
                    sdk_alpha_registrations(doc, {'.run': raw})
        # Knobs may live in another section.
        doc, raw = sdk_alpha_test_controls_document()
        other = raw[KNOBS:knob_at(15)]
        for r in doc['relocs']:
            if KNOBS <= r[4] < knob_at(15) and r[3] == 'sec:.run':
                r[3:5] = ['sec:.other', r[4] - KNOBS]
            if KNOBS <= r[1] < knob_at(15):
                r[0:2] = ['.other', r[1] - KNOBS]
        self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw, '.other': other})), 1)

    def test_sdk_alpha_glyph_registration(self):
        glyph = knob_at(0, B.KNOB_GLYPH)
        for supplied in (False, True):
            doc, raw = sdk_alpha_test_controls_document()
            if supplied:
                doc['relocs'].append(['.run', glyph, 'abs32', 'sec:.run', GLYPH])
            self.assertEqual(len(sdk_alpha_registrations(doc, {'.run': raw})), 1)
        knob = r'^sdk alpha: machine "Empty" \(id 120\), knobs\[0\] "Empty": '
        for kind, message in (
                ('truncated', r'glyph runs past the end of its data \(point it at a struct elek_glyph\)$'),
                ('external', r'glyph points at external_glyph, outside this mod: point it at data in the mod$'),
                ('unrelocated', r'glyph is 0x000003e8, not null or an address in this mod$'),
                ('duplicate', r'glyph is not a plain 32-bit address \(build with the SDK\'s flags\)$'),
                ('unaligned', r'glyph is at an odd address \(point it at a struct elek_glyph\)$')):
            with self.subTest(kind=kind):
                doc, raw = sdk_alpha_test_controls_document()
                doc['relocs'].append(['.run', glyph, 'abs32', 'sec:.run', GLYPH])
                if kind == 'truncated':
                    raw = raw[:GLYPH + 31]
                elif kind == 'external':
                    doc['relocs'][-1][3] = 'sym:external_glyph'
                elif kind == 'unrelocated':
                    doc['relocs'].pop()
                    struct.pack_into('>I', raw, glyph, GLYPH)
                elif kind == 'duplicate':
                    doc['relocs'].append(doc['relocs'][-1][:])
                elif kind == 'unaligned':
                    doc['relocs'][-1][4] += 1
                with self.assertRaisesRegex(build.BuildError, knob + message):
                    sdk_alpha_registrations(doc, {'.run': raw})

    def test_sdk_alpha_defaults_are_clamped_into_the_range(self):
        for low, high, default, expected in ((-50, 50, 0, 0), (0, 127, 100, 100), (10, 20, 0, 10),
                                             (-50, 50, -51, -50), (0, 127, 200, 127)):
            with self.subTest(low=low, high=high, default=default):
                doc, raw = sdk_alpha_test_controls_document()
                struct.pack_into('>iii', raw, knob_at(0, B.KNOB_MIN), low, high, default)
                layouts = sdk_alpha_registrations(doc, {'.run': raw})
                self.assertEqual(layouts[120]['pages'][0]['knobs'][3]['def'], expected)

    def test_sdk_alpha_render_registration(self):
        doc, raw = sdk_alpha_test_render_document()
        self.assertTrue(sdk_alpha_registrations(doc, {'.run': raw})[120]['render'])
        doc, raw = sdk_alpha_test_render_document(render=False)
        self.assertFalse(sdk_alpha_registrations(doc, {'.run': raw})[120]['render'])
        doc, raw = sdk_alpha_test_render_document(render=False)
        struct.pack_into('>I', raw, M + B.MACHINE_RENDER, 0x40001000)        # an address, not a function of the mod
        with self.assertRaisesRegex(build.BuildError, r'^sdk alpha: machine "Empty" \(id 120\): render is '
                                                      r'0x40001000, not null or an address in this mod$'):
            sdk_alpha_registrations(doc, {'.run': raw})
        doc, raw = sdk_alpha_test_render_document()
        doc['relocs'][-1][3] = 'sym:external_render'      # declared only: the builder pairs it, or refuses
        self.assertEqual(sdk_alpha_registrations(doc, {'.run': raw})[120]['render'], 'external_render')

    def test_sdk_alpha_play_and_prio_registration(self):
        doc, raw = sdk_alpha_test_document()
        layout = sdk_alpha_registrations(doc, {'.run': raw})[120]
        self.assertEqual((layout['play'], layout['prio']), (0, 0))         # ELEK_POLY by default
        raw[M + B.MACHINE_PLAY], raw[M + B.MACHINE_PRIO] = 2, 1
        layout = sdk_alpha_registrations(doc, {'.run': raw})[120]
        self.assertEqual((layout['play'], layout['prio']), (2, 1))
        for field, name in ((B.MACHINE_PLAY, 'play'), (B.MACHINE_PRIO, 'prio')):
            doc, raw = sdk_alpha_test_document()
            raw[M + field] = 3
            with self.subTest(field=name), self.assertRaisesRegex(
                    build.BuildError, r'^sdk alpha: machine "Empty" \(id 120\): %s is 3: give ELEK_' % name):
                sdk_alpha_registrations(doc, {'.run': raw})

    def test_a_mono_machine_is_refused_where_no_companion_carries_it_out(self):
        for device in (Digitakt1, Digitone1, Digitone2):
            if 'mono' in device.features:
                with self.subTest(device=device.key), tempfile.TemporaryDirectory() as tmp:
                    _path, _mod, layouts = sdk_alpha_build(str(ROOT / 'examples/sdk-mono'), device.stock(), tmp)
                    self.assertEqual(layouts[124]['play'], 1)
                continue
            with self.subTest(device=device.key), tempfile.TemporaryDirectory() as tmp, \
                    self.assertRaisesRegex(build.BuildError, r'^sdk alpha: machine "SDK Mono" \(id 124\): ELEK_MONO '
                                                             r'is not supported on %s [\d.]+ yet$' % device.key):
                sdk_alpha_build(str(ROOT / 'examples/sdk-mono'), device.stock(), tmp)

    def test_a_voice_state_too_big_does_not_compile(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        for size, fits in ((ELEK_STATE_BYTES, True), (ELEK_STATE_BYTES + 1, False)):
            with self.subTest(size=size):
                result = subprocess.run(
                    [cc, '-std=c99', '-fsyntax-only', '-x', 'c', '-I', str(ROOT / 'elekloader/sdk/include'), '-'],
                    input='#include "elekloader/sdk_alpha_machine.h"\n'
                          'struct state { char bytes[%d]; };\n'
                          'void *f(const struct elek_voice *v) { return ELEK_STATE(struct state, v); }\n' % size,
                    capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, fits, result.stderr)

    def test_the_builder_reads_the_headers_limits_and_layout(self):
        header = (ROOT / 'elekloader/sdk/include/elekloader/sdk_alpha_machine.h').read_text()
        limits = {name: int(n) for name, n in re.findall(r'^#define (ELEK_UI_\w+_MAX) (\d+)', header, re.M)}
        self.assertEqual(limits, B.LIMITS)
        layout = {(t, f): int(n) for t, f, n in re.findall(r'^ELEK_OFFSET\((\w+), (\w+), (\d+)\);', header, re.M)}
        layout.update({(t, None): int(n) for t, n in re.findall(r'^ELEK_SIZE\((\w+), (\d+)\);', header, re.M)})
        self.assertEqual(layout, B.LAYOUT)
        for names in (B.PLAY_NAMES, B.PRIO_NAMES):                       # the enums, in order
            self.assertIn('enum { %s };' % ', '.join(names), header)
        self.assertEqual(ELEK_STATE_BYTES, int(re.search(r'#define ELEK_VOICE_STATE_MAX (\d+)', header)[1]))

    def test_a_callbacks_input_is_read_only(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('no host C compiler')
        result = subprocess.run([cc, '-std=c99', '-fsyntax-only', '-x', 'c',
                                 '-I', str(ROOT / 'elekloader/sdk/include'), '-'],
                                input='#include "elekloader/sdk_alpha_machine.h"\n'
                                      'void bad(const struct elek_ui *ui) { ui->knob[0] = 1; }\n',
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0, 'a callback can write its input')
        self.assertRegex(result.stderr, 'read-only|const', 'failed for another reason')

    def stocks(self):
        return {device.key: device.stock() for device in (Digitakt1, Digitone1)}

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
                path, mod, _ = sdk_alpha_build(folder, stock, tmp)
                built[name] = path, mod
                result = subprocess.run([sys.executable, '-m', 'elekloader.lint', path, '--stock', stock,
                                         '--with', core_path, '--with', pages_path, '--json'],
                                        cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, name + '\n' + result.stdout + result.stderr)
                link.link([core, pages, mod], image)
                self.assertEqual(mod.requires, ['core', 'machine-pages'])
            self.assertEqual(built['empty'][1].implied_names(image), ['machine:%d' % EMPTY.id])
            self.assertEqual(built['hello'][1].implied_names(image), ['machine:122'])
            with open(built['hello'][0]) as fh:
                doc = json.load(fh)
            self.assertEqual([c['to'] for c in doc['contribute']], ['sdk_alpha_machines', 'core_machines'])
            self.assertEqual(doc['contribute'][1]['relocs'][0][2], 'sec:.run')
            at = doc['contribute'][1]['relocs'][0][3]                   # the descriptor the builder wrote
            run = elemod.parts_bytes(built['hello'][1].sections['.run']['parts'], image, dev)
            self.assertEqual(struct.unpack_from('>iiiiiiII', run, at)[::3], (122, 0, 0))   # id, icon, tag
            self.assertEqual(struct.unpack_from('>ii', run, at + 16), (3, 122))         # params, render
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
            firmware, dev, _ = formats.load(stock)
            self.assertEqual((mod.implied_names(formats.main_image(firmware, dev)), mod.requires),
                             (['machine:122'], ['core', 'machine-pages']))

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
            path, _, _ = sdk_alpha_build(one, stocks['digitakt-mk1'], tmp)
            self.assertEqual(Path(path).name, 'sdk-test-empty-0.1.elemod')

    def test_sdk_alpha_refuses_imports_no_companion_exports(self):
        stock = self.stocks()['digitakt-mk1']
        with tempfile.TemporaryDirectory() as tmp:
            sdk_alpha_build(RENDERS.write(tmp), stock, tmp)          # imports only the ABI symbol
            source = Path(tmp, 'renders', 'machine.c')
            plain = source.read_text()
            for sample, refusal in (
                    ('ELEK_SAMPLE(v->knob[0] / 127.0)',
                     r'uses floating point \(.*__divdf3.*\): the ColdFire has none; .* use elek_level'),
                    ('helper()', 'imports helper, which no companion exports')):
                with self.subTest(sample=sample):
                    source.write_text(plain.replace('(void)v;', 'extern int helper(void);').replace(
                        'ELEK_SAMPLE(0)', sample))
                    with self.assertRaisesRegex(build.BuildError, refusal):
                        sdk_alpha_build(str(source.parent), stock, tmp)

    def test_sdk_alpha_one_source_builds_for_both_devices(self):
        stocks = self.stocks()
        dn = stocks['digitone-mk1']
        with tempfile.TemporaryDirectory() as tmp:
            _, dt_mod, _ = sdk_alpha_build(EMPTY.write(tmp + '/dt'), stocks['digitakt-mk1'], tmp + '/dt-out')
            firmware, dev, _ = formats.load(stocks['digitakt-mk1'])
            self.assertEqual((dt_mod.implied_names(formats.main_image(firmware, dev)), dt_mod.requires),
                             (['machine:%d' % EMPTY.id], ['core', 'machine-pages']))
            path, dn_mod, _ = sdk_alpha_build(EMPTY.write(tmp + '/dn'), dn, tmp + '/dn-out')
            firmware, dev, _ = formats.load(dn)
            image = formats.main_image(firmware, dev)
            self.assertEqual((dn_mod.implied_names(image), dn_mod.requires),
                             (['dnmachine:%d' % EMPTY.id], ['core', 'machines']))
            with open(path) as fh:
                doc = json.load(fh)
            self.assertEqual([c['to'] for c in doc['contribute']], ['sdk_alpha_machines'])
            core = build.build(str(ROOT / 'mods/core-dn1'), dn, tmp)[1]
            machines = build.build(str(ROOT / 'mods/machines-dn1'), dn, tmp)[1]
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
            with self.assertRaisesRegex(build.BuildError, 'digitone-mk1 1.44 holds at most 15 SDK machines; '
                                                          'this mod registers 16'):
                sixteen = SIXTEEN.write(tmp + '/sixteen')
                sdk_alpha_build(sixteen, dn, tmp)
            self.assertEqual(len(sdk_alpha_build(sixteen, stocks['digitakt-mk1'], tmp)[2]), 16)   # it holds 32
            for stock in stocks.values():
                with self.assertRaisesRegex(build.BuildError, r'^sdk alpha: machine "Long label" \(id \d+\), knobs\[0\]: '
                                                              r'label "TOOLONG" has 7 characters \(at most 5\)$'):
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
        stock = Digitone2.stock()
        with tempfile.TemporaryDirectory() as tmp:
            core = build.build(str(ROOT / 'mods/core-dn2'), stock, tmp)[1]
            machines = build.build(str(ROOT / 'mods/machines-dn2'), stock, tmp)[1]
            firmware, dev, rel = formats.load(stock)
            dsp = firmware.section(rel.sharc['section'])     # machines-dn2's dispatcher
            image = formats.main_image(firmware, dev)
            built = {}
            for name, folder in (('empty', EMPTY.write(tmp)), ('one page', ONE_PAGE.write(tmp)),
                                 ('hello', str(HELLO)), ('glyphs', str(GLYPHS))):
                path, mod, _ = sdk_alpha_build(folder, stock, tmp)
                built[name] = path, mod
                self.assertEqual(mod.requires, ['core', 'machines-dn2'])
                link.link([core, machines, mod], image, sharc_stream=dsp)
            # The SDK's IDs reach 127: the synths' claims, which native SHARC voices share.
            self.assertEqual(built['empty'][1].implied_names(image), ['mk2-synth:%d' % EMPTY.id])
            self.assertEqual(built['glyphs'][1].implied_names(image), ['mk2-synth:127'])
            with open(built['empty'][0]) as fh:
                duplicate = json.load(fh)
            duplicate['id'], duplicate['exports'] = 'sdk-alpha-conflict', []
            with self.assertRaisesRegex(elemod.ModError, 'mk2-synth:%d' % EMPTY.id):
                link.link([core, machines, built['empty'][1], link.Mod2(duplicate, 'conflict')], image,
                          sharc_stream=dsp)
            with self.assertRaisesRegex(build.BuildError, r'^sdk alpha: machine "Renders" \(id \d+\): render is C, '
                                                          r'which digitone-mk2 1\.12 cannot play: write it in SHARC '
                                                          r'assembly, declare it under #ifdef ELEK_TARGET_SHARC'):
                sdk_alpha_build(RENDERS.write(tmp + '/renders'), stock, tmp)
            defaults = sdk_alpha_build(DEFAULTS.write(tmp + '/defaults'), stock, tmp)[1]
            link.link([core, machines, defaults], image, sharc_stream=dsp)

    def sharc_render(self, tmp, assembly):
        """SHARC_RENDER's folder, with tone.s and mod.json's "sharc" sources."""
        folder = Path(SHARC_RENDER.write(tmp))
        (folder / 'tone.s').write_text(assembly)
        mod = json.loads((folder / 'mod.json').read_text())
        mod['sharc'] = {'sources': ['tone.s']}
        (folder / 'mod.json').write_text(json.dumps(mod))
        return str(folder)

    def test_sdk_alpha_pairs_a_digitone_ii_render_with_its_sharc_label(self):
        stock = Digitone2.stock()
        body = ('.SECTION/SW seg_swco;\n%s\ntone_render.:\n'
                '    I12=DM(M7,I6);\n    JUMP(M14,I12) (DB);\n    RFRAME;\n    NOP;\n')
        where = r'^sdk alpha: machine "Sharc render" \(id \d+\): '
        for name, assembly, refusal in (
                ('missing', body % '.GLOBAL other_render.;',
                 where + r'render is tone_render, but no file in mod.json\'s "sharc": \{"sources": \[...\]\} has '
                         r'the global label tone_render\.$'),):
            with self.subTest(name), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaisesRegex(build.BuildError, refusal):
                    sdk_alpha_build(self.sharc_render(tmp, assembly), stock, tmp)
        if not os.environ.get('SELACHE'):
            self.skipTest('the kernel needs the pinned Selache (SELACHE)')
        with tempfile.TemporaryDirectory() as tmp:
            core = build.build(str(ROOT / 'mods/core-dn2'), stock, tmp)[1]
            machines = build.build(str(ROOT / 'mods/machines-dn2'), stock, tmp)[1]
            firmware, dev, rel = formats.load(stock)
            dsp = firmware.section(rel.sharc['section'])     # machines-dn2's dispatcher
            image = formats.main_image(firmware, dev)
            for folder, mid, knobs in ((self.sharc_render(tmp, body % '.GLOBAL tone_render.;'), SHARC_RENDER.id,
                                        [[0, 127]]),
                                       (str(ROOT / 'examples/sdk-sine'), 123, [[-50, 50]])):
                path, mod, layouts = sdk_alpha_build(folder, stock, tmp)
                doc = json.loads(Path(path).read_text())
                self.assertEqual([(k['kind'], k['id'], k['knobs']) for k in doc['sharc']['kernels']],
                                 [('sdk', mid, knobs)])
                self.assertEqual(doc['imports'], [B.SDK_ALPHA_ABI])
                # .render is nonzero on the ColdFire, which never calls it: an rts in the mod
                sec, off = layouts[mid]['at']
                r, = [r for r in doc['relocs'] if r[:2] == [sec, off + B.MACHINE_RENDER]]
                run = elemod.parts_bytes(mod.sections['.run']['parts'], image, dev)
                self.assertEqual((r[3], run[r[4]:r[4] + 2]), ('sec:.run', b'\x4e\x75'))
                link.link([core, machines, mod], image, sharc_stream=dsp)
            midi = SdkMachine("Midi slot", register=False, source=EMPTY.source)
            midi.id = 4                                 # 4..127 elsewhere; MIDI's type on the Digitone II
            with self.assertRaisesRegex(build.BuildError, r'^sdk alpha: machine "Empty" \(id 4\): id 4 is outside '
                                                          r'5\.\.127 on digitone-mk2 1\.12$'):
                sdk_alpha_build(midi.write(tmp + '/midi'), stock, tmp)


if __name__ == '__main__':
    unittest.main()
