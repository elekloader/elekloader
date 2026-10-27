# SPDX-License-Identifier: GPL-2.0-or-later
"""The DN2 filter capture in DigiKit's coupled emulator: a custom filter gets stock LOWPASS 4's cutoff,
and its G.

Track 1 plays stock LOWPASS 4 and track 2 a custom filter registered on it, with the same
controls and the same note in the same block. LOWPASS 4's setup-table entry is pointed at the
adapter's capture entry too, so both voices' stock cutoffs land in the capture arrays in the
same preparation pass; they must be equal in every block while the envelope moves them. The
custom filter declares G, which CC 18 sets; the kernel's input carries it.
"""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')
DIGIKIT = os.environ.get('ELEKLOADER_DIGIKIT')
EMUD = os.environ.get('ELEKLOADER_DIGI_EMUD')
READY = os.environ.get('ELEKLOADER_DN2_EMU_READY')
MS = 132000  # emulated instructions per millisecond
CUTOFF, RESONANCE, G, INPUT = 0x20097d40, 0x20097d80, 0x20097dc0, 0x20097d00
# DigiKit's DN2 panel codes (tools/patchkit/pk_harness.py).
SYN, YES, UP, NO, DOWN, RIGHT, TRK, FUNC, STOP = 2, 10, 11, 12, 14, 15, 16, 17, 21
FLTR_LIST = [0, 1, 4, 3, 5, 2, 8]  # MACHINE SEL's FLTR side, in screen order
RETURN = 'fe4d3f0e3f083f3401190100'  # I12=DM(M7,I6); JUMP(M14,I12)(DB); RFRAME; NOP;


@unittest.skipUnless(STOCK and DIGIKIT and EMUD and READY and os.environ.get('ELEKLOADER_CROSS'),
                     'needs DN2 stock, the cross compiler, DigiKit, its digi-emud and ready inputs')
class CaptureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from elekloader import patch
        from elekloader.devices.digitone_mk2 import DEVICE
        from elekloader.sdk import build as sdk
        from elekloader.sdk.sharc import filter_artifact
        cls.profile = DEVICE.releases['1.12'].sharc
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        work = Path(cls.tmp.name)
        mods = [sdk.build(str(ROOT / 'mods' / src), STOCK, str(work / src))[0] for src in ('core-dn2', 'machines-dn2')]
        stage = work / 'custom'
        stage.mkdir()
        (stage / 'machine.c').write_text('#include "mk2/machines.h"\n'
                                         'static const struct mk2_param g = {"MODE", 0};\n'
                                         'const struct mk2_filter custom = {8,"CUSTOM","CUSTOM",0,&g};\n')
        (stage / 'kernel.json').write_text(json.dumps(filter_artifact(bytes.fromhex(RETURN), b'', 0, 8, 0)))
        (stage / 'mod.json').write_text(json.dumps({
            'id': 'capture-test', 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
            'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'}, 'resources': {'core': '0.1'},
            'requires': ['core', 'machines-dn2'],
            'contribute': [{'to': 'mk2_voice_filters', 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:custom', 0]]}]}))
        mods.append(sdk.build(str(stage), STOCK, str(stage / 'build'))[0])
        outputs, _manifest = patch.build(STOCK, mods, 'TEST', log=lambda *args: None)
        cls.firmware = work / 'capture.syx'
        cls.firmware.write_bytes(outputs['syx'])
        sys.path.insert(0, DIGIKIT)
        from tools.control.digi_client import Emu
        cls.emu = Emu.spawn_coupled(STOCK, READY, binary=EMUD, timeout=300, run_dir=work / 'run', env={'SHARC_AOT': '0'})
        cls.addClassCleanup(cls.emu.close)
        cls.addClassCleanup(cls.emu.call, 'quit')
        cls.emu.call('firmware.load', syx=str(cls.firmware), start='cold', force=True, auto_no=True)
        cls.wait(300)

    @classmethod
    def wait(cls, ms):
        left = int(ms * MS)
        while left:
            step = min(left, 500 * MS)
            cls.emu.step(step, timeout_wall_s=300)
            left -= step

    def tap(self, *codes):
        for code in codes:
            self.emu.tap(code, hold=20 * MS)
            self.wait(80)

    def chord(self, held, code):
        self.emu.button(held, True)
        self.wait(50)
        self.tap(code)
        self.emu.button(held, False)
        self.wait(100)

    def select_filter(self, track, machine):
        self.tap(STOP, STOP, NO, NO)
        self.chord(TRK, 24 + track)
        self.tap(NO, NO)
        self.chord(FUNC, SYN)
        self.tap(RIGHT, *[UP] * (len(FLTR_LIST) + 1), *[DOWN] * FLTR_LIST.index(machine), YES, NO, NO)

    def words(self, address, count, fmt='I'):
        return struct.unpack('<%d%s' % (count, fmt),
                             bytes.fromhex(self.emu.call('dsp.peek', addr=address, len=4 * count)['hex']))

    def patch_entry(self, entry, value, was):
        site = self.profile['filter_capture'][0] + 4 * entry
        self.emu.call('dsp.patch', text='DM 0x%x %08x was %08x' % (site, value, was))

    def test_custom_filter_gets_stock_lowpass_4s_cutoff_in_every_block(self):
        self.select_filter(1, 1)  # stock LOWPASS 4
        self.select_filter(2, 8)  # the custom filter
        capture = int.from_bytes(bytes.fromhex(self.profile['filter_capture'][2]), 'little')
        lowpass_4_arm, = self.words(self.profile['filter_capture'][0] + 12, 1)
        self.patch_entry(3, capture, lowpass_4_arm)  # LOWPASS 4 is internal type 3
        # FREQ, RESO, ENV, ATK, DEC, SUS and pre-delay on both tracks' channels, and LFO 1 on FREQ
        # (destination slot 67, a triangle restarted on each note, at about 4 Hz).
        controls = ((16, 40), (17, 50), (24, 127), (20, 20), (21, 64), (22, 0), (19, 10), (18, 100),
                    (105, 67), (106, 0), (108, 1), (102, 127), (103, 4), (109, 100))
        burst = bytes(b for channel in (0, 1) for cc, value in controls for b in (0xb0 | channel, cc, value))
        self.emu.call('midi.in', bytes=burst.hex(), period=0)
        self.wait(200)
        self.emu.call('midi.in', bytes='903c64913c64', period=0)  # note 60 on both, in one block
        self.wait(5)
        types = [self.words(0x28255694 + 0x234 * voice, 1)[0] for voice in (0, 1)]
        self.assertEqual(sorted(types), [0, 3], 'voices 0 and 1 should play LOWPASS 4 and the custom filter')

        def pair():
            cutoff, resonance = self.words(CUTOFF, 2, 'f'), self.words(RESONANCE, 2, 'f')
            if cutoff[0] != cutoff[1]:  # paused between the two voices' preparation
                self.emu.step(20000)
                cutoff, resonance = self.words(CUTOFF, 2, 'f'), self.words(RESONANCE, 2, 'f')
            return cutoff, resonance

        seen = set()
        for _ in range(300):  # one 32-sample block each
            cutoff, resonance = pair()
            self.assertEqual(cutoff[0], cutoff[1])
            self.assertEqual(resonance[0], resonance[1])
            seen.add(cutoff[0])
            self.wait(32 / 48)
        self.assertGreater(len(seen), 100, 'the envelope should move the cutoff in most blocks')
        self.assertAlmostEqual(resonance[0], 50 / 128, places=6)
        # The kernel's input carries the custom voice's values (its slot word is shared by every voice):
        # G is CC 18's 100, as G/127.
        custom = types.index(0)
        given = self.words(INPUT + 12, 3, 'f')
        self.assertEqual(given, tuple(self.words(array + 4 * custom, 1, 'f')[0] for array in (CUTOFF, RESONANCE, G)))
        self.assertAlmostEqual(given[2], 100 / 127, places=6)
        status = self.emu.status()
        self.assertIsNone(status['cf'].get('error'))
        self.assertIsNone(status['audio']['halted'])
        self.assertEqual(status['audio']['missing_blocks'], 0)

        # Without the capture patch the custom voice's values stop, and the comparison sees it.
        stock_arm = int.from_bytes(bytes.fromhex(self.profile['filter_capture'][1]), 'little')
        self.patch_entry(0, stock_arm, capture)
        self.wait(20)
        differ = 0
        for _ in range(20):
            cutoff = self.words(CUTOFF, 2, 'f')
            differ += cutoff[0] != cutoff[1]
            self.wait(32 / 48)
        self.assertGreater(differ, 10)
        self.emu.call('midi.in', bytes='803c00813c00', period=0)


if __name__ == '__main__':
    unittest.main()
