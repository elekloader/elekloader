# drive-loader (Analog Rytm MKII)

The +Drive mods loader's flashed part, for core-ar2 0.11 and newer on OS 1.74. It imports
`core_desc`, so the linker writes the build descriptor into every build that has it
(docs/FORMAT.md, "The build descriptor"), and it exports the staging buffer as `drive_staging`
and `drive_staging_size` (512 KB, its `.bss`), which the descriptor's area record names.

From 0.1 it holds the `/mods` route, links the mods `/mods/active` names into the +Drive
mods' area at boot, and adds the MODS page in SETTINGS > GLOBAL, which writes `/mods/active`.
The rules (what a +Drive mod may hold, the checks, the order, the refusal
codes and skip reasons) are docs/FORMAT.md's, "+Drive mods".
The files in `/mods` carry a 16-byte header (below, "Files on the +Drive"), and the MODS page
works like stock's SAMPLES browser (below, "The MODS page").

## The /mods route

Stock's Data router (the one Transfer and Elektroid use for `/projects` and `/soundbanks`) gets
a third `RouteTypeHandler`, so the unit's Data root listing shows `mods`:

| path | what |
|---|---|
| `/mods` | the files in the +Drive's `/mods` folder (`DL_DIR` in `fs.h`), then `.build` and `.status` |
| `/mods/<name>` | read, upload or Clear one file. Uploads take `*.elemod` and `active` only. Other files there can be read and cleared |
| `/mods/.build` | read only: the build descriptor, byte for byte (`python -m elekloader.desc dump`) |
| `/mods/.status` | read only: plain text, the route's last commit or delete, the boot's time, then the boot linker's records |

Each transfer goes through stock's data container (31-byte header, body, 12-byte footer with
CRC-32). An upload's header must say family 2, file type `0x454d4f44` (`'EMOD'`), not
compressed, and a payload of at most 512 KB. Anything else is refused at the header, and
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
empty sample (checked in the emulator on stock 1.74 with remote keys). `fs.c` writes the
header at each open for writing and skips it at each open for reading; stock's node size
(`0x4007d9ec`, the length less 16) is then the data's size. A file without the header doesn't
open. The header is the loader's storage only: the route, the
`.elemod` files on the computer and `/mods/active`'s CRCs are of the data after it.

A build for a unit test run sets `-DDL_TEST_DIR` (mod.json `cflags`), which makes the folder
`/~T MODS`. The test rules allow only `~T` items.
A unit test build may also set `-DDL_TEST_ARMED`, which lets the route take an upload of `.armed` too, so a
test can plant the crash flag (`elekloader mods`' clients do this on purpose only). Without it, the name is
refused like any other dot file. Neither flag is set by default.

### How it hooks stock

- One site, `0x400a005e`, in the resolver's getter (`0x4009febc`): after stock adds its two
  handlers, `dl_route_site` sets the getter's guard (the instruction it displaces) and adds the
  loader's handler with stock's `0x4002da4a`, which takes a `shared_ptr` to it (one with no control block: the handler is never freed). That calls the handler's `register_route`, which
  adds `/mods` to the list table and `/mods/*` to the entry table.
- `/mods/*` resolves to a RAM entry (kind 1) over the staging buffer. Its functions do the rest:
  `+0x1c` refuses with "busy" while a loader part holds the buffer (`dl_staging_take`); `+0x2c`
  fills the buffer from the file once a read stream exists; `+0x6c` checks the container's
  header; `+0x4c` is stock's after-change call. WriteClose calls it with the header after a
  successful upload, which is when the route commits. Clear calls it with no header, which is
  when the route deletes the file.
- The route can't tell a read, an upload and a Clear apart when it resolves a path, so the
  entry carries every function, each closure holding the file's name.
- Stock's Rename refuses RAM entries, so `/mods` has no rename. A new upload under the same
  name replaces the file.
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
| ekFS delete, rename | `0x4007da78`, `0x4007da8c` |
| Directory ctor (0x130 bytes), dtor, count, child, shared_ptr release | `0x4007ed48`, `0x4007ee02`, `0x4007e1bc`, `0x4007e794`, `0x40170bd8` |
| mutex lock, unlock | `0x400010bc`, `0x400011ee` |

## Linking at boot

One more site, `0x401009e0` in `main()` (`0x401006ee`), stock `jsr 0x4009da0e`: `drive_boot`
(`flush.s`) makes that call, then runs `dl_boot` (`boot.c`). There ekFS is mounted and the audio
start and the UI task haven't run, so nothing reads the tables the link rewrites.

- **Skip.** Nothing links when the eMMC (`0x419ae2c0`) or ekFS (`0x419ae2bc`) didn't come up;
  when [NO] is held (the panel query `61 01`, 20 ms, the key bitmap `0x418d1340` through
  `0x40081c28`, then `60 00` again); or when `/mods/.armed` is there, which it deletes.
- **The link** (`dlink.c`, with the JSON reader `json.c`) reads one file at a time into the
  staging buffer and works from `core_desc` only: pass 1, the checks in id order, the layout,
  pass 2 and the tables' merge. A pass-2 refusal starts the checks again without that mod. It
  writes the status records `/mods/.status` shows.
- **Limits and memory.** Up to 256 lines of `/mods/active` (`DL_MAX_MODS`); past that nothing
  loads (`count`). Nothing the boot keeps per line is in `.bss`: it reads `/mods/active` whole
  into the heap (stock's operator new, `0x40082a40`, set up and in use by `main()`'s static
  constructors before the site), counts its lines, and takes the set, the status records, the
  detail strings and the init list from the heap for that many. The link's working memory (the
  mods, the names, the symbols and their hash buckets, the entries) is also taken from the
  heap, sized to the +Drive area (docs/FORMAT.md, "+Drive mods": about 270 KB for the Rytm's
  4.3 MB area) and given back when the link returns; the MODS page's checks do the same. When
  the heap has no room the boot skips (`memory`, `NO MEMORY`).
- **The area** is what the flashed build leaves of core 0.11's: from the build's RAM end (this
  loader's own `.bss`, the 512 KB staging buffer, included) rounded up to 4 KB, to 0x47FFF000.
  The descriptor's area record names it; the loader reads it from there.
- **Lookups.** Global names (the build's exports, the tables, `NAME_n` and the mods' exports)
  are found through hash buckets built after pass 1, each chain in the order the names were
  added, so a lookup finds what a scan in order would and the boot stays near linear in the
  number of mods.
- **The cache.** `dl_flush` pushes every data-cache line by set and way (the V4's CPUSHL takes
  an index, not an address), then sets CACR to `0xA40CE100`: stock's value without DCINVA.
- **The crash guard.** With a mod loaded, it writes `/mods/.armed`, calls each `drive_init` in
  id order, and deletes `.armed` 200 `ev_tick`s (about 10 s) later.
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

A MODS row at the bottom of SETTINGS > GLOBAL (`ev_settings`, `core_additem`) opens a page
(`page.c`, `page.s`) on the `.elemod` files in the folder. The MODS row has a 9 x 9
lightning-bolt icon like the other submenu rows: its draw callback (`dl_mods_icon`, `page.s`) is
stock's icon thunk, tail-calling the bitmap blit `0x400716e0` with a Bitmap of stock's layout
(one long a column, top row the lowest bit, a full mask).

The page is stock's two-pane file view, the one SAMPLES and PROJECTS are built on
(`0x400734f2`), and works like SAMPLES:

- **The file list.** One row a file: its id (its file name until one is known) and, on the
  right where SAMPLES puts sizes, its size in whole KB, or `!` for a file pass 1 refuses (a bad
  file, another device, a newer core, over 512 KB). The ticks start as the saved build on entering MODS
  and after each change; coming back from the build list keeps the ticks as they were.
- **Ticks are the build.** [YES] ticks or unticks a row and steps down, as in SAMPLES. RIGHT
  opens the action panel: SELECT ALL, DESELECT ALL, LOAD TO BUILD (UPDATE BUILD once the build
  holds mods), CLEAR BUILD, DELETE. It asks `LOAD n MODS?` (`UPDATE BUILD? (n MODS)`) over
  `CONFIRM Y/N` in stock's confirm window (with nothing ticked it says
  `NO MODS SELECTED` and changes nothing; CLEAR BUILD empties the build after its own confirm), then runs the boot's own pass 1, checks and layout (`dl_check`, `dlink.c`) on exactly
  the ticked files against `core_desc`, without writing RAM. A set where every mod would load
  replaces `active` (`active.tmp`, then the old file deleted and the tmp renamed), one line per
  file, its name and CRC-32, and a box says `n MODS LOADED` (`BUILD UPDATED`). A set that wouldn't isn't saved,
  and the box gives the first refusal (`NEEDS drive-hello`). Unlike SAMPLES the ticks stay after
  a load: they are the build. The page never loads or unloads code: the next boot links the
  saved set.
- **DELETE** deletes the ticked files after its confirm, refusing a file that is in the saved
  build (`MOD IN USE`: unload it first).
- **The build list.** LEFT opens a one-row left panel, VIEW BUILD (VIEW +DRIVE in the build
  list), like SAMPLES' VIEW RAM, which switches the list and closes the panel (`0x400734a8`).
  The build list shows the saved set, each mod with what the next boot does: `KEEP` (loaded
  now, stays), `ADD` (loads at the next boot), `DROP` (loaded now, gone after the next boot;
  these rows stay until the reboot) or `!` (the last boot refused it). Its action panel has
  UNLOAD, which saves the build without the ticked mods through the same checks (`MOD NOT LOADED` for a ticked mod the build does not hold).
- **No files.** With no `.elemod` in `/mods` the list has one row, `NO MODS ON +DRIVE`, which
  YES does nothing to, and neither panel opens (decided 2026-11-07). Stock's file view hangs
  the UI when it is first shown with no row at all; its own lists never are (a folder has
  `..`, VIEW RAM its 128 slots).
- **Messages** are stock's message box: on opening, why the boot skipped the mods
  (`SKIPPED: [NO] HELD`, `SKIPPED: CRASH`, `NO +DRIVE`, ...); after a
  change, `SAVE FAILED`, `BUSY: TRY AGAIN` or `OVER 256 MODS` (a build of more than the boot
  links); `NO MEMORY` when the heap has no room for the list.
- **No file limit.** The list grows like SAMPLES' (decided 2026-11-07): the page takes its
  per-file arrays from the heap when it reads the folder, sized to the folder's count, and gives
  them back at the next read. Each file is checked alone for its own verdict (id, version, a
  refusal of the file itself), so no number of files reaches a set's limits; opening reads each
  file twice (its CRC, then the check).
- **The boot's verdicts** come from the boot's own records, matched by file name
  (`dl_boot_choice`), so a second file with a loaded mod's id isn't shown as loaded.
- **Reading files.** The page reads each file into the staging buffer, which it takes only
  while it reads: at its opening and at each change. The route answers `busy` then, and only
  then. A separate buffer for the UI task would cost another 512 KB, or limit what the page
  can check. Every ekFS call is made under `dl_lock`.
- **Keeping off reads.** Stock streams a read out of the buffer after the route fills it, so
  a page check during a read would hand the client the page's bytes under a good CRC (in the
  emulator, 6 of 90 reads while ticking). ReadOpen (`0x4008b4a0`) makes a reader that copies
  the entry, its functions included, after the fill, and the reader goes at ReadClose
  (`0x4008b716`) or when the next transfer's open drops it (`0x4008af8e`). The fill marks its
  own closure, and the route's function manager counts the marked copies alive
  (`dl_reads`), so the count is the open reads. The page takes the buffer only at a count of
  0 (`dl_staging_take_idle`); otherwise it shows `BUSY: TRY AGAIN` and saves nothing. Uploads
  aren't counted: stock keeps an upload's entry in the Data handler (`+0x44`) until the next
  upload, and the epoch below covers them.
- **A read left open.** A client that stops partway through a read (a crash, a closed tab,
  a pulled cable) never sends the close, and stock keeps the reader until the next transfer
  opens: a USB disconnect and re-enumeration don't drop it. The route keeps each open
  reader's address (the copy's place, less `0x30`, when the reader's vtable `0x401ca970` is
  there), the page's tick notes its progress (`+0xb0` the body's running CRC, `+0xb4` its
  bytes served), and a reader that hasn't moved for 30 s (600 `ev_tick`s) no longer counts.
- **An upload that met a check.** The route counts each time a loader part takes the buffer
  (`dl_staging_epoch`, `fs.c`). An upload whose body went to the buffer while the page held it
  is refused at the commit, so a file is never written from bytes the page overwrote.

What the page doesn't see:

- Pass 2's refusals (a `pc16` out of range) are only found by the boot's link: the page saves
  such a set, and the build list shows `!` after the restart.
- A read the client leaves open keeps the page `BUSY` for 30 s. If that client takes the read
  up again after a page check, it can get the page's bytes.

| what | address |
|---|---|
| MenuItem ctor (0x54 bytes), `Menu::addItem` | `0x40072fbc`, `0x400727f2` |
| std::function manager for a plain pointer | `0x4019411a` |
| VerticalSettingsMenuView ctor `(this, name, rows, &title, icon, height, width)`, 0x194 bytes, its View at `+0x38` | `0x400780e8` |
| the view's `shared_ptr<View>` `(parent, view + 0x38)`, returned in `%a0` | `0x4007762c` |
| Data handler: ReadOpen, ReadClose, a job's drop, WritePartial, WriteClose | `0x4008b4a0`, `0x4008b716`, `0x4008af8e`, `0x4008c1c6`, `0x4008c5ce` |
| reader: ctor (0xd8 bytes), vtable, its read | `0x4002beba`, `0x401ca970`, `0x4002bab0` |
| show a child `(parent, &shared_ptr)`: kept in the parent's slot `+0x84`, which releases the one before | `0x40077a16` |
| text `(canvas, font, x, y, align, box, clear, fmt, ...)`, the font | `0x4007154c`, `0x40279e24` |

`tools/modsrpc.py` is a small Data RPC client (list, get, put, rm) for checking the route in an
emulator through its `--usb-midi-tcp` port.

```bash
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build mods/drive-loader-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx --mod core-0.11.elemod --mod drive-loader-0.1.elemod --out drive-loader.syx --version 1.74
```
