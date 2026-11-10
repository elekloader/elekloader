# SPDX-License-Identifier: GPL-2.0-or-later
"""Analog Rytm (MKI): a ColdFire V4 with EMAC (the MKII's MCF5441x family); the
main OS runs from DDR at 0x40000400. Only OS 1.74 is supported: it is the
release Elektron distributes. It was measured by draadpiraat on the stock
image, in rytm-emu and on a unit (Modwerk's docs/RYTM_MKI_OS_RESEARCH.md has
the evidence), building on gdeo607's OS 1.73 findings (rytm1_mods). 1.73 is
left out: its bootstrap copy sits 0x60 bytes earlier, and its mods area and
entry compare were never checked.

- **The file family is ELE2** (`syx.py`): the Digitakt mk1's SysEx transport
  (device id 0x07, the same counters and checksums) around a simpler
  container. It has a 0x14-byte header ('ELE2', a 4-character build ('0173',
  '0176'), spaces, the 4-character version at 0x10), the load address at
  0x14, then the one packed section (the main OS) at 0x18. There is no table,
  digest or seal, and the stock file pads the container to 4 bytes. From its
  own section stream, the writer reproduces the stock file byte for byte.
- **The bootstrap** is in flash below 0x20000 (its vectors: SP 0x80010000, PC
  0xa6a), and the container is from 0x20000.
  - At boot it depacks straight from flash: 0x20018 to the address at 0x20014.
  - After an upgrade it depacks the section staged at 0x40200000 to
    0x40000400, in place.
  - Both use the same routine (flash 0x2a, 356 bytes), byte-identical to the
    Digitakt mk1 bootstrap's depacker (0x8000009e). So the packer's streams
    are valid here. Stock's in-place gap is 558,611 bytes.
- **Flash.** The OS reads calibration records (RBOW) from 0x780000, 0x7C0000
  and 0x7F0000, and no other flash address between the container and them
  (1.74). So the container must end below 0x780000 (`flash_limit`). The
  bootstrap's MIDI upgrade erases only the 64 KB sectors the received length
  needs, from 0x20000. Reads near 0x100000 go to the +Drive, not the NOR.
- **Protected.** The main OS carries a copy of the bootstrap: on 1.74 its
  version halfword (0x0602) at 0x4028c768 and the 87,060-byte image at
  0x4028c770-0x402a1b84.
  At every boot the OS entry (0x40000592-0x400005d8) compares that version with
  the installed bootstrap's, read from flash 0x1fff8; only if the installed one
  is lower does it call BOOTSTRAP UPGRADE (0x4011a7a4 on 1.74), which erases the
  boot sectors and writes exactly that image to flash 0. The startup menu (FUNC
  at power-on: TEST MODE, EMPTY RESET, FACTORY RESET, OS UPGRADE) is inside
  that image. So the copy, its version words and the compare are protected,
  and a build that changes them is refused. The bootstrap (flash
  0-0x1FFFF) runs the startup menu before any OS code and unpacks the OS
  from 0x20000 unchecked, so DIN recovery survives a broken main OS; only
  damage to that flash breaks it, and the flash has no write protection.
  So mods may not link to the routines that can write it (`no_link`):
  BOOTSTRAP UPGRADE and the flash helpers, two erases (0x40096ac2, used only
  for the bootstrap; 0x40096a6c, used by the OS store and calibration) and the
  program routine (0x40096af6), since programming alone can corrupt the
  bootstrap. That stops ordinary calls only.
- **Size.** The OS upgrade decodes the file into a fixed 2 MiB RAM buffer
  (0x40366570), 101 bytes a packet, the index checked only against the file's
  own count, with other statics right after it: at most 20,763 packets
  (2,097,063 decoded bytes, about 2,657,696 bytes of .syx). `update_max`
  keeps a build under that; `flash_limit` is never reached first.
- **Mods' area: 0x47BE0000-0x47C00000**, the MKII's (and the Digitakt mk1's)
  area. The sample pool ends at 0x4713e7c0 and nothing above 0x4713e920 up to
  the stack (0x48000000, run down) is named in the image. Verified on a unit
  (1.74, 8 November 2026) with a canary: 0x47B00000-0x47F00000 filled with
  address ^ 0xC0DEF00D at boot and read back with no word changed, with the
  sample memory full, sound locks, playback and a project save. Format 2 mods (with a core: Modwerk's MKI base, the MKII base's
  core-ar2 0.7 at the MKI's addresses) link there.
- **The version field** of a build is its own: a build marked MW01 booted on a
  unit and its start-up screen showed MW01 where stock shows 1.74, so a unit
  shows whether it runs stock.
- **On a unit (1.74, 8 November 2026):** builds whose container differs from
  stock only in its stream and version field, flashed with Elektron Transfer
  over USB (the in-OS upgrade), booted and played, and the stock .syx brought
  the unit back, over USB and over MIDI DIN from the startup menu. Those builds
  were written by Modwerk's ELE2 writer, which this one matches. EL01, stock
  1.74 repacked and written by this writer (version field EL01, sha256
  e3745dce...), was flashed with Transfer on 10 November 2026, booted and
  played, and the stock file brought the unit back.
"""
from . import Device, Release

DEVICE = Device(
    key='analog-rytm-mk1',
    name='Analog Rytm MKI',
    sysex_id=0x07,
    releases={
        '1.74': Release(
            version='1.74',
            syx_sha256='4f0d59051b4b3ac01f336639918ceb8186e0ae1a1abf2ed9bc028b8718514cff',
            main_sha256='6d021e22e8912cfc1ee84e619c35db0174455cbc42bf64dbec1f1d06a5ab811d',
            main_len=2824228),
    },
    main_section=3,
    main_load=0x40000400,
    stage=0x40200000,
    flash_at=0x20000,
    flash_limit=0x780000,                         # the calibration records start there
    trailer=None,
    isa='coldfire',
    container='ele2',
    version_len=4,
    version_at=0x10,
    update_max=0x1F0000,                        # under the OS upgrade's 2 MiB decode buffer (see above)
    protected=((0x40000592, 0x400005d8,
                "the OS entry's bootstrap version compare and the call to BOOTSTRAP UPGRADE"),
               (0x4028c768, 0x402a1b84,
                'the copy of the bootstrap the OS writes into flash, with its version words')),
    no_link=((0x4011a7a4, 0x4011aa7a, 'BOOTSTRAP UPGRADE, which rewrites the boot flash'),
             (0x40096a6c, 0x40096ac2, 'the flash erase the OS store and calibration use'),
             (0x40096ac2, 0x40096af6, 'the flash erase used only for the bootstrap'),
             (0x40096af6, 0x40096b3e, 'the flash program routine every flash writer uses')),
    areas={'ddr': (0x47BE0000, 0x47C00000)},     # verified on 1.74, see above
    ddr=(0x47BE0000, 0x47C00000),
    recovery=('hold FUNC while powering on for the startup menu, press TRIG 4 (OS UPGRADE), '
              'then send the stock .syx over MIDI DIN (not USB)'),
    toolchain={
        'prefix': 'm68k-linux-gnu-',              # or ELEKLOADER_CROSS=m68k-elf-
        'asflags': ['-mcpu=54455'],                 # ColdFire V4, as the MKII
        'cflags': ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
