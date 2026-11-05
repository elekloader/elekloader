# SPDX-License-Identifier: GPL-2.0-or-later
"""python -m elekloader.sdk.sdk_alpha_host, the authors' host run, on the SDK examples."""
from pathlib import Path
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
                self.assertIn('FINE=50    FINE "FINE" glyph', out)
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
        self.assertIn('AMNT=127   PITCH "A 127" hidden  AMNT "AMNT"', out)
        self.assertIn('no render', out)

    def test_refusals(self):
        code, out = host('examples/sdk-sine', '--knob', 'FINE=99')
        self.assertTrue(code and 'FINE takes -50..50' in out, out)
        code, out = host('examples/hello-marker')
        self.assertTrue(code and '0 ELEK_REGISTER_MACHINE' in out, out)


class NoCompilerTests(unittest.TestCase):
    def test_refuses_without_a_host_compiler(self):
        with unittest.mock.patch.dict(os.environ, CC='/nonexistent/cc'), \
                self.assertRaisesRegex(sdk_alpha_host.Refused, 'no host C compiler'):
            sdk_alpha_host.compile_mod(ROOT / 'examples/sdk-sine', 'unused')


if __name__ == '__main__':
    unittest.main()
