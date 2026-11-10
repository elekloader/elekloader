# SPDX-License-Identifier: GPL-2.0-or-later
"""Analog Rytm MKII: a ColdFire MCF5441x-family CPU (the Digitakt mk1's
platform, an MCF54418); the main OS runs from DDR at 0x40000400. Measured
on OS 1.73, by comparison with the Digitakt mk1 1.54 (elekloader's
digitakt-mk1 profile and mods/core, whose 1.54 port is the core this one
starts from) in Ghidra and on the stock images.
1.74's file differs from 1.73's only in the main OS (and the version
string's section 5): the same bootstrap, FPGA bitstream and ARM image. Its
main OS is 1.73's with code added in two places (Outbox 8's main-output
routing), and the same OS entry, SRAM operands, cleared RAM and stack, with
no operand near the mods' area, so what follows holds for both, at 1.73's
addresses (docs/DEVICES.md says how far each moved).

- The container: ELE3, sysex id 0x0C, sections 5 (meta), 2 (bootstrap,
  with the STARTUP menu), 1 (FPGA bitstream), 3 (MAIN OS, packed) and 8
  (an ARM Cortex-M image, byte-identical to the Digitakt mk1's section 8).
  No updater section (the Digitakt mk1 has one, section 4) and no trailer:
  elektron-firmware-tool's -r null repack reproduces the stock file byte
  for byte. Only section 3 changes.
- Same platform as the Digitakt mk1: the OS entry (word[0] = 0x40000870;
  the Digitakt's 0x400004E8) is the Digitakt's instruction for instruction
  apart from addresses and one constant (it ORs 0x8E into 0xFC044000,
  the Digitakt ORs 0x06). It sets SP to 0x48000000, writes the same
  peripheral registers (0xFC04002D, 0xFC04002F, 0xEC09000E bit 11, CACR,
  ITT0, with the same values) and makes the same five setup calls in the same
  order; the third (0x400008C0, the Digitakt's 0x40000538) sets VBR to
  0x40000000.
- The bootstrap reads section 3 from flash (its container offset +
  0x80000) to 0x40200000 and depacks it to 0x40000400: the Digitakt mk1
  bootstrap's loader (Rytm 0x80000590, Digitakt 0x800005FE), call for
  call, with a decompiled-identical depacker (Rytm 0x8000042C, Digitakt
  0x8000049A).
- Flash limit: 0x300000. The Rytm's bootstrap makes no fixed-offset flash
  read (its only flash reads are relative to the container), so the limit
  comes from the main OS, as the Digitakt II's does. Like the Digitakt
  mk1's, it keeps a store at 0x380000-0x400000 (42 pushes of 0x380000 and
  16 of 0x3C0000 around 0x40139000, into the same read, write and erase
  routines, 0x40136FAA, 0x40136FAE and 0x4013702A). It also keeps a second
  store of its own at 0x300000-0x380000: the routines at 0x400FAC1A-
  0x400FB1C6 read a 0x1B072-byte block from 0x300000 or 0x340000 into
  0x4198911A, and erase and write there through the same routines. So the
  container must end below 0x300000: 2.5 MB from 0x80000, a whole number of
  256 KB units. Stock's ends at 0x1EA2F0; elekloader's repack of the stock
  main OS, which compresses less than Elektron's, at 0x20A5D0. The in-OS
  upgrade (0x4009F3A0) erases from 0x80000 only as far as the container it
  received reaches.
- No protected range: the main OS holds no copy of the bootstrap, and
  nothing in it writes its own bytes to flash (docs/DEVICES.md).
- The OS clears 0x402CE000-0x42FBB250 at start (0x4000083A, the
  Digitakt's routine at 0x400004B2, which clears 0x40253000-0x439D1000)
  and runs its stack down from 0x48000000.
- Mods' area: 0x47BE0000-0x47C00000, the Digitakt mk1's
  area, below the stack's page. Verified on hardware 2026-10-11 with the
  DDR canary (mod 0016 + 0003 diag mode 4, Iced-Cream/rytm-mods-src,
  elemod branch at fb1a0ec,
  `make ddr-canary-build`): the area was
  filled with address ^ 0xC0DEF00D at the boot site and read back with 0
  changed words after boot, through kit/sample loading, busy playback,
  FX/drive, MIDI, Overbridge, project save/reload, a long run, and again
  after a power cycle. That run also exercised the boot hook (0x400008C0).
- The bigger area, 'big': 0x47938000-0x47FFF000 (6.8 MB), with the mods'
  area inside it. Checked on hardware 2026-11-05 with the DDR canary over
  the rest (stock 1.74): through boot, a project's setup, playback, sample
  memory filled to its limit, heavy MIDI, kit dumps, Overbridge streaming
  and a power cycle, stock never wrote 0x47938000-0x47BE0000 or
  0x47C00000-0x47FFF000. Sample memory ends at about 0x47937290. Below it, the OS clears a block at 0x479372E0
  (its 0x4F9372E0, which the 128 MB DDR aliases there); above it, the last
  4 KB under 0x48000000 is the stack's. core-ar2 0.11 keeps its step tag
  tables at 0x47938000..0x47B00000, fixed,
  so from 0.11 the linker lays the mods out from 0x47B10000 to 0x47FFF000
  (core_ddr), payloads included. The flash budget (the container below
  0x300000) is the tighter limit for a payload: about 1 MB.
- UI: the periodic UI routine (0x400A2AAA) advances its counter 20 per
  second (the Digitakt's at 30, 0x4000A75C), so ev_tick runs at about
  20 Hz. The tick target is the Digitakt's byte for byte (it returns the
  dirty flag at +0x20 of the view controller) and the draw target
  (drawAll) opens as the Digitakt's does. The Digitakt passes the
  controller embedded at +0x40 of the UI object; the Rytm loads a pointer
  from +0x20 and skips the frame when it is null.
- Recovery: STARTUP menu (hold FUNC at power-on), TRIG 4 (OS UPGRADE, the
  Digitakt's menu, with the Rytm's POTENTIOMETER and PAD TEST added),
  stock .syx over MIDI DIN; checked on hardware with stock 1.73.
"""
from . import Device, Release

DEVICE = Device(
    key='analog-rytm-mk2',
    name='Analog Rytm MKII',
    sysex_id=0x0C,
    releases={
        '1.73': Release(
            version='1.73',
            syx_sha256='8ad671087ec30433d5407c7380d2396e0915ab931c5d3dd18a78bf284d3d1e52',
            main_sha256='28d9ef40c895e03f17dfce096543109adaf708947392db72818fc2a412d685d6',
            main_len=3005432),
        '1.74': Release(
            version='1.74',
            syx_sha256='cb0f99d620edde994fb860c0e79cc2464e427d06882828cfdfb00fc53700e123',
            main_sha256='34cd0c4da8e4b810d1b25284475a99ea53821e2ab7d30a978bf15dc578251f3b',
            main_len=3005432,
            data_release=0x30313736,                  # a stock 1.74 project download's header
            prelink=(                                 # boot-order trace (Ghidra, 2026-11-10): run only before 0x401009e0
                (0x40000690, 0x4000074a, 1, 'reset helpers'),
                (0x4000074a, 0x40000762, 1, 'FUN_4000074a, FUN_40000756'),
                (0x400007e4, 0x40000870, 1, 'data copy, OS .bss clear'),
                (0x40000870, 0x400008fc, 1, 'reset entry'),
                (0x40000946, 0x400009bc, 1, 'FUN_40000946'),
                (0x40000c34, 0x40000c7e, 1, 'FUN_40000c34'),
                (0x40000dac, 0x40000de4, 1, 'scheduler first start'),
                (0x400014a2, 0x400014d8, 1, 'FUN_400014a2, VBR_FN, reset helper'),
                (0x40001c16, 0x40001dea, 1, 'panel UART/DMA setup'),
                (0x4000201c, 0x400021f0, 1, 'FUN_4000201c'),
                (0x400023c8, 0x40002454, 1, 'FUN_400023c8'),
                (0x400025f6, 0x4000261a, 1, 'FUN_400025f6'),
                (0x40003bb2, 0x40003bcc, 1, 'FUN_40003bb2'),
                (0x40003be6, 0x40003bfa, 1, 'FUN_40003be6'),
                (0x40006cd8, 0x40006e8a, 1, 'parameter lookup table build'),
                (0x4007a4d6, 0x4007a4e4, 1, 'FUN_4007a4d6'),
                (0x4007f356, 0x4007f36c, 1, 'ekFS pre-mount'),
                (0x4008119c, 0x40081238, 1, 'FUN_4008119c'),
                (0x40081c10, 0x40081c28, 1, 'FUN_40081c10'),
                (0x40081da0, 0x40081dac, 1, 'FUN_40081da0'),
                (0x4008253a, 0x400825da, 1, 'key-code map'),
                (0x40091998, 0x40091f88, 1, 'eMMC bring-up'),
                (0x40091ff4, 0x40092048, 1, 'FUN_40091ff4'),
                (0x40095e9e, 0x40095eb0, 1, 'FUN_40095e9e'),
                (0x400963ec, 0x40096448, 1, 'FUN_400963ec'),
                (0x400a3448, 0x400a3454, 1, 'FUN_400a3448'),
                (0x400fa456, 0x400fa46e, 1, 'state header'),
                (0x400fa5c6, 0x400fa640, 1, 'state header'),
                (0x400fb268, 0x400fb394, 1, 'boot project load'),
                (0x401006ee, 0x401009e0, 1, 'main() up to the hook'),
                (0x40100a94, 0x40100ac2, 1, 'start task prologue'),
                (0x40100b12, 0x40100b40, 1, 'creates the start task'),
                (0x40101efa, 0x40101fa2, 1, 'FUN_40101efa'),
                (0x40122960, 0x40122bf8, 1, 'SPI migration, working state read'),
                (0x40139b80, 0x40139cde, 1, 'FUN_40139b80, FUN_40139bc0, FUN_40139c00'),
                (0x4013fd52, 0x4013fdae, 1, 'FUN_4013fd52'),
                (0x4013fefe, 0x40140260, 1, 'FUN_4013fefe, FUN_40140068'),
                (0x4017014a, 0x401701c8, 1, 'RTOS init'),
                (0x4017c2cc, 0x4017c384, 1, 'constructor'),
                (0x4017d998, 0x4017d9b6, 1, 'constructor'),
                (0x40181622, 0x4018165e, 1, 'constructor'),
                (0x401893c2, 0x4018954c, 1, 'constructor'),
                (0x4018a290, 0x4018a2c8, 1, 'constructor'),
                (0x4018dfd2, 0x4018e074, 1, 'constructor'),
                (0x401a7622, 0x401b139a, 1, 'constructor FUN_401a7622, param_info copy'),
                (0x401bd2c6, 0x401bd2e6, 1, 'constructor'),
                (0x401bed50, 0x401bed5c, 2, 'old SPI-flash state addresses'),
                (0x402cd650, 0x402cd89c, 2, 'constructor table'),
            )),
    },
    main_section=3,
    main_load=0x40000400,
    stage=0x40200000,
    flash_at=0x80000,
    flash_limit=0x300000,                         # the OS's own store starts there
    trailer=None,
    isa='coldfire',
    areas={
        'ddr': (0x47BE0000, 0x47C00000),          # verified 2026-10-11, see above
        'big': (0x47938000, 0x47FFF000),          # verified 2026-11-05, see above
    },
    ddr=(0x47BE0000, 0x47C00000),
    core_ddr=(('0.11', (0x47B10000, 0x47FFF000)),),   # core-ar2 0.11: 5 MB, above its step tags
    drive_align=0x1000,                           # the +Drive mods' area: what the flashed build
                                                  # leaves of core_ddr's, from a 4 KB boundary
    data_family=2,                                # the data container's family id (stock checks it)
    data_device=5,                                # ... and its device id (a stock project download's)
    recovery=('hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI DIN (not USB)'),
    slot_claims=(('core_machines', 0, 53, 'machine'),),   # a machine's descriptor names its slot at +0
    # core-ar2's knob ids: stock's records are 0..468, core's lockable parameters take 469..554
    # (469 + code - 42, lock.c) and a mod's own ids start at 555 (pages.h CORE_PARAM_LOCK_FIRST)
    param_ids=(555, (469, 554, "core's lockable parameters' ids"),
               (('core_params', False), ('core_param_locks', True), ('core_knob_looks', True))),
    toolchain={
        'prefix': 'm68k-linux-gnu-',              # or ELEKLOADER_CROSS=m68k-elf-
        'asflags': ['-mcpu=54455'],                 # ColdFire V4 (the MCF5441x's ISA), as the Digitakt mk1
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
