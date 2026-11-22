# +Drive mods: the link and the tools

How the Analog Rytm MKII's Drive Loader (`mods/drive-loader-ar2/`) links
the +Drive mods `/mods/active` names at boot, and the computer's tools that
do the same. What a +Drive mod may hold, `/mods/active`'s lines, the refusal
codes and the data container are in [FORMAT.md](FORMAT.md), "+Drive mods";
the loader's firmware side is in its
[README](../mods/drive-loader-ar2/README.md). The Analog Four MKII's loader
(`mods/drive-loader-a4mk2/`) links the same way over a file store of its own,
since the A4's +Drive has no file system; its
[README](../mods/drive-loader-a4mk2/README.md) has the store.

## The boot link

The link runs in this order:
1. *Pass 1*, each line in order: a missing file or a different CRC is
   dropped; a file larger than the room it is read into (the +Drive area
   on a build whose descriptor has `F_STAGE_TOP`, else the staging buffer)
   is refused; then the
   file's own rules, first failure wins: JSON, format (`elemod` 2, the id,
   the version), target (`target.device` and `target.os` against the
   descriptor's), the fields above, sections, regions, relocations,
   contributions (with `F_STD_MAP`, `events` is `"std1"` or none and a
   `"std": true` contribution names a standard event, which the
   descriptor's claims of kind 3 map to the core's table; one they don't
   map is a `std` refusal; with `F_COLLECTIONS`, a `to` the build lacks is
   kept for the check rounds), exports, then storage fields (on a build that keeps
   +Drive storage: each a global name, as an export is; a field the core
   can't keep is a `format` refusal), then, with `F_COLLECTIONS`, the
   tables it declares ("+Drive collections").
2. *Check rounds* over the living mods in id order (UTF-8 bytes; of two
   files with one id the earlier line counts first), again until a round
   refuses nothing; a mod refused earlier in a round stops counting.
   First failure wins: its id is a flashed module's or an earlier living
   mod's; `resources.core` is newer than module 0's version; a `requires`
   is neither flashed nor a living mod; a `conflicts` is flashed, or an
   earlier living mod names it or is named by it (of a pair, the later one
   goes); an export (a +Drive table's `NAME` and `NAME_n` too) is a flashed
   name or an earlier living mod's; a resource name is claimed by the build
   or an earlier living mod; an import is none of the names above and not
   weak; with `F_COLLECTIONS`, a contribution to a table the build lacks has
   no living mod declaring it, or its data is not whole entries of that
   table (`table`). Then each table in
   descriptor order: while its count plus the living mods' entries pass its
   capacity, the last living mod in id order with an entry there is
   refused. Then the storage room ("+Drive storage", `fit`), one refusal
   at a time (`store`), and the rounds again, until neither refuses.
3. *Layout* from the area's start: every `.run` in placement order at its
   alignment (at least 4), then every `.bss` likewise, each block ending
   4-aligned, then with `F_COLLECTIONS` each +Drive table. Past the area's end, the last living mod in id order is
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
   a weak import). A `pc16` or `abs16` out of range refuses the mod and the check
   runs again. With the stage at the top, every `.bss` and the bytes above
   it, to the highest a read reached, are zeroed after pass 2.
5. *Merge*: each table that gets an entry is rewritten with the flashed
   entries and the new ones sorted by (order, id, then the flashed position
   or the contribution's index and entry), the entries' relocations applied
   at their final address, then zero entries to capacity + 1. Other tables
   are not touched. A +Drive table is written the same way, with no
   flashed entries and one zero entry.
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

## Payloads on the +Drive

A loader whose descriptor has `F_PAYLOAD_SKIP` (drive-loader-a4mk2 and
drive-loader-ar2) keeps a mod's `.payload` on the +Drive: its bytes never take
room in the area. The `.elemod` stays one JSON file.

- **Skippable.** A `.payload` whose `"len"` and 64-digit `"sha256"` come
  before `"parts"`, and whose `"parts"` is one `["hex", "..."]` part, as the
  SDK's build writes it (the linker checks a `.payload`'s `sha256`). While the boot stages a file (`skip.c`,
  `dl_stage_file`), it follows the JSON a byte at a time; at that hex
  string's opening quote it notes the file offset, length and sha256, seeks
  past the hex and stages on from the closing quote, so the staged JSON has
  `""` there and the linker reads it unchanged. Reads turn to 64 bytes once
  the `.payload` object opens, so the hex passes through the stage only
  within a read, and is never kept.
- **Linked.** The linker takes a `.payload` left as one empty hex part and
  places nothing for it. Any other `.payload` is a `feature` refusal
  (`.payload`), and so is a relocation to `sec:.payload`, in `relocs` or an
  entry's (`relocs into .payload`): the payload is never in RAM. A symbol in
  `.payload` is no symbol to the loader, so a reference to it is an
  `import` refusal.
- **Staged length and CRC.** Everything that sizes or checks a file uses the
  staged file: the room it needs, the placement by length and the CRC
  `/mods/active` names (`drive.stage`, `drive.staged`, `active_text(...,
  build)`). A file with no skippable payload stages as it is.
- **The table.** After the link the boot keeps, for each loaded mod with a
  payload, {id (FNV-1a of the mod's id, as `core_payloads`), length, hex
  offset, the file's first sector and size, sha256, path} in
  `drive_payloads` (64 entries; more are counted in `drive_payloads_lost`),
  before the mods' `drive_init`. `drive_payload_find(id)` looks one up and
  `dl_payload_read(p, at, out, n)` decodes n bytes from byte at, under the
  loader's mutex, from a task only (drive-loader-ar2 0.12: one seek a call,
  the hex read into `out` and decoded in place, so a 4 KB buffer moves 2 KB
  a read at first, halving, and the last 64 bytes go through the stack);
  it refuses (-2) when the file's sector
  or size changed since the boot (on the Rytm's ekFS, which has no sector,
  the size alone: `sector` is 0), so a reader still checks the sha256 of
  what it ships. A file staged twice (pass 2) counts once, and after the
  link `drive_payloads_lost` counts only the loaded mods' lost payloads.
- **The API** (drive-loader-ar2 0.12, drive-loader-a4mk2 0.2;
  `analog-rytm-mk2/drivepay.h`, `analog-four-mk2/drivepay.h`, the same calls). The
  word `drive_payload_abi` (1) and `drive_payload_count`, `drive_payload_at`,
  `drive_payload_find`, `drive_payload_info` (id, length, sha256) and
  `dl_payload_read` on an opaque handle; `struct drive_payload` stays the
  loader's. sidecar-core imports them weakly, so `drive_payload_abi` reads
  0 without a loader, and a flashed or +Drive sidecar-core uses the same
  names. At the Pi's connect it walks `core_payloads` (a flashed mod's, in
  RAM), then entries 0..count-1, sends {id, length, sha256} for each and
  streams the ones the Pi lacks; caching on the Pi by hash is its side.
- **In use** (drive-loader-ar2 0.12, drive-loader-a4mk2 0.2). A file the boot loaded whose payload
  the table keeps can't be deleted or moved until the next boot: its route
  entry and listing have no Clear, a `.mv` of it or of a folder holding it
  is refused (`In use: restart first`), and the MODS page's DELETE refuses
  it (`IN USE: RESTART FIRST`). An upload over it is kept as `<name>.new`
  (the route's note `pending restart: <name>`); the next boot renames each
  `/mods/active` line's `<name>.new` over `<name>` before staging (the old
  file to `<name>.old` first). A line with an update takes the new file's
  CRC, and `/mods/active` is rewritten with it before the renames, so a
  power cut never leaves the new file under the old CRC: the next boot
  finishes the swap. A failed swap keeps the old file and the `.new` and
  puts the old CRC back; an `active.tmp` a cut left with no `active` goes
  in at the next boot.
  On the A4 the swap is one write of the store's directory (`ds_swap`), with no
  `.old`, and `/mods/active` takes the new CRCs before the swaps; a name below
  `/mods` with no room for `.new` in the A4 store's paths (over 118 bytes)
  refuses an upload over a file in use (`In use: restart first`). On the Rytm,
  stock's
  SAMPLES browser and Transfer can still change such a file:
  `dl_payload_read`'s -2 is the backstop; on the A4 nothing else reaches the
  store, so -2 follows only a FORMAT +DRIVE while mods run. `.status` lists `payloads: N
  kept, M lost`, `updates at boot: N applied, M failed` after a boot that
  had any, a `payload ID LEN PATH` line per entry and a `pending PATH` line
  per waiting update.

## +Drive collections

A loader whose descriptor has `F_COLLECTIONS` (drive-loader-a4mk2 and
drive-loader-ar2) links tables +Drive mods declare, so a flashed mod's
`collections` and the contributions to them load from the +Drive unchanged.
Flashed code never reads a +Drive table: its link is done at build.

- **Declaring.** Pass 1 reads each table of a mod's `collections`: a name
  of 1 to 59 bytes and an `"entry"` that is a multiple of 4 (at most
  65,535), else `format`. A DSP table (`"space"`) is a `feature` refusal
  (`collections`). `"spare"`, `"max"` and `"render"` size a flashed build's
  tables and are ignored here. `NAME` and `NAME_n` are the mod's global
  names, checked as its exports are, so of two mods declaring one name the
  later in id order goes (`export`), and so does one naming a flashed
  table, export or `NAME_n`.
- **Adding.** A contribution whose `to` the build lacks waits for the check
  rounds: there it goes to the first living mod in id order that declares
  that table, and is refused (`table`) when none does or its data is not
  whole entries; when a declarer is refused, its adders go in the next
  round, as a `requires` does. The order of `/mods/active` doesn't matter.
- **Placed.** The layout puts each living mod's tables after every `.bss`,
  in id order and the order the mod lists them: its living entries and a
  zero entry, 4-aligned, so the area's end counts them. `NAME` is its
  address and `NAME_n` its entries. The merge sorts them as it sorts a
  flashed table's, by (order, id, the contribution's index and entry), and
  applies their relocations there.
- **Room.** A +Drive table has no capacity: its entries are bounded by the
  entry bytes' pool, and the merge's room grows to fit them. The tables
  come from a pool of their own (64 per started MB of the area).

## Knob ids on the +Drive

A loader whose descriptor has `F_KNOBS` (drive-loader-a4mk2 and
drive-loader-ar2) gives +Drive mods knob ids by count, as the
linker gives flashed mods theirs (docs/ADAPTING.md, "Knob ids by count
(Analog Four MKII)"), so a flashed mod's `"knobs"` and its `__knobs`
relocations load from the +Drive unchanged. The flashed ids are below
0x4000; the +Drive's are 0x4000..0x7fff on both devices (the profile's
`drive_knobs`), so a reflash never moves them. The rules below are the
device profile's `param_ids`, which each loader's dl.h has as constants:
the A4's are given here, the Rytm's are the core's 469..554, own ids from
555 (0x22b), and four tables (`core_params`, and `core_param_locks`,
`core_knob_looks` and `core_knob_flags`, whose entries take own ids only).

- **The flashed ids.** The descriptor names the ids each flashed module's
  entries in those tables take, its run included, as
  claims of kind 4 (`"<first> <last>"`, hex).
- **Form.** `"knobs"` is 0..1024 and `__knobs` no mod's symbol; a relocation
  to `__knobs` is `abs32` or `abs16` (`abs16` on a build with `F_ABS16`)
  with an addend below `"knobs"` (else `format`). `__knobs` is the import of a mod with `"knobs"` only.
- **Fixed ids.** The ids a mod's entries name in its own `.run` bytes
  follow the linker's rules, in the check rounds after `claim`: none of the
  core's 0x127..0x1ff, a `core_knob_looks` entry's from 0x200 up, and from
  0x200 up none a flashed module's or an earlier living mod's in id order
  (`ids`, its detail the ids and whose).
- **Runs.** After the room, each living `"knobs"` mod gets its run as
  store.knobs gives a lockfile's: a run stays while its mod asks for as
  many and no fixed id is in it; a freed run waits for its mod until the
  never-used ids run out, then goes out again. A mod with no run left is
  refused (`ids`, `run of N: none free`), and the rounds start again.
- **Kept.** The runs, the freed runs (the latest 512) and the next id go into
  `/mods/.store` (format 3 on the A4, format 4 on the Rytm). A link that never settles keeps the old ones;
  FORGET DATA keeps them too.

## Sites

On a build whose descriptor has a site map (ABI 1.2), a +Drive mod may
change the main OS with `sites` and read it with `copied` blocks, as a
flashed mod does. The checks:

- *Pass 1*, after the exports, each file alone: every site's form (`addr`,
  `len`, `new` of that length, `kind` `code` or `data`, a 64-digit
  `stock_sha256`, relocations `abs32`, `pc32` or `pc16`, and `abs16` with
  `F_ABS16`, inside it) and
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
  own address (a `pc16` or `abs16` out of range is `range`).

A `clash` refusal's argument is its kind << 8, plus the earlier mod's line
for kind 2; its detail is the site's, block's or claim's address as 8 hex
digits, then for kind 1 the flashed owner's name when the site map names a
module.

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

## Claims

On a build whose descriptor has `F_CLAIMS` (both +Drive loaders,
`drive_claims`),
a +Drive mod's contribution may carry `claims`, `[[lo, hi], ...]`: byte
ranges of the main OS its entry relies on, which no other mod may change or
claim, as the build's linker checks them (link.py `check`). Nothing is
written for a claim. The site map then also lists each flashed
contribution's claims (kind 7, by its module). The checks are the sites':

- *Pass 1*, after the sites: each claim's form (two numbers, `lo` < `hi`,
  inside the main OS), else `format`; claims count toward the loader's room
  for sites (`limit`). Then refused `clash`: a claim over any range of the
  site map (kind 1, with the flashed owner's name for a site, copied block
  or claim) or a pre-link range (kind 3).
- *Check rounds*, with the sites: a claim that shares a byte with an
  earlier living mod's site or claim, or a site that shares one with an
  earlier mod's claim (kind 2): the later in id order goes. A claim and a
  copied block may overlap.

A +Drive site over a flashed claim is refused as one over a flashed site.
Without the flag (an older loader), `claims` is the feature refusal
`claims`.

## Regions on the A4

The A4's +Drive takes no `resources.regions`: it is the feature refusal
`regions`. A region is a fixed address that nothing zeroes, kept across the
boot link; since core-a4mk2
0.6 the A4's free areas sit inside the mods' own RAM, where the +Drive area
and the flashed mods' `.bss` are, so a region there would overlap memory
the link places and zeroes. A +Drive mod that needs RAM of its own uses
`.bss`.

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
nothing is uploaded if one is refused; the folders are made as needed; after each upload the
`last:` line of `/mods/.status` must read `saved NAME`, as the route can't refuse a commit that
fails, else it stops with that line and no `UPLOADED`), `rm NAME` (a file, or an empty
folder; the route refuses a folder with anything in it, `Folder not empty`) and `active NAMES...` (writes `/mods/active` when the set loads, checked against `last:` as an upload is; with no NAMES, a `#` line, as the A4's store keeps no empty file). A NAME is a path below
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
  "Saved data") drops a parked mod's fields, codes and CCs; their bits are given out again only by a repack.
- **Repack**: when a scope's never-used bits (below its low mark) run out,
  or the flashed build repacked the scope, grew into a field or has less
  room, the scope gets a new generation, its parked fields are dropped and
  its fields are packed again from the top. Items saved before load the
  scope at defaults, flashed fields too. A flashed repack alone gives the
  scope the build's generation.
- **Codes**: from the range's top down (127, 126, ...), past the build's
  highest code; kept while the field keeps its place, kept for a parked
  field, and the one freed longest ago is given out once the codes above
  the build's are gone. A code the build has taken since is given up. On
  the Analog Four MKII all of this is per track kind (0x70..0x7f each): a
  sound field's code is the synth tracks', a kit field's the FX track's, or
  the CV track's with `"track": "cv"`; a code freed in one kind is given out
  again only in that kind, and a field whose kind changes takes a new code.
- **CCs**: from the device's pool, from its end, past the build's own; a
  kit field skips those stock maps on the FX channel (on the A4, those the
  FX or CV tables map). A full `core_lock_ccs` leaves a field on its NRPN
  only.
- **LFO destinations**: an `"lfo"` field is an LFO destination only where
  the core keeps the LFO's code in its own field of that scope
  (`core_lfo_dst` sound, `core_fx_lfo_dst` kit, 7 bits each). A build
  places one only for a flashed `"lfo"` field, so with a core that keeps
  them live (core-ar2 0.14's `core_lfo_dsts`) the allocator places the
  core's field as one more field of the core's id, only while a chosen mod
  has an `"lfo"` field of that scope and the build places none, and the
  loader writes its descriptor into `core_lfo_dsts`. It counts in the room,
  parks and comes back like a mod's field. The A4 has none of this: its
  LFOs take `"mod"` fields, so the link refuses `"lfo"`, and a kit field
  may be `"mod"`.
- **Room**: `fit` refuses the last mod in id order while a scope, the free
  codes (on the A4 each kind's: `codes-synth`, `codes-fx`, `codes-cv`), or
  `core_lock_codes`', `core_field_inits`' or `core_handles`' spare entries
  are short, and checks again.
- **Layout ID**: the flashed ID while `.store` holds nothing and no
  generation moved; otherwise the first 4 bytes of a SHA-256 of `ELKDRIVE`,
  the flashed ID, each scope's generation, every field (parked too: u8
  scope, u8 bits, u16 offset, u32 init, then the mod id and symbol, each a
  u8 length and its bytes, by scope then offset) and every code and freed
  code (u8 code, the two names, by code; on the A4 by kind then code, each
  after its kind's number), 0 read as 1. A park moves no bit,
  so it keeps the ID. The ancestors are the last 15 live IDs, then the
  flashed ID and its ancestors, at most 15.

**A later build** keeps the +Drive's codes and CCs free when it is given the
unit's `.store` (`elekloader mods get .store`, then `elekloader.patch
--drive-store .store`; the route serves it read only). The lockfile keeps
what it read under `"drive"` (each scope's low mark, the codes and CCs the
+Drive's fields and parked fields hold; on the A4 the codes per track kind,
and a field skips only its own kind's), so later builds keep them free too
until another `.store` replaces it (`--drive-store none` drops it). A new
flashed code or CC skips the +Drive's, and a freed one is given out again
only when it is not one of them; only when nothing else is left does the
build take one, with a note, and the loader gives the +Drive field another.
Bits can't be kept that way, as the loader reads the build's used bits as
one mark: a build that grows past a scope's low mark, or repacks a scope the
+Drive keeps bits in, is placed as before, and a note names the scope the
loader repacks at its next boot.

**/mods/.store** (format 1, big-endian, records 4-aligned; format 2 or 3 on
the A4), rewritten only when its bytes change; a missing, short or bad file
(magic, format, size, CRC) reads as none, a fresh placement:

| offset | |
|---|---|
| +0x00 | `DLST`, u16 format, u16 0, u32 size, u32 CRC-32 of the file with this word 0 |
| +0x10 | u32 the flashed layout ID it was written for, u32 the live layout ID |
| +0x18 | u8 next code, u8 ID count, u16 counts: fields, parked fields, codes, freed codes, CCs, freed CCs; u16 0 |
| +0x28 | per scope (sound, kit, global, track, pattern, project): u8 flashed generation, u8 live generation, u16 0, u32 low mark |
| then | the IDs, u32 each, newest first |
| then | fields (by scope, offset), then parked fields (oldest first): u8 scope, u8 bits, u16 offset, u32 init, u8 and u8 the names' lengths, u16 0, the names, zeros to 4 |
| then | codes (by code), freed codes (oldest first), CCs (by CC), freed CCs: u8 code or CC, u8 and u8 the names' lengths, u8 0, the names, zeros to 4 |

Format 2 (the A4, codes per track kind) is format 1 with: +0x18 the synth
tracks' next code, +0x26 the FX track's and +0x27 the CV track's (format 1's
u16 0); each code and freed code record's last header byte its kind (1 synth,
2 FX, 3 CV); codes by kind, then code.

Format 3 (the A4 with +Drive knob ids, written only when there are any) is
format 2 with, after the CC records: u16 runs, u16 freed runs, u32 the next
knob id, then each run and freed run (oldest first): u32 first id, u16
count, u8 the mod's name length, u8 0, the name, zeros to 4. Not part of the
layout ID.

Format 4 (the Rytm with +Drive knob ids, drive-loader-ar2 0.8; written only
when there are any) is format 1 with the same runs after the CC records. A
loader before 0.8 reads a format 4 file as none: going back to one after a
boot that gave knob ids places +Drive storage afresh.

`js/tools/drivestore_vectors.py` writes the runs in
`js/test/drivestore-vectors.json` and, for the A4,
`js/test/drivestore-a4-vectors.json` (placements, notes and `.store`
bytes); each loader's host test replays its file
(tests/test_drive_store_host.py, tests/test_drive_a4_dstore_host.py).

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
  the lockfile's trailer, and with `F_CLAIMS` each contribution's claims
  with its module, so the loader can refuse a +Drive site or claim that
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
  A loader that exports `drive_stock_shadow` (the A4's, and the Rytm's from
  drive-loader-ar2 0.8) gets the same shadow
  for its stock parts alone, with flags bit 6 `F_STOCK_SHADOW`: the build
  matches each run of a mod's own code against the whole stock OS, so a
  part can name an address a flashed site wrote, where the running OS no
  longer holds stock's bytes. Its `dlink.c` lays the shadow over each stock
  part it copies (`shadow_over`), and the twins take the build for no TELOS:
  its sites keep the boot-linked sites' checks.
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
| +0x60 | ABI 1.3: u32 flags, bit 0 `F_STAGE_TOP`: the loader reads each file into the +Drive area itself at boot (a module's export `drive_stage_top`, nonzero); bit 1 `F_PAYLOAD_SKIP`: it stages each file without its `.payload`'s hex ("Payloads on the +Drive"; the export `drive_payload_skip`); bit 2 `F_ABS16`: its boot linker takes `abs16` relocations (the export `drive_abs16`); bit 3 `F_STD_MAP`: it maps std1 subscriptions through the core's `std` map, which the claims of kind 3 carry (the export `drive_std_map`); bit 4 `F_COLLECTIONS`: +Drive mods may declare tables ("+Drive collections"; the export `drive_collections`); bit 5 `F_CLAIMS`: its boot linker takes contributions' `claims`, and the site map lists the flashed ones ("Claims"; the export `drive_claims`); bit 6 `F_STOCK_SHADOW`: the descriptor carries the shadow for the loader's stock parts alone, not as TELOS's (the export `drive_stock_shadow`; "The stock shadow" above); bit 7 `F_KNOBS`: +Drive mods get knob ids, and the claims of kind 4 carry the flashed ones ("Knob ids on the +Drive"; the export `drive_knobs`); zero to +0x64 |

| record | bytes | fields |
|---|---|---|
| module | 16 | name, version, u8 kind (0 core, 1 flashed mod, 2 a mod that imports `core_desc`), u8 flags (bit 0: has sites), u16 index, u16 first export, u16 export count. Index 0 is the core, then the mods by id |
| export | 8 | name, u32 address. Grouped by module in module order, by name within a module |
| table | 24 | name, u32 address, u16 entry size, u16 count, u16 capacity (count + spare), u16 flags (bit 0: more entries may be added; bit 1: a render table, the collection's `"render": true`; bit 2: a DSP table; bit 3: the loader may add entries of its own, a mod's may not), u32 offset of its keys, u32 offset of its orders (0 in ABI 1.0) |
| keys | 4 an entry | `order << 16 \| module index`, in table order, the order held to 0..65,534 (the linker's own entries 65,535) for an ABI 1.0 reader |
| claim | 8 | u16 kind (1: a resource name; 2: a region, `"<lo> <hi> <name>"` in hex; 3, with `F_STD_MAP`: the core's `std` map, `"<event> <table>"`, one a standard event, by the core, in event order; 4, with `F_KNOBS`: the knob ids a module's entries take, `"<first> <last>"` in hex), u16 module index, u32 the name |
| area | 20 | the flashed build's RAM end, the +Drive area's start and end, the staging buffer's address and size (the exports `drive_staging` and `drive_staging_size`, else 0) |
| storage | 12 + 16 a scope | u32 address and u32 length of the lockfile's trailer in the main OS image (for the host: nothing on the unit reads it), u16 scope count, u16 0; per scope: name, u32 room in bits, u32 bits used, u32 generation |
| shadow | 8 + bytes a range | u32 address, u32 length, the stock bytes padded to 4; by address, before the string pool (ABI 1.2, where the profile asks; a block without one is ABI 1.1) |
| orders | 4 an entry | s32: each entry's order, in table order; after the storage record (ABI 1.1). An ABI 1.0 block has none, and its keys' orders stand |
| site map | 12 a range | u32 start, u32 end (exclusive), u16 kind (1 site, 2 copied block, 3 the device's protected or kept range, 4 boot copier, 5 the RAM image's copy, 6 lockfile trailer, 7 a contribution's claim, with `F_CLAIMS`), u16 module index (0xFFFF: none); sorted by start, end, kind, module, and ranges may overlap; after the orders (ABI 1.2) |
| pre-link | 12 a range | u32 start, u32 end, u16 kind (1 code, 2 data), u16 0; after the site map (ABI 1.2) |

A loader accepts any minor of its major; a minor adds fields in reserved
space or records at the end, a major changes a record.
