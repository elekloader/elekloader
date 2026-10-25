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
            main_len=3196288,
            sharc={
                'section': 7,
                'processor': 'ADSP-21569',
                'isa': 'sharc+-visa',
                'silicon_revision': None,       # no chip-stepping observation; OS 1.12 is not a silicon revision
                'stored_sha256': '5ff93df8747bdf9eb28acf50391fb484d6aa6782bfe969b2b3807a8412645660',
                'stored_len': 602076,
                'decoded_sha256': '336e340aa0cdcd34e314cfa44849f709a3134f6bd4cd57dfc7e15702c83115e2',
                'decoded_len': 836956,
                'stored_max': 0x100000,         # the sender's first 1 MiB staging buffer
                'decoded_max': 0xfffff,         # second buffer also holds one command byte
                'record_count': 95,
                # (core-byte start, end, loader-byte start); physical capacity, not free RAM.
                'l1_aliases': ((0x240000, 0x270000, 0x28240000),
                               (0x2c0000, 0x2f0000, 0x282c0000),
                               (0x300000, 0x320000, 0x28300000),
                               (0x380000, 0x3a0000, 0x28380000)),
                'l2_bytes': (0x20000000, 0x20100000),
                'l2_pc16': (0xb80000, 0xc00000, 0x20000000),
                'ddr_bytes': (0x80000000, 0xa0000000),
                # Loader-byte ranges excluded from new code/data, independent of ownership.
                'protected': ((0x200fa000, 0x200fe000, 'DN2 special window'),
                              (0x200fe000, 0x20100000, 'Boot ROM workspace'),
                              (0x282ec000, 0x282f0000, 'L1 block 1 cache'),
                              (0x2831c000, 0x28320000, 'L1 block 2 cache'),
                              (0x2839c000, 0x283a0000, 'L1 block 3 cache')),
                # L1 bridge and pitch-table reservation; patch range is separate.
                'abi': 'dn2-master-mono32-v1',
                'pitch_range': (0x282dda00, 0x282dde00),  # linker-owned table and zero padding
                # Bounded L2 mod reservation; not a general free-memory map.
                'l2_abi': 'dn2-master-mono32-l2-v1',
                'l2_app_range': (0x20090000, 0x20098000),
                'voice_abi': 'dn2-voice-mono32-l2-v1',
                'voice_range': (0x20090000, 0x20094000),
                'voice_table': 'mk2_voice_synths',
                'voice_clone_table': 'mk2_synths',
                'voice_adapter_sha256': '46afad21d9f24483a645ec1b3c70a9df99c40285cdf73cf83b0f0467828572f8',
                'voice_adapter_bytes': 2048,
                'voice_state_bytes': 1024,
                'voice_binding_offset': 15872,
                'voice_stack_max': 128,
                'voice_hook': (0x20004a36, '8715ffffeeff', '3e06bc000080'),
                'filter_abi': 'dn2-filter-mono32-l2-v1',
                'filter_range': (0x20094000, 0x20098000),
                'filter_table': 'mk2_voice_filters',
                'filter_clone_table': 'mk2_filters',
                'filter_adapter_sha256': 'dea368ada96b1b08665e6f3399111aef43c84281a2fe4f85f585acd288e9dd32',
                'filter_adapter_bytes': 2048,
                'filter_state_bytes': 1024,
                'filter_binding_offset': 15872,
                'filter_stack_max': 20,
                'filter_hook': (0x28392b0e, '140f5280c0db', '3e06bc0000a0'),
                'core_range': (0x282dd600, 0x282dda00),
                'stack_max': 544,
                'code_guard_bytes': 64,
                'keyed_pitch_table': (920, '314d5900969c5e00d23c6400b1326a004c837000073477009b4a7e0015cd8500e1c18d00c92f9600031e9f002e94a800'),
                'owned_ranges': ((0x282dd600, 0x282dde00), (0x20090000, 0x20098000)),
                'patch_ranges': ((0x2838489a, 0x283848a0), (0x20004a36, 0x20004a3c),
                                 (0x28392b0e, 0x28392b14)),
                'active_keyed_core': {'segments': ((12, 'code', 'af2f375dc58a8ccd4b8abd89b1fb526441baa6fb5da9266c2b664e69d90fc829'),),
                                      'zero': ((1008, 16),),
                                      'entries': {'hook_offset_bytes': 12}, 'stack_bytes': 116},
                'hook': (0x2838489a, '8715ffff78ff', '3e06160006eb'),
            })
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
    slot_claims=(('mk2_synths', 0, 32, 'mk2-synth'),
                 ('mk2_voice_synths', 0, 32, 'mk2-synth'),  # shared stable synth IDs
                 ('mk2_filters', 0, 32, 'mk2-filter'),
                 ('mk2_voice_filters', 0, 32, 'mk2-filter')),
    recovery=('hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI (not USB)'),
    toolchain={
        'prefix': 'm68k-linux-gnu-',                # binutils + gcc for m68k/ColdFire
        'asflags': ['-mcpu=54455'],                 # ColdFire V4 (the MCF5441x's ISA)
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
