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

## The descriptor's bytes

The build descriptor (FORMAT.md, "The build descriptor"), ABI 1.0, is big-endian, its records 4-byte aligned, and its
names offsets into its string pool (UTF-8, NUL-terminated):

| offset | field |
|---|---|
| +0x00 | `'ELKD'`, u16 ABI major, u16 minor, u32 size, u32 CRC-32 of the block with this word zeroed |
| +0x10 | u16 data family, u16 data device id, u32 OS release: the data container's (0 where the profile does not know them) |
| +0x18 | u32 string pool offset, u32 its size |
| +0x20 | u16 counts: modules, exports, tables, claims |
| +0x28 | u32 offsets: modules, exports, tables, claims, key arrays, area record, storage record |
| +0x44 | u32 the device key, u32 the OS version (names); zero to +0x60 |

| record | bytes | fields |
|---|---|---|
| module | 16 | name, version, u8 kind (0 core, 1 flashed mod, 2 a mod that imports `core_desc`), u8 flags (bit 0: has sites), u16 index, u16 first export, u16 export count. Index 0 is the core, then the mods by id |
| export | 8 | name, u32 address. Grouped by module in module order, by name within a module |
| table | 24 | name, u32 address, u16 entry size, u16 count, u16 capacity (count + spare), u16 flags (bit 0: more entries may be added; bit 1: a render table, the collection's `"render": true`; bit 2: a DSP table), u32 offset of its keys, u32 0 |
| keys | 4 an entry | `order << 16 \| module index`, in table order |
| claim | 8 | u16 kind (1: a resource name), u16 module index, u32 the name |
| area | 20 | the flashed build's RAM end, the +Drive area's start and end, the staging buffer's address and size (the exports `drive_staging` and `drive_staging_size`, else 0) |
| storage | 12 + 16 a scope | u32 address and u32 length of the lockfile's trailer in the main OS image (for the host: nothing on the unit reads it), u16 scope count, u16 0; per scope: name, u32 room in bits, u32 bits used, u32 generation |

A loader accepts any minor of its major; a minor adds fields in reserved
space or records at the end, a major changes a record.
