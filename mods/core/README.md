# core

The boot copier and the hook bus for Digitakt mk1 OS 1.53 and 1.54. Every
set of linkable (format 2) mods needs exactly one core. On its own it
changes nothing the unit does.

`core.s`, `settings.s`, `render.s`, `machines.s`, `fw.s`, `master.s`, `audio.s` and `menu.s` have no
addresses:
`mod.json` gives them (`defsym`), with the sites, for 1.53, and again for
1.54 under `ports`. The stock file you build with picks them.
[../core-dn1](../core-dn1) builds the same `core.s`, `settings.s` and
`render.s` for the Digitone mk1, and [../core-dt2](../core-dt2) `core.s`,
`settings.s` and `fast.s` (with its own `personalize.s`) for the
Digitakt II. The addresses in this README and in the sources' comments
are 1.53's.

- **At boot** (the OS entry's call at 0x40000538), `boot` copies the RAM
  image the linker built to its area (0x47BC0000 from 4.0, 0x47BE0000
  before), zeroes `.bss`, starts the DTIM0
  counter if nothing has, and goes on to the call it replaced.
- **The shared sites** become events that mods subscribe to, in `order`:
  `ev_tick`, `ev_draw`, `ev_key`, `ev_enc`, `ev_settings`, `ev_render_in`
  and `ev_render_out`. `core_additem(menu, row)` adds a SETTINGS row. The
  table of events, their C prototypes and the sites core owns is in
  [docs/ADAPTING.md](../../docs/ADAPTING.md), "The hook bus".
- **SRC machine slots** (2.1, `machines.s`, Digitakt mk1 only): mods add
  SRC machines past the stock four through the table `core_machines`, and
  core makes the firmware list, name, set, load and render them, and treat
  them as the stock machine they stand for where the firmware tests for
  one. Several mods can add machines at once (NEIGHBOR is 4, DIGISLICER
  5). [docs/ADAPTING.md](../../docs/ADAPTING.md), "SRC machines".
- **3.0: core 2.1 and more** (Digitakt mk1). The same 39 sites with the
  same bytes, the same events and tables, every 2.1 symbol in its place
  (2.1's code is the start of 3.0's), and no new site. It adds:
  - the firmware locations machine mods use, as named exports (`fw.s`:
    `fw_track_blocks`, `fw_kit`, ...), so a mod that names them builds for
    another OS unchanged;
  - `core_machine_ui(id)`: the page a machine describes in its
    descriptor's tail, which [../machine-pages](../machine-pages) draws.

  A mod built for 2.1 links with 3.0 as it does with 2.1.
  [docs/ADAPTING.md](../../docs/ADAPTING.md), "Core 3.0".
- **4.0: core 3.0 with twice the RAM** (Digitakt mk1). The same code,
  sites, events, tables and exports; the linker lays the mods' code, tables
  and variables out from 0x47BC0000 to 0x47C00000, 256 KB in place of
  128 KB (the profile's `core_ddr`; nothing in core's code changes for it).
  A builder takes the newest core, 4.0, for every selection.
  [docs/ADAPTING.md](../../docs/ADAPTING.md), "The mods' RAM".
- **4.1: core 4.0 and the master output** (Digitakt mk1, `master.s`, last
  in `sources`, so 4.0's code and symbols keep their places). Seven sites
  more, which mods that work on the whole mix hooked for themselves:
  - `ev_render_master` (0x400721e6): the block's master pair, before it
    goes to the outputs and the USB stream; core puts the stage's MACSR
    back and clears ACC0 and ACC1 after the handlers, so they may use the
    EMAC;
  - `core_mpages`: pages of the master view (FUNC+LFO), through its draw,
    keys, knobs, title and page clear (five pointers in its vtables), with
    `core_mpage_shown`, `core_mview` and `core_mview_draws`;
  - `ev_fxmix` (0x40044acc): rows in SETTINGS > GLOBAL FX/MIX;
  - and `fw_invalidate(view)`.

  With no subscriber the unit is stock. Digi EQ and Tone+FX, which took
  the same places, combine on it; [examples/master-mono](../../examples/master-mono)
  is the smallest mod that uses it. [docs/ADAPTING.md](../../docs/ADAPTING.md),
  "The master output and the master pages".
- **4.2: core 4.1, exclusive audio and the Mod Menu** (Digitakt mk1,
  `audio.s` and `menu.s`, after `master.s` in `sources`, so 4.1's code and
  symbols keep their places). One site more and two tables, their state in
  `.run`:
  - `core_audio` (`audio.s`): a mod that wants the whole audio contributes
    an owner record, as on the Digitone. While one is on, the Digitakt's
    engine stops (0x40077e94, the instruction that starts it: the render
    skips to just past the mixer's call, so the master stage and
    `ev_render_master` do not run either), and the owner's `render` turns
    the inputs into the output; the sequencer, MIDI, USB and the sampler
    run on. Core lends the owner the delay's 4 MB line meanwhile (the
    profile's `lend` area), clearing it before and after;
  - `core_menu` (`menu.s`): core-dn1's Mod Menu, its grid of tiles, shown
    by `core_menu_show(track)` (machine-pages 1.7's command rows, fxkit's
    PEDALS); its key, knob, tick and draw handlers are core's own
    subscriptions;
  - `fw_tempo`, `fw_timeline` and `fw_transport` as exports.

  With no owner on and the menu closed, the unit is stock.
  [docs/ADAPTING.md](../../docs/ADAPTING.md), "Exclusive audio and the Mod
  Menu (core 4.2, Digitakt mk1)"; `examples/dt-thru` is the smallest owner.

Build it with the SDK (it needs m68k binutils):

```bash
python -m elekloader.sdk.build mods/core --stock Digitakt_OS1.53.syx      # out/core-4.2.elemod
python -m elekloader.sdk.build mods/core --stock Digitakt_OS1.54.syx      # out/core-4.2-os1.54.elemod
python -m elekloader.lint mods/core/out/core-4.2-os1.54.elemod --stock Digitakt_OS1.54.syx
```

Core 3.0's files are on the
[core-v3.0](https://github.com/irpina/elekloader/releases/tag/core-v3.0)
pre-release and in the mod shop; 2.1's are in the latest release (v0.4.0);
4.0's are on the [core-v4.0](https://github.com/irpina/elekloader/releases/tag/core-v4.0)
pre-release; 4.1 and 4.2 are on main, not released yet.

The 1.54 port is the same core: the same code, sites and sections, built
with 1.54's addresses. Of its 39 sites, 38 are where 1.53 has them, and
0x400a1706 is 0x400a1862. Of the routines and data it names, DRAWALL,
OP_NEW, ITEM_CTOR, MENU_ADD, FN_MGR, ITEM_ID and BLIT moved by 0x228, and the
machine menu's default names by 0x380. Each was found again by its own code
with the addresses in it masked, and checked against the code that calls
it.

Checked by cold-booting it in digikit's emulator: alone, every screen and
the audio are identical to stock; with other mods, as those mods' own
checks describe. 2.1 alone: the check's nine screens are identical to
stock's, and the audio too, apart from the recording's silent end being
1 ms longer (2.0a does the same). The machine slots were checked with
digislicer 2.0 and digineighbor 0.6, as their READMEs describe.

2.1 on 1.54, alone, the same check against stock 1.54: every stage passes,
the nine screens are identical, and the audio too, apart from the silent
end being 1 ms off, as on 1.53. With [hello-marker](../../examples/hello-marker)
added, every screen differs from stock only at its corner square. The
machine slots have not been checked on 1.54 with a mod that adds a machine:
digislicer and digineighbor are not built for 1.54 yet.

3.0, on 1.53 and on 1.54: `tests/test_sdk.py` checks it against the
released 2.1 (the same sites, tables and symbols, 2.1's code at the start
of 3.0's), and `tests/test_catalog_link.py` links every Digitakt mod of the
shop alone and in pairs against both: 144 selections, each linking or
refused as it is with 2.1, with the same messages. In digikit's emulator,
3.0 alone against stock: the nine screens identical, the audio too apart
from the silent end; NEIGHBOR 0.6 on 3.0 against 0.6 on 2.1, through its
menu, page and LFO page: every screen and the audio identical. On a unit
(OS 1.54), with machine-pages and every machine mod built for it, as
[../machine-pages](../machine-pages) describes: everything worked.

4.2, on 1.53 and on 1.54: `tests/test_sdk.py` checks it against the
released 2.1 and 4.0 and against a 4.1 built from main
(`ELEKLOADER_CORE_41`): every older site with its bytes, table,
contribution and symbol where it was, the older `.run` at the start of
4.2's and 4.1's `.bss`, then one site, two tables and the Mod Menu's
four handlers. The shop's Digitakt mods, alone and in pairs (105
selections on 1.53, 91 on 1.54), link or are refused with 4.2 exactly as
with 4.1. The TypeScript build is byte-identical to Python's in 17
builds and refusals (the two pedal builds, THRU, the lent line refused
beside 2.1 and 4.1). In digikit's emulator: fwcheck of 4.2 alone against
stock passes every stage, its nine screens are the stock's and it
differs where released core 4.0 does (a boot frame, and on 1.53 one 100
ms audio window); so do the two pedal builds; with fxkit and the pedals,
each pedal from PEDALS gets the audio and the cleared line, plays the
inputs and gives both back on NO, and the stock render (61% of a block)
comes back after it (`mods/fxkit-dt1`, "Checked"). Not on a unit.
