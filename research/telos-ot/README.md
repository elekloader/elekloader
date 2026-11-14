# TELOS for the Octatrack: design

Started 2026-11-14. Working material for this branch, not a doc: when TELOS
is built, what holds here moves to its home (docs/DRIVE.md, docs/FORMAT.md,
docs/ADAPTING.md, the loader's README), and the rest goes into the pull
request's description.

TELOS means mods that live on the unit's storage and are switched on and off
on the unit, with no new firmware flashed. The Analog Rytm MKII has it already
(`mods/drive-loader-ar2`, docs/DRIVE.md); the Digitone mk1's research is on
branch `ccr-990c73b3-i5dj5g` (`research/telos-dn1`). This is the Octatrack's
(MKI and MKII, OS 1.40C), from core-ot 0.42.

**Marks.** **[M]** measured here, in octabam's `ot_emu` on stock 1.40C or with
the tools named; **[D]** read in the code once; **[O]** open, with what would
settle it. Nothing here has run on a unit.

## The short version

1. **The Rytm's loader is the model**, rule for rule: a list of chosen files,
   a link at boot from the build descriptor (`core_desc`), refusal codes, a
   crash flag, a skip key, a page that checks a set before it saves it. The
   link's code is shared, and the Octatrack adds what the Rytm's +Drive mods
   don't have (point 3).
2. **Storage is plain files on the CF card**, in `/ELEKLOADER/MODS/`, beside
   the folder core already keeps its card data in. The files are the shop's
   own `.elemod` files, unchanged: the unit fills in their stock bytes from its
   own OS, so nothing has to be packed on a computer. They get onto the card
   by USB DISK MODE or a card reader. No SysEx transfer is needed. (Section 3, below.)
3. **TELOS mods may patch the firmware (sites).** Every one of the 34 shop
   mods does (735 sites); none uses only core's tables [M]. So unlike the
   Rytm's, the Octatrack's loader writes sites, with three guards:
   - a site the firmware has already run once before the load, and doesn't
     run again before it matters, is refused (`early`);
   - sites are written with interrupts masked, only when no task is stopped
     inside one (quiescence);
   - stock bytes a mod copies are taken from the stock bytes of the flashed
     build's own patches, which the descriptor carries (the "stock shadow").
4. **The load point is the card's first mount** (core-ot 0.41's `ev_card`,
   0x40061734, in the sys task). There the card's files read correctly [M].
   That point comes before the project load, the first UI tick that a mod
   hooks, and the first audio frame [M]. The DSPs are booted much earlier,
   before the OS starts, so a TELOS mod can't change DSP code (`feature`).
5. **What has to be flashed** (the base build): core, and whichever
   companions are wanted (intercepts, machines-ot, settings-ot, seq-events-ot,
   trig-events-ot, dspbus-ot), plus everything USB. USB is composed at the
   controller's init, before the card mounts [M]. Everything else in the shop
   can be a TELOS mod ([MEASURED.md](MEASURED.md), section 7).
6. **On the unit**, MAIN MENU > MODS lists every mod file on the card above
   the mods' settings. YES ticks or unticks one; the check runs at once, and a
   clash opens a dialog that names the other mod and offers the swap. APPLY
   saves the list; the change takes effect at the next power-on. ([ON-THE-UNIT.md](ON-THE-UNIT.md).)
7. **Conflicts are resolved by one rule set** on the unit, at boot and in the
   page, and on the computer (`elekloader check`). It is the host linker's
   rules plus the Octatrack's own: sites, battery RAM, the early list, DSP.
   First failure wins; a mod is never loaded half. ([ON-THE-UNIT.md](ON-THE-UNIT.md), section 6.)

## 2. How the Octatrack differs from the Rytm

| | Rytm (drive-loader-ar2) | Octatrack |
|---|---|---|
| storage | ekFS on the +Drive, `/mods`, files with a 16-byte header so SAMPLES doesn't choke | FAT on the CF card, `/ELEKLOADER/MODS`, plain files: the OT's file browsers show only audio and the folders of sets |
| getting files there | the Data route over USB SysEx (`elekloader mods put`) | USB DISK MODE or a card reader; a SysEx route is not needed |
| when | in `main()`, before the audio start and the UI task | the card's first mount in the sys task: after main, before the project load and the first frame |
| what a mod may hold | `.run`, `.bss`, entries in open tables; no sites | the same, **plus sites**, with the early list, quiescence and the stock shadow |
| DSP | none | DSP code is uploaded before the OS runs: refused |
| skip key | [NO] at power-on | [NO] at power-on (the panel's key state at the mount) |
| restart to apply | the page's restart | power off and on (v1); a restart routine is open [O] |

## 3. Where mods are stored on the CF card

```
/ELEKLOADER/
  card.work, card.strd       core's card data (core-ot 0.6), unchanged
  MODS/
    active                   the chosen files: "<path> <crc32>" a line (FORMAT.md's /mods/active)
    active.bak               the previous active, kept by APPLY
    *.elemod                 the mods, as the shop and authors publish them
    <folder>/*.elemod        up to 3 folders deep, as on the Rytm
    .status                  text: the last boot's link, one line per mod (written at boot)
    .build                   the flashed build's descriptor, byte for byte (written at boot when it changed)
    .armed                   the crash flag (written before the link, deleted 10 s after)
```

- **The files are `.elemod` as published.** The link reads the JSON format-2
  file directly, as the Rytm's `dlink.c` does (the JSON reader is shared).
  Stock parts are filled on the unit: from the running OS, or from the stock
  shadow where the flashed build changed those bytes. So the shop can offer
  its files for TELOS unchanged, and a user drags them onto the card.
- **`active`** is FORMAT.md's `/mods/active` (path below `MODS/`, a space, the
  file's CRC-32): a line is dropped when the file is missing or changed, so a
  file edited on a computer never loads unchecked. Lines starting with `#`
  are kept, so a user can turn one off on a computer by commenting it out.
- **`.status` and `.build` are for the computer.** `elekloader check
  /ELEKLOADER/MODS/.build MODS...` (docs/DRIVE.md, "On the computer") runs the
  same check with the card in a reader, and says why a mod won't load before
  it is copied.
- **Writes.** The unit writes four files only, each from the engine task
  (`ot_engine_post`), never while USB DISK MODE has the card (`ev_usb_disk`):
  - `active`, when APPLY saves it: `active.bak` is written first, then
    `active`; a boot that can't parse `active` falls back to `.bak`;
  - `.status` and `.build` at boot, after the project load;
  - `.armed`, before the link.
- **Recovery from a computer.** Delete `active`, or rename it, and the next
  boot loads nothing. `.armed` left behind (the last start crashed) also
  skips the link, as on the Rytm.
- **Card changes.** A different card at the next boot is a different set,
  which is the point: the set lives on the card. A card removed while the unit
  runs changes nothing until the next power-on: the code is in RAM.
- **Why not battery RAM or the flash.** The OS image is unpacked from flash
  by the bootloader, which stays stock. CS1's free range is 31 KB. The card
  is the only place large enough, and the user's own.

## 7a. TELOS 0.1: built and run in the emulator (2026-11-14)

`mods/telos-ot` (its README has the details) on core-ot 0.42, with a 2 MB area:
- **Base build:** core, intercepts, machines-ot, settings-ot, seq-events-ot, trig-events-ot, TELOS.
- **Card:** the 15 core-standard shop files and `active`.
- **Result in ot_emu:**
  - 13 loaded; the two refusals are right (LO-FI AMF FIX: `stock`, its DSP payload is gone by the mount; SCENES P2: `claim`, it clashes with MIDI SCENES);
  - all 111 sites as linked;
  - the area matches the computer's link of the same set (`tests/drive_link_host.c` built with `DL_SITES`), except 25 bytes the mods wrote while running;
  - TUNER opened from the card with UP + TEMPO.
- **Two emulator/compiler disagreements found on the way, both avoided in TELOS's code:**
  - `move.b (a0)+,(0,a0,a1)`: GCC assumes the destination uses the old a0, ot_emu the new one; `dl_memcpy` is kept out of line;
  - a byte push to `-(sp)`: GCC assumes it moves SP by 1, ot_emu by 2; `-fno-ipa-sra`.

  Which reading the ColdFire itself takes is to check on a unit; neither pattern is left in the build.
- **The stock shadow was not enough on its own.** The OS changes some of its image's data before the mount, so TELOS also keeps a copy of 0x400b9000..0x400e2324 from `ev_boot_early`, and stock parts come from that copy.
- **The MODS page (0.2).** CARD MODS rows in MAIN MENU > MODS (settings-ot 0.2's `ot_mod_rows`).
  In ot_emu:
  - a clash (SCENES P2 with MIDI SCENES) opens the swap dialog, and the swap works;
  - LO-FI AMF FIX is refused with its reason;
  - APPLY writes `active`, and the next start loads the new set.
- **Not built yet:** the skip key, `.armed`, the early-site list, `js/` parity for the descriptor's shadow, and DSP mods ([DSP.md](DSP.md)).

## 8. Building it

1. **The descriptor's records for the Octatrack:** sites, stock shadow,
   early ranges, closed tables (`elekloader/desc.py`, ABI 1.2), and the
   early table for 1.40C in `devices/octatrack.py`. Tests: a build's
   descriptor round-trips; the shadow equals the stock bytes.
2. **The early table, measured:** every routine of the 1.40C map,
   `--watch-pc` at its entry, through the first mount and on to the first
   frame. Then each routine is classed none, once or again. Repeat with a
   card holding no project, with an empty card, and with no card (the mount
   fails: TELOS skips).
3. **The shared link** (`dlink.c`, `json.c`) moved from drive-loader-ar2 to a
   folder both loaders build, with a device layer for sites, the stock
   shadow, the early list and the TELOS pool. `elekloader/drive.py` and
   `js/src/drive.ts` get the same rules (the twins must agree byte for byte).
4. **`telos-ot`** (the loader's flashed part): the `ev_card` hook, quiescence,
   the writes, `.armed`, `.status`, `.build`; the MODS rows in settings-ot.
5. **Tests in ot_emu:**
   - a card with three shop mods ticked boots and each one's sites are in
     place before the first frame;
   - a clashing pair loads the earlier id only, and `.status` says why;
   - `.armed` skips;
   - [NO] at power-on skips;
   - the page's dialogs (live script: keys);
   - a card change between boots changes the set.

## 9. Open, and decisions for you

- **[O] The TCB's saved-SP offset**, so quiescence scans only live stack. The
  task list and stacks are known ([LOADING.md](LOADING.md)). Settles: the scheduler's
  save (`trap #0` at 0x40000e46 and its `rte`).
- **[O] Restart to apply.** v1 asks for a power cycle. A restart routine (as
  on the Rytm and DN, masking interrupts and draining the panel link) is to
  find; settles: the OS upgrade's finish path in 1.40C.
- **[O] DSP payload sites.** LO-FI AMF FIX and SYNTH write DSP payload bytes
  in the OS image. If FX code is loaded into the DSPs on demand (octabam's
  `dlpump` notes say demand-loaded effects exist), a payload write before
  the project load might take effect. Until measured: `feature`.
- **[O] Table data sites' first read** (186 sites): watch reads after the
  RTOS handoff (ot_emu's `--watch-read` covers only the boot).
- **[O] The skip key at the mount:** the panel's key bitmap at that time.
- **Decision 1: the area's default size.** Recommend 1 MB. The whole
  compatible shop is about 61 KB plus the staging buffer, and 1 MB costs 171
  of the sample arena's 14,602 pages.
- **Decision 4: early sites that are redone later** (MIDI SCENES). Recommend
  "superseded" entries in the profile's early table, over a new mod field
  ([MEASURED.md](MEASURED.md), section 7).
- **Decision 5: publish the core-standard conversions** (QUANTIZER and SYNTH
  `-intercepts`) as octabam2elemod v1.2. Needed for TELOS and for any flashed
  build with intercepts.
- **Decision 2: apply by power cycle (v1)** or wait for a restart routine.
- **Decision 3: USB stays flashed.** USB is composed before the mount, so
  the USB mods (and usb-ot's family) are base-build only. The page shows
  them as `CAN'T LOAD FROM THE CARD / FLASH IT`.
