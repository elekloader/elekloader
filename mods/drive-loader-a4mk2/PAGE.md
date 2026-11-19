# drive-loader-a4mk2: the MODS page

The MODS page of the Analog Four MKII's Drive Loader (`page.c`), in SETTINGS > GLOBAL. The
loader's store, route and boot link are in [README.md](README.md). The page's model (files,
folders, ticks, the stepped save, FORGET DATA) is the Rytm page's
([drive-loader-ar2 PAGE.md](../drive-loader-ar2/PAGE.md)); this file says what the A4's page
does and where it differs.

## How it is drawn

The A4 has no SAMPLES file view to build on, so the page draws itself with core-a4mk2's std1
helpers (decided 2026-11-14: std1 where it reaches); only the row's label and icon use stock
addresses (`page.s`):

- **The row.** A MODS row after SYSTEM in the GLOBAL key's menu (`ev_settings`,
  `core_additem`). Selecting it opens the page on `/mods` itself. It has a 9 x 9 lightning-bolt
  icon like the other GLOBAL rows, the Rytm loader's: its draw callback (`dl_mods_icon`,
  `page.s`) is stock's icon thunk, tail-calling the bitmap blit `0x4005aed4` with a Bitmap of
  stock's layout (vptr `0x401973f4`; one long a column, top row the lowest bit, a full mask).
  `core_setting_add` would draw the row without one. The label is a std::string built by
  `0x4018471e`.
- **The frame.** While the page shows, `ev_draw` (order 95) clears the whole 128 x 64 frame
  and draws the page over stock's GLOBAL menu, which stays underneath unchanged, in the A4's
  own list style (MANAGE PROJECTS and SOUND BROWSER): no title, eight list rows of 8 pixels
  from the top; on the right MANAGE PROJECTS' dotted scroll track with a box thumb (the whole
  track while every row shows) and edge line with the page-arrow mark at mid-height, on the
  left SOUND BROWSER's edge line with its mark. The side panes split the screen as stock's
  do: the list stays, cut by a full-height line carrying the mark, the action pane right of
  x 65 as MANAGE PROJECTS' pane, VIEW BUILD / VIEW +DRIVE left as SOUND BROWSER's pane, as
  wide as its label; the other edge's line loses its mark while a pane is open, as on the
  Rytm. Over them the confirm window and the message box. The list's and the panes' text is
  stock's font 13 (bold, 6 rows), the font the Rytm's file view uses and the same font on
  the A4, drawn by core's `core_text_draw`; the boxes keep `core_text`'s `TEXT_SMALL`
  (font 16). The cursor's row is a 7-row set bar with
  light text, and while a pane has the keys an outline open on the pane's side, as MANAGE
  PROJECTS draws it; a ticked row has a 3 x 3 square left of its label. The page asks for
  a frame (`core_redraw`) only when something on it changed.
- **Keys and turns.** While the page shows, `ev_key` and `ev_enc` (order 1) take every event,
  so GLOBAL gets nothing. A key whose press the page took keeps its release from stock after
  the page has closed (NO's release). Once the page is closed both pass everything on.

| key | list | side panel | confirm window | message box |
|---|---|---|---|---|
| UP, DOWN (repeat) | cursor; FUNC + UP/DOWN a page of 6 | cursor | - | closes it |
| YES | tick a file and step down, open a folder, `..` up | the row's action | does it | closes it |
| NO | closes the page (back to GLOBAL) | closes the panel | doesn't | closes it |
| RIGHT | action panel | closes the left panel | - | closes it |
| LEFT | VIEW BUILD / VIEW +DRIVE panel | closes the action panel | - | closes it |
| LEVEL / DATA turn | cursor, one row a turn event | cursor | - | - |

Other encoders' turns are taken and do nothing. Other keys are taken; they close a message box.

## What it shows and does

As the Rytm's page, with these differences:

- **Rows.** The file list: `..` (below `/mods`), the folders with `(D)` or `(n)` ticked
  below, then the `.elemod` files by file name without `.elemod`, their size in KB once the
  background check has read them, `!` for a file pass 1 refuses. The build list: each mod's id
  with `KEEP`, `ADD`, `DROP` or `!`, and `*` before it when the mod has saved data (below),
  then the parked mods not in the build with `*` alone. Every label is clipped from the left
  behind `<` to the room before its right-hand column (`clip.inc`, `core_text_width`).
- **Action panel** (x 62..127): SELECT ALL, DESELECT ALL, LOAD TO BUILD (UPDATE BUILD), CLEAR
  BUILD, DELETE (DELETE DIR on a folder with nothing ticked here); in the build list SELECT ALL,
  DESELECT ALL, UNLOAD, INFO, FORGET DATA, and APPLY & RESTART while the saved set differs
  from the running one (below). The panel stays open after its action, as stock's.
- **Left panel** (x 0..55): VIEW BUILD in the file list, VIEW +DRIVE in the build list; YES
  switches the list and closes the panel. The build list's ticks (UNLOAD's and FORGET DATA's
  choice) start empty each time it is shown.
- **Confirm window**: the question (`LOAD 2 MODS?`, `UPDATE BUILD? (2 MODS)`, `CLEAR THE
  BUILD?`, `DELETE DIR?`, `FORGET DATA? (n MODS)`, `RESTART NOW?` with `UNSAVED PROJECT` and `CHANGES ARE
  LOST` under it) over `CONFIRM Y/N`, centred.
- **Message box**: centred, split at newlines (about 30 characters a line), closed by any key or
  by itself after 3 s; INFO's and a parked mod's INFO box stay until a key.
- **No empty list.** An empty list draws one placeholder row, `NO MODS ON +DRIVE` or `BUILD
  EMPTY`, that YES does nothing to, and with no file, folder or parked data neither panel opens
  (decided 2026-11-07 for the Rytm, kept for the A4 2026-11-14). The A4's stock menus read a
  missing row on LEFT, RIGHT and a turn when they have none;
  this page draws its own rows, and keeps the placeholder so an empty page acts the same on
  both units.
- **Closing.** NO closes the page; a background read gives the buffer back to the route at
  once. A save that is running goes on to its end with the page closed (its result box is not
  shown); opening the page again drops a save still running, as on the Rytm.
- **Background work** is the Rytm's: one file a check, a stretch a tick, quiet for 1 s after any
  key or turn, at most a quarter of the UI task (`WORK_SHARE`, timed with DTIM0's count), and
  only while the page shows and its frames are drawn. `/mods/.status`'s `check: n/N` line is
  published the same way. Each read stages the file as the boot does, its payload's hex
  skipped (`skip.h`, `dl_stage_more`), so pass 1 sees what the boot sees and the CRC is the one
  `/mods/active` names; a row's KB is the whole file on the +Drive, payload included.

## Saved data

`/mods/.store` is read when the page opens and after FORGET DATA (`park.h`). A build row whose mod
it keeps anything for (fields, lock codes or CCs, live or parked: `dl_store_has`) gets a `*`
(the Rytm's page says `PARK`). The `*` has a column of its own, its
right edge `STATE_GAP` left of the widest state word, so every row's `*` lines up whatever word
follows it: `* KEEP`, `* ADD`, `* DROP`, `* !`.

The parked mods (fields no chosen mod declares) that have no build row follow the
build's rows, with the `*` alone in that column. A parked mod back in the build has no second
row: its build row carries the `*`. YES on a parked-only row ticks it and shows its parked bits
per scope, lock codes and CCs; FORGET DATA drops the ticked rows' parked data, on a build row or
a parked-only one, after its confirm, in steps (read, write `.store.tmp`, swap). With `/mods`
empty but data parked, the panels still open, so FORGET DATA can be reached.

## APPLY & RESTART

The build list's action pane gets a sixth row, APPLY & RESTART, while the saved set differs
from the running one: a build row reads `ADD` or `DROP`, or `!` for a saved mod the last boot
refused or dropped (another boot tries it again). Parked data doesn't count: FORGET DATA
rewrites it at once. Its confirm always warns, since a reboot drops what the project hasn't
saved and core has no read of the modified mark. YES closes the pane, shows `RESTARTING` and
takes no more keys; 2 s later (as stock's SUCCESS! REBOOTING shows, and so YES's release and a
remote key's status are answered first) the page takes the crash guard off
(the restart is the user's, and the guard waits while the page shows after a boot that applied
sites), takes `dl_lock` for good, so no upload or page write is cut between its store calls,
and calls core-a4mk2's `core_restart` (`analog-four-mk2/system.h`), which waits for the +Drive
store and card and the panel link and UART, then resets. An upload the route hasn't committed is
dropped, as at a power cut; the store's directory stays whole. The next boot links the saved set
(decided 2026-11-16: in VIEW BUILD's pane, shown only when the sets differ, warned every time).

## Keys still to confirm

From core-a4mk2's `analog-four-mk2/keys.h`: LEFT 0x40 and RIGHT 0x41 [read], YES 0x44 and NO
0x45 [emu], UP 0x42 and DOWN 0x43 [emu, which is which not yet confirmed]. The encoder's delta
is read at EncoderEvent +16, the Rytm's layout, not yet read on the A4. The first emulator gate
proves both before any plan relies on them: DOWN from row 0 must move the cursor to row 1, and
a DATA turn clockwise likewise.

## Tests

`tests/test_drive_a4_page.py` builds `page.c` for the computer (`tests/drive_a4_page_host.c`)
with a made-up `/mods` and checks that pass every file: the empty page's placeholder row and
closed panels, every key and turn taken while the page shows and passed on after it closes
(NO's release kept), rows, folders, sizes and ticks, `..` back onto the folder left, LOAD TO
BUILD's confirm and the `/mods/active` it writes, NO on the confirm window, the build list after
a load, scrolling by UP/DOWN, FUNC and the DATA encoder, the message box's close by a key
and by time, and APPLY & RESTART: shown only while the sets differ, NO on its confirm doing
nothing, YES showing `RESTARTING`, keeping it through a key, and calling `core_restart` with
`dl_lock` held and the crash guard off.
