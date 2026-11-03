# SPDX-License-Identifier: GPL-2.0-or-later
"""A DN2 machine's own stack, in DigiKit's coupled emulator.

A custom filter whose kernel fills N words of stack and counts its calls in its data:

    R0=DM(0x200a8000); R0=R0+1; DM(0x200a8000)=R0;
    I7=MODIFY(I7,-N)(NW); I4=I7; M4=1; R1=N;
    fill: DM(I4,M4)=R0; R1=R1-1; IF NE JUMP fill;
    I12=DM(M7,I6); JUMP(M14,I12)(DB); RFRAME; NOP;

With N = 256 (1 KB, more than the stock stack ever allowed) it runs on every block and the guard
word at the base of its L1 stack stays. With N = 1100 (4.4 KB, past the 4 KiB stack) the first
call breaks the guard: the adapter mutes the machine instead of calling it again, and the unit
keeps running.
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
SYN, YES, UP, NO, DOWN, RIGHT, TRK, FUNC, STOP = 2, 10, 11, 12, 14, 15, 16, 17, 21
FLTR_LIST = [0, 1, 4, 3, 5, 2, 8]  # MACHINE SEL's FLTR side, in screen order


def kernel(words):
    """The kernel above, assembled by Selache for the filter's code PC16 0xbd0400."""
    return bytes.fromhex('00100a20008000c500110a2000808715ffff' + struct.pack('<H', -words & 0xffff).hex()
                         + '7f713f8aa40f0100810f' + struct.pack('<H', words).hex()
                         + '309911c62006bd001004fe4d3f0e3f083f3401190100')


@unittest.skipUnless(STOCK and DIGIKIT and EMUD and READY and os.environ.get('ELEKLOADER_CROSS'),
                     'needs DN2 stock, the cross compiler, DigiKit, its digi-emud and ready inputs')
class MachineStackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from elekloader import patch
        from elekloader.devices.digitone_mk2 import DEVICE
        from elekloader.sdk import build as sdk
        from elekloader.sdk.sharc import filter_artifact, machine_layout
        cls.profile = profile = DEVICE.releases['1.12'].sharc
        cls.calls = machine_layout('filter')['data_address']
        cls.guard = profile['filter_stack'][0]
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        work = Path(cls.tmp.name)
        base = [sdk.build(str(ROOT / 'mods' / src), STOCK, str(work / src))[0] for src in ('core-dn2', 'machines-dn2')]
        cls.firmware = {}
        for words in (256, 1100):
            stage = work / ('stack-%d' % words)
            stage.mkdir()
            (stage / 'machine.c').write_text('#include "mk2/machines.h"\n'
                                             'const struct mk2_filter custom = {8,"STACK","STACK",0,0};\n')
            (stage / 'kernel.json').write_text(json.dumps(filter_artifact(kernel(words), bytes(4), 0, 8, 2048)))
            (stage / 'mod.json').write_text(json.dumps({
                'id': 'stack-test', 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'}, 'resources': {'core': '0.1'},
                'requires': ['core', 'machines-dn2'],
                'contribute': [{'to': 'mk2_voice_filters', 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:custom', 0]]}]}))
            mod = sdk.build(str(stage), STOCK, str(stage / 'build'))[0]
            outputs, _manifest = patch.build(STOCK, base + [mod], 'TEST', log=lambda *args: None)
            cls.firmware[words] = work / ('stack-%d.syx' % words)
            cls.firmware[words].write_bytes(outputs['syx'])
        sys.path.insert(0, DIGIKIT)
        from tools.control.digi_client import Emu
        cls.emu = Emu.spawn_coupled(STOCK, READY, binary=EMUD, timeout=300, run_dir=work / 'run', env={'SHARC_AOT': '0'})
        cls.addClassCleanup(cls.emu.close)
        cls.addClassCleanup(cls.emu.call, 'quit')

    def wait(self, ms):
        left = int(ms * MS)
        while left:
            step = min(left, 500 * MS)
            self.emu.step(step, timeout_wall_s=300)
            left -= step

    def tap(self, *codes):
        for code in codes:
            self.emu.tap(code, hold=20 * MS)
            self.wait(80)

    def word(self, address):
        return struct.unpack('<I', bytes.fromhex(self.emu.call('dsp.peek', addr=address, len=4)['hex']))[0]

    def play(self, words):
        """Boot the build with the N-word kernel, select it on track 1 and hold a note."""
        self.emu.call('firmware.load', syx=str(self.firmware[words]), start='cold', force=True, auto_no=True)
        self.wait(300)
        self.tap(STOP, STOP, NO, NO)
        self.emu.button(TRK, True)
        self.wait(50)
        self.tap(25)
        self.emu.button(TRK, False)
        self.wait(100)
        self.tap(NO, NO)
        self.emu.button(FUNC, True)
        self.wait(50)
        self.tap(SYN)
        self.emu.button(FUNC, False)
        self.wait(100)
        self.tap(RIGHT, *[UP] * (len(FLTR_LIST) + 1), *[DOWN] * FLTR_LIST.index(8), YES, NO, NO)
        self.emu.call('midi.in', bytes='903c64', period=0)
        self.wait(300)

    def assert_running(self):
        status = self.emu.status()
        self.assertIsNone(status['cf'].get('error'))
        self.assertIsNone(status['audio']['halted'])
        self.assertEqual(status['audio']['missing_blocks'], 0)

    def test_a_1_kb_stack_runs(self):
        self.play(256)
        first = self.word(self.calls)
        self.wait(200)
        self.assertGreater(self.word(self.calls) - first, 100, 'the kernel should run on every block')
        self.assertEqual(self.word(self.guard), self.profile['stack_guard'])
        self.emu.call('midi.in', bytes='803c00', period=0)
        self.assert_running()

    def test_an_overrun_mutes_the_machine_and_the_unit_keeps_running(self):
        self.play(1100)
        self.assertEqual(self.word(self.calls), 1, 'the first call overruns; no other is made')
        self.assertNotEqual(self.word(self.guard), self.profile['stack_guard'])
        self.emu.call('pcm.tap', action='start')
        self.wait(300)
        self.assertEqual(self.word(self.calls), 1)
        self.assertLess(self.emu.call('pcm.tap', action='report')['peak'], 1e-4, 'the muted voice is silent')
        self.emu.call('midi.in', bytes='803c00', period=0)
        self.assert_running()


if __name__ == '__main__':
    unittest.main()
