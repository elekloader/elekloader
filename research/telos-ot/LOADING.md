# TELOS for the Octatrack: loading

Part of [README.md](README.md).

## 4. Loading

At the first `ev_card` with state 1 and result 0, in the sys task, TELOS
(the loader, a flashed mod that imports `core_desc`):

1. **Skips** on [NO] held, `.armed` present, no `active`, or a descriptor
   whose ABI major it doesn't know (the Rytm's skip codes).
2. **Writes `.armed`.**
3. **Checks and links** the files `active` names, into the TELOS area:
   pass 1, the check rounds, the layout, pass 2 (docs/DRIVE.md, "The boot
   link"). It adds the Octatrack's rules ([ON-THE-UNIT.md](ON-THE-UNIT.md), section 6) and fills stock parts
   from the stock shadow first, then the running OS. Nothing the firmware
   runs is touched yet.
4. **Quiesces.** It masks interrupts (`move.w #0x2700,sr`; the MCF5445x has
   one core, so nothing else runs). It then scans each task's stack for
   words pointing strictly inside a site the link will write. A preempted
   task's PC is in the exception frame on its own stack, so the scan covers
   saved PCs and return addresses alike. Landing exactly on a site's first
   byte is fine; landing inside is not. If one does, it unmasks, waits one
   tick and tries again (up to 50 times). A mod whose site is still busy is
   refused (`busy`), and the link runs again without it.
   - **The tasks are static [D]** (octabam's RTOS model, `ot_emu/rtos.cpp`,
     `g_expectedTasks`): main (TCB 0x46c7ae84) and ten others, each with a
     fixed TCB and stack: voice, p2a, p2b, p2c, engine, p1b, sys (which
     creates storage, keyrepeat and ui). Task create is 0x400005fc and the
     current TCB is at 0x800068fc.
   - **The scan's bounds.** It scans whole stack regions from the device
     profile, which is conservative: dead words can only cause a retry. With
     the TCB's saved-SP offset [O] it would scan only the live part.
   - **The loader's own task.** The sys task, which runs the loader, is the
     one task that is not stopped. Its own stack holds return addresses into
     the card-mount path (0x40061648 and its callers), so a mod with a site
     there is refused (`busy`).
5. **Writes**, interrupts still masked:
   - each site, after checking that its bytes are what the descriptor says
     the flashed build left there;
   - the merged tables;
   - a data-cache push and an instruction-cache invalidate (the Rytm's
     `dl_flush`, on the same V4 core).

   Then it unmasks.
6. **Inits.** Each mod's `drive_init` (the Rytm's name, so one source builds
   for both), in id order.
7. **Lets go.** `.armed` is deleted 10 s after, by the engine task. `.status`
   and `.build` are written after the project load.

**The TELOS area.** The flashed build reserves it in the reserve core
already takes from the sample arena (6,144-byte pages). The size is a build
choice: 1 MB (171 pages) by default, 512 KB to 4 MB offered. It holds every
TELOS mod's `.run` and `.bss` and a 256 KB staging buffer. The largest shop
mod's code is 22 KB (TUNER) and every compatible shop mod together is about
61 KB ([MEASURED.md](MEASURED.md), section 7). The staging buffer holds one `.elemod` file, JSON with hex. The largest
TELOS-able file is SYNTH's, 91,542 bytes; the USB IO files reach 273,530 but
are flash-only. So 256 KB holds every shop file TELOS can load, with room to
spare.

**The descriptor (ABI 1.2, records added at the end):**
- **sites:** every range the flashed build writes (386 with core-ot 0.42
  and the companions), with its owner;
- **stock shadow:** the stock bytes of each such range, taken from the
  user's stock file when the build is made (a few KB). It is never
  published: it is part of the user's own build;
- **early:** the stock code and data ranges the firmware runs or reads
  before the load point, each marked once or again ([ON-THE-UNIT.md](ON-THE-UNIT.md), section 6), from a
  table per OS release in the device profile (addresses only);
- **boot tables:** the tables read before the load point (`ot_sram`,
  `ot_usb_functions`, `ev_boot_early`, `ev_boot_done`, the early settings),
  flagged closed: no TELOS entries.

**What TELOS adds to core and the companions** (core first, AGENTS.md):
- `ev_telos_ready` in core: after the link, for companions that build lists
  once (settings-ot rebuilds MODS' rows; machines-ot's chooser list);
- core's battery RAM gets a TELOS pool (4 KB in the flashed build). TELOS
  places mods' `ot_sram` claims there by (mod id, tag) with a directory in the
  pool's header, so a claim keeps its place when other mods come and go;
- settings-ot takes TELOS mods' settings like any others, except
  `OT_SET_EARLY`, which a TELOS mod can't have (refused: `early`).
