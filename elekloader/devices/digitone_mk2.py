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
                'voice_range': (0x20090000, 0x200a0000),
                'voice_table': 'mk2_voice_synths',
                'voice_adapter_sha256': '5bbf8b98c5ff13d4121a17e2d7a8ca550f05821b4564a67c0ce84737319da3f0',
                'voice_adapter_bytes': 2048,
                'voice_state_bytes': 1024,
                'voice_stack': (0x282de000, 0x282df000),  # L1: a FreeRTOS context switch saves through the PM bus
                'voice_binding_offset': 65024,
                'voice_stack_max': 2048,         # of the 4 KiB stack; interrupts taken during a call use the rest
                'voice_hook': (0x20004a36, '8715ffffeeff', '3e06bc000080'),
                'filter_abi': 'dn2-filter-mono32-l2-v1',
                'filter_range': (0x200a0000, 0x200b0000),
                'filter_table': 'mk2_voice_filters',
                'filter_g_at': 16,              # mk2_filter's g (mk2/machines.h)
                'filter_adapter_sha256': '10576ba32964c88b620a72c73cd46aadb8bb66095eef98cfce29280f648b42c4',
                'filter_adapter_bytes': 2048,
                'filter_state_bytes': 1024,
                'filter_stack': (0x282df000, 0x282e0000),
                'stack_guard': 0x57ac6a2d,
                # core-dn2's DSP peek (mods/core-dn2/peek.s, rebuild_peek.py), with any SHARC mod:
                # answers the frame tail's request in reply word 2 at the block handler's end.
                'peek_range': (0x282e0000, 0x282e0800),
                'peek_code': '01112600388a0d102e001006ddc20006170050000d102e001806ddc50d112e0018068e0fff03decc200617005000be76bf860e102e0020060d112e0020060f102e0010060b102e001c060f112e001c06eec2000617005000dec1fbc10e102e003006eec50e112e0030060f112e0034060d112e0038060b102e003c06bbc20006170047008001fbb0220617004d000f112e003c060d112e0040060e112e004406089c7d058b0f800aabc0bf703f09089480060b0f0000ffff8401db0b0e0f0000eed5bec3200617003e0108940107089482078401de2aafce0b0f5a5a5a5aabce08948305abc3200617003e018b0ff0ffa001db0a0b102e000006abc30006170089000a112e0000068b0f00000b112e000406bfc68d0fff038001bdb0240617002c018b0f03008401eb0b200617002c013e020000bf02bec08001beb0040617002c010d0f240000008001edb0020617002c010d0f270000008001bdb004061700f1000d0f2c0000008001edb0020617002c010d0f2e0000c08001bdb004061700f1000d0f300000008001edb0020617002c010d0f310000c08001bdb004061700f1000d0f380000008001edb0020617002c010d0f390000c08001bdb004061700f1000d0f002000008001edb0020617002c010d0f0f2000a08001bdb0240617002c010d102e0004068b0fffffa001db0b3e020000bb02bec0bf70bf89089680053e0233000d00100617000a018e0ff0ffa001be0b0e0f0000ffffbecc8e0f0f008401ae0e3e020000ee0cedcd3e020000ee10ebcd08990207ddc53e020000bf01dbc30206170026018d0f00000d112e0004063e061c00629e8e0f0f008401ae0e3e020000ee0c8d0fff0fedcd3e020000ee10089902073e061c00629e8e0f0000089902073e061c00629ebe763f8001102e00140600112e00140601c1810fb80b800101b02206b800ab8a01102e00100610c001112e0010063e06b800ab8a0100',
                'peek_hook': (0x28393cbe, '01112600388a', '3e0617000000'),
                # Optional companion sites: exact stock/replacement bytes, not automatic patches.
                'data_sites': {0x20011576: ('3e07ff00f0ff', '3e0617004501')},
                'filter_binding_offset': 65024,
                'filter_stack_max': 2048,
                'filter_hook': (0x28392b0e, '140f5280c0db', '3e06bd000000'),
                # The filter setup table's internal type 0 entry, sent to the adapter's capture entry.
                'filter_capture': (0x8052dba4, '7e921c00', '8703bd00'),
                'core_range': (0x282dd600, 0x282dda00),
                'stack_max': 544,
                'code_guard_bytes': 64,
                'keyed_pitch_table': (920, '314d5900969c5e00d23c6400b1326a004c837000073477009b4a7e0015cd8500e1c18d00c92f9600031e9f002e94a800'),
                'owned_ranges': ((0x282dd600, 0x282e0800), (0x20090000, 0x200b0000)),
                'patch_ranges': ((0x2838489a, 0x283848a0), (0x20004a36, 0x20004a3c),
                                 (0x28392b0e, 0x28392b14), (0x8052dba4, 0x8052dba8),
                                 (0x28393cbe, 0x28393cc4)),
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
    slot_claims=(('mk2_voice_synths', 0, 32, 'mk2-synth'),  # stable synth IDs
                 ('mk2_voice_filters', 0, 32, 'mk2-filter'),
                 ('mk2_voice_pages', 0, 32, 'mk2-pages')),   # one pages entry per synth ID
    recovery=('hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI (not USB)'),
    toolchain={
        'prefix': 'm68k-linux-gnu-',                # binutils + gcc for m68k/ColdFire
        'asflags': ['-mcpu=54455'],                 # ColdFire V4 (the MCF5441x's ISA)
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
