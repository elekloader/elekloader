# SPDX-License-Identifier: GPL-2.0-or-later
"""The SDK's sound contract (sdk_alpha_machine.h), with no device: test_sdk_alpha_render.c built with the host
compiler as the ColdFire's Q31 and as the SHARC's float. Each build checks the helpers itself; here the two
render the same sine, which must be one waveform, continuous across blocks, at full level."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which('cc')


@unittest.skipUnless(CC, 'no host C compiler')
class RenderContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.programs = {}
        for target, flags in (('q31', []), ('float', ['-DELEK_TARGET_SHARC'])):
            program = Path(cls.tmp.name) / target
            done = subprocess.run([CC, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', *flags,
                                   '-I', str(ROOT / 'elekloader/sdk/include'),
                                   str(ROOT / 'tests/test_sdk_alpha_render.c'), '-o', str(program), '-lm'],
                                  capture_output=True, text=True)
            if done.returncode:
                raise AssertionError('%s build failed:\n%s' % (target, done.stderr))
            cls.programs[target] = program

    def test_digitakt_adapter_keeps_state_and_publishes_complete_shared_blocks(self):
        program = Path(self.tmp.name) / 'dt1-adapter'
        done = subprocess.run([CC, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                               '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast',
                               '-I', str(ROOT / 'elekloader/sdk/include'),
                               str(ROOT / 'tests/test_sdk_alpha_dt1.c'), '-o', str(program)],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        done = subprocess.run([str(program)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)

    def render(self, target, level=127, fine=0):
        """The samples (-1.0..1.0) and the phase after them; the program's own checks must pass first."""
        done = subprocess.run([str(self.programs[target]), str(level), str(fine)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, '%s: a helper check failed\n%s' % (target, done.stderr))
        lines = done.stdout.split()
        return [float(x) for x in lines[:-2]], int(lines[-1])

    def test_both_targets_render_the_same_sine(self):
        q31, q31_phase = self.render('q31')
        flt, flt_phase = self.render('float')
        self.assertEqual(len(q31), 64 * 32)
        self.assertEqual(q31_phase, flt_phase)                  # the same integer phase on both
        self.assertLess(max(abs(a - b) for a, b in zip(q31, flt)), 1e-6)

    def test_the_sine_starts_at_zero_reaches_full_level_and_does_not_jump(self):
        for target in ('q31', 'float'):
            with self.subTest(target=target):
                samples, _ = self.render(target)
                self.assertEqual(samples[0], 0.0)               # ELEK_VOICE_START reset the phase
                self.assertGreater(max(samples), 0.99)
                self.assertLess(min(samples), -0.99)
                # 440 Hz moves at most 2 pi 440 / 48000 a sample, plus one 256-entry table step.
                step = max(abs(b - a) for a, b in zip(samples, samples[1:]))
                self.assertLess(step, 0.0576 + 0.0246, 'a jump, at a block boundary or within one')

    def test_level_and_fine_reach_the_render(self):
        for target in ('q31', 'float'):
            with self.subTest(target=target):
                half, _ = self.render(target, level=64)
                self.assertAlmostEqual(max(half), 64 / 127, delta=0.01)
                _, base = self.render(target)
                _, sharp = self.render(target, fine=50)
                self.assertNotEqual(base, sharp)                # FINE moved the pitch


if __name__ == '__main__':
    unittest.main()
