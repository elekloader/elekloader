# SPDX-License-Identifier: GPL-2.0-or-later
"""Analog Four MKII: the Analog Rytm MKII's platform (a ColdFire
MCF5441x-family CPU, the Digitakt mk1's); the main OS runs from DDR at
0x40000400. Measured on OS 1.56, by comparison with the Rytm 1.74
(elekloader's analog-rytm-mk2 profile and mods/core-ar2 0.3, the core
mods/core-a4mk2 starts from) in Ghidra and on the stock images, with rytm-emu's
A4 build where noted. Proof-of-concept profile: on a unit (20 Oct 2026),
a build of core-a4mk2 with hooks-check-a4mk2 booted and ran every event, and
(21 Oct 2026) one with store-check-a4mk2 kept its mod storage through saves,
loads and power cycles; the facts below are otherwise static and emulator
evidence.

- The container: ELE3, sysex id 0x0B, sections 5 (meta), 2 (bootstrap,
  with the STARTUP menu), 1 (FPGA bitstream), 3 (MAIN OS, packed) and 8
  (an ARM Cortex-M image, byte-identical to the Rytm's section 8). No
  updater section and no trailer: the writer reproduces the stock file
  byte for byte from its own main OS stream. Only section 3 changes.
- Same platform as the Rytm: the OS entry (word[0] = 0x40001130; the Rytm's
  0x40000870) is the Rytm's instruction for instruction apart from
  addresses and one constant (it ORs 0x06 into 0xFC044000, as the Digitakt
  mk1 does; the Rytm ORs 0x8E). It sets SP to 0x48000000, writes the same
  peripheral registers (0xFC04002D, 0xFC04002F, 0xEC09000E bit 11, CACR,
  ITT0, with the same values) and makes the same five setup calls in the
  same order; the third (0x40001180, the Rytm's 0x400008C0) calls
  0x40002064, which sets VBR to 0x40000000.
- The bootstrap's loader (0x80000590, the Rytm's address) is the Rytm's
  call for call: section 3 from flash at its container offset + 0x80000 to
  0x40200000, depacked to 0x40000400 by a byte-identical depacker
  (0x8000042C). Stock's in-place gap is 540,192 bytes; elekloader's repack
  of stock, 649,113.
- Flash limit: 0x300000, where the OS keeps its voice calibration record
  ("RBOW", 0xE56 bytes: read at 0x400F18E8, erased and written at
  0x400F190E). Its settings store is at 0x380000-0x400000 (26 pushes of
  0x380000 and 11 of 0x3C0000 around 0x40118C4E-0x40119360); unlike the
  Rytm, there is no store at 0x340000. Stock's container ends at 0x188390;
  elekloader's repack of the stock main OS at 0x1A2D10. The in-OS upgrade
  (0x4007DA3C) erases from 0x80000 only as far as the container it
  received reaches.
- No protected range: every call into the SPI NOR program and erase
  routines (0x401155A6; 0x40115622 and five others) is from the
  calibration record, the settings store, the factory console (0x4011988A:
  a received bootstrap at 0x40000, a received container from 0x80000), the
  in-OS upgrade, or the COKI copy (0x40106F9C, erasing 0x400000, 0x800000
  and 0xC00000 after copying them to the card); none writes bytes of the
  main OS image. The main OS holds no copy of the bootstrap: the only bytes
  they share are a 1,175-byte font-width table (section 2 +0x71E8, main OS
  0x40203E20).
- The OS clears 0x40253000-0x448B6D50 at start (0x400010FA, the Rytm's
  0x4000083A) and runs its stack down from 0x48000000.
- Mods' area: 0x47BE0000-0x47C00000, the Rytm's and the Digitakt mk1's.
  No operand in the code points into it or its uncached view (0x4FBE0000).
  What the OS addresses at run time is bounded: the heap is one buddy
  allocator (operator new 0x400653B8) over a fixed 16 MB arena,
  0x41872230-0x42872230, whose descriptor (0x40212338) is static data no
  code rewrites; the task stacks are static; both lie in the RAM cleared at
  start. The buffers outside it (effects, the 4 MB delay ring at uncached
  0x4D000000, console and DMA buffers) have fixed sizes and end at
  uncached 0x4D6472C0 (cached 0x456472C0). In rytm-emu, watches over the
  area and over 0x448B6D50-0x44C00000 and 0x456472C0-0x47BE0000 saw no
  access through a cold boot, a busy session, a project save and a load,
  a power-off save, SysEx sent and received (a whole-project dump of
  2.3 MB included) and a five-minute mixed session;
  the boot stack used only 0x47FFFFD0-0x47FFFFFF. On a unit (21 Oct 2026)
  ddr-canary-a4mk2 filled the area after the mods' data and both gaps with
  address ^ 0xC0DEF00D and read them back with no word changed, through a
  session of normal use (Overbridge included) and after a power cycle.
  So from core-a4mk2 0.6 the linker lays the mods out from 0x45650000 to
  0x47C00000 (core_ddr, 37.7 MB, payloads included): that gap from its
  first 64 KB boundary, with the 128 KB area above it. The flash budget
  (the container below 0x300000) is the tighter limit for a payload.
- UI: the periodic UI routine (0x40098754) is the Rytm's (0x400A2AAA on
  1.73) instruction for instruction, with its object's fields moved (the
  idle counter +0x918, the Rytm's +0x748): about 20 Hz, a 128 x 64
  frame, the view controller loaded from +0x20 and the frame skipped when
  it is null.
- Recovery: STARTUP menu (hold FUNC at power-on), TRIG 4 (OS UPGRADE),
  stock .syx over MIDI DIN; checked on a unit with stock 1.56 after the
  test builds (21 Oct 2026).
"""
from . import Device, Release

DEVICE = Device(
    key='analog-four-mk2',
    name='Analog Four MKII',
    sysex_id=0x0B,
    releases={
        '1.56': Release(
            version='1.56',
            syx_sha256='5aea489db3e747547f71d5e08a2d20253149fe6258defad60306402d2fc23cb5',
            main_sha256='748fec15ddc61bc3ff66047facba74328934c5fb59f427ddaf34a4e2629a474c',
            main_len=2500408),
    },
    main_section=3,
    main_load=0x40000400,
    stage=0x40200000,
    flash_at=0x80000,
    flash_limit=0x300000,                         # the calibration record starts there
    trailer=None,
    isa='coldfire',
    areas={
        'ddr': (0x47BE0000, 0x47C00000),          # static and rytm-emu, see above
        'gap': (0x456472C0, 0x47BE0000),          # canary-checked on a unit, 21 Oct 2026, see above
    },
    ddr=(0x47BE0000, 0x47C00000),
    core_ddr=(('0.6', (0x45650000, 0x47C00000)),),   # core-a4mk2 0.6: 37.7 MB, the gap and the area
    recovery=('hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI DIN (not USB)'),
    toolchain={
        'prefix': 'm68k-linux-gnu-',              # or ELEKLOADER_CROSS=m68k-elf-
        'asflags': ['-mcpu=54455'],                 # ColdFire V4, as the Rytm
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
