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
   dropped; a file larger than the staging buffer is refused; then the
   file's own rules, first failure wins: JSON, format (`elemod` 2, the id,
   the version), target (`target.device` and `target.os` against the
   descriptor's), the fields above, sections, regions, relocations,
   contributions, exports.
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
   refused.
3. *Layout* from the area's start: every `.run` in id order at its
   alignment (at least 4), then every `.bss` likewise, each block ending
   4-aligned. Past the area's end, the last living mod is refused and the
   check runs again.
4. *Pass 2*, in id order: `.run` decoded into place (a `stock` part is
   copied from the running OS), `.bss` zeroed, relocations applied (a
   symbol is the mod's own first, then a global name, then `core_zero` for
   a weak import). A `pc16` out of range refuses the mod and the check
   runs again.
5. *Merge*: each table that gets an entry is rewritten with the flashed
   entries and the new ones sorted by (order, id, then the flashed position
   or the contribution's index and entry), the entries' relocations applied
   at their final address, then zero entries to capacity + 1. Other tables
   are not touched.

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

## On the computer

`elekloader/drive.py` links the same way on the computer:
`elekloader check BUILD MODS...` (the build-checker) and `elekloader link
--device-layout BUILD MODS... --out DIR` (the boot linker's twin). BUILD
is a built `.syx` or main OS image, or the descriptor alone (`/mods/.build`,
as `elekloader mods get .build` saves it), which is enough to check but not
for the tables' flashed bytes. The twin writes `area.bin` (the area from its
start to the layout's end), `tables/NAME.bin` for each table it rewrites
(its capacity + 1 entries, the address in `summary.json`), `status.bin` and
`detail.bin` (the status records, "The boot link" above), `status.txt` and `summary.json`. The same
inputs give the same bytes.

`elekloader mods` moves files over USB MIDI (`elekloader/datarpc.py`; on
Windows through winmm, `elekloader/winmm.py`, as python-rtmidi drops SysEx
over 1 KB there; elsewhere with `mido` and `python-rtmidi`): `ls [FOLDER]`, `get NAME` (also `.build` and
`.status`), `put FILES... [--into FOLDER]` (`check` runs them against the unit's `.build` first;
nothing is uploaded if one is refused; the folders are made as needed), `rm NAME` (a file, or an empty
folder; the route refuses a folder with anything in it, `Folder not empty`) and `active NAMES...` (writes `/mods/active` when the set loads). A NAME is a path below
`/mods`, in up to 3 folders (`fx/delays/x.elemod`); the route lists a folder as an item whose
children byte is 1, as stock's `/soundbanks` lists its banks, and `Mods.walk` lists every folder. `--folder` names the route's
folder (`/mods`). The TypeScript engine has `check` (`js/src/drive.ts`),
the container and the same requests (`container.ts`, `datarpc.ts`, over any
port: Web MIDI on a page), with the same results and bytes
(`js/test/drive.test.ts`).

## +Drive storage

A +Drive mod's storage fields go in the same bytes as the flashed build's,
so saves, write-through, SysEx and library sounds carry them as they carry
flashed fields. The loader places them at boot; `elekloader/drive_store.py`
is that allocator on the computer, and `js/src/drivestore.ts` its port (the
same placements, `.store` bytes and notes: `js/test/drivestore.test.ts`).
Pass 1 still refuses `storage` until the loader runs it.

It reads only what the unit can read: each scope's room, used bits and
generation (the descriptor's storage record), the layout ID and ancestors
(`core_store_ids`), and the codes, CCs and inits the build gives its own
fields (`core_lock_codes`, `core_lock_ccs`, `core_field_inits`). The
descriptor alone (`/mods/.build`) is enough for `fit`, not for placing.

- **Pool**: a scope's bits from the build's used bits to its room. Fields
  go in from the room's top down, in (mod id, symbol) order (UTF-8 bytes),
  so a later flashed build that grows from its used bits meets them late.
- **Kept**: a field keeps its bits while its mod declares it with the same
  scope, width and init.
- **Parked**: a field no chosen mod declares keeps its bits and values; the
  core counts it as owned, and it gets its place back when its mod
  returns. `forget` (FORGET DATA) drops a parked mod's fields, codes and
  CCs; their bits are given out again only by a repack.
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
  no +Drive site goes where it would never run. The Analog Rytm MKII's
  list is empty until the boot-order trace fills it.
- **The +Drive area.** On a device whose profile has `drive_align` (the
  Analog Rytm MKII's is 4 KB), the +Drive mods' area is what the flashed
  build leaves of the core's linker area: from the build's RAM end (its
  last `.bss`, the Drive Loader's own included) rounded up to `drive_align`,
  to the top of that area (0x47FFF000 with core-ar2 0.11). A bigger flashed
  build leaves a smaller area; the linker's own RAM limit is the only cap.
- **Orders.** Each entry's order is kept whole, a signed 32-bit number,
  so a +Drive entry sorts against core-ar2 0.13's screen handlers (at
  -1,000,000 and 1,000,000) as the flashed build's linker sorted them. An
  order must be -2,147,483,648 to 2,147,483,646; the linker's own entries
  (storage, payloads, `.fast` copies) have 2,147,483,647.

Its bytes (ABI 1.2) are below, "The descriptor's bytes".

## The descriptor's bytes

The build descriptor ("The build descriptor" above), ABI 1.2, is big-endian, its records 4-byte aligned, and its
names offsets into its string pool (UTF-8, NUL-terminated):

| offset | field |
|---|---|
| +0x00 | `'ELKD'`, u16 ABI major, u16 minor, u32 size, u32 CRC-32 of the block with this word zeroed |
| +0x10 | u16 data family, u16 data device id, u32 OS release: the data container's (0 where the profile does not know them) |
| +0x18 | u32 string pool offset, u32 its size |
| +0x20 | u16 counts: modules, exports, tables, claims |
| +0x28 | u32 offsets: modules, exports, tables, claims, key arrays, area record, storage record |
| +0x44 | u32 the device key, u32 the OS version (names) |
| +0x4c | ABI 1.2: u32 offset of the site map, u16 its count, u16 the pre-link count, u32 offset of the pre-link ranges; zero to +0x60 |

| record | bytes | fields |
|---|---|---|
| module | 16 | name, version, u8 kind (0 core, 1 flashed mod, 2 a mod that imports `core_desc`), u8 flags (bit 0: has sites), u16 index, u16 first export, u16 export count. Index 0 is the core, then the mods by id |
| export | 8 | name, u32 address. Grouped by module in module order, by name within a module |
| table | 24 | name, u32 address, u16 entry size, u16 count, u16 capacity (count + spare), u16 flags (bit 0: more entries may be added; bit 1: a render table, the collection's `"render": true`; bit 2: a DSP table; bit 3: the loader may add entries of its own, a mod's may not), u32 offset of its keys, u32 offset of its orders (0 in ABI 1.0) |
| keys | 4 an entry | `order << 16 \| module index`, in table order, the order held to 0..65,534 (the linker's own entries 65,535) for an ABI 1.0 reader |
| claim | 8 | u16 kind (1: a resource name), u16 module index, u32 the name |
| area | 20 | the flashed build's RAM end, the +Drive area's start and end, the staging buffer's address and size (the exports `drive_staging` and `drive_staging_size`, else 0) |
| storage | 12 + 16 a scope | u32 address and u32 length of the lockfile's trailer in the main OS image (for the host: nothing on the unit reads it), u16 scope count, u16 0; per scope: name, u32 room in bits, u32 bits used, u32 generation |
| orders | 4 an entry | s32: each entry's order, in table order; after the storage record (ABI 1.1). An ABI 1.0 block has none, and its keys' orders stand |
| site map | 12 a range | u32 start, u32 end (exclusive), u16 kind (1 site, 2 copied block, 3 the device's protected or kept range, 4 boot copier, 5 the RAM image's copy, 6 lockfile trailer), u16 module index (0xFFFF: none); sorted by start, end, kind, module, and ranges may overlap; after the orders (ABI 1.2) |
| pre-link | 12 a range | u32 start, u32 end, u16 kind (1 code, 2 data), u16 0; after the site map (ABI 1.2) |

A loader accepts any minor of its major; a minor adds fields in reserved
space or records at the end, a major changes a record.
