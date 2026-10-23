# SPDX-License-Identifier: GPL-2.0-or-later
"""Digitone II: an NXP MCF5441x (ColdFire V4m, no FPU), the Digitakt II's
platform; the main OS runs from DDR at 0x40000400. Measured on OS 1.12
(docs/DEVICES.md has the evidence):

- The container: sections 5, 2 (the bootstrap; its `dest` is a version,
  0x0201), 3 (MAIN OS), 4, 7 (the SHARC DSP's program) and 8, the Digitakt
  II's. Section 3 is packed in the aPLib-shaped codec, 3,196,288 bytes
  depacked: it ends at 0x4030C980.
- Sealed as the Digitakt II, with its own seed: the key comes from the
  bootstrap's "Multiplier", a NUL and the 32 bytes after it (syx.seal_key).
  The OS's upgrade validator (0x400dbdb0) derives the same key from its own
  copy (0x40210a4a, 0x4021b9ef) and checks the content checksum first. From
  a stock file's own main OS stream, syx.write reproduces that file byte for
  byte.
- The bootstrap is the Digitakt II 1.17's code instruction for instruction:
  its loader (0x80000596) reads section 3 from flash, at its offset +
  0x80000, to 0x40400000, and its depacker (0x80000432) unpacks it to
  0x40000400. That depacker, run in place on elekloader's repack of stock,
  yields the stock image. Stock's in-place gap is 2,128,334 bytes.
- Flash: the container starts at 0x80000. The OS keeps a store at
  0x380000-0x400000 (26 and 12 pushes of 0x380000 and 0x3c0000), so the
  container must end below 0x380000: 3 MB, of which stock 1.12 takes 1.80 MB.
  The in-OS upgrade (0x40139104) erases from 0x80000 only as far as the
  container it received. No flash writer takes its data from the main OS
  image, so there is no protected range.
- At reset (0x400004e8), the OS copies 0x402fd000-0x40305000 to SRAM at
  0x80000000 and 0x40305000-0x4030c980 to 0x80008000, zeroing the rest; it
  clears DDR 0x402fd000-0x466b84d0 (its heap, 0x4464bbf0-0x4664bbf0, is
  inside), and its stack runs down from 0x48000000.
- Free at run time: DDR 0x47F00000-0x47F40000, the Digitakt II's area.
  Above the clear the OS names only its DMA buffers, through the uncached
  alias (0x4E6C0000-0x4E709340, so 0x466C0000-0x46709340), and the stack
  top. No decoded instruction names 0x47E00000-0x48000000 or its alias
  besides the stack top and FLT_MAX. A canary over 0x46710000-0x48000000,
  cached and uncached, held through a cold boot and two panel sessions in
  digikit's emulator (ColdFire only, to 2.25G instructions) but for the
  stack's top 52 bytes, and the final state matched a zero-filled run's.
  That compares contents, not accesses.
  It is the linker's `ddr` too (core-dn2), and a whole build's blob gets its
  size. SRAM is not qualified: there is no .fast area.
"""
from . import Device, Release

DEVICE = Device(
    key='digitone-mk2',
    name='Digitone II',
    sysex_id=0x15,
    releases={
        '1.12': Release(
            version='1.12',
            syx_sha256='7e5b8a733d4ab1fdf7e5d22757f7d8eddaa3cbf71d0858874a791844c02ceb35',
            main_sha256='a098f620019b2ad38fd7e0397df39501fb1cf6bc26d0283915f34c49446fdf1a',
            main_len=3196288)
    },
    main_section=3,
    main_load=0x40000400,
    stage=0x40400000,
    flash_at=0x80000,
    flash_limit=0x380000,
    update_max=0x200000,                       # conservative USB cap, measured on a unit; not found in code
    trailer='hmac',
    hmac_key_from=(2, b'Multiplier'),
    isa='coldfire',
    areas={
        'ddr': (0x47F00000, 0x47F40000),          # above the DMA buffers, below the boot stack
    },
    ddr=(0x47F00000, 0x47F40000),
    recovery=('hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI (not USB)'),
    toolchain={
        'prefix': 'm68k-linux-gnu-',                # binutils + gcc for m68k/ColdFire
        'asflags': ['-mcpu=54455'],                 # ColdFire V4 (the MCF5441x's ISA)
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
