# SPDX-License-Identifier: GPL-2.0-or-later
"""ColdFire ABI probes for core-dn1's LFO name hooks; no stock firmware needed.

Run with digiemu's Python and ELEKLOADER_CROSS=m68k-elf-.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.elf import Elf  # noqa: E402

FIXTURE = '''
.section .run,"ax"
.globl core_param_override
core_param_override: .long empty, decline, answer, last, 0
empty: .long 0, 0
decline: .long 0, refuse
answer: .long 0, names
last: .long 0, wrong
refuse: moveq #0,%d0
        rts
wrong:  moveq #1,%d0
        move.l %d0,failed
        moveq #0,%d0
        rts
names:  move.l 4(%sp),%d1
        move.l %d1,seen_id
        move.l 8(%sp),%d1
        move.l %d1,seen_field
        subi.l #0x28,%d1
        lea replies,%a0
        move.l (%a0,%d1.l),%d0
        move.l #0xbad1,%d1
        movea.l #0xbada,%a0
        movea.l #0xbadb,%a1
        rts
.balign 4
replies: .long 0x10a100,0x10a200,0x10a300
seen_id: .long 0
seen_field: .long 0
failed: .long 0
'''


class DigitoneLfoNames(unittest.TestCase):
    def test_name_overrides_and_stock_fallback_preserve_native_callers(self):
        try:
            from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
            from unicorn import m68k_const as r
        except ImportError:
            self.skipTest("needs digiemu's Python (unicorn)")
        prefix = os.environ.get('ELEKLOADER_CROSS', 'm68k-elf-')
        if not all(shutil.which(prefix + tool) for tool in ('as', 'ld', 'objcopy')):
            self.skipTest('needs ELEKLOADER_CROSS (m68k cross assembler and linker)')
        with tempfile.TemporaryDirectory(prefix='dn-name-hooks-') as folder:
            p = Path(folder)
            (p / 'fixture.s').write_text(FIXTURE)
            (p / 'link.ld').write_text('SECTIONS { . = 0x100000; .text : { *(.run) } }')
            for source, out in ((ROOT / 'mods/core-dn1/names.s', 'names.o'),
                                (p / 'fixture.s', 'fixture.o')):
                subprocess.run([prefix + 'as', '-mcpu=54455', str(source), '-o', str(p / out)], check=True)
            subprocess.run([prefix + 'ld', '-T', str(p / 'link.ld'), str(p / 'names.o'),
                            str(p / 'fixture.o'), '-o', str(p / 'probe.elf')], check=True)
            subprocess.run([prefix + 'objcopy', '-O', 'binary', str(p / 'probe.elf'),
                            str(p / 'probe.bin')], check=True)
            symbols = {s.name: s.value for s in Elf.load(p / 'probe.elf').symbols}
            code = (p / 'probe.bin').read_bytes()
            regs = [getattr(r, 'UC_M68K_REG_' + name)
                    for name in (['D%d' % i for i in range(8)] + ['A%d' % i for i in range(7)])]
            for override in (False, True):
                for fn, nargs in (('list', 2), ('compact', 2), ('group', 2), ('name', 1)):
                    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
                    uc.ctl_set_cpu_model(r.UC_CPU_M68K_CFV4E)
                    uc.mem_map(0x100000, 0x10000)
                    uc.mem_write(0x100000, code)

                    def put(at, value):
                        uc.mem_write(at, struct.pack('>I', value))

                    def get(at):
                        return struct.unpack('>I', bytes(uc.mem_read(at, 4)))[0]

                    if not override:
                        put(symbols['core_param_override'], 0)
                    params = 0x103000
                    slot = params + 60 * 119
                    for field in (0x28, 0x2c, 0x30):
                        put(slot + field, 0x10a000 + field)
                    for i, reg in enumerate(regs):
                        uc.reg_write(reg, 0x100 + i)
                    inputs = {'list': {'D1': 60 * 119, 'D6': 0x10a02c, 'A3': params},
                              'compact': {'D2': 60 * 119, 'D6': 0x10a02c, 'A3': params},
                              'group': {'D0': 60 * 119, 'A0': params, 'D3': 0x10a400},
                              'name': {'D0': 4 * 119, 'D2': 64 * 119, 'A1': params}}[fn]
                    for name, value in inputs.items():
                        uc.reg_write(getattr(r, 'UC_M68K_REG_' + name), value)
                    before = [uc.reg_read(reg) for reg in regs]
                    if fn == 'name':
                        before[2] = 60 * 119    # the displaced sub.l changes D2
                    uc.reg_write(r.UC_M68K_REG_SR, 0x2700)
                    stack, stop = 0x10f000, 0x10ff00
                    put(stack - 4, stop)
                    uc.reg_write(r.UC_M68K_REG_A7, stack - 4)
                    uc.emu_start(symbols['core_lfo_' + fn], stop, count=20000)
                    self.assertEqual(uc.reg_read(r.UC_M68K_REG_PC), stop, fn)
                    self.assertEqual([uc.reg_read(reg) for reg in regs], before, (fn, 'registers'))
                    self.assertEqual(uc.reg_read(r.UC_M68K_REG_A7), stack - 4 * nargs, (fn, 'stack'))
                    actual = [get(stack - 4 * nargs + 4 * i) for i in range(nargs)]
                    long, group, short = ((0x10a100, 0x10a200, 0x10a300) if override else
                                          (0x10a028, 0x10a02c, 0x10a030))
                    # The compact row names a destination (0x28): a label-only override keeps its stock text.
                    expected = {'list': [group, long], 'compact': [group, long if override else short],
                                'group': [0x10a400, group], 'name': [long if override else short]}[fn]
                    self.assertEqual(actual, expected, (fn, override))
                    self.assertEqual(get(symbols['failed']), 0, 'a later override ran after the first answer')
                    if override:
                        self.assertEqual(get(symbols['seen_id']), 119, fn)


if __name__ == '__main__':
    unittest.main()
