# TELOS for the Octatrack: DSP mods

Part of [README.md](README.md). Marks as there.

## Why a DSP mod can't load like a ColdFire one

- **The DSP code is gone from RAM by the load point [M].** The boot site's call (0x40001e50, core's
  `.boot` runs beside it) uploads both cores' programs from the payloads in the OS image (A at
  0x400e2324, 79,563 bytes; B at 0x400f59ef, 77,061 bytes) before the OS starts. By the card's
  first mount, 76,702 of A's bytes and 73,982 of B's read `0xFF` in RAM (ot_emu, stock 1.40C +
  core + TELOS): the memory was reused. A patch to a payload at the load point changes nothing,
  which is why TELOS refuses LO-FI AMF FIX (`stock`, 0x400f4bbb): the stock check sees the bytes
  are not stock's.
- **P memory is full.** Each core's P is 8K words, full with the 13 stock effects; P above
  `0x1fff` is absent (octabam, measured on a unit).
- **The ColdFire can't write P.** It reaches DSP X `0..0x3fff` by host command `$08`, 16 data
  bits a word, and only on frames whose ping word (`0x800000e0`) is 0 (octabam, port).

## How it can work: octabam's demand loader, run on a unit

Octabam loads effect code into the DSPs at run time
(`docs/contributing/DEMAND_LOADER.md`, fe8d6a25; the pump, the host write, the copier and the stub
run on a unit):
- **The window.** `0x30000..0x3ffff` is one memory, P, X and Y aliased, and code there runs from
  both cores. It is the FX2 slots' buffer memory, so code goes where no running stock effect
  writes (each slot's top: offset `0x3da3` and up is clear of every stock effect).
- **A copier flashed in payload B** (DLCOPY, 138 words, a hook at `P:0x76`, the frame head): when
  the doorbell (`X:0x2900` = `0xa55a`) is set, it copies the staged block into the window, checks a
  sum, then publishes the entry's init and proc and its BOUND word last.
- **The ColdFire pump** writes 100 halfwords a quiet frame to core B's `X:0x2901..` and rings the
  doorbell two frames later; a load is sent three times.
- **Dispatch.** The stock dispatcher calls `INIT[id]` and `PROCESS[id]`; a demand effect's id
  goes to a stub that runs dry until BOUND, then the real code.

## The design for TELOS

The same split as for ColdFire mods: what must exist before the OS runs is flashed once; what a
mod brings comes from the card.

| part | where | what |
|---|---|---|
| `dspload-ot` (new companion) | flashed: payload B (DSP) + ColdFire | the copier and a dispatch table in the window's clear gap; on the ColdFire, `ot_dsp_send(core, window_addr, words, n)` and its frame pump on `ev_frame_begin`, ping-0 frames only |
| DSP hooks for mods | flashed: dspbus-ot | `ev_dsp_rx` and the bus's slots: a TELOS DSP mod's entry is a slot in a table the copier fills, never a stock word |
| a DSP mod's code | the card | the `.elemod`'s DSP sections (`dsp.py`'s format), linked by TELOS at the mount to a window address it allocates, then sent by the pump |
| what a mod is allowed | rules | only window homes the flashed build declares, never P or stock X/Y; at most the free gap (605 + 349 words in octabam's layout, more with homes at slot tops: 1,411 to 2,016 words each) |

Steps at the card's first mount (after the ColdFire link):
1. TELOS links each DSP section of each loaded mod to a home (a window range from the flashed
   build's descriptor; a new record, `dsp_homes`), resolving DSP relocations against the flashed
   DSP exports (`ev_dsp_rx`'s table and the bus slots).
2. It queues each block with `ot_dsp_send`. The pump sends them from the first audio frame on (at
   the mount no frame has run yet), three times each, and the copier binds them.
3. The mod's DSP entry sits in a bus slot from the start, behind a BOUND check: dry until bound,
   as octabam's stub does, so a frame before the copy finishes is silent, not a crash.

New refusals: `dsp-home` (no home the size of the mod's code), `dsp-import` (a DSP symbol the
flashed build doesn't export), `dsp-busy` (the pump has more queued than it can send in 2 s).

## What has to be measured or built

- **[O] The window's free homes with core-ot 0.42 and dspbus-ot**, not octabam's remixes: which
  slot tops stay clear with the stock reverbs (octabam's guard handles PLATE and DARK REV
  reaching a home; TELOS v1 can take only homes no reverb reaches).
- **A copier we can build [D].** Octabam is MIT (Sam Banks), so DLCOPY (`modules/dlcopy`:
  `dlcopy.asm`, `dlstub.asm`, `dlwrap.asm`) can be reused with its notice, as elekloader already
  does for octabam's other code. A built `dsp_asm` is in WSL at
  `/root/elek/octatrick/vendor/dsp56300/build/source/dsp_host/dsp_asm` (`ELEKLOADER_DSP_ASM`).
- **[O] The emulator.** `ot_emu --dsp` runs both cores; octabam's `verify_dlload` gate drives the
  same path, so a TELOS DSP gate can follow it.
- **Order of work:** dspload-ot's ColdFire pump and copier with one test block (a tone into
  `ev_dsp_rx`), then TELOS's DSP link, then one shop DSP module (BusDelay or MiniVerb) converted.
