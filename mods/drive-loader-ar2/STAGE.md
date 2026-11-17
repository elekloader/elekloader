# The stage (drive-loader, Analog Rytm MKII)

Where the loader holds a file while it moves it: the boot link, the `/mods` route's reads and
uploads, and the MODS page's checks. The route is the README's, "The /mods route".

Top-staging (decided 2026-11-11; docs/DRIVE.md, "The boot link"): the boot reads each file into
the +Drive area itself, pass 1 at its start and pass 2 just under its end. After the link, the
route and the MODS page stage in the area's free tail, from the layout's end (rounded up to 16)
to the area's end, when that is larger than the 64 KB floor, else in the floor
(`dl_stage_tail`). `dl_stage_at` and `dl_stage_len` (route.c) are the only start and size
anything uses. The tail is memory no mod uses on that boot, and nothing is reserved for it: a
set that fills the area leaves the floor. `/mods/.status` names it, `stage: N bytes at
0xADDRESS, free tail` (or `floor`), and `elekloader mods room` and the web view read that line.

- **Reads.** A file over the stage lists and clears but doesn't read (`file_perm`); the fill
  zeroes only to the entry's size.
- **Uploads.** Stock 1.74 puts no size cap on a RAM entry and writes the first chunk to the
  entry's address before the `+0x6c` header check (`FUN_4008c1c6`, traced 2026-11-11), so
  `+0x1c` (`dl_pre`) refuses an upload whose size, which stock has put in the entry by then, is
  over the stage (`Too large for the free room`); `+0x6c` and the commit check again. Folders,
  moves and `.dir` uploads stay in the floor.
- **The MODS page.** A file over the stage but within the area is read for its size and CRC
  alone (`dl_read_more`), shows its size, and is saved with the set unchecked
  (`SAVED: BIG FILE CHECKED AT NEXT BOOT`); the boot gives its verdict.
- **Progress.** Stock's per-mille read progress (`0x4008c9d6..0x4008c9e8`) wraps past
  4,294,967 bytes, so the clients count bytes.

Tests: `tests/test_drive_route.py` (the stage in a guarded arena: reads, uploads, refusals and
the `.status` line), `tests/test_drive_page.py` (`Reader`: a file over the stage),
`tests/test_drive_top_stage.py` (the boot link against its twin), and in the TypeScript
engine `js/test/drive.test.ts` (the `top:` cases).
