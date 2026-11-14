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
            main_len=2500408,
            prelink=(                                 # boot-order trace (Ghidra, 2026-11-14): run or read only
                                                      # before 0x401061e8, ev_boot_drive's site in main()
                (0x40000fbc, 0x40001076, 1, 'FUN_40000fbc reset helper'),
                (0x40001076, 0x4000108e, 1, 'FUN_40001076, FUN_40001082 callback registration'),
                (0x400010a4, 0x400011ba, 1, 'data copy, OS .bss clear, reset entry'),
                (0x400017cc, 0x40001816, 1, 'FUN_400017cc'),
                (0x40001944, 0x4000197c, 1, 'FUN_40001944 vector/timer setup'),
                (0x4000203a, 0x40002070, 1, 'FUN_4000203a, FUN_40002046, FUN_40002064 reset helpers'),
                (0x400027ae, 0x40002982, 1, 'FUN_400027ae'),
                (0x40002bb4, 0x40002d88, 1, 'FUN_40002bb4'),
                (0x40002f60, 0x40002fec, 1, 'FUN_40002f60'),
                (0x4000318e, 0x400031b2, 1, 'FUN_4000318e'),
                (0x400638d6, 0x400638e4, 1, 'FUN_400638d6'),
                (0x40063d7c, 0x40063e18, 1, 'FUN_40063d7c'),
                (0x4006478a, 0x400647a2, 1, 'FUN_4006478a'),
                (0x40064f00, 0x40064f52, 1, 'FUN_40064f00'),
                (0x40071714, 0x40071cf8, 1, 'SD bring-up'),
                (0x40071d64, 0x40071db8, 1, 'SD bring-up'),
                (0x40075b9a, 0x40075bac, 1, 'FUN_40075b9a'),
                (0x4007608c, 0x400760e8, 1, 'FUN_4007608c'),
                (0x40099068, 0x40099074, 1, 'FUN_40099068'),
                (0x400f6ada, 0x400f6b6a, 1, 'FUN_400f6ada'),
                (0x400f6ff8, 0x400f7006, 1, 'state header check'),
                (0x400f706a, 0x400f70da, 1, 'state header'),
                (0x40105fb6, 0x401061e8, 1, 'main() up to the hook'),
                (0x4010630a, 0x40106338, 1, 'start task prologue'),
                (0x40106388, 0x401063b6, 1, 'creates the start task'),
                (0x40106f9c, 0x401071ca, 1, 'first-boot seed, power-off image read'),
                (0x40118368, 0x401186c6, 1, 'FUN_40118368..FUN_40118518'),
                (0x40142e1a, 0x40142e98, 1, 'RTOS init'),
                (0x4014735e, 0x4014737c, 1, 'constructor FUN_4014735e'),
                (0x40147c68, 0x40147c86, 1, 'constructor FUN_40147c68'),
                (0x4014897a, 0x40148ac6, 1, 'constructor (walk extent)'),
                (0x40149472, 0x40149490, 1, 'constructor (walk extent)'),
                (0x40149644, 0x40149662, 1, 'constructor (walk extent)'),
                (0x4014a3ac, 0x4014a3ca, 1, 'constructor (walk extent)'),
                (0x4014a9a6, 0x4014aa9e, 1, 'constructor (walk extent)'),
                (0x4014ac34, 0x4014ac52, 1, 'constructor (walk extent)'),
                (0x4014ae46, 0x4014ae78, 1, 'constructor (walk extent)'),
                (0x4014b0c0, 0x4014b0de, 1, 'constructor (walk extent)'),
                (0x4014b124, 0x4014b160, 1, 'constructor FUN_4014b124'),
                (0x4014b32e, 0x4014b374, 1, 'constructor (walk extent)'),
                (0x4014bbbc, 0x4014bc32, 1, 'constructor FUN_4014bbbc'),
                (0x4014c910, 0x4014c96a, 1, 'constructor (walk extent)'),
                (0x4014ce2a, 0x4014ce98, 1, 'constructor (walk extent)'),
                (0x4014d0b2, 0x4014d10c, 1, 'constructor (walk extent)'),
                (0x4014d29a, 0x4014d2b8, 1, 'constructor (walk extent)'),
                (0x4014d484, 0x4014d4a2, 1, 'constructor (walk extent)'),
                (0x4014d8a6, 0x4014d8d8, 1, 'constructor (walk extent)'),
                (0x4014da9c, 0x4014dace, 1, 'constructor (walk extent)'),
                (0x4014dc48, 0x4014dc7a, 1, 'constructor (walk extent)'),
                (0x4014deb0, 0x4014dece, 1, 'constructor (walk extent)'),
                (0x4014dfd0, 0x4014dfee, 1, 'constructor (walk extent)'),
                (0x4014e0f0, 0x4014e10e, 1, 'constructor (walk extent)'),
                (0x4014e21e, 0x4014e23c, 1, 'constructor (walk extent)'),
                (0x4014e4fe, 0x4014e530, 1, 'constructor (walk extent)'),
                (0x4014e654, 0x4014e6ae, 1, 'constructor (walk extent)'),
                (0x4014e8ee, 0x4014e920, 1, 'constructor (walk extent)'),
                (0x4014eaae, 0x4014eacc, 1, 'constructor (walk extent)'),
                (0x4014ecd4, 0x4014ed1a, 1, 'constructor (walk extent)'),
                (0x4014f45c, 0x4014f4de, 1, 'constructor (walk extent)'),
                (0x4014fa5c, 0x4014faa2, 1, 'constructor (walk extent)'),
                (0x4014fb80, 0x4014fbc6, 1, 'constructor (walk extent)'),
                (0x4014fcb0, 0x4014fce2, 1, 'constructor (walk extent)'),
                (0x4014fdee, 0x4014fe48, 1, 'constructor (walk extent)'),
                (0x401500ea, 0x4015011c, 1, 'constructor (walk extent)'),
                (0x401502e2, 0x40150328, 1, 'constructor (walk extent)'),
                (0x40151284, 0x401512f2, 1, 'constructor (walk extent)'),
                (0x401513dc, 0x401513fa, 1, 'constructor (walk extent)'),
                (0x40151400, 0x4015145a, 1, 'constructor (walk extent)'),
                (0x4015152e, 0x40151574, 1, 'constructor (walk extent)'),
                (0x401517c0, 0x401517f2, 1, 'constructor (walk extent)'),
                (0x40151be0, 0x40151bfe, 1, 'constructor (walk extent)'),
                (0x40151c04, 0x40151c36, 1, 'constructor (walk extent)'),
                (0x40151d14, 0x40151d46, 1, 'constructor (walk extent)'),
                (0x40151e48, 0x40151e66, 1, 'constructor (walk extent)'),
                (0x40151f26, 0x40151f44, 1, 'constructor (walk extent)'),
                (0x40152046, 0x40152064, 1, 'constructor (walk extent)'),
                (0x401520fa, 0x40152118, 1, 'constructor (walk extent)'),
                (0x401521e6, 0x40152370, 1, 'constructor (walk extent)'),
                (0x4015246a, 0x40152604, 1, 'constructor (walk extent)'),
                (0x401530b4, 0x401530ec, 1, 'constructor FUN_401530b4'),
                (0x4015325c, 0x4015327a, 1, 'constructor (walk extent)'),
                (0x40153b4e, 0x40153b6c, 1, 'constructor (walk extent)'),
                (0x40153d28, 0x40153d46, 1, 'constructor (walk extent)'),
                (0x40156952, 0x401569cc, 1, 'constructor (walk extent)'),
                (0x401577de, 0x40157944, 1, 'constructor (walk extent)'),
                (0x40159d78, 0x40159d96, 1, 'constructor (walk extent)'),
                (0x4015a1dc, 0x4015a1fa, 1, 'constructor (walk extent)'),
                (0x4015a608, 0x4015a662, 1, 'constructor (walk extent)'),
                (0x4015a870, 0x4015a928, 1, 'constructor (walk extent)'),
                (0x4015ab34, 0x4015ac04, 1, 'constructor (walk extent)'),
                (0x4015af6a, 0x4015b076, 1, 'constructor (walk extent)'),
                (0x4015b51c, 0x4015b53a, 1, 'constructor (walk extent)'),
                (0x4015b920, 0x4015b93e, 1, 'constructor (walk extent)'),
                (0x4016451a, 0x401645d2, 1, 'constructor (walk extent)'),
                (0x40165432, 0x401654f0, 1, 'constructor (walk extent)'),
                (0x401659cc, 0x40165bb4, 1, 'constructor (walk extent)'),
                (0x40166824, 0x40166856, 1, 'constructor (walk extent)'),
                (0x40167030, 0x40167062, 1, 'constructor (walk extent)'),
                (0x401677a8, 0x401677da, 1, 'constructor (walk extent)'),
                (0x401682e2, 0x40168328, 1, 'constructor (walk extent)'),
                (0x40168ade, 0x40168b10, 1, 'constructor (walk extent)'),
                (0x4016997a, 0x401699d4, 1, 'constructor (walk extent)'),
                (0x40169db0, 0x40169de2, 1, 'constructor (walk extent)'),
                (0x4016a578, 0x4016a5aa, 1, 'constructor (walk extent)'),
                (0x4016bb52, 0x4016bc24, 1, 'constructor (walk extent)'),
                (0x4016c5a2, 0x4016c5c0, 1, 'constructor (walk extent)'),
                (0x4016c8ec, 0x4016c946, 1, 'constructor (walk extent)'),
                (0x4016d2f0, 0x4016d35e, 1, 'constructor (walk extent)'),
                (0x4016d95e, 0x4016ddfe, 1, 'constructor (walk extent)'),
                (0x4016e1a6, 0x4016e1d8, 1, 'constructor (walk extent)'),
                (0x4016f834, 0x4016f87a, 1, 'constructor (walk extent)'),
                (0x4016fc26, 0x4016fc6c, 1, 'constructor (walk extent)'),
                (0x4016fd2c, 0x4016fd72, 1, 'constructor (walk extent)'),
                (0x4016fe32, 0x4016fe78, 1, 'constructor (walk extent)'),
                (0x401701a2, 0x40170210, 1, 'constructor (walk extent)'),
                (0x40172ed8, 0x40172f82, 1, 'constructor (walk extent)'),
                (0x401731d2, 0x4017322c, 1, 'constructor (walk extent)'),
                (0x401732ec, 0x40173346, 1, 'constructor (walk extent)'),
                (0x4017341a, 0x40173474, 1, 'constructor (walk extent)'),
                (0x40173534, 0x4017357a, 1, 'constructor (walk extent)'),
                (0x4017364e, 0x40173694, 1, 'constructor (walk extent)'),
                (0x40173976, 0x40173994, 1, 'constructor (walk extent)'),
                (0x40173a54, 0x40173a9a, 1, 'constructor (walk extent)'),
                (0x40173c98, 0x40173d06, 1, 'constructor (walk extent)'),
                (0x40173eee, 0x40173f34, 1, 'constructor (walk extent)'),
                (0x40174036, 0x40174090, 1, 'constructor (walk extent)'),
                (0x401742c2, 0x401742e0, 1, 'constructor (walk extent)'),
                (0x401744de, 0x401744fc, 1, 'constructor (walk extent)'),
                (0x4017463c, 0x40174682, 1, 'constructor (walk extent)'),
                (0x401748ce, 0x40174900, 1, 'constructor (walk extent)'),
                (0x40174a02, 0x40174a5c, 1, 'constructor (walk extent)'),
                (0x40174b5e, 0x40174b7c, 1, 'constructor (walk extent)'),
                (0x40174b82, 0x40174bc8, 1, 'constructor (walk extent)'),
                (0x40174dba, 0x40174dd8, 1, 'constructor (walk extent)'),
                (0x40174e98, 0x40174eb6, 1, 'constructor (walk extent)'),
                (0x40174f7e, 0x40174fc4, 1, 'constructor (walk extent)'),
                (0x40175280, 0x401752b2, 1, 'constructor (walk extent)'),
                (0x40175412, 0x40175444, 1, 'constructor (walk extent)'),
                (0x40175664, 0x401756be, 1, 'constructor (walk extent)'),
                (0x401757c0, 0x4017581a, 1, 'constructor (walk extent)'),
                (0x4017591c, 0x4017593a, 1, 'constructor (walk extent)'),
                (0x40175a3c, 0x40175a5a, 1, 'constructor (walk extent)'),
                (0x40175f72, 0x40175ff4, 1, 'constructor (walk extent)'),
                (0x40176364, 0x401763aa, 1, 'constructor (walk extent)'),
                (0x40176518, 0x4017655e, 1, 'constructor (walk extent)'),
                (0x401766ce, 0x40176714, 1, 'constructor (walk extent)'),
                (0x401768ec, 0x40176932, 1, 'constructor (walk extent)'),
                (0x40176a06, 0x40176a4c, 1, 'constructor (walk extent)'),
                (0x40176a7c, 0x40176b44, 1, 'parameter object helpers'),
                (0x40176e04, 0x4017d6c4, 1, 'constructor FUN_40176e04 (parameter objects)'),
                (0x4017da7a, 0x4017db24, 1, 'constructor (walk extent)'),
                (0x4017e48c, 0x4017e4d2, 1, 'constructor (walk extent)'),
                (0x4017e710, 0x4017e76a, 1, 'constructor (walk extent)'),
                (0x4017e83e, 0x4017e898, 1, 'constructor (walk extent)'),
                (0x4017ec8e, 0x4017ecc0, 1, 'constructor (walk extent)'),
                (0x4017efec, 0x4017f01e, 1, 'constructor (walk extent)'),
                (0x4017f120, 0x4017f152, 1, 'constructor (walk extent)'),
                (0x4017f40a, 0x4017f450, 1, 'constructor (walk extent)'),
                (0x4017f720, 0x4017f73e, 1, 'constructor (walk extent)'),
                (0x4017f85e, 0x4017f86c, 1, 'constructor (walk extent)'),
                (0x401849f4, 0x40184a14, 1, 'constructor FUN_401849f4'),
                (0x401b4984, 0x401b498c, 2, 'first-boot seed addresses'),
                (0x4024d99c, 0x4024d9a8, 2, 'FUN_40118518 data'),
                (0x40252728, 0x4025293c, 2, 'constructor table'),
            )),
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
    drive_align=0x1000,                           # the +Drive mods' area: what the flashed build
                                                  # leaves of core_ddr's, from a 4 KB boundary
    recovery=('hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI DIN (not USB)'),
    toolchain={
        'prefix': 'm68k-linux-gnu-',              # or ELEKLOADER_CROSS=m68k-elf-
        'asflags': ['-mcpu=54455'],                 # ColdFire V4, as the Rytm
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
