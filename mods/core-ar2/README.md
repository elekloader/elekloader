# core-ar2

The boot copier, the hook bus, mod storage, parameter pages and added machines for the Analog Rytm MKII. From
0.3 it is for OS 1.74 only; 0.2 is the last core with 1.73 (below). It builds the Digitakt mk1's `core.s`, `settings.s` and
`render.s` with the Rytm's addresses, which `mod.json` gives (`defsym`,
`sites`), and from 0.2 its own `store.c` and `store.s` (Mod storage, below),
from 0.3 with `route.s`, from 0.4 with `pages.s` (Parameter pages, below), from 0.5
with `machines.c` (Machines, below), from 0.6 with page functions in `pages.s`, from 0.7 with
write-through storage (Write-through, below) and `finish.s` (The OS upgrade's restart, below),
and from 0.8 with lockable parameters in `lock.c` and `lock.s` (Lockable parameters, below) and
their p-locks in `rows.c` and `rows.s` (Mod p-locks, below), and from 0.9 the smoothed value,
velocity and aftertouch destinations and the FX track's parameters (below), and from 0.10 field
defaults and the event `ev_project_save` around every project save (below), and from 0.11 locks a mod keeps
itself (below). On its own it changes nothing
the unit does, apart from that restart, which it makes safe.

The Rytm runs on the Digitakt mk1's platform (a ColdFire MCF5441x), and its OS
entry, bootstrap loader and UI loop are the Digitakt's, so each site below is
the counterpart of one the Digitakt mk1 core hooks, found by matching the two
images and checked by the code around it
(`elekloader/devices/analog_rytm_mk2.py` has the platform evidence).

| event | Rytm 1.73 (to 0.2) | Rytm 1.74 | Digitakt 1.54 | note |
|---|---|---|---|---|
| boot | 0x400008c0 | 0x400008c0 | 0x40000538 | the OS entry's third call, VBR := 0x40000000 |
| `ev_tick` | 0x400a2bee | 0x400a2c5a | 0x4000a770 | about 20 Hz (the Digitakt's 30) |
| `ev_draw` | 0x400a2c56 | 0x400a2cc2 | 0x4000a7d6 | drawAll(ctrl, bmp), 128 x 64 |
| `ev_key` | 0x400a47cc | 0x400a4838 | 0x4000b770 | |
| `ev_enc` | 0x400a4816 | 0x400a4882 | 0x4000b7ba | |
| `ev_settings` | 0x400db87e | 0x400db8ea | 0x40058800 | the SETTINGS menu's builder, 0x400db174 (1.73); rejoins 6 bytes on |
| `ev_render_in` | 0x4011fb02 | 0x4011fb6e | 0x40077428 | the audio interrupt (vector 191), 32 frames a block |
| `ev_render_out` | 0x40121656 | 0x401216c2 | 0x400784c8 | its only exit; the frame is 0xb4 (the Digitakt's 0xa8) |

1.74's main OS is 1.73's with code added for Outbox 8's main-output
routing, so its sites and routines are 1.73's moved (+0x6c, `FN_MGR`
+0x1cc), each found again by its own bytes. A core built for one release
is refused with the other's stock file.

**OS versions.** New core versions drop old OS support: 0.3 builds for
1.74 only (`mod.json`'s `os`, with no `ports`), and is refused with a 1.73
stock file. 0.1 and 0.2 build for 1.73 and 1.74 (1.73 their `os`, 1.74 their
port) and keep their 1.73 support as released; 0.2 is the last core with
1.73. A build takes the newest core it has for the stock OS
([CORES.md](../../docs/CORES.md#how-a-build-picks-one)), so a 1.73 build
takes 0.2, and a mod that needs 0.3 (`"core": "0.3"` in `resources`) is
for 1.74 only. Core 0.2 for 1.73 is built from the tree before 0.3
(commit 92a99c9: `git worktree add ../core-ar2-0.2 92a99c9`, then build its
`mods/core-ar2` with the 1.73 stock file).

The routines `core_additem` calls are the Digitakt's, the same code with the
addresses in it masked: `OP_NEW` 0x40082a40, `ITEM_CTOR` 0x40072fbc,
`MENU_ADD` 0x400727f2 and `FN_MGR` 0x4019411a on 1.74 (the manager for the
same function-pointer type, by its typeinfo's name).

What differs from the Digitakt mk1 at these sites:

- **The tick.** The UI routine passes the view controller from a pointer at
  +0x20 of its object and skips the frame when it is null; the Digitakt passes
  the controller embedded at +0x40. `core_tick` sees the same argument either
  way, and its target (the dirty flag at +0x20) is the Digitakt's byte for byte.
- **The render.** The Rytm's drum voices are analog. Its audio interrupt is
  the digital path: the sample voices, the delay and the reverb. In order:
  `ev_render_in`; the analog voice controls; the PRE and POST effect returns
  mixed from the previous block's delay output (0x80007c44) and reverb buffer
  (0x80007b44); the voices received from the ADC; then 0x4010e650 builds the
  sends and runs the delay (0x4010d9b8) and the reverb (0x4010d3c8); then
  `ev_render_out`. Two other handlers (0x4013cc08, 0x4013e9b8) can take vector
  191, as on the Digitakt, whose core hooks only the main one too.
- **No machines.** The Digitakt's SRC machine slots (`machines.s`) and its
  firmware exports (`fw.s`) are not ported.

## Mod storage (0.2, 0.3, 0.7)

Mods declare fields in their mod.json (`storage`) and read and write them
with `core_store_get` and `core_store_set`, which mask interrupts while they
do (one byte can hold several mods' fields); elekloader places the fields
from the build's lockfile. docs/ADAPTING.md ("Mod storage (core-ar2 0.2,
Analog Rytm MKII)") is the mod writer's guide; `store.c`'s opening comment
has the byte layout.

Every byte used is one the stock OS never reads or writes, or (from 0.3)
one it only ever holds at 0 and reads, where it matters, through a site
core hooks; no stock function gives up space:

| scope | live, while in use | saved in |
|---|---|---|
| sound | live sound +0x6d..+0x6f; from 0.3 also the 4 velocity routes' amount low bytes, +0x71, +0x74, +0x77, +0x7a (56 bits in all; 24 with 0.2) | stored sound +0x70..+0x72 and (0.3) +0x78..+0x7b, with the layout ID (+0x73) and the sound generation (+0x77) |
| kit | live kit +0x796..+0x79d (FX words 38-41) | stored kit +0x7c6+0x50..+0x57, with the layout ID (+0x58) and the kit generation (+0x5c) |
| global | `core_store_globals` (5 bytes a slot) | stored global slot +0x4b..+0x4f |
| pattern track | live track +785 (64 bytes) | the tagged store |
| pattern | live pattern +85696 (64 bytes) | the tagged store |
| project | the tagged store | the tagged store |

The tagged store is byte +2 of each of the 256 entries of the 17 songs in a
project image (4,352 bytes). The stock OS leaves those bytes out of a
project file, so core hooks the file save and load and the save to a
buffer in RAM, and carries the store across.

**The velocity routes (0.3).** A live sound's four velocity routes (+0x70,
3 bytes each) hold an amount word, then a destination. The amount is the
word's high byte: stock writes the low byte only as 0 (the route load's
byte << 8, the defaults, the menu's whole steps) and saves only the high
byte, so the four low bytes carry the sound scope's bits 24..55. Where stock
reads them in a way that matters (the push to the engine, the VELOCITY MOD
menu's getAmount) they read as 0 again; the menu's setAmount and setRoute
(paste, undo) keep them, and so does its reset (FUNC+PLAY). Both menus'
setRoute store only the amount's high byte, so a route pasted from velocity
into aftertouch keeps the aftertouch low byte at 0, as stock has it. The
aftertouch routes' low bytes are not used: stock holds them at 0 and core
leaves them so, which needs no other aftertouch site. Kept in `route.s`.

**Per-part saves (0.3).** After a front-panel edit and on a pattern change,
stock saves each edited track, a pattern's settings and its parameter locks
into the stored project one part at a time, not through the pattern
serializer, so 0.2 kept a pattern-track or pattern field only when the whole
pattern was saved (SAVE PROJECT, a project file): a field a mod set was lost
at a power cycle. From 0.3 each of those saves also writes the pattern's
track and pattern fields into the store. No stock routine loads one of
those parts by itself: a pattern comes back whole, through the pattern
load. Up to 0.6 a field a mod set from its own page reached the stored
project only at the next stock save of that pattern's track, settings or
locks, or the next whole save.

**Write-through (0.7).** Stock writes each parameter edit of the current
kit both into the live kit and into the stored project's copy of it (the
working kit, stored +0x19c9c5, the copy of the kit whose index is at
+0x19d3f7), which is what a power cycle brings back. Up to 0.6 a field set
with `core_store_set` waited for a stock save of its part, and was lost at a
power cycle before one. From 0.7 `core_store_set` writes the item's bytes
straight into its stored copy, with the item's layout ID and generation
where it carries its own:

| item (live project 0x407c89cb +) | stored copy (stored project +) |
|---|---|
| the edited current kit (+0xa8cf18 + 2531 k), and its 12 sounds (+42 + 152 t) | the working kit (+0x19c9c5), its sounds at +46 + 162 t |
| a saved kit (+0xadc098 + 2531 k), and its sounds | kit k of the 128 (+0x19d3fb + 2610 k) |
| a pool sound (+0xb4b27c + 152 i) | pool sound i (+0x1f4397 + 162 i) |
| a global slot | the stored slot (+0x1f9dee + 80 g, +0x4b) |
| a track or the pattern of the working pattern (+24) or of pattern n (+85784 + 85760 n) | their slots in the tagged store (index 0, or n + 1) |

An edited kit that is not the current one, and items outside the live
project, reach the stored project by stock's saves, as their stock data
does: a power cycle brings an edited kit back from its saved copy in
stock too.

**SAVE PROJECT AS (0.7).** SAVE PROJECT AS writes only the project's name
into the stored project (the first of its two project saves, 0x400ae310,
flags 0x40) and then the whole live project into the file's buffer
(0x400b0870). After a save of the live project into another image, core
copies its own bytes from that image into the stored project, by the parts
the save wrote (flags 2 the global slots, 4 the kits and the working kit
when the image's index is the stored project's, 8 the tagged store, 0x20
the pool), so a power cycle straight after the save keeps the mods'
fields. The same save routine also has a SysEx dump of the working project
(0x40034d86, in the dump request handler 0x4008680c), but OS 1.74 never
reaches it: the SysEx router above that handler (0x40085e44, code the Rytm
shares with the Analog Four) has no caller, and the ID table sends its
message ID, 0x20, to a routine that does nothing (checked 25 Oct 2026: in
an emulator the request was dropped over USB and DIN and 0x40034d86 never
ran; on a unit it got no reply over USB or DIN while a kit request was
answered on both). Core hooks the site anyway, so the save routine behaves the same
wherever it runs. A Total Recall take runs no
serializer: it sends the stored project as it is (Overbridge command 0x20,
type 0x1b), so it carries whatever mod fields the stored project holds, and
saves nothing on the unit (checked on a unit, 25 Oct 2026: with
write-through off, a take and a power cycle lost the same fields as no save
at all; the take's image held exactly the stored project's bytes).

The 32 sites, by `jmp` unless marked, each with its displaced instructions
in `store.s` or `route.s` (0.3 hooks them on 1.74; the 1.73 column is
0.2's, which has the first 22):

| site | 1.74 | 1.73 (0.2) | why |
|---|---|---|---|
| pattern save / load | 0x4000937c / 0x40009146 | 0x4000937c / 0x40009146 | pattern tracks, patterns, and the project region with the working pattern |
| sound save / load | 0x4000843a / 0x400081f6 | 0x4000843a / 0x400081f6 | the sound's 3 bytes, ID and generation |
| kit save / load | 0x400088d6 / 0x4000863e | 0x400088d6 / 0x4000863e | the kit's 8 bytes, ID and generation |
| global save / load | 0x40007a5c / 0x4000763c | 0x40007a5c / 0x4000763c | the slot's 5 bytes |
| pattern init, track init | 0x40011ab8, 0x400118e8 | 0x40011ab8, 0x400118e8 | clear the fields (stock leaves the spare bytes as they were) |
| sound init | 0x400122b6 | 0x400122b6 | the same |
| project init | 0x40011d38 | 0x40011d38 | CREATE NEW and the project reset: a fresh store |
| project load | 0x4000cd2e | 0x4000cd2e | after any load: drop scopes another layout wrote, write this build's header |
| SysEx dump of the working project (`keep2`) | 0x40034d86 | 0x40034d86 | which image is being saved; 1.74 never reaches it (above) |
| file save (`keep2`) | 0x400b0870 | 0x400b0804 | the same, for a project file |
| file load (`keep2`) | 0x40035000 | 0x40035000 | which image is being loaded |
| SysEx song copy, write, swap (`keep2`) | 0x4018b8b8, 0x4018c266, 0x4018c488 | 0x4018b6ec, 0x4018c09a, 0x4018c2bc | copy whole stored songs but leave the target's store bytes |
| SysEx pattern copy, write, swap (`keep2`) | 0x4018bc5c, 0x4018c902, 0x4018cb34 | 0x4018ba90, 0x4018c736, 0x4018c968 | clear the target pattern slot's fields, which can't follow the pattern |
| track save (`keep2`, 0.3) | 0x400bf818 | — | an edited track saved by itself (0x40008dc8): the pattern's track and pattern fields too |
| pattern settings save (`keep2`, 0.3) | 0x400aacc4 | — | the same, for the settings (0x40008b06) |
| locks save, one parameter / all (`keep2`, 0.3) | 0x400ac2ce / 0x400ac2ec | — | the same, for the parameter locks (0x4000940c, 0x40009216) |
| velocity push to the engine (0.3) | 0x4011f6a8 | — | the engine gets the amount with its low byte 0 |
| VELOCITY MOD getAmount (0.3) | 0x400c5326 | — | the menu reads the amount with its low byte 0 |
| VELOCITY MOD setAmount (0.3) | 0x400c52b2 | — | an edit keeps the low byte (interrupts masked from its read to the write) |
| setRoute, velocity / aftertouch (0.3) | 0x400c51b4 / 0x4003391e | — | a pasted or undone route stores only the amount's high byte |
| VELOCITY MOD reset (`keep2`, 0.3) | 0x400c542c | — | the defaults (0x4001224e) keep the low bytes (interrupts masked meanwhile) |

After CREATE NEW and after every project load (the project init and
project load sites), core calls the mods' `ev_project` handlers with what
happened and which scopes came in as zeros.

From 0.8 a project load asks first after an edit of only mod fields, as it
does after a stock edit (ANY CHANGES WILL BE LOST from the project manager,
SAVE ... BEFORE LOADING from LOAD PROJECT). Stock marks the project modified
(0x400aed98(project, 1), kept at 0x405c8e4e) from the UI task on the
project's change signal, which an edit of mod fields alone never sends.
`core_project_edited()` asks for the mark from any context, and
`core_edit_tick` (`ev_tick`) makes it with the project model at 0x419f28a8.
Core calls it after a turn a mod's page knob takes, a lockable parameter's
CC or NRPN, and RANDOMIZE PAGE and RELOAD PAGE on a mod's knob; a mod
calls it after its own keys, encoders or settings change a field.

What it does not do:

- **Global SysEx dumps** have no room for a layout ID, so a global slot
  received as SysEx drops its mod data, even from the same build; its
  stock settings load as usual.
- **Freed bits** (a field a build dropped) are not cleared on save; no other
  field is placed there until the scope's generation is bumped, which turns
  the old data to zeros anyway, and the field takes them back, with its
  values, if its mod returns.
- **Pattern tracks** are limited to 16 bits each: three bytes a track would
  not fit in the store.
- **Sounds**: 56 bits, set by the stored sound's spare bytes, 49 for mods
  in a build with an `"lfo"` parameter (`core_lfo_dst`, below). The four
  aftertouch routes' low bytes are free in a live sound too, but there is
  no stored byte left to keep them in.
- **voice-dac** (rytm2_mods) hooks the same velocity-route sites itself
  and so does not combine with 0.3; moved onto mod storage, its route bytes
  are sound fields.
- **Projects saved before OS 1.70** are not supported.
- **Patterns copied by the SysEx project commands** between stored patterns
  (in one project or two), swapped, or written into one from a received
  record don't take their pattern-track and pattern fields with them: those
  are kept by pattern slot in the tagged store. Core clears the target
  slot's fields instead, so the pattern loads with its mods' defaults, as a
  pattern received as a SysEx dump does. A target project saved with another
  mod layout keeps its store as it is.

## The project save (0.10)

From 0.10 core calls the mods' `ev_project_save` handlers,
`void f(unsigned pre, unsigned how)`, before (`pre` 1) and after (`pre` 0)
every whole save of the live project to a +Drive slot. On 1.74 all of them
end in one routine, 0x400b07a4 (project model, slot, progress), the one that
holds the file save site above: it makes the slot the active one, serializes
the live project into the slot's image, writes the image to the +Drive
(0x40034e98) and clears the modified mark. Core takes its first
instructions (`jmp`, `st_proj_save` in `store.c`, the displaced ones in
`store.s`). Two jobs call it:

- **Project Save** (0x40052f06, queued by 0x40052908, which shows SAVING
  and sets the name): every save the front panel starts goes through
  0x4005417e, which checks write protection and asks OVERWRITE first: SAVE
  PROJECT, SAVE PROJECT AS, the project manager's saves and SAVE ... BEFORE
  LOADING.
- **Project Load** (0x40052fb0): LOAD of an empty slot, which runs CREATE
  NEW (0x400b3bd6, so `ev_project` with `how` 0 first) and then writes the
  new project into the slot.

`how` holds the kind in bits 0..7: `CORE_SAVE_SAME` (1) when the slot is
the active one, `CORE_SAVE_OTHER` (2) when it isn't (SAVE PROJECT AS to
another slot, or a project that has no slot yet), `CORE_SAVE_NEW` (3) for
the empty slot's LOAD (told by the call's return address, 0x400530e6). The
slot is in bits 8..15, 0..127 (the number the screen shows, less one; 255 when stock has none), and the call after has
`CORE_SAVE_FAILED` (bit 16) set when the save failed. A field set with
`core_store_set` in the call before is in the saved copy: the serializer
reads the live homes, and a whole save copies the stored project's header
and project data into the image (`st_pat_save`). So a SAVE PROJECT AS
copy can be given its own project-scope value while the slot it came from
keeps the old one on the +Drive.

Both calls come from the save's job, not a UI tick, with the save screen
up. A handler keeps them short, starts no load or save, and leaves MIDI or
USB work to its next `ev_tick`. It doesn't call `core_project_edited`
there: the save clears the modified mark after the call before.

Not covered (no event): a project received over SysEx or a Transfer file
load written into a slot, CLEAR and INIT NEW on the project manager (they
change a slot without the live project), and the stored project's own
writes outside a save (the "Write project MRAM" job, 0x400a1058, and the
boot's and reset's copies at 0x400fb31a and 0x400fb36c, all from the live
project into the stored one at 0x405c8f20). The "Project::updateMirror"
job (0x400ae2da) serializes the live project into a mirror buffer and is
no save either.

## Parameter pages (0.4, 0.6)

`pages.s` takes the first instructions of the four routines every parameter
page goes through and hands each call to the mods' entries in `core_pages`
and `core_params` before stock (docs/ADAPTING.md, "Parameter pages (core-ar2
0.4, Analog Rytm MKII)"; the SDK header is `analog-rytm-mk2/pages.h`). Each
stub runs the instructions its jump displaced and resumes after them, so
deeper hooks in these routines (filter-hz's text, ctrl-all's turn) are
untouched:

| routine | site (1.74) | displaced | resumes |
|---|---|---|---|
| page_info | 0x400ff5e0 | `moveq #11,%d1; movel %sp@(4),%d0` | 0x400ff5e6 |
| page_get_value | 0x40038462 | `lea %sp@(-12),%sp; moveml %d2-%d3/%a2,%sp@` | 0x4003846a |
| param_apply_delta | 0x400378a6 | the same | 0x400378ae |
| param_value_text | 0x400a864c | `lea %sp@(-20),%sp; moveml %d2-%d4/%a2-%a3,%sp@` | 0x400a8654 |

A turn a mod takes is followed by core's call to the view's redraw
(0x40078580), so the mod's own value shows at once.

From 0.6 page_info asks `core_page_fns` first: `{page, info}`, where
`info(page)` returns the `core_pages` entry to answer, or 0 to pass on to
`core_pages` and stock. A page whose knobs change with the selected sound
uses it (voice-dac 1.4's DAC FILTER leaves RES empty for LP1 and HP1, and
FRQ and RES with TYP OFF). Additive: a build with no entries in it behaves
as 0.5.

The page keys' lists: each view's constructor passes its list to a setter
(0x401a1e46) with `moveq #n,%d1; movel %d1,%sp@-; movel #list,%d0`, ten
times in one routine. Core takes those three instructions per view and
passes a list of its own instead (`view_list`): stock's pages, then the
`core_view_pages` entries for that view, in order. The view keeps a copy
as a vector at +0x7c, the selected index at +0x8c.

The page key's step (`core_cycle`, at 0x4003ab86 over the index's
`remsl`, its store and the invalidate) skips a page whose entry's `shown`
answers 0, then pushes the new page's title to the header as stock's
page_view_activate does. The view is told by its copy of the list: the
first of core's ten lists with the same pages. The FX track's page code
asked a view shown again to go back to its first page (0x400cbd3e, a
`beqs` around that call); core makes it a `bras`, so FX views keep their
page as the voice track's do. Both were mods' hooks before (voice-dac's
cycle, lfo-rnd's title, comp-preview's set-page slot).

| view | site (1.74) | stock pages |
|---|---|---|
| SRC | 0x400caf04 | 0 |
| SAMPLE | 0x400caf7e | 4, 5 |
| FILTER | 0x400cafee | 6 |
| AMP | 0x400cb040 | 7 |
| LFO | 0x400cb096 | 3 |
| DELAY | 0x400cb0e8 | 8 |
| REVERB | 0x400cb144 | 9 |
| DIST | 0x400cb196 | 10 |
| COMP | 0x400cb1ee | 11 |
| FX LFO | 0x400cb25a | 3 |

**Breaking change.** Mods built for 0.3 that jumped from these entries
themselves (rytm2_mods v1.2's lfo-rnd, voice-dac, euclid-accents,
velocity-humanise), or that patch a page key's list (also comp-preview
1.0), overlap core's sites and are refused at link; their 0.4 versions
contribute entries instead. Mods that hook the page key's step or the COMP
view's set-page slot themselves (lfo-rnd 1.2's and voice-dac 1.3's first
builds, comp-preview 1.1's) overlap them too. `core_view_pages` entries have three fields, so a mod built with the
two-field entry of the first 0.4 test builds must be rebuilt.

## The audio load (0.4)

`load.s` subscribes core to its own `ev_render_in` (order 0) and
`ev_render_out` (order 100) and reads DMA timer 0's counter at each:
`core_audio_load` is the last block's time as a share of the block, in
256ths (`analog-rytm-mk2/load.h`). Mods with work that does not change the
sound skip it while the load is high (comp-preview's estimate, above 95%).
On a unit (cpu-probe, 21 Oct 2026, a pattern playing): stock with core alone
averaged 78-80% of the time in the interrupt and peaked at 82-84%; all
eleven rytm2_mods mods and duck added 1 to 2 points; fx-sends' chorus about
5; comp-preview's estimate 4 to 5; with voice-dac's DAC SUM on as well, the
peak reached 95%, with no audible artefacts. The 100% that cpu-probe's CPU
reading touches in that case is the whole processor, the interface's tasks
included: they get less idle time, not less than they need (the knobs keep
responding). With the guard the readings were 92% and a 94% peak, within a
point of the build without it, so at these loads it rarely engages: it is a
ceiling for heavier builds, not a fix for anything heard.

## Machines (0.5)

`machines.c` lets a mod add a machine in one of the hidden slots 34..43
(docs/ADAPTING.md, "Machines (core-ar2 0.5, Analog Rytm MKII)"; the SDK
header is `analog-rytm-mk2/machines.h`). The OS keeps its machines in three
arrays indexed by the machine's number (names at 0x401bf254, home track and
voice mask at 0x401bf42c, the engine at 0x402906dc, all data of the 1.74
image) and in the parameter records each machine owns. Stock hides 34..52
with a voice mask of 0. From `.boot` (core.s's `BOOT_INIT`), before the OS
reads any of it, `core_boot_init` writes each `core_machines` entry's engine,
home track, voices and names into the arrays, and its knobs' names, ranges
and defaults into the slot's records. The OS then lists, stores and maps the
machine as it does its own. elekloader names an entry's slot as the resource
`machine:<n>` (the profile's `slot_claims`), so two mods that take one slot
are refused at link.

Slots 44..52 (the TEST machines) are not offered: mods use their records as
spare parameters (fx-sends, voice-dac). A kit saved with an added machine
loads on a build without the mod with that track's machine set to DISABLE,
as stock does for a machine its voice may not run (checked on a unit, below).

`examples/machine-ar2` is the smallest machine: BD COPY in slot 36, which
runs BD CLASSIC with BD CLASSIC's knobs.

## The OS upgrade's restart (0.7)

When the unit has written a new OS (from Transfer, or any host's OS upgrade),
stock 1.74 shows SUCCESS! REBOOTING, waits 3 s, masks every interrupt
(`move #0x2700,sr` at 0x4009f4ec) and calls 0x40001bce, which waits for its
serial output to drain: while bytes are pending behind the panel link's DMA
(0x413da6bc), then while the UART queue holds bytes (0x413da6cc), then for
the UART to be empty. Only the DMA's and the UART's interrupts move those
counts on. If the panel task queued bytes while a panel DMA was still running,
just before the mask, they are never sent and the unit stays on SUCCESS!
REBOOTING until it is switched off. The new OS is already written, so it
boots then. Nothing the host does or stops doing changes this: it happens
with no traffic at all after the upgrade's last message.

`finish.s` takes that site: `core_finish_drain` masks interrupts only once
nothing is pending, unmasking again to let the interrupts send what was
queued, then calls 0x40001bce as stock does, with interrupts masked. It runs
only when a build on this core is the OS being replaced, so it protects the
upgrade away from a custom build (to stock, or to another build), not one
away from stock. The addresses come from `mod.json` (`FINISH_PANEL`,
`FINISH_UART`, `FINISH_DRAIN`); core-a4 0.4 uses the same file with the
A4's.

Checked in analog-emu (25 Oct 2026; each flash a fresh headless emulator
booted on the source OS, then `elkflash upgrade --no-confirm --wait` of a
custom 1.74 build). From stock with only this routine patched in, 0 of 15
hung; from the same build without it, 5 of 15 (one-sided Fisher p = 0.02).
A source built on core 0.4 hung 0 of 15, so the race is rarer from core
builds than from stock. With the emulator's panel link paced from its baud
divider (the unit's 158.7 kbaud, where it had run about 6 times faster), none
of 30 hung, from the fixed, the unfixed and the stock source alike, so on a
unit the race is likely rare. On a unit (25 Oct 2026), the flashes away
from builds on this core (to other builds and to stock, five flashes)
restarted in 15 s, and on an Analog Four MKII a flash from core-a4 0.4
back to stock 1.56 restarted in 21 s; with a race this rare that is no
proof the hang is gone, only that the routine does no harm.

## Lockable parameters (0.8)

The first part of the lock service (docs/ADAPTING.md, "Lockable parameters
(core-ar2 0.8, Analog Rytm MKII)"). A sound field declared `"lock": true`
gets a lock code (42..127) from the lockfile; the linker writes `{field,
code}` into `core_lock_codes`, with 0x100 set in the code of a field also
declared `"lfo": true`, and the mod adds `{field, id, min, max, short name,
name, category}` to `core_lock_params`. A parameter without `"lfo"` is lockable only:
none of the LFO sites below take it (an NRPN 1:35 that sets its code shows
ERR and modulates nothing). Core gives each LFO parameter a
**pseudo id**, 469 + code - 42, above stock's 469 records: the LFO
destination list holds parameter ids and turns them back into a sound slot
and names, and the code is the slot, so the LFO's DST byte holds it.

| site | 1.74 | what |
|---|---|---|
| LFO apply | 0x4009eac0 (`jmp`, 10 B) | stock skips a slot above 41; core keeps the track's code and the offset stock would add (`core_lfo_offset`), and clears the code for a stock slot |
| id to slot | 0x40006ea8 (`jmp`) | a pseudo id's slot is its code; the sound setter then rejects it (slot above 41), as for any stock id it can't set |
| slot to id | sound vtable +76, 0x401dce70 (`ptr`) | a slot with a code gives its pseudo id, so the DST value and the menu's cursor find it |
| sound edit's write | 0x400bd412 (`jmp`, store.c, `st_sound_through`, 0.9) | stock writes each sound edit into the stored copy at once, the DST byte through 0x40007602, which `core_code_id` passes for 42..127 (for the scene and performance entries), so a code reached the stored byte (0.8: after a power-off it loaded as 0xfb, on core and on stock). After stock's write, a code in the stored byte becomes 41 (None) and goes into `core_lfo_dst` of the live sound the stored one is kept from; a stock destination clears it |
| sound save, load | 0x4000843a, 0x400081f6 (store.c, `st_sound_save`, `st_sound_load`) | stock saves the LFO's destination (live sound +28, a slot) as the stored sound's +100 through a table of its 42 parameters (0x401c5c7c) and loads it through the inverse (0x401c5d24), which have no entry for a code: a code would come back as a stock parameter (42 as FIN). Core keeps the code, less 41, in its sound field `core_lfo_dst` (7 bits, placed by the linker in a build with an `"lfo"` field) and writes 41 into the stored byte, stock's own None (META:None, slot 0): stock, or a build that does not know the code, loads that LFO with no destination, and stock's saves keep it so. Neither table has a bound: a slot above 41 that stock saves comes back as one of its parameters (64, as 42 loads, as ACC, 39), so on load a slot of 42..127 that `core_lfo_dst` did not give (a sound from stock or another layout) is set to None |
| list end | 0x400aa31e (`jmp`) | the destination list builder (0x400aa280) appends the pseudo ids for a sound's set (one whose +76 is core's), in the LFO's list (kind 0x600) only: the velocity and aftertouch lists (0x200) index the audio side's parameter table by code, where a code above 41 is another track's parameter |
| menu label | 0x4004a7e8 (`jmp`) | the category (at most 4 characters; `MOD` when there is none), a colon and the name, through stock's `%.16s:%.32s` |
| DST value | 0x400ffab2 (`jmp`) | the short name |
| randomize | 0x400aa32a (`jmp`) | one parameter's randomize (vtable +28 of every set, and RANDOMIZE PAGE's per knob): a parameter's page knob gets a random value in its range, in the set's sound (its object +24, vtable +40) |
| reload | 0x400af32c (`jmp`) | RELOAD PAGE's copy: after stock's, each parameter's knob takes its value from the track's sound in the current kit's saved copy (live kits +0x4f180) |
| knob label | 0x40037c1a, 0x40037d42, 0x400d37fa (`jsr`) | the calls of 0x400a9000 (a record's label, +0x30) in the page's two knob draws and the knob's pop-up: a parameter's page knob shows its short name |
| knob name | 0x4003a1c6, 0x4003a8d6 (`jsr`) | the calls of 0x400a9022 (a record's name, +0x28) in the header's pop-up as a knob turns: a parameter's page knob shows its name there, not the borrowed record's ERROR |
| value text | param_value_text, 0x400a864c (pages.s) | after every `core_params` text entry passed: a parameter's page knob, whose value stock would check against the borrowed record and write as ERR (as it does with a trig held), shows its value (with a trig held its lock's) as a number |

The step, the validity check, page turns and RANDOMIZE on DST follow from
the list, with no sites of their own. Flags need none: pseudo ids read
record 0, as stock's ids above 468 do, and the list builder's mask check is
skipped for them. Randomize, reload and the label leave a knob to stock whenever stock's
sound setter would take its id (a stock slot, and a common parameter or the
sound's machine's).

A call to a stock routine that returns a pointer is typed as returning a
number: stock returns it in `%d0`, and gcc reads a returned pointer from
`%a0`.

## Mod p-locks (0.8)

The second part of the lock service (docs/ADAPTING.md, "Mod p-locks
(core-ar2 0.8, Analog Rytm MKII)"): a lockable parameter is p-locked like a
stock one, with a trig held and its page knob turned, or by live
recording (its knob turned, or its NRPN or CC received, while live
recording runs), and the mod reads the playing trig's value with
`core_lock_value(track, field)`.

A pattern keeps its p-locks in a pool of 72 rows of 66 bytes, `[code]
[track][64 step bytes]`; stock's codes are 0..41, a mod parameter's row
carries its lock code (42..127). Stock only stores the rows: it edits and
plays a live lock store, an 88-byte record per step (42 lock words and a
count, no room for more), and rebuilds the rows from it, dropping codes it
doesn't know. So core keeps the mod locks itself, in a ledger on the heap:
`{step record's address, code, value}`, sorted by address. Stock moves step
records with memcpy and clears them with a few routines, so the ledger
follows them by address, wherever they go: a whole pattern, a track, a
page or a trig, to and from the clipboards and the undo buffers, and a
trig shift's swap. After a change to a live pattern's mod locks, core
writes them into the stored pattern as rows, as stock's rebuild would.

| site | 1.74 | what |
|---|---|---|
| memcpy | 0x40098bc0 (`jmp`) | a copy of 88 bytes or more: the mod locks of the step records copied go with them, and those it overwrote go (pattern switch, SAVE and RELOAD PATTERN and TRACK, copy, paste and undo of a pattern, track or page, trig shift) |
| memset, operator new | 0x40098c08, 0x40082a40, 0x40082a46 (`jmp`) | the mod locks in the block go (none are left in memory given out afresh) |
| track clear | 0x4001197c (`jmp`) | the track's 64 step records' mod locks go |
| step clear | 0x400ac86e (`jmp`) | after stock, the step's mod locks go (trig lock clear, page clear, a paste's target) |
| trig copy, Euclid convert | 0x400c4d62, 0x400c3eb0 (`jmp`) | after stock lists a step's locks as `{value, code, fine}` for a paste, core lists its mod locks |
| lock set | 0x400c058c (`jmp`) | a listed lock with a code above 41, as a paste sets it, is a mod lock |
| row load | 0x40008f98 (`jmp`) | after stock, the rows with codes 42..127 and a track below 13 become the lock store's mod locks |
| rebuild | 0x40009216 (`jmp`) | after stock's rows, the lock store's mod locks as rows, in code then track order, while fewer than 71 rows are used, so stock's locks always fit first |
| trig locks | 0x4009a954 (`jmp`) | after stock reads a trig's locks, core notes that trig's mod locks for its track (`core_lock_value`), clearing only the codes the track's last trig set; they stay through STOP and a pattern or kit change until the track's next trig, as stock's do, and a project load or CREATE NEW (`st_proj_load`, `st_proj_init`) clears every track's, as stock does at CREATE NEW |
| lock get, set, clear, turn | sound set vtable +60..+72, 0x401dce60..0x401dce6c (`ptr`) | a mod parameter's knob (its page id) with a trig held: the lock goes into the ledger; any other id goes to stock's routine |
| dump out | 0x4000eff2 (`jsr`) | stock's pattern save into a SysEx dump's buffer, then a marker row for its mod rows (code 127, track 0xfe, the layout ID), or, with no free row, no mod rows |
| dump in | 0x4000d79c (`jmp`) | a received pattern, once stock has checked it: its mod rows stay only with a marker holding this build's own layout ID; the marker goes |

Pattern init (`st_pat_init`, Mod storage) also clears the pattern's mod
locks, and a project load whose image's layout ID this build doesn't read
(`st_proj_load`) drops every pattern's: codes are a build's, from its
lockfile. A set or turn takes whole steps, starting a turn from the sound's
value; values are clamped to the parameter's `min..max` and to 16383. A
parameter above 127 (`core_lock_shift`) takes two rows per track, as stock's
fine locks do: bits 13..7 in a row with its code and track, bits 6..0 in a
row with its code and the track + 0x40; core places both or neither, and
loads a first row without its second as the value's bits 13..7. A new row
is refused, as stock refuses one, when 71 rows (two, for a parameter above
127) would be in use. The
ledger holds 16,384 locks at most (all patterns, clipboards and undo
buffers together); a copy that would pass that drops the locks it copies.

Checked in the emulator (dtboot, OS 1.74, `examples/lock-check-ar2` 1.1,
25 Oct 2026), with locks on steps 1 and 6 of track 1 in A01: a hold and a
knob turn makes the row; the trig plays its value; trig copy and paste
(step 1 to 9) carries the lock, and the trig's lock clear removes it; page
clear removes both and its undo brings them back; so do track clear and
its undo, and a track copy, clear and paste; trig shift right twice moves
them to steps 3 and 8, left once to 16 and 5 (wrapping); pattern clear and
its undo; a pattern copy into A03 gives A03's stored pattern the row; after
a power cycle with no save the pattern plays the lock (again on 26 Oct, after
a fix: 0.8's layout check in `st_proj_load` read the ID's bytes wrongly and
dropped every mod lock at each project load and boot). A SysEx dump of A01
carries the mod row and the marker; sent back into A03 it keeps the row
(and not the marker), and with the marker's ID changed or with no marker
A03 gets no mod row. In the audio interrupt (26 Oct, timed with DMA timer
0 in a debug build, A01 playing with its two locks): noting a trig's mod
locks takes at most 70 timer ticks of the 88,010 between audio blocks,
0.08% a trig (503, 0.6%, before it cleared only the codes the last trig
set), so 12 tracks trigging in one block add under 1%. A project saved
as 003 (SAVE PROJECT AS) and loaded back, after another project and after a
project stock 1.74 created (CREATE NEW, so with no layout ID, which booted
on 0.8 with no mod data), brings back the fine rows of C, the sound's mod
fields and a performance depth of C (26 Oct). Euclid's convert (PL1 4,
TRO +1, EUC turned off with FUNC held) treats C's two rows as it treats a
stock filter lock on the same steps: step 1's move to step 2, and step 5's
go, as the stock lock on steps 5 and 9 goes (26 Oct). Not on a unit.

## Mod scene and performance locks (0.8)

The third part of the lock service (docs/ADAPTING.md, "Mod scene and
performance locks (core-ar2 0.8, Analog Rytm MKII)"). A kit keeps its scene
locks and its performance locks in two lists of 48 entries, `{value (8.8),
track, code, scene or performance}` (the kit +2190 and +1950), edited
through an object per list (vtables 0x401df974 and 0x401ddfe8: +20 set, +24
get, +28 has, +32 relative). A mod parameter's lock is an entry with its
lock code, so it shares the 48 with stock's, and copy, paste, clear and the
kit's copies take it as they take stock's. Mods read it with
`core_scene_value(track, field)` and `core_perf_offset(track, field)`.

| site | 1.74 | what |
|---|---|---|
| knob turn, push | 0x400385f8, 0x400387ba (`jmp`) | a parameter view's turn and push: with a scene's or a performance's pad held, a mod parameter's knob (its page id, where stock doesn't take the id for the track's sound) sets its entry: a scene's from the sound's value, a performance's depth from 0, whole (coarse) steps, clamped to `min..max` (a depth to ±127 steps); stock refuses the id there |
| knob value | page_get_value, 0x40038462 (pages.s) | before the mods' `get`: with a scene's or a performance's pad held, a mod parameter's knob shows its entry there (a scene's lock, a performance's depth; with no entry, a scene's knob the sound's value and a performance's 0), as stock's knobs do; the mod's `get` would show the sound's value |
| performance build, scene build | 0x4011f744, 0x4011f844 (`jsr`) | the audio side's lists index a parameter table by track × 42 + 26 + code, so an entry with a code above 41 would write another track's parameter: core gives it track 0xff, and stock takes it as an empty entry (the dummy index 25) |
| code to file, file to code | 0x40007602, 0x400075c8 (`jmp`) | a kit file stores each entry's code as a parameter number (0..41 both ways, a permutation); a drum track's 42..127 stays itself, so a saved kit keeps its mod entries |
| kit dump | 0x4000f2a2 (`jsr`) | stock's kit save into a SysEx dump's buffer, then an empty entry (`00 ff 00 00`) in place of each mod entry |

A kit loaded from an image whose layout ID this build doesn't read
(`st_kit_load`, Mod storage; a received kit dump goes through it too)
loses its mod entries, emptied as stock empties one. `core_scene_value` reads the kit the
audio side plays (SRAM 0x800067b8) and its active scene (+2430);
`core_perf_offset` adds each matching performance's depth times its pad's
pressure (SRAM 0x8000f774 + 2 × the performance, 0..0x7f00), a fraction of
0x8000, as stock's frame adds a performance's to a stock parameter, and is 0
while stock skips the performances (SRAM 0x8000ed74).

Checked in the emulator (dtboot, OS 1.74, `examples/lock-check-ar2` 1.2,
25 Oct 2026), on knob A of the LOCK CHECK page, track 1: scene mode, BD
pad, SCNE held into scene edit, the pad held and the knob turned 20 steps
gives a scene entry (code 42, value 20), and the third bar shows 20 with
the scene active; the entry is still there after a power cycle with no
save; the SRAM scene table holds the dummy index for it where stock would
write index 68 (track 2's first parameter). A kit saved (YES + PLAY MODE)
and dumped (kit request, ID 0x62) carries an empty entry in its place. In
performance edit on the SD pad the knob sets a depth (18) on performance
2; the performance table holds the dummy index for it; with the pad's
pressure at full (CC 36 on the PERF channel, as the emulator's pads send no
pressure) the bar shows 17. The LFO destination list ends with the two
lock-check parameters (ids 469, 470), read from memory after the list
build. Not checked: the velocity and aftertouch lists without them (the
emulator didn't open their parameter choice; the list kind, 0x200, is read
from the code), a scene entry through a project's save and load (a
performance entry of C comes back, above). A kit dump
(26 Oct, lock-check 1.3, C at 900 on track 1, the kit saved) carries the
sounds' mod fields as saved (`001c20` and the layout ID); sent back with
kit 6 as its target it gives stored kit 6 the same fields, and its scene
and performance entries come in empty, as they go out. Not on a unit.

The knob's value with a pad held (26 Oct, lock-check 1.3, panel keys): on
LOCK CHECK's knob A, PERF + SD held and the knob turned shows the depth it
sets (10), and SCNE + BD the scene's lock (11). With no pad held the knob
shows the sound's value, and with a trig held its lock (7). The Rytm takes
a pad pressed within about 0.15 s of PERF or SCNE as a plain press, so
the turn then edits the sound's value instead. LOCK CHECK's bars cover the
bottom rows, where stock draws the SCENE and PERF bar.

## Mod parameters on MIDI (0.8)

The fourth part of the lock service (docs/ADAPTING.md, "Mod parameters on
MIDI (core-ar2 0.8, Analog Rytm MKII)", in `midi.c` and `midi.s`). Stock's
NRPNs for a track's sound use MSBs 0..3; a mod parameter is NRPN MSB 4, LSB
its lock code - 42, its value in the data entry MSB. On a track's channel
stock maps no parameter to 33 CCs (its CC tables, 0x413dc2d8 and the
machines' 0x413dd3b4, read from memory after boot, hold 0 for them) and
leaves them out of the performances' 12 (0x401de1f4, which every track's
channel answers): 8, 9, 48..63, 65..69, 84, 85, 110..116, 119. The
lockfile gives them to fields declared `"cc": true`, in that order, and
keeps each with its field whatever its code; the linker hands core the
table `core_lock_ccs`, `{field descriptor, CC}`.

| site | 1.74 | what |
|---|---|---|
| NRPN in | 0x400b2b26 (`jmp`) | a track's NRPN in (obj, nrpn, value, track, flag), the track's channel's and the auto channel's: an NRPN of MSB 4 on a drum track is core's, which sets the parameter in the track's sound of the current kit, clamped to `min..max` (an LSB with no parameter is dropped); any other goes on to stock |
| CC in | 0x400b0544 (`jmp`) | a track's CC in (obj, cc, value, track, flag): a CC in `core_lock_ccs` whose field has a parameter is core's, set as above; any other goes on to stock |
| live record | `core_lock_record` (rows.c), from the NRPN and CC in above and `core_param_delta` | while live recording runs (0x40036370), the value a mod parameter's knob or MIDI gives the sound is locked on the step the track plays, as stock's live record (0x400af762) finds it, with a lock trig added to a step without a trig |
| knob out | `core_param_delta` (pages.s) | after a mod's `delta` takes a turn of a lockable parameter's knob, core sends it as stock's parameter out (0x400a9754) sends a stock one: only while PARAM OUTPUT is on (MIDI CONFIG +160, 0x400b56c0), as an NRPN (+112 = 0, 0x40085962) or a CC (+112 = 1, 0x4008591a; none for a field without a CC), on the channel stock's 0x400a9844 gives |

Checked in the emulator (dtboot, OS 1.74, `examples/lock-check-ar2` 1.2,
26 Oct 2026), on the LOCK CHECK page: NRPN 4:0 value 100 on channel 1 turns
knob A to 100, and value 16 on the auto channel (14) to 16; NRPN 4:5 (no
parameter) changes nothing; a stock NRPN (LFO SPEED, 1:32) still sets its
knob; CC 8 value 100 on channel 1 turns knob A to 100. With PARAM OUTPUT on
(written into the MIDI CONFIG, since this card's globals have it off and
set to CC), three steps of knob A send `BD 63 04 BD 62 00 BD 06 65 BD 26 00`
and the next two values as NRPN, as a stock LFO knob's turn sends `BD 63 01
BD 62 20 BD 06 71 BD 26 00`, and `BD 08 65 BD 08 66 BD 08 67` as CC. With
the CCs from the lockfile (lock-check 1.3: A CC 8, C CC 9), CC 9 value 100
sets C to 800 (its coarse step) and knob B sends `BD 09 64` as CC. On 26
Oct (lock-check 1.3): NRPN 4:1 with 15, 100 or 127 sets B to 15, and NRPN
4:2 with 16383 sets C to 1000; with a trig held, or a scene's or a
performance's pad held, knob A sends nothing, as a stock knob sends
nothing there. In live recording, 150 turns of knob A over a bar lock all
16 steps, as a stock knob's do (stock's SRC TUN, the same turns), and CC 8
and CC 9 lock A and C (two rows) on the steps playing as they arrive, as
CC 16 locks stock's TUN; the same turns and CCs while playing without
recording, or in grid recording, lock nothing. On a unit (26 Oct 2026)
knob turns and CC 8 while live recording locked steps 3, 7, 11 and 15, and
the same while only playing locked none.

Values above 127 (0.8) follow stock's fine parameters: a p-lock (two rows,
above) and an NRPN (both data bytes) hold the whole value, up to 16383; a
scene lock, a performance depth and a CC hold its coarse step,
`value >> core_lock_shift`, the shift that brings `max` into 0..127, since a
kit's file keeps only an entry's high byte. Checked in the emulator
(`examples/lock-check-ar2` 1.3, LOCK CHECK C, 0..1000, on knob B, 26 Oct
2026): NRPN 4:2 with 7, 4 sets C to 900, and a turn sends 904 back as
`... 06 07 26 08`; a trig held and knob B turned locks 901 as two rows
(`2c 00 07`, `2c 40 05`), and the trig plays 901 (`core_lock_value`); a
power cycle with no save keeps both rows and the lock plays; a scene lock
from 900 turned 20 steps stops at the coarse 125 (`7d` in the entry, 1000).
A SysEx dump of the pattern carries both rows, and sent into A03 it gives
A03's stored pattern both; a trig copy of step 1 onto step 5 copies both
rows; in performance edit knob B gives C a depth of 19 coarse steps (entry
`13 00 00 2c 01`). Not checked: the value C plays with that performance's
pad pressed (no bar shows C).

## The smoothed value (0.9)

`smooth.c`, with no sites of its own (docs/ADAPTING.md, "The smoothed
value (core-ar2 0.9, Analog Rytm MKII)"). `core_param_value(track, field)`
follows stock's order in the audio interrupt: the target (the sound in the
kit at 0x800067b8, then `core_lock_value`, then `core_scene_value`) through
stock's one-pole, s = 0x03d7 t + 0x7c29 s over 0x8000 a block (0x40120d26),
then `core_perf_offset` (0x40120fde) and `core_lfo_offset` (0x4012101a,
scaled by `(max - min) / 127`, or as it is with `core_param_value_ex`'s
`CORE_PARAM_LFO_RAW`), clamped to `min..max`, in 8.8.

It is lazy: per track and smoothed parameter, core keeps the value (16.16)
and the block it was stepped to, and a call steps it k blocks at once,
s = t + (s - t) 0.97^k, from a table of 0.97^k in Q15 (k ≥ 256 in steps of
255). The block count, `core_block`, is load.s's: core's `ev_render_in`
adds one before any mod's handler. The state is 13 tracks × 32 parameters
× 8 bytes (3.3 KB of .bss); a field declared `"smooth": false` (0x200 in
`core_lock_codes`) takes no slot and jumps. `core_perf_offset` returns 0
without walking the kit's 48 entries while no performance's pressure is
above 0.

From 0.10 a read looks up only the field's code, through an open hash made
once (lock.c), and keeps everything else it needs of the code in a table
made on the first read: the range, the smoother's slot, the LFO and route
flags, and where the field's bits are in a sound or kit (read without
`core_store_get`'s interrupt mask: the audio interrupt isn't interrupted by
a field's writer, which masks interrupts itself). Like stock, which works
its parameters out once a block, it reads the audio side's state once a
block, on the block's first read: the kit the audio side plays, whether a
performance's pad is pressed (the pressures a word at a time), and per
track whether any of its 8 routes points at a mod parameter. The scene's
list, the performances and the routes are then asked by the code
(`core_scene_value_in`, `core_perf_offset_in`, `core_mod_offset_code`)
only when they can add something. `core_param_handle(field)` gives the
code as a handle, `core_param_value_h(track, handle, flags)` reads by
it, and `core_param_values(track, handles, n, flags, out)` reads several
parameters of one track with the track's checks, the block's state and
the sound found once. `core_store_get` reads a field a byte at a time (a bit at a time
before). The values are 0.9's: lock-check 1.10's ring gives the same A, B,
C, A's and C's sound values, p-lock, scene lock, LFO and performance
offsets under both, after NRPNs set A, B and C and with the LFO's
destination on A (emulator, 5 Nov 2026). The three reads a block cost
1,828 DMA timer ticks on average under 0.9's code and about 480 under
0.10's (609 and about 160 a read; a call that returns at once costs 39),
and 366 as one `core_param_values` call (122 a read). Most of what
was saved is the routes' walk: lock-check's A and C are `"mod": true`, and
each of their reads walked the track's 8 routes. On a unit (T126, 5 Nov
2026, lock-check 1.11): the same values, the batch read never differed
from the single reads, and a read cost 733 ticks one at a time and 324 in
a call of three (the unit's timer counts differently from the emulator's,
so compare only within one machine). On the unit a block is 1/1,500 s
(the block count against the dumps' times, T127) and DMA timer 0 counts
about 88,000 ticks in one (above), so a read costs about 0.86% of a block
one at a time and 0.37% in a call of three; a mod that reads 14 a block
spends about 12% one at a time. Read several parameters of a track with
`core_param_values`.
## Velocity and aftertouch destinations (0.9)

`routes.c` and `routes.s`, three sites (docs/ADAPTING.md, "Lockable
parameters", Velocity and aftertouch destination; the trace is the
knowledge base's core-ar2 0.9 note). A lockable field declared `"mod":
true` has 0x400 in its code in `core_lock_codes`.

- **The lists.** VELOCITY MOD and AFTERTOUCH are built by the destination
  list builder (0x400aa280) with kind 0x200, the LFO's with 0x600.
  `core_dst_list_end` (lock.s) appends the LFO destinations when 0x400 is
  set and, from 0.9, the `"mod"` ones when only 0x200 is; their pseudo ids,
  names and slot (the code) are lock.c's, as for the LFO. The views draw a
  route's knob from the id's record (0x4004b7f2: record 0, ERR, for an id
  above 468); `core_route_label` (0x4004b81a) gives a pseudo id's knob its
  category (MOD when it has none) and short name instead.
- **Save and load.** A route's destination byte goes through 0x40007602 on
  save and 0x400075c8 on load with track 0, which 0.8's `core_code_id` and
  `core_id_code` already pass for 42..127: the code stays itself with no
  further site. After a sound's load (store.c), `core_routes_load` makes a
  route to a code above 41 that is not a `"mod"` parameter of this build,
  or any in a sound of another layout, code 0 with amount 0, keeping a
  velocity route's amount low byte (route.s, mod storage).
- **The apply.** The engine push (0x4011f60a) writes each drum track's 8
  entries {s16 amount, s16 index} at 0x8000ea3c + 0x20 t, index =
  t × 42 + 26 + code. `core_vel_index` (0x4011f6b2) and `core_at_index`
  (0x4011f6d8) take each route's code there: up to 41 stock's add; above,
  the entry gets index 25 and amount 0 (stock's empty entry, as its scene
  and performance builds give it) and core keeps {amount, code} in
  `core_routes`, laid out as the entries. The audio interrupt adds
  g × amount to each entry's word (0x4011de2a, EMAC fractional: 2 g a, high
  half); g is, for velocity, the track's engine word 39 when bit 3 of
  0x8000e53c + 4t is set, plus 0x800067c4[t], saturated (0x40120f10), and
  for aftertouch 0x800067de[t]. `core_mod_offset` sums g × amount >> 15 over
  the track's routes to the field's code, in 8.8 steps of a 0..127 knob;
  `core_param_value` adds it after the smoother, scaled as the LFO's.

Checked in the emulator only (analog-emu from the PC copy, OS 1.74, 1 Nov
2026), with [lock-check 1.8](../../examples/lock-check-ar2) (A and C
`"mod"`, B and D not): both lists end with Lock Check A and C; a knob turned
to C and YES writes code 44 in the route's byte and the knob reads LCHK /
LCC; the push gives the route index 25, amount 0, and `core_routes` holds
{amount, 44}. With a velocity route at +64 and an aftertouch route at +16
to C, a note at velocity 127 and channel pressure 64 give
`core_param_value(C)` 144125, g × amount >> 15 for each summed (18304) and
scaled by 1000 / 127, exactly; with a route at +127 to A, velocities 100
and 64 give 25400 and 16256. The codes come back after a power-off save and
a cold boot; stock 1.74 cold-booting the same card shows them as code 0
(the unused route's META), with the amount kept. Not checked: a sound
loaded with a code the build lacks (the scrub), kit save and reload,
SysEx and Overbridge transfers, a unit, listening. The amounts here were
written into the live sound; the amount is knobs E to H in MODULATION
SETUP VEL (A to D pick the destination, YES commits). Later, in the
emulator (1 Nov 2026, lock-check 1.9): knob E sets the amount (60 at
velocity 127 gives C 120000, as predicted); a sound loaded with a code the
build lacks (a core-only build on that card) has that route back as code 0,
amount 0; SAVE KIT and RELOAD (KIT menu) keep a route to C; a kit dump over
USB carries it and sent back restores it. Not checked: a single sound's
dump, Overbridge, a unit, listening.

## The FX track (0.9)

A lockable kit field is the FX track's (track 12) parameter (docs/ADAPTING.md,
"The FX track's parameters (core-ar2 0.9, Analog Rytm MKII)"). The FX block
is live kit +0x74a, 42 words: stock's 38 parameter slots (0..37) and four
spare words, 38..41 (kit +0x796..+0x79d), which are core's kit fields
(`KIT_HOME`). The FX LFO's DST byte is the high byte of word 4 (kit
+0x752), None slot 0. The FX set's (FxParameterSet, vtable 0x401dcea0)
object at +20 gives the FX block through its vtable +40, and its setter
(0x400a9f28) takes any id whose slot is 0..41 with no machine check, so
`core_lock_set_param` keeps a kit parameter's knob from a borrowed id stock
takes.

| site | 1.74 | what |
|---|---|---|
| FX lock get, set, clear | FX vtable +60, +64, +68: 0x401dcedc, 0x401dcee0, 0x401dcee4 (`ptr`) | rows.c, as the sound set's: a kit parameter's p-lock on the FX track |
| FX knob turn | FX vtable +72, 0x401dcee8 (`ptr`) | the sound set's `core_lock_turn_vt`, shared, as stock shares 0x400a8560 |
| FX slot to id | FX vtable +76, 0x401dceec (`ptr`) | `core_fx_slot_id`: a kit LFO parameter's code gives its pseudo id; `core_dst_list_end` appends the kit LFO parameters for a set whose +76 is this one |
| FX words' save | 0x400a545c (`jsr` of 0x40007f58) | the save of the live FX words into a stored kit: None in the DST byte while it runs, the code in `core_fx_lfo_dst` |
| FX edit's write | 0x400a5692 (`jmp`, `st_fx_through`) | stock writes each FX edit into the stored kit at once, the DST byte through 0x40007602 (which passes a code, as for a sound); after it a code in the live byte becomes the stored 37 (None, slot 0) and goes into `core_fx_lfo_dst`, a stock destination clears it |

Stock saves the FX LFO's DST through a 38-entry table (0x401c5b4c) and loads
it through the inverse (0x401c5be4), neither bounded, so a code would load
as a stock slot (46 as 40). `st_kit_save` and `st_kit_load` treat the byte
as `st_sound_save` and `st_sound_load` treat a sound's: None in the stored
kit, the code in `core_fx_lfo_dst` (7 bits, placed in a build with a kit
`"lfo"` field), and on load a byte of 42..127 that field did not give is
None. Scenes and performances (scenes.c, scenes.s: `pass` takes track 12),
NRPN and CC (midi.c, on the FX track's channel), the smoothed value
(smooth.c) and `core_reload` take track 12 with the kit as its item. A
kit field never gets CC 84 or 85, the two of the track pool stock uses on
the FX channel (`fx_ccs_taken`; the FX CC table 0x413dcb14).

Checked in the emulator only (analog-emu, OS 1.74, 1 Nov 2026, lock-check
1.9's kit field K on a LOCK CHECK FX page): the FX LFO's DST list ends with
LCHK:Lock Check K and picking it puts 46 in the DST byte, with the LFO's
offset in `core_lfo_offset(12, K)`; knob A, NRPN 4:4 and CC 48 on channel
13 set K (nothing on channel 1; NRPN 4:0 on channel 13 sets nothing); a
p-lock with a trig held, a scene entry and a performance entry give K's
lock, scene value and offset, and `core_param_value(12, K)` adds them as
predicted; stock's FX words are unchanged. K, its p-lock and the DST come
back after a power-off and a cold boot, and stock 1.74 on the same card
loads the FX LFO with DST None; SAVE KIT and RELOAD keep K's DST; a kit
dump over USB carries it and restores it. Not on a unit or by ear.

## Field defaults (0.10)

A storage field declared with `"init": N` reads N where its bits are zero
(docs/ADAPTING.md, "Mod storage (core-ar2 0.2, Analog Rytm MKII)"). The
linker gives each such field a slot in the table `core_field_inits` (1..63)
and puts the slot in the descriptor's bits 16..21; the offset keeps bits
0..15, as no scope has 2^16 bits. `core_store_get` returns the bits XOR the
slot's init and `core_store_set` stores the value XOR it, so a field
without an init costs one test more and nothing else changes. Every read
and write core makes of a field goes through those two (lock.c's RANDOMIZE
and RELOAD PAGE, midi.c, rows.c, scenes.c, smooth.c, store.c's LFO
destination fields), so all of them work on the value; the saves, loads,
SysEx and Total Recall copy the bits. No new sites.

## Mod-kept locks (0.11)

A knob whose id is in a `core_param_locks` entry (docs/ADAPTING.md, "Locks
a mod keeps itself (core-ar2 0.11, Analog Rytm MKII)") from 555 up is the
mod's: core keeps none of its locks. No new sites; `rows.c` routes these
ids in the four lock calls it already owns, and `lock.c` and `pages.s` in
the label, name, text and turn routines core already owns:

| where | what |
|---|---|
| lock get, set, clear, turn (rows.c, sound and FX set vtable +60..+72) | an id core's own parameters don't take, in an entry's range: the entry's function, with the current track (12 on the FX track) and the step; a step past the track's length is none (-1, 0); a turn that answers marks the project modified |
| knob label and name (lock.c, 0x400a9000 and 0x400a9022's callers) | the entry's `label(id)` and `name(id)`, for any set, before stock's |
| value text (pages.s, after the `core_params` text entries) | the value as a number, as for core's own parameters |
| turn (pages.s, after a `core_params` entry took it) | with live recording on, the entry's turn with the playing step and `CORE_LOCK_RECORDING` (0x100), found as for core's own locks (`record_step`, a lock trig added to a step without one) |

`core_held_step(&track)` is the step and track of the last lock call, for 4
ms of DMA timer 0 (a page draws each knob's lock with get just before its
text). The profile's `param_ids` gives the linker the ranges it checks.

## Checked so far

On a unit (OS 1.73). With test images built by rytm-mods-src (its `elemod`
branch):

- **The boot site**, and the DDR area the linker uses: mod 0016 filled
  0x47BE0000-0x47C00000 from 0x400008c0 at boot, and a readout showed no word
  of it changed after a long, hard session and a power cycle.
- **Both render sites**: mod 0017 armed a flag at `ev_render_in` and cleared
  the effect returns from `ev_render_out` on alternate 0.7 s stretches. The
  delay and the reverb pulsed, each on its own setting, with the reverb return
  PRE or POST.

- **This core**, with [hello-marker-ar2](../../examples/hello-marker-ar2), built
  and patched by elekloader (11 Oct 2026): the unit booted the build, reported
  its version (HM01), and drew the 3x3 square on every screen. So the boot
  copier, the linked image in DDR and `ev_draw` work.

- **Every event**, with [hooks-check-ar2](../../examples/hooks-check-ar2)
  (11 Oct 2026, build HC01): `ev_tick` blinked its square and requested
  redraws, `ev_render_in` advanced its bar steadily and never disagreed with
  `ev_render_out`, `ev_key` and `ev_enc` counted every key and encoder event
  while the unit acted on each as stock does, and `ev_settings` listed the
  HOOK TEST row through `core_additem`, which counted each selection while
  the other rows worked as stock. The same through a short session with a
  pattern playing into the delay and reverb.

On a unit (OS 1.74, 17 Oct 2026), sent with Transfer over USB: the 1.74
builds with hello-marker-ar2 (HM74) and hooks-check-ar2 (HC74) work as the
1.73 ones do.

Statically, against the stock image (python, no toolchain): every site's
stock bytes, and that each covers whole instructions.

- **Mod storage (0.2)**, in an emulator (OS 1.74 and 1.73, 19 Oct 2026), with
  [store-check-ar2](../../examples/store-check-ar2) marking every field of
  every item: after save, loading another project (all zeros), loading the
  project back, and a power cycle, every marker read back. A build with an
  ancestor layout read all of it and rewrote the header; a build with the
  kit generation bumped read zeros for kits only; a build with a fresh
  layout read zeros everywhere; CREATE NEW cleared everything. A global
  slot received as SysEx came in with its mod data cleared, and the other
  three slots and every other scope kept theirs. `ev_project` came once
  at boot (a load; on the first boot with all four scopes dropped, after a
  power cycle with none), once per project load, not on save, and as
  `CORE_PROJECT_NEW` after CREATE NEW. On 1.73 the same chain, CREATE
  NEW, the SysEx global and the events gave the same results; the ancestor,
  kit generation and other-layout builds ran on 1.74 only.

- **Mod storage (0.2) on a unit** (19 Oct 2026), sent with Transfer over
  USB, with store-check (builds SC74, OTHER74 and SC73: two layouts, both
  OS versions). On 1.74: every column stayed empty after marking, saving,
  loading the project back and a power cycle, and was full after loading a
  stock-saved project (with all four drop flags); the project exported as
  .arprj and imported into another slot kept everything, as did a Total
  Recall sync and recall; CREATE NEW cleared everything, as a new-project
  event. The same mods built with another lockfile read none of the saved
  project (all four drop flags), and the first build read all of it again
  afterwards. On 1.73 the project saved on 1.74 read in full, and marking,
  saving, loading another project, loading back and a power cycle passed.
  One flash (OTHER74) stopped at SUCCESS: REBOOTING and booted normally
  after a power cycle.

- **Mod storage (0.3)**, in an emulator (OS 1.74, 20 Oct 2026; before 0.3
  dropped 1.73, the same checks also passed with a 1.73 build of it):
  with 0.2, a track and a pattern field put in a live pattern, then a trig
  placed on that track (stock's track save), read 0 after a power cycle;
  with 0.3 they read back, and the stock track record was the same as with
  0.2. A parameter lock alone (the locks save) and a SCALE menu edit alone
  (the settings save) each carried them too. store-check with a 24-bit and
  a 32-bit sound field: marking, SAVE PROJECT AS, loading it back and a
  power cycle kept every marker. With velocity amounts' low bytes set, the
  VELOCITY MOD menu (open, edit all four amounts, negative ones too) drew
  every frame as stock did from the same amounts, and the edited amounts
  and the engine's copies were stock's, with the low bytes kept in the
  sound and 0 in the engine (with 0.2 they reached the engine, and the menu
  drew negative amounts one step nearer 0); copy, clear (FUNC+PLAY) and
  paste kept them; SAVE KIT and RELOAD brought them back. A 0.2 build's
  lockfile built with 0.3 read the 0.2 project's sound fields in place
  (checked on 1.73 only, with that 1.73 build).

- **Parameter pages (0.4)**, in an emulator (OS 1.74, 21 Oct 2026): with
  rytm2_mods' 0.4 versions, every page screen of the same key and knob
  sequences was pixel-identical to the v1.2 release build on core 0.3
  (LFO RND, FUNC + VEL, euclid OP, DAC FILTER with filter-hz's Hz text);
  comp-preview 1.1's COMP and preview screens matched 1.0's; with duck
  added, COMP cycled COMP, preview, DUCK.

- **Parameter pages (0.4) on a unit** (21 Oct 2026), sent with Transfer
  over USB: with all eleven rytm2_mods mods (Q4A4), LFO RND, DAC FILTER
  (shown only with DAC SUM on), euclid OP, FUNC + VEL, the COMP preview
  and the keys no mod uses all behaved as on core 0.3, and the VELOCITY
  MOD menu kept the mods' values. With duck and cpu-probe added (Q4D4),
  COMP cycled COMP, preview, DUCK, DUCK ducked from its source and stayed
  shown on leaving the FX track. fx-sends' CHORUS SEND page (Q4F4, without
  lfo-rnd) opened behind AMP and its chorus played on its track only.

- **Page key's step and audio load (0.4) on a unit** (21 Oct 2026): the
  same three builds plus core, voice-dac and duck alone. FILTER stayed on
  FILTER with DAC SUM off and stepped FILTER, DAC FILTER, FILTER with it on;
  LFO stepped LFO, LFO RND, LFO; the header named each page after the step,
  also in the builds without lfo-rnd; COMP, the preview and DUCK were each
  kept on coming back to the FX track, with and without comp-preview.
  `core_audio_load`: readings with cpu-probe matched the builds before it.
  In the busiest case (DAC SUM, fx-sends' chorus and the preview) CPU still
  read 100% at times, with the audio interrupt at 92% and peaking at 94%;
  the preview's trace stopped now and then and came back by itself, knobs
  and values kept responding meanwhile, and nothing was audible.

- **Machines (0.5)**, in an emulator (OS 1.74, 22 Oct 2026): with the
  example, the tables held its engine, mask and names after boot and slot
  36's Tune record its range 32..96 and default 58; the machine list on
  the BD track showed BD COPY, and choosing it set the track's machine to
  36 and the voice's engine to the example's; its SRC page matched BD
  CLASSIC's. Not checked: its sound (the emulator's audio is too quiet to
  compare), and a kit with it loading as DISABLE without the mod.

- **Machines (0.5) on a unit** (22 Oct 2026), with the example and all
  eleven rytm2_mods v1.3 mods (M5A4): BD COPY was in the BD track's machine
  list, played as BD CLASSIC with its knobs, and came back after SAVE KIT, a
  power cycle and RELOAD. On the same build without the example (M5N4) that
  kit loaded with BD on DISABLE and the rest as saved.

- **Write-through and SAVE PROJECT AS (0.7)**, in an emulator (OS 1.74,
  24 Oct 2026), with store-check 1.1 on a fresh card: STORE CHECK, then a
  power-off without a save and a cold boot: every column read back empty
  (core 0.6 lost the pattern-track, pattern, kit-sound, pool-sound, kit and
  global markers there). The SAVE PROJECT AS copy was checked by itself on
  a build with the write-through call taken out: STORE CHECK, SAVE PROJECT
  AS, a power-off and a cold boot read back empty, and the same build
  powered off without the save lost those six columns.
  On a unit (25 Oct 2026), with store-check 1.2.1 and analoghealth 1.2: on
  core 0.7 (W07F) STORE CHECK then a power cycle with no save read back
  empty. On a test build with the write-through call taken out (W07N) the
  same lost the pattern-track, pattern, kit-sound, pool-sound, kit and global
  markers (the project's stayed: they live in the stored project's tagged
  store), and STORE CHECK, SAVE PROJECT AS and a power cycle read back empty.
  A Total Recall take before the power cycle kept nothing (see SAVE PROJECT
  AS above). On core 0.7 a take carries every marker, since write-through
  keeps the stored project current: a take restored onto the unit brought
  them back, and they survived a power cycle with no save; a take restored
  onto a build with another layout ID loaded with the mods' fields at their
  defaults. Before that, on core 0.8 with store-check 1.2 (which carries
  this code unchanged), the same power cycle read back empty.
  Cost: three STORE CHECKs (about 5,400 sets each) under audio gave no
  dropout, and raised the 1 s CPU average by 1 to 4 points, the same as on
  the build without write-through. Not measured on a unit: the time
  core_store_set holds interrupts masked; in an emulator write-through adds
  about 143 instructions to an average set (805 against 662), and the
  longest masked stretch, about 1,130 instructions, is the field loop's, as
  without write-through.

- **Lockable parameters (0.8)**, in an emulator (OS 1.74, 24 Oct 2026),
  with [lock-check](../../examples/lock-check-ar2) (codes 42 and 43): DST
  set to 42 over NRPN showed LCA on the LFO page, and turning DST opened the
  menu with the cursor on MOD:LOCK CHECK A, after stock's AMP entries;
  with DEP at its top, `core_lfo_offset` swung about +-30,000 (about
  +-120 steps) while the code stayed 42; DST 43 moved the code to 43, and a
  stock DST (5) cleared it and modulated the stock slot. On the LOCK CHECK
  page four RANDOMIZE PAGEs set `lc_a` to 0, 92, 4 and 101, and RELOAD PAGE
  took it back to the saved kit's 0. On 25 Oct the page's knob A showed
  LCA, its short name, and the SRC page kept stock's labels. On 26 Oct, 80
  RANDOMIZE PAGEs on the stock LFO page set DST to LCA, LCB or LCC 7 times
  (3 of the list's 36 entries) and to stock destinations otherwise, with
  the mod code in the sound's DST byte. With track 1's machine byte (sound
  +104) written to each of 0..35 in turn, RANDOMIZE PAGE put a mod code
  in DST for every one (stock lists of 28 to 33 entries, the mod
  parameters after them); the machine was written, not switched through
  its menu. RELOAD PAGE
  took `lc_a` from 10 back to the saved kit's 77. With a trig held, knob A
  shows its lock's value (it showed ERR before 0.8's value text), and a
  stock knob its own. With DST on LCA, turning DST scrolls the menu down
  to MOD:LOCK CHECK B and C and up to stock's AMP entries, and YES on
  C sets DST to LCC (the DST byte holds 44). Also on 26 Oct: PAGE + RECORD,
  PAGE + PLAY and PAGE + STOP (copy, clear, paste) take a page's mod locks
  away and bring them back, both of C's rows included; a paste over a page
  whose locks changed puts the copied ones back and drops the new; PAGE +
  STOP again (undo) brings the changed ones back. A scene lock saved with
  SAVE PROJECT, changed, and loaded back after another project, comes back
  as saved. Track init (0x400118e8) writes only a track's settings
  (length, scale, speed), so it moves no lock, stock's or a mod's; its
  callers are pattern init and the pattern load (row load), checked above,
  and a third (0x400c19dc) no code refers to. DST set to LCA (NRPN 1:35,
  42), SAVE PROJECT, DST changed, the project loaded: DST is LCA again
  (L8H6, before `core_lfo_dst`, brought it back as FIN, 18). With
  lock-check 1.4 (`"lfo"` on A and C) the DST menu ends with MOD:LOCK
  CHECK A and C, B's code by NRPN shows ERR, and LCC comes back through a
  save and a load; that project loaded on stock 1.74 has DST None (stored
  41, shown `…`). On stock 1.74, sound dumps of each stored value 42..127
  load as the slots its table gives (5 of them stock parameters), and a
  stock save of slot 64 (what the earlier stored 42 loaded as) comes back
  as ACC; on 0.8 the stock sounds whose byte loads as 64 or 47 load as
  None. On a unit (OS 1.74, 26 Oct
  2026, core 0.8 with lock-check 1.3 and analoghealth) remote turns
  reached MOD:LOCK CHECK C and YES set LCC; an earlier unit run (T37)
  stopped on MOD:LOCK CHECK A because its turns were one count each, too
  few for a list menu to move. The same run passed trig p-locks and trig
  copy, the lock rows through a power cycle, a scene lock and a
  performance depth on the knob and in the bar, pad pressure, RELOAD and
  RANDOMIZE PAGE, NRPN and CC, and 75 s of play with the LFO on LCA (CPU
  75-85 %, no dropouts). A second unit run (L8H9: core 0.8 with lock-check
  1.4 and analoghealth) passed page copy, clear, paste and undo of a
  trig's lock; live recording (above); a scene lock and DST on LCA through
  SAVE PROJECT and a load, with LCB's code by NRPN showing ERR; and NRPN
  4:2 at 16383 setting C to 1000. Both flashes came back in 15 s. Then,
  on stock 1.74 on the unit, that project (DST LCA) loaded with DST None
  (`…`), and kept it through two rounds of SAVE PROJECT, another
  project's load and a load back. By hand on the unit (L8H9): the DST
  list ends with MOD:LOCK CHECK A and C, without B; knob A with a trig held
  sets that trig's lock, shown when held again and in the second bar as it
  plays; a scene lock set with the scene's pad held shows on the knob in
  scene edit only and in the third bar while the scene is active, as a stock
  parameter's scene lock does; and Overbridge shows the LFO destination
  LCA as None. By hand on the unit (L8C2, 8cfa555, lock-check 1.5): the DST
  list ends with MOD:Lock Check A and LCHK:Lock Check C, without B, and
  picking C shows LCC. In an emulator, a 240-character short name, a
  7-character category and a 62-character name show as 4, 4 and the menu's
  width, with no fault. Unit run T51 (27 Oct, L8C3: 1db9bfc, lock-check
  1.6): a saved sound with DST LCA stores stock's None (41) in stock's byte
  and the code in core's field, read back from slot 125; a performance's
  depth on C follows its pad's pressure (lock-check's bar for C full at 127,
  empty at 0); the velocity and aftertouch lists end with stock's AMP
  entries (its Euclid step never turned Euclid on, so it checked nothing);
  RANDOMIZE PAGE put a mod
  destination in DST once in 12; every machine in BD's list (28) ends its
  DST list with A, C and D; D's 200-character short name shows as LCDX and
  its name cut at the menu's width; RELOAD PAGE from kit 2 brings A back. A
  kit dump sent back is received, but C's value after it was not read.
  Unit run T53 (27 Oct, L8C4): kit dumps of the saved kit with C at 800
  and at 0 differ in one data byte and the checksum; the 800 dump sent back
  brings C back to 800, and a fresh dump then equals the first.
  Unit run T54 (27 Oct, L8C4): with Euclid on (PL1 4) and C and the
  filter's FRQ locked on hit step 5, TRO -15 and FREEZE TRACK move both
  locks to step 6 and leave step 5 with neither.

- **The smoothed value (0.9)**, in an emulator only (analog-emu, OS 1.74,
  1 Nov 2026), with [lock-check 1.7](../../examples/lock-check-ar2)'s
  per-block record of A, B and C on track 1: a step of the sound's value
  (NRPN), a p-lock played in and out, a scene lock activated over a playing
  p-lock and released, and a performance's pressure (CC 36) each follow
  stock's one-pole block by block to within 1/256 of a step (90% in 76
  blocks, 50 ms); the LFO and the performance add to it exactly, with no
  lag; B (`"smooth": false`) jumps in the block its NRPN lands. Three reads
  a block cost 1,290 DMA timer ticks on average (2,005 at most), about 430
  a read. Not yet on a unit or by ear.
  The load prompt after a mod-only edit, in an emulator (27 Oct, OS 1.74,
  lock-check 1.6): on the LOCK CHECK page a turn of A (0 to 30), CC 8 and
  NRPN 4:0 each mark the project modified and LOAD PROJECT then asks SAVE
  'PRESETS' BEFORE LOADING; the same turn on L8C3's core loads with no
  question. With no edit the load still goes straight through. Unit run T52
  (27 Oct, L8C4: 6b29f44, lock-check 1.6): after a save, LOAD FROM on the
  same slot loads with no question; after only a turn of A, CC 8 or NRPN
  4:0, LOAD FROM asks ANY CHANGES WILL BE LOST. PROCEED WITH LOADING?, as it
  does after a stock knob's turn.

- **The project save (0.10)**, in an emulator only (analog-emu, OS 1.74,
  4 Nov 2026), with [save-check 1.0](../../examples/save-check-ar2): INIT
  NEW of slot 125 calls no handler. After LOAD FROM 125, SAVE PROJECT calls
  both with `CORE_SAVE_SAME`, slot 124 (the screen's 125), and `sv_saves` 1.
  SAVE PROJECT AS to 126 calls both with `CORE_SAVE_OTHER`, slot 125, and
  `sv_saves` 2. LOAD FROM 125 then shows 1, and 126 shows 2: the field set
  before the save is in the copy, and the slot it came from keeps its own.
  After a knob's turn, SAVE ... BEFORE LOADING? YES calls both with
  `CORE_SAVE_SAME` for 126, which then loads with 3. The project manager's
  SAVE TO 127 calls both with `CORE_SAVE_OTHER`, slot 126, and makes 127
  the active project. No call failed. On a unit (T122, 5 Nov 2026, the
  Rytm reached through the Sidecar mirror): SAVE PROJECT to 121, SAVE PROJECT
  AS 122 and the two loads gave the emulator's kinds, slots and values. Not
  checked: an empty slot's LOAD (`CORE_SAVE_NEW`; the LOAD PROJECT list shows
  only used slots) and a failed save.

- **Page functions (0.6)**, in an emulator (OS 1.74, 23 Oct 2026): core 0.6
  with all eleven rytm2_mods (voice-dac 1.4) shows DAC FILTER without RES
  for LP1 and HP1 and without FRQ and RES for TYP OFF, the empty knobs take
  no turn, and the page follows a track switch. With rytm2_mods v1.3 (no
  `core_page_fns` entry) core 0.6 gives a 59-screen walk pixel-identical to
  core 0.4's. Not yet on a unit.

- **Field defaults (0.10)**, in an emulator (analog-emu, OS 1.74, 4
  Nov 2026), with [lock-check 1.10](../../examples/lock-check-ar2)'s N
  (`"init": 100`, 7 bits, slot 1) read from `lc_init`: on a new card's
  first project N reads 100 and `core_param_value` 100 << 8 (25600) with no
  lock or scene; knob C on the LOCK CHECK page turns it to 101; with a trig
  held the first turn makes a lock at 100 and the second takes it to 101,
  which plays (lock 101, value 101 << 8) while the sound keeps 100; N
  turned to 0 survives a power-off and a cold boot as 0, not 100. RANDOMIZE
  PAGE and an LFO on N were not tried. On a unit (T127, 5 Nov 2026,
  lock-check 1.11): in a new project (CREATE NEW) N reads 100 in the sound
  and 100 << 8 from `core_param_value`; NRPN 4:5 sets it to 101 and it
  reads 101 (101 << 8).

Checked in the emulator (dtboot, OS 1.74, core 0.11 with
`examples/sidecar-check-ar2` 0.1, build SC11, 5 Nov 2026), mod-kept locks
(0.11): the SIDECAR CHECK page's knobs show SCA..SCD and, as a knob turns
with a trig held, the header shows its name (SC KNOB C), not ERR / ERROR.
In grid record, T1 held and knob C turned: set (track 0, step 0, the
unlocked value) then turn, and the knob shows the lock with the held step
in its text (`L1 6`, from `core_held_step`); after the release the text has
no step. T1 and T9 held and knob A turned: one call each, steps 0 and 8.
T1 held and knob B pushed: set (no lock yet); T1 held, FUNC and knob C
pushed: clear. On the FX track (FX, its LFO view's added page) T3 held and
knob C turned: set then turn with track 12, step 2. Live recording (RECORD
held + PLAY) with knob B turned: turn with the playing step and
`CORE_LOCK_RECORDING` (0x104, 0x10a: steps 5 and 11), and a lock trig added
on step 11, which had no trig. An empty step held in grid record keeps a
trig after release whether a mod's knob or a stock one (AMP) is turned, or
none: that is stock's press, and no lock trig is added. Part 1 adds no
work in an interrupt. Core's .run grows 1,092 bytes over 0.10 (33,356),
.bss 8; core + sidecar-check take 60,712 of the 128 KB area.

## Building

Core 0.8 builds with `Analog-Rytm_MKII_OS1.74.syx` only: the SDK writes
`core-0.8.elemod` (1.74 is its `os`) and, for hello-marker-ar2 (whose `os`
is 1.73, with a 1.74 port), `hello-marker-1.0-os1.74.elemod`. For 1.73,
build core 0.2 from commit 92a99c9 (OS versions, above).

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/hello-marker-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.lint examples/hello-marker-ar2/out/hello-marker-1.0-os1.74.elemod \
    --stock Analog-Rytm_MKII_OS1.74.syx --with mods/core-ar2/out/core-0.8.elemod
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.8.elemod --mod examples/hello-marker-ar2/out/hello-marker-1.0-os1.74.elemod \
    --out rytm-hello.syx --version HM74
```
