# TELOS (Octatrack)

Mods from the CF card for the Octatrack (MKI and MKII, OS 1.40C), with core-ot 0.41 or newer.
A build flashed once with this mod links more mods at every boot from the card, with no new
firmware. Version 0.1 is a first working version, run in octabam's emulator only. The design
and what was measured are in [research/telos-ot](../../research/telos-ot/README.md).

## Using it

- **Files.** Copy the `.elemod` files to `/ELEKLOADER/MODS/` on the card (USB DISK MODE or a
  card reader), and list the ones to load in `/ELEKLOADER/MODS/active`, one line each: the file's
  name, a space, its CRC-32 (zlib's, 8 hex digits). A line without a CRC is skipped. A file
  changed since `active` was written is dropped, never loaded.
- **When.** At the card's first mount at boot (core-ot's `ev_card`, in the sys task), before the
  project load and the first audio frame. Changes apply at the next power-on.
- **What it wrote.** `/ELEKLOADER/MODS/status.txt`: how many loaded, then one line per line of
  `active`: the mod's id, `loaded`, `refused` or `dropped`, and the code and detail of a refusal
  (docs/FORMAT.md, "+Drive refusal codes", plus 21 `site`, 24 `stock`, 26 `busy` below).
- **Nothing to load.** No `active` (delete or rename it on a computer) loads nothing.

## How it works

- **The link** is the Rytm's boot linker (`mods/drive-loader-ar2/dlink.c` and `json.c`), built
  with `DL_SITES`, against the build descriptor (`core_desc`). A mod may write sites, as the shop's
  octabam conversions all do:
  - each site's bytes on the unit must still be stock's (its `stock_sha256`), so a site over
    anything the flashed build wrote is refused (24 `stock`), and so is a site inside the DSP
    payloads, which the OS has reused by the load point;
  - two mods' sites may not overlap: the later in id order goes (21 `site`);
  - a mod's stock parts are copied from the running OS, with the descriptor's stock shadow (the
    stock bytes of every range the flashed build wrote, `elekloader/desc.py`) laid over them.
- **The commit.** The sites and the tables' merge are written with interrupts masked, once no
  task's live stack holds an address strictly inside a site (the ten tasks' TCBs and stacks are
  fixed; a TCB keeps its SP at +72). Then the data cache is pushed and the instruction cache
  invalidated. After 50 tries without a quiet moment nothing is written (26 `busy`).
- **Memory.** `drive_area` (2 MB) holds the mods; `drive_staging` (256 KB) one file at a time;
  `telos_heap` (768 KB) the link's working memory. All three are `.bss` in core's reserve, so the
  build takes about 3 MB of sample memory more.
- **`memcpy`.** `telos.c` exports one: GCC calls it for `dlink.c`'s struct copies, and core-ot
  has none.

## On the unit: MAIN MENU > MODS (0.2)

With settings-ot 0.2 (required), MODS starts with CARD MODS: one row per `.elemod` file in
`/ELEKLOADER/MODS` (the FAT driver's opendir and readdir, 0x46c8240e and 0x46c8244e), by title,
then APPLY.
- **Each row** reads `<state> <title>`. The state is `ON`, `OFF`, `+ON` (ticked, loads at the
  next start), `-OFF` (unticked, still running) or `ERR` (refused at this start).
- **YES on a mod** unticks it, or ticks it after the boot's own checks (`dl_check`) on the set
  as it would be:
  - a clash with a ticked mod opens "CLASHES WITH <mod>", and YES swaps them;
  - any other refusal opens "CAN'T LOAD <mod>" with the reason.
- **APPLY** writes `active` (on the engine task) and says it applies at the next start.
- **The checks see the OS as it was before the link.** The menu's checks run with mods already
  running, so TELOS keeps what each site it wrote replaced (`dl_site_saving`), and the stock test
  reads that, the boot-time copy of the data and the shadow (`dl_stock_read`). A site over a
  range the flashed build wrote is refused outright (22 `flashed`).

## Not yet

The skip key, the crash flag (`.armed`) and the early-site list. DSP code goes through
`mods/dspload-ot` (`ot_dsp_load`), not through this loader's link: a TELOS mod hands its DSP words
to it from `drive_init` (research/telos-ot/dsptone).
