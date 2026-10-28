# SPDX-License-Identifier: GPL-2.0-or-later
"""core-dn2's DSP peek in DigiKit's coupled emulator (mods/core-dn2/peek.c and peek.s).

A build with a SHARC filter carries the peek's DSP side. The test starts reads as
core_dsp_peek does, by writing the core's request variables, and checks that the answers
arriving through reply word 2 are the DSP's memory: the filter stack's guard word, the
filter's data, and a refusal for a peripheral address. It also checks that the record of how
busy the DSP is (CORE_DSP_BUSY_RECORD) stays zero without dspbusy-dn2 and publishes
whole windows with it.
"""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')
DIGIKIT = os.environ.get('ELEKLOADER_DIGIKIT')
EMUD = os.environ.get('ELEKLOADER_DIGI_EMUD')
READY = os.environ.get('ELEKLOADER_DN2_EMU_READY')
MS = 132000  # emulated instructions per millisecond
RETURN = 'fe4d3f0e3f083f3401190100'  # I12=DM(M7,I6); JUMP(M14,I12)(DB); RFRAME; NOP;
NAMES = ('request', 'words', 'got', 'received', 'total', 'refused', 'seq')


@unittest.skipUnless(shutil.which('cc'), 'needs a native C compiler')
class ColdFirePeekTests(unittest.TestCase):
    def test_idle_replies_cannot_complete_a_read_across_sequence_wrap(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable = str(Path(tmp) / 'peek')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Werror',
                            '-I', str(ROOT / 'elekloader/sdk/include'),
                            str(ROOT / 'tests/test_dn2_dsp_peek.c'), '-o', executable], check=True)
            subprocess.run([executable], check=True)


@unittest.skipUnless(STOCK and DIGIKIT and EMUD and READY and os.environ.get('ELEKLOADER_CROSS'),
                     'needs DN2 stock, the cross compiler, DigiKit, its digi-emud and ready inputs')
class DspPeekTests(unittest.TestCase):
    def test_reads_dsp_memory_and_refuses_peripherals(self):
        self.check_build(False)

    def test_busy_companion_publishes_windows(self):
        self.check_build(True)

    def check_build(self, busy):
        from elekloader import patch
        from elekloader.devices.digitone_mk2 import DEVICE
        from elekloader.sdk import build as sdk
        from elekloader.sdk.sharc import filter_artifact, machine_layout
        profile = DEVICE.releases['1.12'].sharc
        pattern = bytes(range(256)) * 4
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            mods = [sdk.build(str(ROOT / 'mods' / src), STOCK, str(work / src))[0] for src in ('core-dn2', 'machines-dn2')]
            if busy:
                mods.append(sdk.build(str(ROOT / 'mods/dspbusy-dn2'), STOCK, str(work / 'busy'))[0])
            stage = work / 'custom'
            stage.mkdir()
            (stage / 'machine.c').write_text('#include "mk2/machines.h"\n'
                                             'const struct mk2_filter custom = {8,"PEEK","PEEK",0,0};\n')
            (stage / 'kernel.json').write_text(json.dumps(filter_artifact(bytes.fromhex(RETURN), pattern, 0, 8, 0)))
            (stage / 'mod.json').write_text(json.dumps({
                'id': 'peek-test', 'version': '0.1', 'device': 'digitone-mk2', 'os': '1.12',
                'sources': ['machine.c'], 'sharc': {'artifact': 'kernel.json'}, 'resources': {'core': '0.1'},
                'requires': ['core', 'machines-dn2'],
                'contribute': [{'to': 'mk2_voice_filters', 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:custom', 0]]}]}))
            mods.append(sdk.build(str(stage), STOCK, str(stage / 'build'))[0])
            outputs, manifest = patch.build(STOCK, mods, 'TEST', log=lambda *args: None)
            at = {n: manifest['_map']['core:' + n] for n in NAMES}
            at = {n: int(v, 0) if isinstance(v, str) else v for n, v in at.items()}
            firmware = work / 'peek.syx'
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

                def peek(address, count):
                    """Start a read as core_dsp_peek does, wait for it, -> (refused, words)."""
                    seq = (emu.peek32(at['seq']) + 1) & 0xffff
                    if not seq & 15:
                        seq = (seq + 1) & 0xffff
                    emu.poke32(at['request'], 0)
                    for i in range(0, 256, 4):
                        emu.poke32(at['got'] + i, 0)
                    for name, value in (('received', 0), ('refused', 0), ('total', 2 * count), ('seq', seq)):
                        emu.poke32(at[name], value)
                    emu.poke32(at['request'] + 4, address)
                    emu.poke32(at['request'] + 8, count)
                    emu.poke32(at['request'] + 12, (0xd5ee | seq << 16) ^ address ^ count ^ 0x5a5a5a5a)
                    emu.poke32(at['request'], 0xd5ee | seq << 16)
                    wait(2 * count + 100)
                    pairs = [emu.peek32(at['words'] + 4 * i) for i in range(count)]
                    return emu.peek32(at['refused']), [(w & 0xffff) << 16 | w >> 16 for w in pairs]

                emu.call('firmware.load', syx=str(firmware), start='cold', force=True, auto_no=True)
                wait(300)
                guard = profile['filter_stack'][0] - 0x28000000          # the core's view of L1
                self.assertEqual(peek(guard, 1), (0, [profile['stack_guard']]))
                data = machine_layout('filter')['data_address']
                self.assertEqual(peek(data, 8), (0, [int.from_bytes(pattern[4 * i:4 * i + 4], 'little') for i in range(8)]))
                self.assertEqual(peek(0x31000000, 1)[0], 1)              # peripherals are refused
                self.assertEqual(peek(0x200f9ffc, 1)[0], 0)             # the last readable L2 word
                self.assertEqual(peek(0x200f9ffc, 2)[0], 1)             # crossing its end is refused
                self.assertEqual(peek(0xfffffffc, 1)[0], 1)             # as is a uint32-wrapping end
                self.assertEqual(peek(guard, 1), (0, [profile['stack_guard']]))   # and the next read is clean
                wait(1500)                                               # two 1024-block windows
                seq, idle, total, peak_idle, peak_total, seq2 = struct.unpack(
                    '<6I', bytes.fromhex(emu.call('dsp.peek', addr=0x282e0630, len=24)['hex']))
                self.assertEqual(seq, seq2)
                if busy:
                    self.assertGreaterEqual(seq, 1)
                    self.assertTrue(0 < idle < total and 0 < peak_idle <= peak_total)
                else:
                    self.assertEqual((seq, idle, total, peak_idle, peak_total, seq2), (0,) * 6)
                status = emu.status()
                self.assertIsNone(status['cf'].get('error'))
                self.assertIsNone(status['audio']['halted'])
                self.assertEqual(status['audio']['missing_blocks'], 0)
            finally:
                emu.call('quit')
                emu.close()


if __name__ == '__main__':
    unittest.main()
