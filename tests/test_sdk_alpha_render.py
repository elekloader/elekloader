# SPDX-License-Identifier: GPL-2.0-or-later
"""The SDK's sound contract (sdk_alpha_machine.h), with no device: test_sdk_alpha_render.c built with the host
compiler as the ColdFire's Q31 and as the SHARC's float. Each build checks the helpers itself; here the two
render examples/sdk-sine, which must be one waveform, at the note, starting at zero, at half level."""
from pathlib import Path
import json
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

    def test_digitakt_adapter_keeps_state_and_maps_effective_values(self):
        self.adapter('dt1', 'machine-pages')

    def test_digitone_adapter_maps_voices_defaults_and_post_controller_values(self):
        self.adapter('dn1', 'machines-dn1')

    def test_shared_part_maps_defaults_sanitises_and_skips_unchanged_inputs(self):
        self.adapter('machine', 'machines-dn2')

    def adapter(self, device, companion):
        """tests/test_sdk_alpha_<device>.c, which includes ../mods/sdk-alpha/machine.c and so needs the
        companion's dev.h on the include path, and the -D flags of the build that compiles its sdk_alpha.c
        (mod.json: the OS release's layouts)."""
        program = Path(self.tmp.name) / (device + '-adapter')
        mod = json.loads((ROOT / 'mods' / companion / 'mod.json').read_text())
        sdk = next((p for p in mod.get('ports', {}).values() if 'sdk_alpha.c' in p.get('sources', ())), mod)
        defines = [f for f in sdk.get('cflags', mod.get('cflags', [])) if f.startswith('-D')]
        done = subprocess.run([CC, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                               '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast',
                               '-I', str(ROOT / 'elekloader/sdk/include'), '-I', str(ROOT / 'mods' / companion),
                               *defines, str(ROOT / ('tests/test_sdk_alpha_' + device + '.c')), '-o', str(program)],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        done = subprocess.run([str(program)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)

    def render(self, target, fine=0, note=69):
        """The samples (-1.0..1.0) and the phase after them; the program's own checks must pass first."""
        done = subprocess.run([str(self.programs[target]), str(fine), str(note)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, '%s: a helper check failed\n%s' % (target, done.stderr))
        lines = done.stdout.split()
        return [float(x) for x in lines[:-2]], int(lines[-1])

    def test_both_targets_render_the_same_sine(self):
        q31, q31_phase = self.render('q31')
        flt, flt_phase = self.render('float')
        self.assertEqual(len(q31), 64 * 32)
        self.assertEqual(q31_phase, flt_phase)                  # the same integer phase on both
        self.assertLess(max(abs(a - b) for a, b in zip(q31, flt)), 1e-6)

    def test_the_sine_starts_at_zero_at_half_level_and_does_not_jump(self):
        for target in ('q31', 'float'):
            with self.subTest(target=target):
                samples, _ = self.render(target)
                self.assertEqual(samples[0], 0.0)               # the note's start reset the phase
                self.assertAlmostEqual(max(samples), 0.5, delta=0.001)
                self.assertAlmostEqual(min(samples), -0.5, delta=0.001)
                # 440 Hz at 0.5 moves at most pi 440 / 48000 a sample
                step = max(abs(b - a) for a, b in zip(samples, samples[1:]))
                self.assertLess(step, 0.0289, 'a jump, at a block boundary or within one')

    def test_pitch_follows_the_note_and_fine(self):
        for target in ('q31', 'float'):
            with self.subTest(target=target):
                for note, fine, hz in ((69, 0, 440.0), (81, 0, 880.0), (69, 50, 440.0 * 2 ** (0.5 / 12)),
                                       (69, -50, 440.0 * 2 ** (-0.5 / 12))):
                    samples, _ = self.render(target, fine, note)
                    self.assertAlmostEqual(frequency(samples), hz, delta=hz * 0.0005, msg=(note, fine))


def frequency(samples, rate=48000):
    """From the first to the last rising zero crossing, each placed between its two samples."""
    rises = [i + a / (a - b) for i, (a, b) in enumerate(zip(samples, samples[1:])) if a <= 0 < b]
    return rate * (len(rises) - 1) / (rises[-1] - rises[0])


if __name__ == '__main__':
    unittest.main()
