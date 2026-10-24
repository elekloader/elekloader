[elekloader docs](README.md) › Tests

# Tests

Each suite runs with plain Python (or Node for the TypeScript engine and the
page) and takes its input files from environment variables. A test whose
input files are not given is skipped, not passed: say which ran. Firmware
files never go in this repository.

```bash
python tests/test_units.py        # needs nothing
python tests/test_link_refusals.py   # needs nothing: every refusal of the linker, refused and passed
ELEKLOADER_STOCK=Digitakt_OS1.53.syx ELEKLOADER_MODS=path/to/mods python tests/test_link.py
ELEKLOADER_STOCK=... ELEKLOADER_STOCK_154=Digitakt_OS1.54.syx ELEKLOADER_MODS=... python tests/test_sdk.py   # the example needs the cross compiler
ELEKLOADER_RELEASES=folder/of/stock/files python tests/test_releases.py   # every known release, and its cores
ELEKLOADER_STOCK=... ELEKLOADER_BUNDLE=bundle.elemod ELEKLOADER_CTOOL_SYX=its-build.syx python tests/test_patcher.py
ELEKLOADER_OT_SYX=OCTATRACK_OS1.40C.syx ELEKLOADER_OT_BIN=OCTATRACK_OS1.40C.bin python tests/test_octatrack.py
ELEKLOADER_OT_SYX=... ELEKLOADER_OCTABAM=path/to/octabam python tests/test_octabam.py   # octabam optional
ELEKLOADER_OT_SYX=... ELEKLOADER_OT_EMU=path/to/octabam/out/emu/ot_emu python tests/test_octatrack_boot.py   # the boot gate
ELEKLOADER_DN_SYX=Digitone_and_Digitone_Keys_OS1.43.syx python tests/test_digitone.py
ELEKLOADER_DT2_SYX=Digitakt_II_OS1.17.syx python tests/test_digitakt2.py
ELEKLOADER_AR2_SYX=Analog-Rytm_MKII_OS1.74.syx python tests/test_rytm_mk2.py   # or 1.73
ELEKLOADER_A4_SYX=Analog-Four_MKII_OS1.56.syx python tests/test_analog_four_mk2.py
ELEKLOADER_DN2_SYX=Digitone_II_OS1.12.syx ELEKLOADER_DN2_ZIP=Digitone_II_OS1.12_dist.zip python tests/test_digitone2.py
ELEKLOADER_CATALOG_DIR=folder/of/catalog/files ELEKLOADER_STOCK=... python tests/test_catalog_link.py   # the catalog against core 3.0
ELEKLOADER_STOCK=... ELEKLOADER_STOCK_154=... ELEKLOADER_OT_SYX=... ELEKLOADER_MODS=... python tests/test_gui.py   # the window, hidden (Tk)
node tests/test_web.mjs build/site   # the web page's engine, in Pyodide (packaging/build_web.py first)
```

## The refusal fixtures

`tests/test_link_refusals.py` needs no firmware. For every message the linker
and the mod loader can refuse with (`link.py`, and the loader's functions in
`elemod.py`) it holds a fixture that must be refused with that message and one
that must link: the smallest change that makes the first one pass, through
`link.link` whole, so the image check runs on it too. The fixtures run on made-up
releases (an image of nops as long as the real one) of three real profiles, so
the profile's protected and kept ranges, free areas, RAM and fast SRAM are the
ones a build meets.

`test_every_refusal_has_a_fixture` reads the messages out of the sources by
their syntax and fails for a message no row names (and for a row whose message
is gone), so a new refusal arrives with its fixtures. `test_units.py` runs it
too, because that is the file the workflows run. The DSP refusals need the
real payload and keep their fixtures in `tests/test_octatrack.py`; the test
checks their text is still there. Whole builds (format 1) have their own tests
(`test_patcher.py`, `test_octatrack.py`) and are listed in the test, not covered
by it.

When the check that refuses is itself the thing under test (the image check
behind the site checks), the fixture makes the earlier check say nothing, as a
bug in it would, and expects the later one to refuse.

More inputs some suites take:
- `ELEKLOADER_CROSS`: the cross toolchain's prefix, when it is not
  `m68k-linux-gnu-` (Homebrew's is `m68k-elf-`). The suites that build code
  need it on the path: test_sdk, test_releases, test_catalog_link,
  test_octatrack, test_octabam and test_digitakt2.
- `ELEKLOADER_DN_SYX` and `ELEKLOADER_DN_SYX_144` (the Digitone's 1.43 and
  1.44 files) for test_sdk's Digitone core and its 1.44 port;
  `ELEKLOADER_CORE_21` (a released `core-2.1.elemod`) for its core 3.0
  checks.
- `ELEKLOADER_DSP_ASM` (octabam's `dsp_asm`) for test_octatrack's and
  test_octabam's DSP code.
- `ELEKLOADER_OT_EMU` (octabam's built `ot_emu`; or `ELEKLOADER_OCTABAM`, a
  checkout that has `out/emu/ot_emu`) for test_octatrack_boot. Without it the
  suite's emulator tests are skipped, and its no-emulator ones run.
- `ELEKLOADER_DN_MODS`, a folder of Digitone mods, for test_gui.
- `ELEKLOADER_DN2_SYX` and `ELEKLOADER_DN2_MODS` for test_gui's Digitone II
  test: a folder with core-dn2 and its mods, and another device's core and
  mods, which must not fit (test_digitone2 builds its own with the cross
  toolchain).
- `ELEKLOADER_MODS` for test_gui: `core-2.1.elemod` and `core-2.1-os1.54.elemod`,
  and, for the two-core test, `core-3.0.elemod` and the newest
  `machine-pages-*.elemod` for 1.53.

The Octatrack's seq-events (mods/seq-events-ot) is also checked in octabam's
ColdFire port, with the Octatrack's own sequencer running: the sites and the
build are in test_octatrack, and `tests/probe_octatrack_seq_events.py` boots
the images in `ot_emu` (build it with `make emu-cf` in an octabam checkout) and
checks that alone it is stock, that each stage does what it says in the
pending step records and in the MIDI out, and that the pattern is put back:

```bash
python tests/probe_octatrack_seq_events.py --emulator path/to/octabam --stock OCTATRACK_OS1.40C.syx
```

The Octatrack's trig-events (mods/trig-events-ot) is checked the same way:
`tests/probe_octatrack_track_trig.py` (it borrows the seq-events probe's
fixture and driver) runs the images in `ot_emu` with `--watch-pc` on the
machine's trig call, and checks that alone it is stock (the registers and the
stack at every call, the pending records, the frame records, the MIDI out),
that a handler sees every trig, and that a veto stops the machine's call for
that trig and nothing else:

```bash
python tests/probe_octatrack_track_trig.py --emulator path/to/octabam --stock OCTATRACK_OS1.40C.syx
```

`tests/probe_octatrack_step_built.py` does the same for `ev_step_built`, the
record the stock builder has staged: that a handler on it leaves the pending
records stock's, is given the arguments and the words the tick later copies out,
and can do what PLOCKS P2's fill, MIDI SCENES' staged row and REC TRIG MUTE do at
the builders (cancel a step, set a lock, move a time, mute a recorder trig,
snapshot a row, key a table by the build), also beside seq-events' own stages and
in a build nested in a build:

```bash
python tests/probe_octatrack_step_built.py --emulator path/to/octabam --stock OCTATRACK_OS1.40C.syx
```

## The boot gate

`tests/test_octatrack_boot.py` boots a linked Octatrack image in octabam's
emulator (`ot_emu`, built from octabam's `tools/emu`) and checks what the
build can claim only by reading its own bytes: that they are the bytes that
run. It builds `mods/core-ot` and some mods, links them, and boots the image
with `--watch-pc` on `boot` and `--mem-dump` at the stop.

| check | how |
|---|---|
| `.boot` ran once, and the RTOS handoff was reached | the watch's hit count; the stop line says `HANDOFF` |
| the reserve holds the linked RAM image | the dump against the blob the linker appended, byte for byte, except the bytes a test declares mutable (none for the RAM image) |
| `.bss` holds zeros | the same dump, except the bytes core writes itself: its boot record and the meter's counts-per-frame guess |
| the hash `.boot` took is the linker's | `ot_boot_info` against `__run_hash` and the test's own x33 over the dump |
| a wrong copy halts the boot | one byte of the image inverted at its first, middle and last word: the stop is the emulator's `FAULT -- unrecognised spin` inside `core_boot_fatal`, never `HANDOFF`, and the record says `OT_BOOT_FAIL` with the linker's hash and the planted copy's |
| the pages the reserve gave back hold nothing | the dump from the reserve's end to the end of the profile's `ddr`, all zero at the handoff |

The tests that need no emulator (the comparison with planted differences, the
hash against the linker's) always run; the rest are skipped without
`ELEKLOADER_OT_EMU`, and say so. The checks were tried against code made
wrong: with `.boot`'s seed changed the clean boots halt, and with its compare
removed the planted copies run on.

What it cannot see: the caches (the emulator has none, so it says nothing of
`.boot`'s use of the uncached alias for the copy and the read-back), the recorder,
anything after the handoff, and who wrote a byte. The built `ot_emu` of octabam 363861e has no
`--foreign-writes`, which octabam's gate uses to tell a store from stock's code
from a store from a module's; so a write into the given-back pages is seen only
when it is nonzero at the handoff. A mod's own writes to its `.run` after the
handoff are out of sight too: a mod that keeps state there declares it in a
test as mutable.
The TypeScript engine has its own tests, which need no firmware, and a
parity run that compares it with the Python on your own files
([js/README.md](../js/README.md)):

```bash
cd js && node --test "test/*.test.ts"
```
