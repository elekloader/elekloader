# drive-loader-a4mk2: the +Drive mods loader for the Analog Four MKII

The flashed part of +Drive mods on the A4 MKII, OS 1.56, on core-a4mk2 0.8 and newer: mods kept as
files in `/mods` on the A4's +Drive are linked into the +Drive mods' area at boot, so they can be
added and removed without reflashing. It is drive-loader-ar2 (the Analog Rytm MKII's, docs/DRIVE.md)
carried over as a mod of its own; what differs is the A4's +Drive, which has no file system.

State: built, linted and patched in the cloud against stock 1.56 with core-a4mk2 0.8; the store runs
on the computer (tests/test_drive_a4_store.py). Not yet run in the emulator or on a unit. The MODS
page, drawn with core's std1 helpers, is in [PAGE.md](PAGE.md).

## The store

Stock 1.56 keeps the +Drive as raw 512-byte sectors under one slot store: 128 project slots and 4,096
sound slots at fixed sectors, the power-off image at 0x7a000 and 0x7c000. Sectors 0x3000..0x7a000
(about 238 MiB) are addressed by no stock code and survive FORMAT +DRIVE (Ghidra trace, and an
emulator watch of every SD command through boots, saves, loads, Data RPC transfers and a format).
The loader keeps its own store there (`store.c`, layout in `fs.h`):

| sectors | what |
|---|---|
| 0x3000 + 320 | directory copy A: a header sector, then the entries (144 bytes each, up to 1,024) |
| 0x3140 + 320 | directory copy B |
| 0x3280 | a scratch sector no file gets (a route entry with no file of its own points here) |
| 0x3288..0x7a000 | files, each one run of sectors from a 4 KB boundary |

- **Two copies.** A change writes the whole directory into the copy that isn't the newest, its
  entries first and its header (generation, CRC of the entries, its own CRC) last. A power cut
  mid-write leaves that copy invalid and the other stands; a torn newer copy falls back to the
  older one.
- **Write, then name.** A file is written into sectors no entry holds and becomes the file only when
  the directory names it, replacing an older one of that name. A failed or cut write loses only
  itself. So `/mods/active`, `.store` and `.armed` are written straight over their names, without
  the Rytm's `.tmp` and rename.
- **No header on the files.** The Rytm's 16 zero bytes kept stock's SAMPLES browser from loading a
  raw `.elemod`; nothing in stock 1.56 reads these sectors.
- **Folders** are entries of their own: three levels below `/mods`, mkdir, rmdir of an empty one,
  and a rename that moves a file or a folder with everything below it anywhere (paths to 127 bytes).
- **A card with no store** (new, or never used by the loader) reads as an empty store; the first
  change writes it.
- **FORMAT +DRIVE** empties the store, both copies, through core's `ev_drive_format` (`dl_format`): `/mods/active` and every mod go with the projects and sounds. It also covers
  stock's format of a card with no header at UI start, which comes after the boot link.

The store's calls are the Rytm loader's ekFS calls by name and meaning (`dl_fs_open`, `read`,
`write`, `close`, `delete`, `rename`, `mkdir`, `rmdir`, `file_size`, `dl_list_dir`), so the route,
the linker and the page keep drive-loader-ar2's logic. Sector I/O is stock's raw read 0x400721a6,
write 0x40072376 (each takes the card mutex and works through stock's DMA buffer); the store writes
whole sectors only, since a write of part of one writes the DMA buffer's leftovers after it.

## The /mods route

As the Rytm's: a RouteTypeHandler added in the resolver's getter (site 0x40095ec8, the guard set
after stock's two handlers are added; the handler add 0x40019596 moves a single owning pointer where
the Rytm's copies a shared_ptr), patterns `/mods` to three folders deep, `.build` (the build
descriptor), `.status`, `.store`, `FOLDER/.dir` (mkdir) and `.mv` (move). What differs is that files
are **+Drive entries (kind 2)** over their own sectors, with no RAM stage:

- **Reads** stream from the file's run through stock's +Drive reader, at any size.
- **Uploads** go straight to sectors. `+0x1c` sees the upload's payload size (stock puts the
  header's size in the entry's copy first) and refuses one larger than the largest free run;
  `+0x3c`, which stock calls on the same copy just before it makes the +Drive writer, reserves the
  run and writes its byte offset into the copy's `+0x0c`; `+0x6c` checks the header; `+0x4c`, after
  the writer reported every byte and the content hash passed, makes the run the file. A failed
  upload never reaches `+0x4c`, and its reservation is only in memory, so it costs nothing.
  Every std::function slot of an entry holds its own copy of the closure, so the upload in flight
  is the route's own record (one upload at a time per Data handler), not the closure's: `+0x1c`
  keeps the copy there tagged with its resolution's serial, and `+0x3c` reserves only for the same
  resolution (tests/test_drive_a4_route.py).
- **Uploads in more than one chunk.** Stock's +Drive block writer (the write at 0x4001d9fc) takes
  at most the larger of the bytes it still has to write and the bytes it still has to accept.
  Between chunks it keeps up to a sector's worth in its ring, so the first count is the larger
  one, and the chunk that ends the payload also takes the container's 12-byte footer: the footer
  is never processed and WriteClose refuses the upload ("Footer was not processed"). A one-chunk
  upload has nothing left over, so it closes. The site at 0x4001da16 (2 bytes, stock SHA-256
  16119cec...233dc2) makes that bound always the bytes still to accept. It changes nothing for a
  write that fits what is left, so stock's other +Drive writers behave as before. [Traced in the
  emulator, 2026-11-15: an 18,466-byte upload in three chunks had 481 bytes in the ring at its
  last chunk, which took 2,125 bytes with 2,113 left to accept.]
- **Folders an upload names.** The store keeps no file whose folder isn't in its directory, where
  the Rytm's stock file system makes the folders as it writes. So `+0x4c` makes the file's missing
  folders first (`make_parent`, as `FOLDER/.dir` does) and then commits; a folder it can't make (a
  file of that name) leaves the run unused and notes `commit failed: no folder for NAME`. `+0x4c`
  can't refuse, so the clients read `.status`'s `last:` line after each upload and report anything
  but `saved NAME` (`Mods.upload`; tests/drive_a4_route_host.c, tests/test_drive.py).
- **A new name** resolves to the scratch sector with size 0 and no Clear permission; a Clear of a
  file erases or zeroes at most its own first sector, then deletes it.
- **Permissions** are read, write and clear only: Copy and Swap would make writers without a
  reservation.
- `.status` gives the room (the largest free run and the total) and the directory's generation in
  place of the Rytm's stage line.

## The boot

`dl_boot` in core's `ev_boot_drive` (0x401061e8, before the boot project load 0x400f1b18, the audio
interrupt and the UI): the store is read first (skipped when SD init's result 0x41865e98 isn't 0,
there's no card, or the card is too small), then the Rytm's sequence: [NO] held skips; `.armed` left
from a start that didn't get 10 s in skips once; `/mods/active` is read, `.armed` written, the set
linked with each file read into the area's top, the caches pushed, each mod's `drive_init` run.

- **[NO] at power-on.** Main's key bitmap doesn't hold a key held since power-on, so the loader asks
  the panel for a full report: `60 01` through main's byte send 0x4000274a, 20 ms on 0x40078064, the
  held test 0x400647a2(0x45), then main's own `60 00` again. [In the emulator: held reads 1, not
  held 0, and the boot carries on unchanged; the mode switch itself is for the unit run.] A held
  [NO] also reaches the UI as one NO press.
- **The area** is the whole of core_ddr above the flashed build: from its RAM end, 4 KB aligned, to
  0x47C00000 (the profile's `drive_align`).
- The boot time in `.status` reads DTIM0's count as the Rytm does; that it runs at the Rytm's bus
  clock on the A4 is not traced.

## Payloads

A mod's `.payload` stays on the +Drive (docs/DRIVE.md, "Payloads on the +Drive"; decided 2026-11-15):
the boot stages each file without the payload's hex (`skip.c`, through `dl_read_file` in `boot.c`),
the linker places nothing for it, and the descriptor's `F_PAYLOAD_SKIP` (`drive_payload_skip` in
`route.s`) tells host tools to stage, check and CRC files the same way. After the link `payload.c`
keeps the loaded mods' payloads in `drive_payloads`, and sidecar-core reads one with
`dl_payload_read` from its own task when the Pi is ready. A relocation into `.payload` is refused, so
a mod that addresses its payload from code (sidecar-codetest's `abs32`) loads only flashed. Uploads
were never bounded by a stage here: `/mods` takes a file up to the largest free run.

## Files

| file | from drive-loader-ar2 | what |
|---|---|---|
| `store.c`, `fs.h` | new (replaces `fs.c`) | the store, the loader's mutex and the staging floor |
| `route.c`, `route.s` | logic kept, glue retraced | the /mods route (tests/test_drive_a4_route.py) |
| `boot.c` | logic kept, glue retraced | the boot, the crash guard, `dl_format` |
| `json.c`, `json.h`, `sha256.c`, `park.c`, `park.h`, `names.inc` | unchanged (tests/test_drive_a4_store.py checks) | JSON, hashes, parked mods, the route's names |
| `dlink.c` | storage fields as the A4 takes them: `"track": "cv"`, `"mod"` on a kit field, no `"lfo"`; the payload skip is built only with `DL_PAYLOAD_SKIP` | the linker |
| `dstore.c`, `dstore.h` | lock codes per track kind (0x70..0x7f each), the A4's CC pool, `.store` format 2 (tests/test_drive_a4_dstore_host.py) | +Drive storage |
| `dl.h`, `flush.s` | comments, and `DL_PAYLOAD_SKIP` | the linker's interface; the site writes and cache push (the A4 sets the Rytm's CACR and ACR0) |
| `skip.c`, `skip.h`, `payload.c` | new | payload skip while staging (tests/test_drive_payload_skip.py), the Drive payload table and its reader |
| `page.c`, `page.s`, `clip.inc` | model kept, view new (std1; stock addresses only for the row's label and icon, `page.s`) | the MODS page ([PAGE.md](PAGE.md); tests/test_drive_a4_page.py) |

## Still to prove

In the emulator (elkbench on the PC), before any unit run: the route's list item layout (0x14 bytes
on the Rytm) and a whole upload, read, Clear, mkdir and move through kind-2 entries; the boot link
of a test set; the crash guard; FORMAT +DRIVE emptying the store; a set with a payload mod (the
table filled, `dl_payload_read` against the file's bytes). On a unit: the `60 00` mode
switch, the card's own sector count, boot time with a full area.

## Build

```bash
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build mods/drive-loader-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx --mod core-0.8.elemod --mod drive-loader-0.1.elemod --out test.syx --version XXXX
```

`-DDL_TEST_DIR` keeps the mods in `/~T MODS` for a unit run under the test rules; `-DDL_TEST_ARMED`
lets the route plant `.armed`, as on the Rytm.
