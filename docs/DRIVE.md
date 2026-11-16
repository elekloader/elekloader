# +Drive mods: the link and the tools

How the Analog Rytm MKII's Drive Loader (`mods/drive-loader-ar2/`) links
the +Drive mods `/mods/active` names at boot, and the computer's tools that
do the same. What a +Drive mod may hold, `/mods/active`'s lines, the refusal
codes and the data container are in [FORMAT.md](FORMAT.md), "+Drive mods";
the loader's firmware side is in its
[README](../mods/drive-loader-ar2/README.md).

## The boot link

The link runs in this order:
1. *Pass 1*, each line in order: a missing file or a different CRC is
   dropped; a file larger than the room it is read into (the +Drive area
   on a build whose descriptor has `F_STAGE_TOP`, else the staging buffer)
   is refused; then the
   file's own rules, first failure wins: JSON, format (`elemod` 2, the id,
   the version), target (`target.device` and `target.os` against the
   descriptor's), the fields above, sections, regions, relocations,
   contributions, exports, then storage fields (on a build that keeps
   +Drive storage: each a global name, as an export is; a field the core
   can't keep is a `format` refusal).
2. *Check rounds* over the living mods in id order (UTF-8 bytes; of two
   files with one id the earlier line counts first), again until a round
   refuses nothing; a mod refused earlier in a round stops counting.
   First failure wins: its id is a flashed module's or an earlier living
   mod's; `resources.core` is newer than module 0's version; a `requires`
   is neither flashed nor a living mod; a `conflicts` is flashed, or an
   earlier living mod names it or is named by it (of a pair, the later one
   goes); an export is a flashed name or an earlier living mod's; a
   resource name is claimed by the build or an earlier living mod; an
   import is none of the names above and not weak. Then each table in
   descriptor order: while its count plus the living mods' entries pass its
   capacity, the last living mod in id order with an entry there is
   refused. Then the storage room ("+Drive storage", `fit`), one refusal
   at a time (`store`), and the rounds again, until neither refuses.
3. *Layout* from the area's start: every `.run` in placement order at its
   alignment (at least 4), then every `.bss` likewise, each block ending
   4-aligned. Past the area's end, the last living mod in id order is
   refused and the check runs again. Then the living mods' storage fields
   are placed from `/mods/.store` ("+Drive storage"), which gives each
   field's symbol its descriptor. The Drive Loader stages files at the
   area's top (decided 2026-11-11; `dl_stage_top`, the descriptor's
   `F_STAGE_TOP`, `Build.stage_top`): pass 1 reads each file at the area's
   start and pass 2 just under its end, so the placement order is by file
   length, the longest first (ties in id order), and the layout also
   refuses when a mod's file, read there, would reach below the end of its
   own `.run`; the refusal's KB is the worse of the two overruns. Without
   the flag, the placement order is the id order.
4. *Pass 2*, in placement order: `.run` decoded into place (a `stock` part is
   copied from the running OS), `.bss` zeroed, relocations applied (a
   symbol is the mod's own first, then a global name, then `core_zero` for
   a weak import). A `pc16` out of range refuses the mod and the check
   runs again. With the stage at the top, every `.bss` and the bytes above
   it, to the highest a read reached, are zeroed after pass 2.
5. *Merge*: each table that gets an entry is rewritten with the flashed
   entries and the new ones sorted by (order, id, then the flashed position
   or the contribution's index and entry), the entries' relocations applied
   at their final address, then zero entries to capacity + 1. Other tables
   are not touched.
6. *Storage*: `core_store_ids` gets the live layout ID and its ancestors,
   `core_store_gens` each scope's live generation, and `core_lock_codes`,
   `core_lock_ccs`, `core_field_inits` and `core_handles` the +Drive fields'
   entries after their flashed ones. This happens on every boot that links,
   also when nothing loads (every field then parks) or no set is chosen
   but `/mods/.store` is there, and on a boot with [NO] held (no mods, the
   fields parked, `.store` not written); a boot the crash guard skips
   touches nothing. Then `/mods/.store` is written, as `.store.tmp` and a
   rename under the loader's mutex, when its bytes changed. `.status` has a
   line `store: id 0x..., N fields, M parked, kept|written|write failed`.
7. *Sites*: each loaded mod's sites, in id order, written over the running
   main OS (below, "Sites").

**Status records**: one of 32 bytes for each line of `/mods/active` (none
past 256 lines): u8 state, u8 code, u16 argument, then the id (the file
name until pass 1 reads the mod's id), NUL-padded to 28 bytes; and beside
them a detail string of 32 bytes each (a name the refusal is about,
NUL-terminated); their codes are FORMAT.md's, "+Drive refusal codes". The loader takes them from the heap at boot, sized to the
lines.

**The loader's room**: the link's working memory comes from the heap for
the link and goes back after it. Its pools are sized to the area, per
started MB of it (at least 1), each at most 65,520: 12,288 bytes of names
and 1,024 list items over all files (a refused file keeps its share of
these two), 1,024 global names past the flashed build's own, 1,024 symbols
in one file, 256 entries and 2,048 bytes of them with 256 relocations. A
merge has room for the biggest table +Drive entries can go to. Global names
are found through hash buckets, in the order a scan would find them.

## Sites

On a build whose descriptor has a site map (ABI 1.2), a +Drive mod may
change the main OS with `sites` and read it with `copied` blocks, as a
flashed mod does. The checks:

- *Pass 1*, after the exports, each file alone: every site's form (`addr`,
  `len`, `new` of that length, `kind` `code` or `data`, a 64-digit
  `stock_sha256`, relocations `abs32`, `pc32` or `pc16` inside it) and
  every copied block's (`lo` < `hi`, `to`), each inside the main OS (from
  its load address to the build's boot copier), else `format`; past the
  loader's room for them, `limit`. Then, refused `clash` (code 30): a site that
  overlaps a range of the site map (kind 1: the flashed build's sites,
  copied blocks, protected and kept ranges and appended code) or a
  pre-link range (kind 3), then a site whose bytes aren't its
  `stock_sha256` (kind 4), then a copied block over a flashed site (kind 1).
- *Check rounds*, after the imports: a site or copied block that shares a
  byte with an earlier living mod's site, or a site that shares one with an
  earlier mod's copied block (kind 2, the line the earlier mod's): the
  later in id order goes. Two copied blocks may overlap.
- *Pass 2*: each site's `new` bytes, its relocations applied at the site's
  own address (a `pc16` out of range is `range`).

A `clash` refusal's argument is its kind << 8, plus the earlier mod's line
for kind 2; its detail is the site's or block's address as 8 hex digits,
then for kind 1 the flashed owner's name when the site map names a module.

After the merge, the loader keeps each site's stock bytes (`dl_sites`, from
the heap; with no room, nothing loads: skip `memory`), then writes each
site with interrupts masked (SR 0x2700) and pushes the data cache's lines
over it; the cache flush after the link invalidates the instruction cache.
A later check of a set (the MODS page's) reads a site's stock bytes through
the ones kept, so the mods the boot loaded still pass. `/mods/.status`
lists every applied site (`site ID 0xADDRESS LENGTH`), and after a boot that
applied one the crash guard (`/mods/.armed`) stays on until 60 s have passed
and the MODS page doesn't show, whichever is later.

On the computer, `link --device-layout` writes each site's bytes to
`sites/ADDRESS.bin` and lists them in `summary.json`; the stock bytes are
checked only against a built image (a `.build` file alone has none).

## On the computer

`elekloader/drive.py` links the same way on the computer:
`elekloader check BUILD MODS...` (the build-checker) and `elekloader link
--device-layout BUILD MODS... --out DIR` (the boot linker's twin). BUILD
is a built `.syx` or main OS image, or the descriptor alone (`/mods/.build`,
as `elekloader mods get .build` saves it), which is enough to check but not
for the tables' flashed bytes. The twin writes `area.bin` (the area from its
start to the layout's end), `tables/NAME.bin` for each table it rewrites
(its capacity + 1 entries, the address in `summary.json`), `status.bin` and
`detail.bin` (the status records, "The boot link" above), `sites/ADDRESS.bin` (each site's
patched bytes, "Sites" above), `status.txt` and `summary.json`. The same
inputs give the same bytes.

`elekloader mods` moves files over USB MIDI (`elekloader/datarpc.py`; on
Windows through winmm, `elekloader/winmm.py`, as python-rtmidi drops SysEx
over 1 KB there; elsewhere with `mido` and `python-rtmidi`): `ls [FOLDER]`, `get NAME` (also `.build`,
`.status` and `.store`), `put FILES... [--into FOLDER]` (`check` runs them against the unit's `.build` first;
nothing is uploaded if one is refused; the folders are made as needed), `rm NAME` (a file, or an empty
folder; the route refuses a folder with anything in it, `Folder not empty`) and `active NAMES...` (writes `/mods/active` when the set loads). A NAME is a path below
`/mods`, in up to 3 folders (`fx/delays/x.elemod`); the route lists a folder as an item whose
children byte is 1, as stock's `/soundbanks` lists its banks, and `Mods.walk` lists every folder. `--folder` names the route's
folder (`/mods`). `room` prints the most bytes one upload or read takes on
the unit now: the `stage:` line of `/mods/.status`, the Drive Loader's live
stage ([STAGE.md](../mods/drive-loader-ar2/STAGE.md)), which changes with each boot's set; `put`
refuses a file over it before sending anything. Reads count the bytes they
get and never use the router's per-mille progress, which wraps past
4,294,967 bytes. The TypeScript engine has `check` (`js/src/drive.ts`),
the container and the same requests (`container.ts`, `datarpc.ts`, over any
port: Web MIDI on a page), with the same results and bytes
(`js/test/drive.test.ts`).

## +Drive storage

A +Drive mod's storage fields go in the same bytes as the flashed build's,
so saves, write-through, SysEx and library sounds carry them as they carry
flashed fields. The loader places them at boot; `elekloader/drive_store.py`
is that allocator on the computer, and `js/src/drivestore.ts` its port (the
same placements, `.store` bytes and notes: `js/test/drivestore.test.ts`).
The loader (`mods/drive-loader-ar2/dstore.c`) is its port to C, checked
against the same vectors (`tests/test_drive_store_host.py`, which also runs
random sets against `drive_store.py`), and `elekloader link --device-layout
--store FILE` runs it inside the boot link's twin (`store.bin`,
`store_gens.bin` and the five tables; `tests/test_drive_link_store.py`
compares the C link and the twin through a run of boots). A build keeps
+Drive storage when its descriptor has the six scopes, the five tables and
core 0.14's `core_store_gens`; elsewhere pass 1 refuses `storage` as before. A field the core can't keep (a
`"track"`, a `"mod"` kit field, FORMAT.md's "Mod storage" rules) is a `format`
refusal; a room refusal (`store`) has the detail `WHAT NEED HAVE`: a scope,
`codes` or a table (`core_lock_codes`, `core_field_inits`, `core_handles`),
what the set needs and what there is.
A `.store` past the loader's own room (64 KB, 512 fields or parked fields,
256 of each code and CC list) reads as none, as a bad one does.

It reads only what the unit can read: each scope's room, used bits and
generation (the descriptor's storage record), the layout ID and ancestors
(`core_store_ids`), the codes, CCs and inits the build gives its own
fields (`core_lock_codes`, `core_lock_ccs`, `core_field_inits`), and
whether the core exports `core_lfo_dsts`. The
descriptor alone (`/mods/.build`) is enough for `fit`, not for placing.

- **Pool**: a scope's bits from the build's used bits to its room. Fields
  go in from the room's top down, in (mod id, symbol) order (UTF-8 bytes),
  so a later flashed build that grows from its used bits meets them late.
- **Kept**: a field keeps its bits while its mod declares it with the same
  scope, width and init.
- **Parked**: a field no chosen mod declares keeps its bits and values; the
  core counts it as owned, and it gets its place back when its mod
  returns. `forget` (FORGET DATA on the MODS page: mods/drive-loader-ar2/PAGE.md,
  "Parked data") drops a parked mod's fields, codes and CCs; their bits are given out again only by a repack.
- **Repack**: when a scope's never-used bits (below its low mark) run out,
  or the flashed build repacked the scope, grew into a field or has less
  room, the scope gets a new generation, its parked fields are dropped and
  its fields are packed again from the top. Items saved before load the
  scope at defaults, flashed fields too. A flashed repack alone gives the
  scope the build's generation.
- **Codes**: from the range's top down (127, 126, ...), past the build's
  highest code; kept while the field keeps its place, kept for a parked
  field, and the one freed longest ago is given out once the codes above
  the build's are gone. A code the build has taken since is given up.
- **CCs**: from the device's pool, from its end, past the build's own; a
  kit field skips those stock maps on the FX channel. A full
  `core_lock_ccs` leaves a field on its NRPN only.
- **LFO destinations**: an `"lfo"` field is an LFO destination only where
  the core keeps the LFO's code in its own field of that scope
  (`core_lfo_dst` sound, `core_fx_lfo_dst` kit, 7 bits each). A build
  places one only for a flashed `"lfo"` field, so with a core that keeps
  them live (core-ar2 0.14's `core_lfo_dsts`) the allocator places the
  core's field as one more field of the core's id, only while a chosen mod
  has an `"lfo"` field of that scope and the build places none, and the
  loader writes its descriptor into `core_lfo_dsts`. It counts in the room,
  parks and comes back like a mod's field.
- **Room**: `fit` refuses the last mod in id order while a scope, the free
  codes, or `core_lock_codes`', `core_field_inits`' or `core_handles`'
  spare entries are short, and checks again.
- **Layout ID**: the flashed ID while `.store` holds nothing and no
  generation moved; otherwise the first 4 bytes of a SHA-256 of `ELKDRIVE`,
  the flashed ID, each scope's generation, every field (parked too: u8
  scope, u8 bits, u16 offset, u32 init, then the mod id and symbol, each a
  u8 length and its bytes, by scope then offset) and every code and freed
  code (u8 code, the two names, by code), 0 read as 1. A park moves no bit,
  so it keeps the ID. The ancestors are the last 15 live IDs, then the
  flashed ID and its ancestors, at most 15.

**A later build** keeps the +Drive's codes and CCs free when it is given the
unit's `.store` (`elekloader mods get .store`, then `elekloader.patch
--drive-store .store`; the route serves it read only). The lockfile keeps
what it read under `"drive"` (each scope's low mark, the codes and CCs the
+Drive's fields and parked fields hold), so later builds keep them free too
until another `.store` replaces it (`--drive-store none` drops it). A new
flashed code or CC skips the +Drive's, and a freed one is given out again
only when it is not one of them; only when nothing else is left does the
build take one, with a note, and the loader gives the +Drive field another.
Bits can't be kept that way, as the loader reads the build's used bits as
one mark: a build that grows past a scope's low mark, or repacks a scope the
+Drive keeps bits in, is placed as before, and a note names the scope the
loader repacks at its next boot.

**/mods/.store** (format 1, big-endian, records 4-aligned), rewritten only
when its bytes change; a missing, short or bad file (magic, format, size,
CRC) reads as none, a fresh placement:

| offset | |
|---|---|
| +0x00 | `DLST`, u16 format, u16 0, u32 size, u32 CRC-32 of the file with this word 0 |
| +0x10 | u32 the flashed layout ID it was written for, u32 the live layout ID |
| +0x18 | u8 next code, u8 ID count, u16 counts: fields, parked fields, codes, freed codes, CCs, freed CCs; u16 0 |
| +0x28 | per scope (sound, kit, global, track, pattern, project): u8 flashed generation, u8 live generation, u16 0, u32 low mark |
| then | the IDs, u32 each, newest first |
| then | fields (by scope, offset), then parked fields (oldest first): u8 scope, u8 bits, u16 offset, u32 init, u8 and u8 the names' lengths, u16 0, the names, zeros to 4 |
| then | codes (by code), freed codes (oldest first), CCs (by CC), freed CCs: u8 code or CC, u8 and u8 the names' lengths, u8 0, the names, zeros to 4 |

`js/tools/drivestore_vectors.py` writes the runs in
`js/test/drivestore-vectors.json` (placements, notes and `.store` bytes);
the loader's own host test can read them too.

## The build descriptor

A build that will link more mods on the unit (the +Drive mods loader)
needs to know what was flashed. When a mod imports `core_desc`, the
linker writes one block, the build descriptor, into the RAM image after
the tables, and defines `core_desc` as its address. A build without such
a mod is the same as before, byte for byte. `elekloader/desc.py` writes
and reads it; `python -m elekloader.desc dump <built .syx or main OS
image>` prints it.

- **Spare entries.** Each table gets spare zero entries after its zero
  entry, so a loader can merge more entries in at boot: 16, or the
  table's `"spare"` in the declaring mod's `collections`. A `"max"` there
  caps the table's entries in all: the spare ones are only what the
  build's own entries leave of it (none when they reach it), so
  core-ar2's `core_machines` (`"max": 10`) leaves the +Drive mods the
  hidden slots the flashed mods' machines don't take. Where the
  collection gives no `"spare"`, `core_lock_params` gets 32. The storage
  tables the linker fills, `core_store_ids` (16: a layout ID and 15
  ancestors), `core_lock_codes`, `core_lock_ccs` (32 each) and
  `core_field_inits` (32, at most 63 in all) and `core_handles` (32),
  are open to the loader alone (flag bit 3): it adds the +Drive mods'
  fields there, and a mod's own entries for them are refused.
  `core_payloads`, the `.fast` copy table and DSP tables get none.
  `NAME_n` stays the count of entries the build has. The spare entries
  and the block count toward the RAM budget.
- **The site map.** Every byte range of the flashed main OS that is
  taken: each site and copied block with its module, the device's
  protected and kept ranges, the boot copier, the RAM image's copy and
  the lockfile's trailer, so the loader can refuse a +Drive site that
  overlaps one.
- **Pre-link ranges.** Main OS code and data the OS is done with before
  the boot link runs (the release's `prelink` in its device profile), so
  no +Drive site goes where it would never run. The Analog Rytm MKII
  1.74's come from a Ghidra trace of its boot (main() up to the hook at
  0x401009e0, the reset path, the start task, the RTOS init, the boot
  project load and the static constructors).
- **The +Drive area.** On a device whose profile has `drive_align` (the
  Analog Rytm MKII's and the Analog Four MKII's are 4 KB), the +Drive mods' area is what the flashed
  build leaves of the core's linker area: from the build's RAM end (its
  last `.bss`, the Drive Loader's own included) rounded up to `drive_align`,
  to the top of that area (0x47FFF000 with core-ar2 0.11, 0x47C00000 with core-a4mk2 0.6). A bigger flashed
  build leaves a smaller area; the linker's own RAM limit is the only cap.
- **The stock shadow.** On a device whose profile has `drive_shadow` (the
  Digitone mk1, for TELOS), the descriptor carries the stock bytes of every
  range the flashed build writes (ABI 1.2), and a claim of kind 2 for each
  region a flashed mod claims. TELOS's linker (`dlink.c` built with
  `DL_SITES` and `DL_REGIONS`) then takes mods' sites and regions: a site
  must be stock's on the unit, not over the flashed build's or another
  mod's, and is written when no task is stopped in it; a region may not
  overlap a flashed or earlier mod's and must lie in a free area (FORMAT.md, codes 21 to 28). A mod's
  stock part over a flashed site copies the shadow's bytes. Such a loader
  takes no boot-linked sites (above), and the twins skip them on a build
  with the shadow. The twins
  (drive.py, js/src/drive.ts) check the same, leaving the stock hash to the
  unit when they have the descriptor alone.
- **Orders.** Each entry's order is kept whole, a signed 32-bit number,
  so a +Drive entry sorts against core-ar2 0.13's screen handlers (at
  -1,000,000 and 1,000,000) as the flashed build's linker sorted them. An
  order must be -2,147,483,648 to 2,147,483,646; the linker's own entries
  (storage, payloads, `.fast` copies) have 2,147,483,647.

Its bytes (ABI 1.3) are below, "The descriptor's bytes".

## The descriptor's bytes

The build descriptor ("The build descriptor" above), ABI 1.3, is big-endian, its records 4-byte aligned, and its
names offsets into its string pool (UTF-8, NUL-terminated):

| offset | field |
|---|---|
| +0x00 | `'ELKD'`, u16 ABI major, u16 minor, u32 size, u32 CRC-32 of the block with this word zeroed |
| +0x10 | u16 data family, u16 data device id, u32 OS release: the data container's (0 where the profile does not know them) |
| +0x18 | u32 string pool offset, u32 its size |
| +0x20 | u16 counts: modules, exports, tables, claims |
| +0x28 | u32 offsets: modules, exports, tables, claims, key arrays, area record, storage record |
| +0x44 | u32 the device key, u32 the OS version (names) |
| +0x4c | ABI 1.2: u32 offset of the site map, u16 its count, u16 the pre-link count, u32 offset of the pre-link ranges |
| +0x58 | u32 the shadow's offset, u32 its range count (ABI 1.2; 0 without one) |
| +0x60 | ABI 1.3: u32 flags, bit 0 `F_STAGE_TOP`: the loader reads each file into the +Drive area itself at boot (a module's export `drive_stage_top`, nonzero); zero to +0x64 |

| record | bytes | fields |
|---|---|---|
| module | 16 | name, version, u8 kind (0 core, 1 flashed mod, 2 a mod that imports `core_desc`), u8 flags (bit 0: has sites), u16 index, u16 first export, u16 export count. Index 0 is the core, then the mods by id |
| export | 8 | name, u32 address. Grouped by module in module order, by name within a module |
| table | 24 | name, u32 address, u16 entry size, u16 count, u16 capacity (count + spare), u16 flags (bit 0: more entries may be added; bit 1: a render table, the collection's `"render": true`; bit 2: a DSP table; bit 3: the loader may add entries of its own, a mod's may not), u32 offset of its keys, u32 offset of its orders (0 in ABI 1.0) |
| keys | 4 an entry | `order << 16 \| module index`, in table order, the order held to 0..65,534 (the linker's own entries 65,535) for an ABI 1.0 reader |
| claim | 8 | u16 kind (1: a resource name; 2: a region, `"<lo> <hi> <name>"` in hex), u16 module index, u32 the name |
| area | 20 | the flashed build's RAM end, the +Drive area's start and end, the staging buffer's address and size (the exports `drive_staging` and `drive_staging_size`, else 0) |
| storage | 12 + 16 a scope | u32 address and u32 length of the lockfile's trailer in the main OS image (for the host: nothing on the unit reads it), u16 scope count, u16 0; per scope: name, u32 room in bits, u32 bits used, u32 generation |
| shadow | 8 + bytes a range | u32 address, u32 length, the stock bytes padded to 4; by address, before the string pool (ABI 1.2, where the profile asks; a block without one is ABI 1.1) |
| orders | 4 an entry | s32: each entry's order, in table order; after the storage record (ABI 1.1). An ABI 1.0 block has none, and its keys' orders stand |
| site map | 12 a range | u32 start, u32 end (exclusive), u16 kind (1 site, 2 copied block, 3 the device's protected or kept range, 4 boot copier, 5 the RAM image's copy, 6 lockfile trailer), u16 module index (0xFFFF: none); sorted by start, end, kind, module, and ranges may overlap; after the orders (ABI 1.2) |
| pre-link | 12 a range | u32 start, u32 end, u16 kind (1 code, 2 data), u16 0; after the site map (ABI 1.2) |

A loader accepts any minor of its major; a minor adds fields in reserved
space or records at the end, a major changes a record.
