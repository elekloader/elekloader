# SPDX-License-Identifier: GPL-2.0-or-later
"""Digitakt II: an NXP MCF5441x (ColdFire V4m, no FPU), like the mk1; the
main OS runs from DDR at 0x40000400. Measured on OS 1.17, with digikit's
research on 1.15C and 1.16 (https://github.com/m-dwyer/digikit,
docs/findings/01-container-and-patching.md) as the map:

- The container: sections 5, 2 (the bootstrap; its `dest` is a version,
  0x0201, and it runs at 0x80000400), 3 (MAIN OS), 4 (updater), 7 (the
  SHARC DSP's program) and 8. Section 3 is packed in the aPLib-shaped codec,
  3,275,616 bytes depacked: it ends at 0x4031FF60.
- Sealed: after the last section's 16-byte padding, a 32-byte HMAC-SHA256
  of everything before it, inside the preamble's length. The bootstrap
  checks it (and the content checksum) before it flashes; the OS checks it
  on an upgrade. The key is derived from the bootstrap's seed string
  "Master Overdrive" and the 32 bytes after its NUL (syx.seal_key). From a
  stock file's own main OS stream, syx.write reproduces that file byte for
  byte.
- Staging: the bootstrap's loader (0x80000596) reads section 3 from flash,
  at its offset + 0x80000 (the container's place), to 0x40400000, and
  unpacks it from there to 0x40000400 with its depacker (0x80000432). The
  mk1 stages at 0x40200000, which this image runs past. Stock's in-place
  gap is 2,065,240 bytes.
- Flash: the container starts at 0x80000 (the bootstrap, and digikit's map
  of the OS's flash reads). The OS keeps a store at 0x380000-0x400000: it
  reads a 0x14-byte header at 0x380000 and erases the sectors at 0x380000
  and 0x3c0000 (26 and 12 call sites, from 0x400ca504), the mk1's code at
  its own addresses. So the container must end below 0x380000: 3 MB, of
  which stock 1.17 takes 1.48 MB. The bootstrap's receive path has no size
  check of its own.
- At reset (0x400004e8), the OS copies 0x40312000-0x40318e80 to SRAM at
  0x80000000 and 0x40318e80-0x4031ff60 to 0x80008000, zeroing the rest of
  each 32 KB half; it clears DDR 0x40312000-0x47E28470, and its stack runs
  down from 0x48000000.
- Free at run time (docs/DEVICES.md has the evidence):
  - DDR 0x47F00000-0x47F40000: above the clear's end and the DSP's buffers
    (reached through the uncached alias, 0x4FE30000-0x4FE7AE80), below the
    boot stack. No decoded instruction names 0x47E7B000-0x48000000 (or its
    alias), it is zero in every rung of a cold boot, and a write watch over
    all of it saw nothing from 400M to 808M instructions of a session with
    key presses.
  - SRAM 0x80006E80-0x80008000 and 0x8000F100-0x80010000: past the data
    the reset copies into each half, which it zeroes; no decoded
    instruction names either.
  .fast code goes to the tail; core-dt2 declares its copy table, core_fast,
  and copies it on the first ev_tick (mods/core/fast.s).
"""
from . import Device, Release

DEVICE = Device(
    key='digitakt-mk2',
    name='Digitakt II',
    sysex_id=0x14,
    releases={
        '1.17': Release(
            version='1.17',
            syx_sha256='26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5',
            main_sha256='a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964',
            main_len=3275616,
            # Section 7 (the ADSP-21569's boot stream), for raw SHARC mods (`sharc_raw`, docs/FORMAT.md):
            # new loads in the L2 that the stock stream leaves unused, and patches of whole stock
            # instructions in the L1 code load. Measured on 1.17; the free L2 is digikit's
            # (devices/ports/dt2-1.17.toml region dsp.l2, with its exclude).
            sharc={
                'section': 7,
                'processor': 'ADSP-21569',
                'isa': 'sharc+-visa',
                'stored_sha256': '39113a8ae7f7ac7336bda4a56eeb45108f0618ea451bcf9f466803017184375d',
                'stored_len': 184856,
                'decoded_sha256': '0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2',
                'decoded_len': 321016,
                'stored_max': 0x100000,
                'decoded_max': 0xfffff,
                'record_count': 104,
                'l1_aliases': ((0x240000, 0x270000, 0x28240000),
                               (0x2c0000, 0x2f0000, 0x282c0000),
                               (0x300000, 0x320000, 0x28300000),
                               (0x380000, 0x3a0000, 0x28380000)),
                'l2_bytes': (0x20000000, 0x20100000),
                'l2_pc16': (0xb80000, 0xc00000, 0x20000000),
                'ddr_bytes': (0x80000000, 0xa0000000),
                'protected': ((0x200fa000, 0x200fe000, 'DT2 L2 window two functions load as literals'),
                              (0x200fe000, 0x20100000, 'Boot ROM workspace'),
                              (0x282ec000, 0x282f0000, 'L1 block 1 cache'),
                              (0x2831c000, 0x28320000, 'L1 block 2 cache'),
                              (0x2839c000, 0x283a0000, 'L1 block 3 cache')),
                'raw': True,
                'owned_ranges': ((0x2001ab7c, 0x200fa000),),
                'patch_ranges': ((0x283827cc, 0x283827cc + 104500),),
            })
    },
    main_section=3,
    main_load=0x40000400,
    stage=0x40400000,
    flash_at=0x80000,
    flash_limit=0x380000,
    trailer='hmac',
    hmac_key_from=(2, b'Master Overdrive'),
    isa='coldfire',
    areas={
        'ddr': (0x47F00000, 0x47F40000),          # above the DSP buffers, below the boot stack
        # below the linker's area, above the DSP buffers (0x47E30000-0x47E7AE80): what mods that place
        # code at fixed addresses claim (`resources.regions`); digikit's patchkit puts its expansion
        # region at 0x47E8B000
        'ddr-low': (0x47E7B000, 0x47F00000),
        'sram-tail': (0x8000F100, 0x80010000),    # the SRAM's free tail
        'sram-block': (0x80006E80, 0x80008000),   # free SRAM after the first half's data
    },
    ddr=(0x47F00000, 0x47F40000),
    sram_code=(0x8000F100, 0x80010000),
    fast_table='core_fast',                     # declared and copied by core-dt2 (fast.s)
    recovery=('hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI (not USB)'),
    toolchain={
        'prefix': 'm68k-linux-gnu-',                # binutils + gcc for m68k/ColdFire
        'asflags': ['-mcpu=54455'],                 # ColdFire V4 (the MCF5441x's ISA)
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
