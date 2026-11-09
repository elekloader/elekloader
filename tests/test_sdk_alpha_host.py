# SPDX-License-Identifier: GPL-2.0-or-later
"""python -m elekloader.sdk.sdk_alpha_host, the authors' host run, on the SDK examples."""
from pathlib import Path
import contextlib
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk import sdk_alpha_host  # noqa: E402

# Shows the track in its knob's label and the page in its value text, and renders its velocity on track 3 only.
PROBE = r'''#include "elekloader/sdk_alpha_machine.h"
static const struct elek_knob knobs[] = { { "K", .max = 1 } };
static void present(const struct elek_ui *ui)
{
    ui->view[0].label = elek_num(ui, "T", (int32_t)ui->track);
    ui->view[0].value = elek_num(ui, "P", ui->page);
}
static void render(const struct elek_voice *v, elek_sample *out)
{
    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
        out[i] = v->track == 3 ? elek_level(v->velocity, 127) : 0;
}
static const struct elek_machine machine = {
    .id = 200, .name = "PROBE", .short_name = "PRB", ELEK_KNOBS(knobs),
    .pages = { { "ONE", { [ELEK_A] = &knobs[0] } }, { "TWO", { [ELEK_A] = &knobs[0] } } },
    .present = present, .render = render,
};
ELEK_REGISTER_MACHINE(machine);
'''


def host(*args):
    done = subprocess.run([sys.executable, '-m', 'elekloader.sdk.sdk_alpha_host', *args], cwd=ROOT,
                          capture_output=True, text=True)
    return done.returncode, done.stdout + done.stderr


@unittest.skipUnless(shutil.which(os.environ.get('CC', 'cc')), 'no host C compiler')
class HostRunTests(unittest.TestCase):
    def test_sine_renders_the_note_at_half_level_in_both_builds(self):
        for flags in ([], ['--float']):
            with self.subTest(flags=flags):
                code, out = host('examples/sdk-sine', '--note', '69', '--knob', 'FINE=50', *flags)
                self.assertEqual(code, 0, out)
                self.assertIn('FINE=50    FINE "FINE"="+50" glyph', out)
                self.assertIn('peak 0.500', out)
                hz = float(re.search(r'about ([\d.]+) Hz', out).group(1))
                self.assertAlmostEqual(hz, 440 * 2 ** (0.5 / 12), delta=0.2)

    def test_glyphs_and_wav(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, out = host('examples/sdk-sine', '--glyphs', '--blocks', '10', '--wav', tmp + '/sine.wav')
            self.assertEqual(code, 0, out)
            self.assertIn('    ....########....', out)              # FLAT's arrow head
            with wave.open(tmp + '/sine.wav') as w:
                self.assertEqual((w.getnchannels(), w.getframerate(), w.getnframes()), (1, 48000, 320))

    def test_hello_world_presents_and_has_no_render(self):
        code, out = host('examples/sdk-hello-world')
        self.assertEqual(code, 0, out)
        self.assertIn('AMNT=127   PITCH "A 127"=10 hidden  AMNT "AMNT"=127', out)
        self.assertIn('no render', out)

    def test_fractional_note(self):
        code, out = host('examples/sdk-sine', '--note', '69.5')
        self.assertEqual(code, 0, out)
        hz = float(re.search(r'about ([\d.]+) Hz', out).group(1))
        self.assertAlmostEqual(hz, 440 * 2 ** (0.5 / 12), delta=0.2)

    def test_retrigger_starts_each_note_at_phase_zero(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            held = sdk_alpha_host.host(ROOT / 'examples/sdk-sine', blocks=20)
            again = sdk_alpha_host.host(ROOT / 'examples/sdk-sine', blocks=20, retrigger=7)
        self.assertIn('a new note every 7', out.getvalue())
        self.assertNotEqual(held[7 * 32], 0)
        self.assertEqual((again[7 * 32], again[14 * 32]), (0, 0))
        self.assertEqual(again[:7 * 32], held[:7 * 32])

    def test_present_starts_from_the_knob_values(self):
        code, out = host('examples/sdk-hello-world', '--knob', 'AMNT=5')
        self.assertEqual(code, 0, out)
        self.assertIn('PITCH=20   PITCH "A 5"=20', out)

    def test_velocity_track_and_page_reach_the_machine(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 'mod.json').write_text('{"sources": ["m.c"]}')
            Path(tmp, 'm.c').write_text(PROBE)
            code, out = host(tmp, '--velocity', '30', '--track', '3', '--page', '1', '--blocks', '1')
            self.assertEqual(code, 0, out)
            self.assertIn('present on page 1, track 3,', out)
            self.assertIn('K "T3"="P1"', out)
            self.assertIn('velocity 30, track 3, 1 blocks: peak 0.236', out)
            code, out = host(tmp, '--page', '2')
            self.assertTrue(code and '--page 2: PROBE has 2 page(s)' in out, out)

    def test_refusals(self):
        code, out = host('examples/sdk-sine', '--knob', 'FINE=99')
        self.assertTrue(code and 'FINE takes -50..50' in out, out)
        for args, message in ((['--page', '1'], '--page 1: SDK Sine has 1 page(s)'),
                              (['--velocity', '0'], '--velocity 0: give 1..127'),
                              (['--track', '-1'], '--track -1: give a track from 0'),
                              (['--note', '127.5'], '--note 127.5: give a MIDI note 0..127'),
                              (['--retrigger', '-1'], '--retrigger -1: give blocks between notes')):
            code, out = host('examples/sdk-sine', *args)
            self.assertTrue(code and message in out, out)
        code, out = host('examples/hello-marker')
        self.assertTrue(code and '0 ELEK_REGISTER_MACHINE' in out, out)


class NoCompilerTests(unittest.TestCase):
    def test_refuses_without_a_host_compiler(self):
        with unittest.mock.patch.dict(os.environ, CC='/nonexistent/cc'), \
                self.assertRaisesRegex(sdk_alpha_host.Refused, 'no host C compiler'):
            sdk_alpha_host.compile_mod(ROOT / 'examples/sdk-sine', 'unused')


if __name__ == '__main__':
    unittest.main()
