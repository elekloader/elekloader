# drive-loader (Analog Rytm MKII)

The +Drive mods loader's flashed part, for core-ar2 0.14 and newer on OS 1.74. It imports
`core_desc`, so the linker writes the build descriptor into every build that has it
(docs/DRIVE.md, "The build descriptor"). It exports its staging buffer's floor as
`drive_staging` and `drive_staging_size` (64 KB, its `.bss`; `DL_STAGING_SIZE` in `fs.h` and
the `.space` in `route.s`, which change together), which the descriptor's area record names,
and `drive_stage_top`, which sets the descriptor's `F_STAGE_TOP` (ABI 1.3). The boot reads
each file into the +Drive area itself, and after it the route and the MODS page use the
area's free tail (STAGE.md), so a mod file may be as large as the area.

It holds the `/mods` route, links the mods `/mods/active` names into the +Drive
mods' area at boot, and adds the MODS page in SETTINGS > GLOBAL, which writes `/mods/active`.
What a +Drive mod may hold, the refusal codes and skip reasons are docs/FORMAT.md's,
"+Drive mods"; the link's checks and order are docs/DRIVE.md's, "The boot link".
The files in `/mods` carry a 16-byte header (below, "Files on the +Drive"), and the MODS page
works like stock's SAMPLES browser (below, "The MODS page").

## The /mods route

Stock's Data router (the one Transfer and Elektroid use for `/projects` and `/soundbanks`) gets
a third `RouteTypeHandler`, so the unit's Data root listing shows `mods`:

| path | what |
|---|---|
| `/mods` | the folders and files in the +Drive's `/mods` folder (`DL_DIR` in `fs.h`), then `.build`, `.status` and `.store` |
| `/mods/<folder>`... | one folder's folders and files, to 3 folders below `/mods` (`DL_DEPTH`). A folder inside a third-level one (made over USB) is left out of its listing and off the MODS page, and no route reaches it or its files (`tests/drive_depth_host.c`, `tests/test_drive_route.py`) |
| `/mods/<name>`, `/mods/<folder>/.../<name>` | read, upload or Clear one file, in `/mods` or a folder up to 3 deep. Uploads take `*.elemod` (at any depth) and `active` (in `/mods` only); an upload makes the folders it needs. Other files can be read and cleared. A Clear of a folder removes it when it is empty, as SAMPLES' DELETE DIR |
| `/mods/<folder>/.../.dir` | upload only: an upload (of nothing, `elekloader mods mkdir`) makes the folder, and any parents not there yet, empty, up to 3 deep. No file is written, and a folder already there is kept |
| `/mods/.mv` | upload only: an upload of `FROM\nTO` (paths below `/mods`; `elekloader mods mv`) moves or renames a `.elemod` file or a folder with everything in it, and rewrites `/mods/active`'s lines naming FROM or a file below it. No file is written |
| `/mods/.build` | read only: the build descriptor, byte for byte (`python -m elekloader.desc dump`) |
| `/mods/.status` | read only: plain text, the route's last commit or delete, the MODS page's check (`check: n/N /mods/FOLDER`), the boot's time, then the boot linker's records |
| `/mods/.store` | read only, when there is one: docs/DRIVE.md, "+Drive storage" |

Each transfer goes through stock's data container (31-byte header, body, 12-byte footer with
CRC-32). An upload's header must say family 2, file type `0x454d4f44` (`'EMOD'`), not
compressed, and a payload no larger than the stage (STAGE.md). Anything else is refused at the header, and
nothing is written. The router checks the CRC at the footer, and only a transfer that passes
every check reaches the commit: the body goes to `<name>.tmp`, the old file is deleted, and the
tmp file is renamed over it.

### Files on the +Drive

The files live on the +Drive's sample file system (ekFS), and stock's SAMPLES browser lists
every folder there, `/mods` too: its root is ekFS's `/`, and its directory scan
(`Directory`, vtable `0x401d7cbc`, filled by `0x4007e950` through readdir `0x4007c0b4`) keeps
every entry, so no name or node flag hides a folder (a `/.mods` shows like any other). LOAD TO
PROJECT reads a file's first 16 bytes as FsSample's own header. A raw `.elemod` there hangs
stock, and a project that holds one stalls the next OS upgrade in PREPARING until a CREATE NEW.
So every file the loader writes starts with 16 zero bytes, which stock loads as an
empty sample. `fs.c` writes the
header at each open for writing and skips it at each open for reading; stock's node size
(`0x4007d9ec`, the length less 16) is then the data's size. A file without the header doesn't
open. The header is the loader's storage only: the route, the
`.elemod` files on the computer and `/mods/active`'s CRCs are of the data after it.

A build for a unit test run sets `-DDL_TEST_DIR` (mod.json `cflags`), which makes the folder
`/~T MODS`. The test rules allow only `~T` items.
A unit test build may also set `-DDL_TEST_ARMED`, which lets the route take an upload of `.armed` too, so a
test can plant the crash flag (`elekloader mods`' clients do this on purpose only). Without it, the name is
refused like any other dot file. Neither flag is set by default.

### The stage

The boot reads files into the +Drive area, and the route and the MODS page use its free tail
after the link: [STAGE.md](STAGE.md).

### How it hooks stock

- One site, `0x400a005e`, in the resolver's getter (`0x4009febc`): after stock adds its two
  handlers, `dl_route_site` sets the getter's guard (the instruction it displaces) and adds the
  loader's handler with stock's `0x4002da4a`, which takes a `shared_ptr` to it (one with no control block: the handler is never freed). That calls the handler's `register_route`, which
  adds one pattern a depth to each table: `/mods` to `/mods/*/*/*` to the list table and
  `/mods/*` to `/mods/*/*/*/*` to the entry table. Stock's matcher (`0x4002f29c`) takes only a
  path of its pattern's segment count, each `*` one segment, so `/mods/*` never matches
  `/mods/a/b`; `/soundbanks` registers its depths the same way (`0x40030708`). The route joins
  the captures into the path below `/mods`. A path is at most 127 bytes (`DL_NAME_MAX`), a
  folder or file name at most 63 (`DL_SEG_MAX`), no folder name starts with `.`, and no
  folder or file name starts with a space (`/mods/active`'s reader trims a
  line's leading blanks, so such a name would save and not load; the MODS page doesn't list one).
- `/mods/*` resolves to a RAM entry (kind 1) over the stage. Its functions do the rest:
  `+0x1c` refuses with "busy" while a loader part holds the buffer (`dl_staging_take`), and an
  upload over the stage (STAGE.md); `+0x2c`
  fills the buffer from the file once a read stream exists; `+0x6c` checks the container's
  header; `+0x4c` is stock's after-change call. WriteClose calls it with the header after a
  successful upload, which is when the route commits. Clear calls it with no header, which is
  when the route deletes the file.
- A path that names a folder resolves for a Clear only: an empty folder gets an entry whose
  after-change call removes it with stock's rmdir. That call can't report a failure, so a folder
  with anything in it is refused when it resolves (`Folder not empty`), and `elekloader mods rm`
  exits with that refusal.
- The router has no request for a folder, so `FOLDER/.dir` (`DL_MKDIR`, names.inc's `mkdir_of`)
  resolves to a write-only entry whose after-change call makes each missing level of FOLDER with
  stock's mkdir (route.c's `make_folder`). It can't report a failure either, so a file of the
  folder's name is refused when it resolves (`A file has that name`), and the clients read the
  parent's listing afterwards to see the folder there (`Mods.mkdir`; `.status` notes `made` or
  `mkdir failed:`). The `.dir` pattern is the entry table's own (`/mods/*/*/*/*` for a third-level
  folder), so no pattern is added and a fourth level stays out of reach.
- The route can't tell a read, an upload and a Clear apart when it resolves a path, so the
  entry carries every function, each closure holding the file's name.
- Stock's Rename refuses RAM entries, so the router's own rename doesn't reach `/mods`: a move or
  rename is an upload of `FROM\nTO` to `.mv` (`DL_MOVE`), up to 256 bytes, whose after-change call
  does it (route.c's `move`). It checks the names (names.inc's `move_why`: plain names, a file
  stays a `.elemod` in a folder up to 3 deep; a folder with its deepest folder within 3 and its
  longest path within 127 bytes, never into itself), that FROM is there, TO isn't (no overwrite)
  and TO's folder is (none is made), and that folder's entry count (stock's rename skips ekFS's
  2000). Stock's rename (`0x4007bcd2`, a link then an unlink) moves a file to any folder but a
  folder only within its parent, so a folder going to another parent is made there with mkdir, its
  contents moved in one by one and the emptied folder removed, as SAMPLES' MOVE moves each item
  (`0x400ea92a`); a failure part way leaves what moved where it went. Then `/mods/active`'s lines
  follow (`keep_active`, written as an upload's commit is), and the MODS page renames its own
  files and open folder at its next tick (`dl_move_from`, `dl_move_to`); a next move waits for it
  (`Busy: the MODS page`). The page also reads its folder again after any change the route makes
  (`dl_fs_gen`: uploads, deletes, new folders, DELETE DIR), and goes up while its folder is gone.
  The after-change call can't report a refusal: `.status` notes it (`move: ...`), and the clients
  read both listings afterwards (`Mods.move`). A new upload under the same name replaces the
  file.
- One transfer at a time per Data handler: stock drops an open transfer when the next one
  opens, and a dropped upload never commits.
- Calls come from the SysEx receive tasks. The loader's mutex (`dl_lock`) covers the staging
  buffer and each commit, delete and fill. ekFS takes its own mutex inside each call.

### Firmware calls

All addresses are OS 1.74's. They were read in Ghidra.

| what | address |
|---|---|
| resolver, its getter's guard | `0x419438cc`, `0x419438c8` |
| add a handler | `0x4002da4a` |
| pattern compile (a route's entry is 0x1c bytes: the pattern's segments, a std::function; the route grows a table itself) | `0x4002f1d0` |
| std::string ctor, dtor, the empty string | `0x401bcff0`, `0x401bb540`, `0x419f2a0c` |
| operator new, delete | `0x40082a40`, `0x40082a4c` |
| ekFS file ctor (0x2c bytes; 0 read, 1 write), dtor, read, write, node, node size (less 16, for FsSample's own header) | `0x4007dcfa`, `0x4007d956`, `0x4007d822`, `0x4007d860`, `0x4007d9d2`, `0x4007d9ec` |
| ekFS delete (files only), rename (the bool wrapper of `0x4007bcd2`: a file to any folder, a folder within its parent; never over a name there), rmdir (an empty folder only; `0x4007daa4` is its bool wrapper), mkdir (one folder, its parent there; called by SAMPLES' CREATE DIR, `0x400ea5b6`) | `0x4007da78`, `0x4007da8c`, `0x4007bef8`, `0x4007c1dc` |
| Directory ctor (0x130 bytes), dtor, count, child, shared_ptr release | `0x4007ed48`, `0x4007ee02`, `0x4007e1bc`, `0x4007e794`, `0x40170bd8` |
| mutex lock, unlock | `0x400010bc`, `0x400011ee` |

## Linking at boot

`dl_boot` (`boot.c`) runs in core's `ev_boot_drive` (core-ar2's README, "+Drive storage at
boot"): ekFS is mounted and the audio start and the UI task haven't run, so nothing reads the
tables the link rewrites.

- **Skip.** Nothing links when the eMMC (`0x419ae2c0`) or ekFS (`0x419ae2bc`) didn't come up;
  when [NO] is held (the panel query `61 01`, 20 ms, the key bitmap `0x418d1340` through
  `0x40081c28`, then `60 00` again); or when `/mods/.armed` is there, which it deletes.
- **The link** (`dlink.c`, with the JSON reader `json.c`) reads one file at a time into the
  +Drive area (STAGE.md) and works from `core_desc` only: pass 1, the checks in id order, the layout,
  pass 2 and the tables' merge. A pass-2 refusal starts the checks again without that mod. It
  writes the status records `/mods/.status` shows.
- **Limits and memory.** Up to 256 lines of `/mods/active` (`DL_MAX_MODS`); past that nothing
  loads (`count`). Nothing the boot keeps per line is in `.bss`: it reads `/mods/active` whole
  into the heap (stock's operator new, `0x40082a40`, set up and in use by `main()`'s static
  constructors before the site), counts its lines, and takes the set, the status records, the
  detail strings and the init list from the heap for that many. The link's working memory (the
  mods, the names, the symbols and their hash buckets, the entries) is also taken from the
  heap, sized to the +Drive area (docs/DRIVE.md, "The boot link": about 270 KB for the Rytm's
  4.3 MB area) and given back when the link returns; the MODS page's checks do the same. When
  the heap has no room the boot skips (`memory`, `NO MEMORY`).
- **The area** is what the flashed build leaves of core 0.11's: from the build's RAM end (this
  loader's own `.bss`, the 64 KB floor, included) rounded up to 4 KB, to 0x47FFF000.
  The descriptor's area record names it; the loader reads it from there.
- **Lookups.** Global names (the build's exports, the tables, `NAME_n` and the mods' exports)
  are found through hash buckets built after pass 1, each chain in the order the names were
  added, so a lookup finds what a scan in order would and the boot stays near linear in the
  number of mods.
- **The cache.** `dl_flush` pushes every data-cache line by set and way (the V4's CPUSHL takes
  an index, not an address), then sets CACR to `0xA40CE100`: stock's value without DCINVA.
- **The crash guard.** With a set chosen, it writes `/mods/.armed` before the link, so a link
  that faults or hangs is caught, and inits. When the link loads nothing it
  deletes `.armed` at once; otherwise it calls each `drive_init` in id order and deletes
  `.armed` 200 `ev_tick`s (10 s; sites: DRIVE.md) on.
- **The time.** `dl_boot` reads DTIM0's count (`0xfc07000c`, free running from the 132 MHz bus
  clock; stock's own clock at `0x401006bc` reads it) as it starts, after the checks, after the
  link and the flush, and as it ends. `/mods/.status` shows it as `boot: N us (checks N, link N, inits N)`
  (the skip key's query alone is 20 ms of the checks); a part the boot didn't reach is 0.

`tests/drive_link_host.c` builds the same linker for the computer, and
`tests/test_drive_link.py` checks it on a made-up Rytm build, and checks that the MODS page's
`dl_check` gives the same verdicts without writing RAM.

| what | address |
|---|---|
| panel lock, send, unlock; sleep (µs); key held; DTIM0's count | `0x40001bf6`, `0x400019e4`, `0x40001c06`; `0x40098c84`; `0x40081c28`; `0xfc07000c` |

## The MODS page

A MODS row in SETTINGS > GLOBAL opens the page (`page.c`, `page.s`): stock's SAMPLES-style
two-pane browser on the `.elemod` files in `/mods` and its folders, whose ticked files are the
build. LOAD TO BUILD saves them to `/mods/active` only when the boot's own checks (`dl_check`)
pass them; the build list shows what the next boot keeps, adds and drops. Its keys, messages,
background checks and firmware calls are in [PAGE.md](PAGE.md).

## Files and tests

- `route.c`, `route.s`, `names.inc` (the name rules), `fs.c` (ekFS calls and the mutex): the
  route. Tests: `tests/drive_route_names_host.c`, `tests/drive_depth_host.c` (the 3-deep
  limit), `tests/drive_move_host.c` (moves on a made-up ekFS, the stage in a guarded arena),
  `tests/test_drive_route.py`.
- `flush.s`, `boot.c`, `dlink.c`, `json.c`, `sha256.c`: the boot link. Tests: `tests/drive_link_host.c`,
  `tests/test_drive_link.py`, `tests/test_drive_top_stage.py`.
- `page.c`, `page.s`, `clip.inc` (the build list's label clip), `park.c` (parked data): the
  MODS page. Tests: `tests/drive_page_clip_host.c`, `tests/drive_build_rows_host.c` (the build
  list after folder changes and moves over USB, parked data against `elekloader/drive_store.py`),
  `tests/drive_check_tick_host.c`, `tests/drive_reader_host.c` (a file over the stage),
  `tests/test_drive_page.py`.

`tools/modsrpc.py` is a small Data RPC client (list, get, put, rm) for checking the route in an
emulator through its `--usb-midi-tcp` port.

```bash
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build mods/drive-loader-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx --mod core-0.14.elemod --mod drive-loader-0.6.elemod --out drive-loader.syx --version 1.74
```
