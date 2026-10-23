# machines (Octatrack)

Source machines for the Octatrack (OS 1.40C): a machine mod adds a row to
the track's machine chooser (SELECT MACHINE TYPE) and to SRC SETUP, below
PICKUP, with its own name, page, defaults and sound. A companion beside the
core (core-ot 0.3.1 or newer), like machine-pages on the Digitakt mk1 and
machines on the Digitone: it is only in a build when a mod requires it.

Without it, every source machine patches the same firmware places itself:
the chooser's rows and names, SRC SETUP, the page resolver, the Part
validator and the FLEX renderer. So no two machines can be in one build
(octabam refuses a second, and its ANALOG BD, OCTA303, CXOSC and
BLACKPYRAMIDS-303 all take the same places). With it, those places have one
owner, and each machine adds a **record** to the `ot_machines` table.
Several machines combine; past six rows the chooser scrolls.

On its own it changes nothing the unit does (checked in ot_emu: the chooser,
a commit, the SRC page and SRC SETUP leave the same Part, battery copy and
screen as stock).

| | |
|---|---|
| needs | core-ot 0.3.1 (`resources.core`): its `ev_tick` runs the machines' `ui_tick` |
| sites | 32, none a byte octabam2elemod v1.0's mods write, SYNTH MACHINE's included (seams.s) |
| table | `ot_machines`: a machine contributes a pointer to its `struct ot_machine` |
| exports | `ot_stock_render`, `ot_part`, `ot_track_machine`, `ot_machine_row` |
| header | `elekloader/sdk/include/octatrack/machines.h` |
| claims | `octatrack:machine-signatures` |
| RAM | 5.5 KB |

## A machine

```c
#include "octatrack/machines.h"

static int my_render(unsigned track, unsigned ping, unsigned start, unsigned end)
{
    volatile uint8_t *h = ot_render_header();       /* before stock writes it */
    int r = ot_stock_render(track, ping, start, end);
    /* h[3] samples, from h + 16 each as two longs, L and R, the sample in the high half */
    return r;
}

const struct ot_machine my_machine = {
    .abi = OT_MACHINE_ABI, .sig = {'M', 'Y'}, .sig_lo = 1, .sig_hi = 1,
    .label = "MINE", .flags = OT_MACHINE_PROTECT,
    .defaults = my_defaults,                         /* the 12 FLEX bytes a new track takes */
    .render = my_render,
};
```

and in `mod.json`:

```json
"contribute": [{"to": "ot_machines", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:my_machine", 0]]}],
"requires": ["core", "machines"]
```

[examples/crush-machine-ot](../../examples/crush-machine-ot) is one to
start from.

**The record** has the layout and meaning of version 1 of the record of the
same name on Modwerk's Octatrack base (repeat98/modwerk), so a machine
written for one describes itself to the other the same way. Only `abi`,
`sig` and `label` are needed; every other field may be 0:

| field | what machines-ot 0.1 does with it |
|---|---|
| `sig`, `sig_lo`, `sig_hi` | a track is the machine when its Part signature (Part + 60 + 30 × track) has these two letters and a version in range |
| `label` | its name in the chooser, SRC SETUP and the SRC page's title |
| `flags` | `PROTECT`: stock's Part validator never sees its tracks' 12 FLEX bytes; `RENDER_FLEX`: its render is also offered the FLEX tracks no machine owns |
| `page`, `track_page` | its PLAYBACK page (a FLEX descriptor's form), else FLEX's |
| `defaults` | the 12 FLEX bytes a track takes when it becomes the machine |
| `assign` | signs and unsigns its own tracks, in place of the companion |
| `admit_track` | 0 refuses the track: the chooser commits nothing |
| `chooser_type` | not 5: the row is not shown as the track's |
| `ui_tick` | 60 times a second in the UI task (core-ot's `ev_tick`) |
| `leave` | the track becomes another machine |
| `render` | in place of the FLEX renderer for its tracks |
| `validate` | wraps the Part validator, in row order: call `next` |
| `pool_open`, `pool_left`, `choice_left`, `list_draw`, `POOL`, `BACKING` | not served yet: the row has stock's sample pool |
| `source_voice`, `source_engine`, `source_kind` | a DSP voice: 0.1 has no DSP source stage, so a record with `source_kind` set is not listed |

## How it works

- **A machine's track stays a FLEX track** (its machine byte is 1). What
  makes it the machine is its signature: three bytes at Part + 60 + 30 ×
  track (NEIGHBOR's first three PLAYBACK bytes), two letters and a version,
  the form octabam's machines write (ANALOG BD `AB` 1, OCTA303 `TB` 1, CXOSC
  `CX` 1). A Part saved by one of them reads the same here. Its parameters
  are FLEX's 12 bytes of the track (PLAYBACK and SETUP).
- **Rows.** The chooser lists stock's five machines, then the table's in
  (order, mod id) order, as rows 5 on, at most 8. A row is never stored, so
  a project keeps its tracks when machines come or go.
- **The chooser and SRC SETUP** read each track's machine byte through the
  companion, which reads a signed track as its machine's row; their row
  counts and bounds are 5 + n. The machine chooser draws the rows from the
  list's top, so it scrolls past six rows (stock's drew rows 0-5 only).
- **The names and pages** are grown copies of stock's tables (`otm_names`,
  `otm_pages`), which start as stock's, so a reader that comes first sees
  stock.
- **A commit** (YES in the chooser, or SRC SETUP) signs the track and gives
  it the machine's defaults; choosing a stock machine unsigns it and, for a
  `PROTECT` machine, puts FLEX's defaults back. Both the Part and its
  battery-backed copy change, as stock's commit changes both.
- **The Part validator** sees stock's FLEX defaults in a `PROTECT` track's
  12 bytes while it runs, so it never clamps the machine's values; the
  machines' `validate` wrap it. A track signed by a machine octabam or
  Modwerk knows (`AB`, `TB`, `CX`, `FM`, `PL`, `S2`) that is not in the build
  is protected the same way (one value out of FLEX's ranges makes the boot's
  battery restore refuse the whole Part); it plays as stock plays it.
- **The signature is three knob bytes** of NEIGHBOR's that FLEX tracks keep
  too, as octabam's machines have it: a FLEX track whose NEIGHBOR knobs were
  left at a machine's signature reads as that machine.
- **The sound.** The FLEX renderer (STATIC's and PICKUP's too) asks the
  companion first: an owned track goes to its machine's `render`, which
  calls `ot_stock_render` and changes the samples it shipped. SYNTH MACHINE
  v1.0 keeps its own entry in the renderer table and calls the stock
  renderer for tracks it does not play, which comes here: both work in one
  build.

## The sites

| site | what |
|---|---|
| 0x40002318 | the Part validator's entry |
| 0x40004008 | the FLEX renderer's entry |
| 0x40031e74 | the page resolver's PLAYBACK case |
| 0x400334d8 | name(row) |
| 0x4003d718, 0x4004c36a | names by machine byte |
| 0x4003c928 (keep2), 0x4003c950 | SRC SETUP's name table and its bound |
| 0x4003c980, 0x400585dc | SRC SETUP's row and its opening |
| 0x4003a52e, 0x4003cd98 | SRC SETUP's edit and draw columns (a machine edits FLEX's bytes) |
| 0x400585fa | SRC SETUP's row count |
| 0x4005a616, 0x4005a850 | SRC SETUP's two commits |
| 0x40079248 | the machine chooser's row count |
| 0x40078678, 0x400786c8, 0x400786ce | the chooser's row drawer, row match and highlight |
| 0x40078886 | the chooser's opening row |
| 0x4007981e | the chooser's commit |
| 0x40079904 | the chooser's cursor bound |
| 0x40078c22, 0x40078cb0, 0x40078d2e, 0x40078dee, 0x40078e7c | the chooser's row clamps |
| 0x4003bff0 | the parameter page by machine byte |
| 0x4003a524, 0x4003c8ca, 0x4003d134, 0x400585ea (keep2) | the page table's readers |

None is a byte a mod of octabam2elemod v1.0 writes. SYNTH MACHINE v1.0
keeps the renderer table's entry (0x400d6438) and the resolver's tail
(0x40031ece). The companion's commit seam is one instruction past
octabam's own 0x4007981c, so it ends where SYNTH's 0x40079816 begins.

## Checked

In octabam's ot_emu (OS 1.40C), with CRUSH alone and with CRUSH and two
test machines:
- the chooser lists CRUSH below PICKUP; YES signs track 1 `CR` 1 in the
  Part and its battery copy, with FLEX's defaults, and the title and the
  SRC page say CRUSH;
- FLEX back unsigns it;
- with three machines the chooser scrolls to the eighth row, which commits;
  reopened, it highlights the track's machine;
- SRC SETUP lists CRUSH;
- playing a trig on the CRUSH track runs its render through the stock FLEX
  renderer at every call, also with octabam2elemod v1.0's SYNTH MACHINE and
  SCALE QUANTIZER in the build;
- the Part validator called on a Part with a CRUSH track, an `AB`-signed
  track (ANALOG BD absent) and a plain FLEX track, each with PTCH 0 and LOOP
  9: stock clamps all three, the companion only the plain FLEX track;
- with no machine, a chooser commit, the SRC page and SRC SETUP leave the
  same Part, battery copy and screen as stock.

Not yet run on a unit.

## Not yet

- Pools and engine lists (`pool_open` and the rest): a machine's row has
  stock's sample pool.
- DSP voices (`source_kind`): ANALOG BD and BLACKPYRAMIDS-303 render on the
  DSP, over SPRING REV's words, and need a DSP-side event at the source seam
  first.
- `sdk.octabam --machines`, to move octabam's machines onto the table
  (OCTA303 first: it uses ANALOG BD's seams, and renders on the ColdFire).
