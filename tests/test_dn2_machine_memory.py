# SPDX-License-Identifier: GPL-2.0-or-later
"""The DN2 machine areas are unused at run time, in DigiKit's coupled emulator.

Static analysis says stock never references L2 [0x2008823c, 0x200f9fff] or L1 above core's
bridge. This boots core and machines-dn2 with no machine, so the stock voice pipeline runs, fills
both 64 KiB machine areas and both machines' 4 KiB L1 stacks with an address-derived pattern,
plays every track hard (chords on all 16 channels, filter and parameter sweeps, several
seconds), and checks that no word of the pattern changed.
"""
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')
DIGIKIT = os.environ.get('ELEKLOADER_DIGIKIT')
EMUD = os.environ.get('ELEKLOADER_DIGI_EMUD')
READY = os.environ.get('ELEKLOADER_DN2_EMU_READY')
MS = 132000  # emulated instructions per millisecond


def pattern(lo, hi):
    return b''.join(((a * 2654435761) & 0xffffffff).to_bytes(4, 'little') for a in range(lo, hi, 4))


@unittest.skipUnless(STOCK and DIGIKIT and EMUD and READY and os.environ.get('ELEKLOADER_CROSS'),
                     'needs DN2 stock, the cross compiler, DigiKit, its digi-emud and ready inputs')
class MachineMemoryTests(unittest.TestCase):
    def test_the_machine_areas_stay_untouched_under_load(self):
        from elekloader import patch
        from elekloader.devices.digitone_mk2 import DEVICE
        from elekloader.sdk import build as sdk
        profile = DEVICE.releases['1.12'].sharc
        areas = [(profile['voice_range'][0], profile['filter_range'][1]),
                 (profile['voice_stack'][0], profile['filter_stack'][1])]
        self.assertEqual(profile['voice_range'][1], profile['filter_range'][0])
        self.assertEqual(profile['voice_stack'][1], profile['filter_stack'][0])
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            mods = [sdk.build(str(ROOT / 'mods' / src), STOCK, str(work / src))[0] for src in ('core-dn2', 'machines-dn2')]
            outputs, _manifest = patch.build(STOCK, mods, 'TEST', log=lambda *args: None)
            firmware = work / 'memory.syx'
            firmware.write_bytes(outputs['syx'])
            sys.path.insert(0, DIGIKIT)
            from tools.control.digi_client import Emu
            emu = Emu.spawn_coupled(STOCK, READY, binary=EMUD, timeout=300, run_dir=work / 'run', env={'SHARC_AOT': '0'})
            try:
                def wait(ms):
                    left = int(ms * MS)
                    while left:
                        step = min(left, 500 * MS)
                        emu.step(step, timeout_wall_s=300)
                        left -= step
                emu.call('firmware.load', syx=str(firmware), start='cold', force=True, auto_no=True)
                wait(300)
                fill = work / 'fill.patch'
                fill.write_text(''.join('BYTES %x %s\n' % (at, pattern(at, min(at + 2048, hi)).hex())
                                        for lo, hi in areas for at in range(lo, hi, 2048)))
                emu.call('dsp.patch', file=str(fill))
                for lo, _hi in areas:
                    self.assertEqual(bytes.fromhex(emu.call('dsp.peek', addr=lo, len=64)['hex']), pattern(lo, lo + 64))
                emu.call('pcm.tap', action='start')
                # Every channel: a chord, then FREQ, RESO and the first synth knobs swept while it sounds.
                for rounds in range(4):
                    notes = bytes(b for ch in range(16) for b in (0x90 | ch, 36 + 7 * (ch % 6) + rounds, 100))
                    emu.call('midi.in', bytes=notes.hex(), period=0)
                    for value in range(0, 128, 16):
                        sweep = bytes(b for ch in range(16) for cc in (16, 17, 40, 41, 42)
                                      for b in (0xb0 | ch, cc, (value + 13 * ch) & 127))
                        emu.call('midi.in', bytes=sweep.hex(), period=0)
                        wait(120)
                    emu.call('midi.in', bytes=bytes(b for ch in range(16) for b in (0xb0 | ch, 123, 0)).hex(), period=0)
                    wait(200)
                heard = emu.call('pcm.tap', action='report')
                self.assertGreater(heard['peak'], 0.05, 'the load should be audible')
                for lo, hi in areas:
                    got = b''.join(bytes.fromhex(emu.call('dsp.peek', addr=at, len=min(0x10000, hi - at))['hex'])
                                   for at in range(lo, hi, 0x10000))   # a peek reads at most 64 KiB
                    expected = pattern(lo, hi)
                    changed = [lo + at for at in range(0, hi - lo, 4) if got[at:at + 4] != expected[at:at + 4]]
                    self.assertEqual(changed[:8], [], '%d words changed in %#x..%#x' % (len(changed), lo, hi))
                status = emu.status()
                self.assertIsNone(status['cf'].get('error'))
                self.assertIsNone(status['audio']['halted'])
                self.assertEqual(status['audio']['missing_blocks'], 0)
            finally:
                emu.call('quit')
                emu.close()


if __name__ == '__main__':
    unittest.main()
