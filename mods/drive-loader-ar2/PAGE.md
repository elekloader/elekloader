# drive-loader: the MODS page

The MODS page of the Analog Rytm MKII's Drive Loader (`page.c`, `page.s`), in SETTINGS >
GLOBAL. The loader's route and boot link are in [README.md](README.md).

A MODS row at the bottom of SETTINGS > GLOBAL (`ev_settings`, `core_additem`) opens a page
on the `.elemod` files in the folder. The MODS row has a 9 x 9 lightning-bolt icon like the
other submenu rows: its draw callback (`dl_mods_icon`, `page.s`) is stock's icon thunk,
tail-calling the bitmap blit `0x400716e0` with a Bitmap of stock's layout (one long a column,
top row the lowest bit, a full mask).

The page is stock's two-pane file view, the one SAMPLES and PROJECTS are built on
(`0x400734f2`), and works like SAMPLES:

- **The file list.** One row a file: its id (its file name until one is known) and, on the
  right where SAMPLES puts sizes, its size in whole KB once the check has read it (blank
  before), or `!` for a file pass 1 refuses (a bad file, another device, a newer core, larger
  than the +Drive area). A file over the stage but within the area (STAGE.md) shows
  its size, and a save leaves its check to the next boot. The ticks start as the saved build on entering MODS and after each change; coming
  back from the build list keeps the ticks as they were.
- **Why a `!`.** Ticking a `!` row also shows its whole refusal in stock's message box: in the
  file list the file's own (`OTHER DEVICE/OS`, `NEEDS CORE 0.12`, `NOT FORMAT 2`...), in the
  build list the last boot's after `BOOT: ` (`BOOT: NOT FOUND core_fn`, `BOOT: NO ROOM +12K`,
  `BOOT: FULL ev_x (0 SPARE)`), with the detail name the linker kept. A storage-room refusal
  gives its scope and bits on a second line (`NO STORAGE ROOM` over `sound: 62 of 56 bits`;
  `codes: 3 of 1 free` for lock codes, `core_handles: 3 of 2 spare` for a table), and a site
  refusal the site and who has it (`SITE 0x400a005e` over `TAKEN BY duck`, `TAKEN BY THE BUILD`
  or the flashed module, `IN A PRE-LINK RANGE` or `NOT STOCK BYTES`; a copied block's clash
  names its first address), the other +Drive mod named from the set's records (docs/FORMAT.md,
  "+Drive refusal codes": `store`, `site`). A reason longer than 30
  characters puts its detail on a second line: stock's box splits at newlines (`0x400764cc`)
  and about 32 characters fit the screen. The box closes by itself after a few seconds.
  Unticking shows nothing; tick it again to see it again.
- **Ticks are the build.** [YES] ticks or unticks a row and steps down, as in SAMPLES. RIGHT
  opens the action panel: SELECT ALL, DESELECT ALL, LOAD TO BUILD (UPDATE BUILD once the build
  holds mods), CLEAR BUILD, DELETE. It asks `LOAD n MODS?` (`UPDATE BUILD? (n MODS)`) over
  `CONFIRM Y/N` in stock's confirm window (with nothing ticked it says `NO MODS SELECTED` and
  changes nothing; CLEAR BUILD empties the build after its own confirm), then runs the boot's
  own pass 1, checks and layout (`dl_check`, `dlink.c`) on exactly the ticked files against
  `core_desc`, without writing RAM. A set where every mod would load replaces `active`
  (`active.tmp`, then the old file deleted and the tmp renamed), one line per file, its name
  and CRC-32, and a box says `n MODS LOADED` (`BUILD UPDATED`). A set that wouldn't isn't
  saved, and the box gives the first refusal (`NEEDS drive-hello`). Unlike SAMPLES the ticks
  stay after a load: they are the build. The page never loads or unloads code: the next boot
  links the saved set.
- **Folders.** The file list shows one folder of `/mods` at a time, to 3 folders deep, as
  SAMPLES does: `..` first (not in `/mods` itself), then the folders, then the `.elemod`
  files. A folder row and `..` show the bare name with `(D)` on the right, as SAMPLES; a file
  row shows its file name without `.elemod` (`syntakt-art-0.1`), never the mod's id, so rows
  don't change under the check. YES on a folder opens it with the cursor at the top; YES on
  `..` goes up with the cursor back on the folder just left (SAMPLES' select, `0x400e92f8`).
  Each folder is read when it is opened, and the background checks only the open folder's
  files, so no scan of every folder holds the UI task. A folder row shows `(n)` in place of
  `(D)` when n files below it are ticked (`(3)`). A mod is known by its path below `/mods`
  (`drums/kicks/x.elemod`), the name `/mods/active` keeps (at most 127 bytes).
- **Ticks across folders.** Unlike SAMPLES, which clears its ticks whenever the folder changes
  (`0x400722c4`), the ticks stay: they are the build, and a build takes mods from any folder.
  The page keeps them itself, one flag a file; stock's tick vector only shows the open
  folder's. LOAD TO BUILD takes every ticked file in every folder. At most 256 files can be
  ticked (`OVER 256 MODS`).
- **SELECT ALL and DESELECT ALL** tick or untick the open folder's files, as SAMPLES'
  (`0x400e6620`): no folder row, nothing below.
- **DELETE** deletes the open folder's ticked files after its confirm, refusing a file that is
  in the saved build (`MOD IN USE`: unload it first). With none of the open folder's files
  ticked and the cursor on a folder, it reads DELETE DIR and asks `DELETE DIR?`, as SAMPLES
  (`0x400e9ee6`, `0x400e77b2`): stock's rmdir removes an empty folder only, and a folder with
  anything in it gives `ERROR (NOT EMPTY?)`. Stock never deletes a folder's files for you, and
  neither does the page.
- **The build list.** LEFT opens a one-row left panel, VIEW BUILD (VIEW +DRIVE in the build
  list), like SAMPLES' VIEW RAM, which switches the list and closes the panel (`0x400734a8`).
  The build list shows the saved set, each mod by its id: the mods the last boot read take it
  from the boot's status records, so it shows at once, and a mod added since takes it from the
  save's check; a mod with no id known shows its file name without `.elemod`. Ids can't repeat
  in a build (the linker refuses `SAME ID AS`). A label too wide for the row keeps its tail
  behind a `<`, measured with stock's font so that it ends before the state column
  (`clip.inc`, `tests/test_drive_page.py`). Each row shows what the next boot does: `KEEP`
  (loaded, stays), `ADD` (loads at the next boot), `DROP` (loaded, gone after the next boot;
  these rows stay until the reboot) or `!` (the last boot refused it). Its action panel has
  UNLOAD, which saves the build without the ticked mods through the same checks
  (`MOD NOT LOADED` for a ticked mod the build does not hold), and INFO, which shows the
  cursor's mod's folder (`/mods/drums/kicks`, clipped from the left behind a `<` past 30
  characters) and its file name below it (30 characters a line) in a box closed with NO, and
  FORGET DATA (below, "Parked data").
- **Parked data.** After the build's rows the build list shows each mod `/mods/.store` keeps
  parked fields, lock codes or CCs of (docs/DRIVE.md, "+Drive storage"), by its id, with `PARK`
  in the state column, in the order they were parked. Ticking one, or INFO on it, shows
  `PARKED DATA` and its bits per scope (`sound: 12 bits, kit: 4 bits`, two a line), then the
  lock codes and CCs kept for it. FORGET DATA asks `FORGET DATA? (n MODS)` for the ticked
  parked rows (`NO PARKED MOD SELECTED` with none, `NO PARKED DATA` when there is none), then
  rewrites `/mods/.store` without their parked fields, codes and CCs, one step a tick as a save
  does: the file read, `.store.tmp` written, the old file deleted and the tmp renamed, each under
  `dl_lock`; a box says `FORGETTING DATA`, then `DATA FORGOTTEN (n MODS)`. The rest of the file
  stays as it is, so the bits go back to the scope only at its next repack, as
  `drive_store.forget` (`park.c`; `tests/test_drive_page.py` checks its bytes against it). A
  file that is missing or doesn't read (its CRC, its records) parks nothing. With `/mods` empty
  and data parked, the panels open so that VIEW BUILD and FORGET DATA can be reached; the file
  list keeps its `NO MODS ON +DRIVE` row.
- **No files.** With no `.elemod` and no folder in `/mods` the list has one row,
  `NO MODS ON +DRIVE`, which YES does nothing to, and neither panel opens unless data is parked.
  A build list with no row (nothing in the build and nothing parked, or after CLEAR BUILD,
  UNLOAD or FORGET DATA took its last rows) has one row the same way, `BUILD EMPTY` (short enough for the list beside its panels).
  Stock's lists are never left with no row: its label scroll reads the cursor's row without
  checking the count, which on a cleared list is a freed row (a fatal halt, the DV2B emulator
  runs) and on a list never filled a null one (the UI hung on DLP7).
- **Messages** are stock's message box: on opening, why the boot skipped the mods
  (`SKIPPED: [NO] HELD`, `SKIPPED: CRASH`, `NO +DRIVE`, ...); after a change, `SAVE FAILED`,
  `BUSY: TRY AGAIN` or `OVER 256 MODS` (a build of more than the boot links); `NO MEMORY` when
  the heap has no room for the list.
- **No file limit.** The list grows like SAMPLES': the page takes its per-file arrays from the
  heap when it reads the folder, sized to the folder's count, and gives them back at the next
  read. Opening reads only the folder's listing and opens no file: reading 13 files of 484 KB
  on opening held the key path and USB for over 5 s, and reading 257 files for their sizes
  alone lost a key on a unit. The files are then read and checked one at a time in the
  background, at most one every 2 ticks and within a quarter of the UI task ("A quarter of
  the UI task"), while the page's rows are drawn: a row shows its size once read, and `!` for
  a file pass 1 refuses. Each file is checked alone for its own verdict (id, version, a
  refusal of the file itself), so no number of files reaches a set's limits. Ticking never
  reads a file (a row not yet checked shows no `!` and no reason box).
- **The checked count.** `/mods/.status` has a line `check: n/N /mods/FOLDER`: `n` the open
  folder's files read and checked so far, `N` its files (`check: -` before the page has read
  a folder), so that a test plan can prove the check finished before its first key. It is not
  drawn on the page, which looks like SAMPLES. The check runs only while the page shows, so
  `n` stays below `N` after the page closes early. A page closed partway through a file's read
  gives the staging buffer back once a frame it asked for doesn't come (without that, NO
  pressed mid-read left `/mods` busy until a power cycle); the file is read again from its
  start at the next showing.
- **A save in steps.** LOAD TO BUILD, UPDATE BUILD, UNLOAD and CLEAR BUILD check the new set
  one file a tick (`dl_check_start`, `dl_check_step`, `dl_check_finish`: the same check as
  `dl_check`), taking the staging buffer and the mutex for one file at a time, so the key path
  and USB stay free between files (a LOAD of 18 files of 482 KB in one call held them for
  minutes). A box says `CHECKING n MODS`, and the result's box follows. Until it ends, the
  page's actions answer `BUSY: TRY AGAIN` and its background check waits. FORGET DATA runs
  the same way, and waits 1 s after a key as the save does.
- **Keys first.** Work in the UI task holds stock's UI event queue. Reading a whole 481 KB
  file every 2 ticks lets it grow about 40 events a second, and past 16 queued analoghealth
  refuses a remote key as busy until it expires at 2 s; the refused key never reaches
  `ev_key`, so it can't pause the check. So the background check and the save read a file
  32 KB a tick (`dl_read_open`, `dl_read_more`, its CRC taken as it comes; the queue stays at
  8 or under), holding the staging buffer from the open to the file's check, and check it once
  read (`dl_check_buf`, `dl_check_step_buf`). Both wait 1 s after any key or encoder turn
  (`ev_key`, `ev_enc`, taking nothing), and long checks sleep 1 ms each 32 KB for the lower
  tasks (`dl_breathe`). The cost is time: 18 files of 481 KB take about 41 s to check with a
  key every 1.5 s, against 14 s for whole-file reads.
- **A quarter of the UI task.** The unit's card is slower than the emulator's: with 257 tiny
  files the first DOWN after YES into the folder could be lost. So reading a folder opens no
  file (a file's size shows once the check has read it), and the check's and the save's work
  on a tick is timed with the bus clock (DTCN0) and then rests three times as long, so the
  background takes at most a quarter of the UI task whatever the card's speed (`WORK_SHARE`;
  `tests/drive_check_tick_host.c` times it on a made-up slow card).
- **Long scans.** A file's CRC and its long strings (its hex) are read through tables, 32 KB
  between breaths, and a hex string in one walk. In the emulator 18 files of 482 KB link in
  3.2 s.
- **The boot's verdicts** come from the boot's own records, matched by file name
  (`dl_boot_choice`), so a second file with a loaded mod's id isn't shown as loaded.
- **Reading files.** The page reads each file into the stage (STAGE.md), which it
  takes only while it reads that one file. The route answers `busy` then, and only then. A
  separate buffer for the UI task would cost area, or limit what the page can check. Every
  ekFS call is made under `dl_lock`.
- **Keeping off reads.** Stock streams a read out of the buffer after the route fills it, so
  a page check during a read would hand the client the page's bytes under a good CRC.
  ReadOpen (`0x4008b4a0`) makes a reader that copies the entry, its functions included, after
  the fill, and the reader goes at ReadClose (`0x4008b716`) or when the next transfer's open
  drops it (`0x4008af8e`). The fill marks its own closure, and the route's function manager
  counts the marked copies alive (`dl_reads`), so the count is the open reads. The page takes
  the buffer only at a count of 0 (`dl_staging_take_idle`); otherwise it shows
  `BUSY: TRY AGAIN` and saves nothing. Uploads aren't counted: stock keeps an upload's entry
  in the Data handler (`+0x44`) until the next upload, and the epoch below covers them.
- **A read left open.** A client that stops partway through a read (a crash, a closed tab, a
  pulled cable) never sends the close, and stock keeps the reader until the next transfer
  opens: a USB disconnect and re-enumeration don't drop it. The route keeps each open reader's
  address (the copy's place, less `0x30`, when the reader's vtable `0x401ca970` is there), the
  page's tick notes its progress (`+0xb0` the body's running CRC, `+0xb4` its bytes served),
  and a reader that hasn't moved for 30 s (600 `ev_tick`s) stops counting.
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
