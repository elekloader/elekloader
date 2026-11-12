# core-a4mk2

The boot copier, the hook bus and mod storage for the Analog Four MKII, OS
1.56. It builds the Digitakt mk1's `core.s`, `settings.s` and `render.s` with
the A4's addresses, which `mod.json` gives (`defsym`, `sites`), as
[core-ar2](../core-ar2) does for the Analog Rytm MKII; each section below
names its own files.
On its own it changes nothing the unit does.

The A4 MKII is the Rytm MKII's platform (a ColdFire MCF5441x): its OS entry,
bootstrap loader and UI loop are the Rytm's, instruction for instruction
apart from addresses, so each site below is the counterpart of one core-ar2
hooks on Rytm 1.74, found by masked matching of the two images and checked
by the code around it (`elekloader/devices/analog_four_mk2.py` has the
platform evidence).

| event | A4 1.56 | Rytm 1.74 | note |
|---|---|---|---|
| boot | 0x40001180 | 0x400008c0 | the OS entry's third call, VBR := 0x40000000 (0x40002064) |
| `ev_tick` | 0x40098898 | 0x400a2c5a | about 20 Hz, as the Rytm's |
| `ev_draw` | 0x40098900 | 0x400a2cc2 | drawAll(ctrl, bmp), 128 x 64 |
| `ev_key` | 0x400999f8 | 0x400a4838 | |
| `ev_enc` | 0x40099a40 | 0x400a4882 | |
| `ev_settings` | 0x400d05d2 | 0x400db8ea | the settings menu's builder, 0x400cff5a (the GLOBAL key's menu); rejoins 6 bytes on |
| `ev_render_in` | 0x401013c6 | 0x4011fb6e | the audio interrupt (vector 191, 0x401013be) |
| `ev_render_out` | 0x401040f6 | 0x401216c2 | its only exit; the frame is 0xec (the Rytm's 0xb4) |

The routines `core_additem` calls are the Rytm's, the same code with the
addresses in it masked: `OP_NEW` 0x400653b8, `ITEM_CTOR` 0x4005c7c4,
`MENU_ADD` 0x4005bffa and `FN_MGR` 0x40164940. The A4 has no manager for the
Rytm's function-pointer type (`PFSs15FileStorageInfoE`); 0x40164940 is the
only one with this code, for `PFSsR15FileStorageInfoPhE`, and its code is the
same for any function-pointer type.

What differs from the Rytm at these sites:

- **The UI object.** The UI routine (0x40098754) is the Rytm's with its
  object's fields moved (the idle counter +0x918, the Rytm's +0x748); it
  loads the view controller from +0x20 and skips the frame when it is null,
  as the Rytm's does.
- **The key and encoder calls.** The main loop pushes the brain from the
  stack, not from a frame (`2f2f0044`, `2f2f0048`); the callee sees the same
  arguments.
- **The settings menu** opens with the GLOBAL key (PROJECT, GLOBAL SLOT,
  MIDI CONFIG, CV CONFIG, SYSEX DUMP, …, AUDIO ROUTING, CONTROL, SYSTEM);
  rows added through `ev_settings` come after SYSTEM.
- **The render.** The A4's voices are analog; the audio interrupt is the
  digital path (effects and the inputs). Another vector 191 handler
  (0x401162a4) exists, as on the Rytm; core hooks only the main one.

## Parameter pages (0.2, 0.3)

`pages.s` is core-ar2 0.4's parameter routines at the A4's
(docs/ADAPTING.md, "Parameter pages (core-a4mk2 0.2, Analog Four MKII)"), and
core builds core-ar2's `load.s` for `core_audio_load`. Each A4 site is the
Rytm 1.74 routine's counterpart, found by its code and its callers:

| routine | A4 1.56 | Rytm 1.74 | displaced | note |
|---|---|---|---|---|
| page_info | 0x400f6212 | 0x400ff5e0 | `moveq #47,%d1; movel %sp@(4),%d0` | stock's pages are 0..47 (the Rytm's 0..11); an entry is 0x2c bytes, the name and ten knob ids, in a table at 0x401b31e8 |
| page_get_value | 0x40023d54 | 0x40038462 | `lea %sp@(-12),%sp; moveml %d2-%d3/%a2,%sp@` | the Rytm's prologue and frame; slot 21 of each of the 9 page views' vtables |
| param_apply_delta | 0x400232e2 | 0x400378a6 | the same | slot 22 |
| param_value_text | 0x4009ea68 | 0x400a864c | `movel %d3,%sp@-; movel %d2,%sp@-; movel %sp@(20),%d2` | the same arguments; slot 30 of the ParameterSet vtables (the Rytm's 21) |

The redraw after a turn a mod takes is VIEW_INVALIDATE, 0x40061a58 (the
Rytm's 0x40078580, the same code). No page site overlaps one of 0.1's.

core-ar2 0.4's page-key lists become views here. The A4's main screen
makes one view per page, 29 in all (0x400c1366, indices 0..28), and each
page key steps through a list of view indices for each track type (every
track, synth, FX, CV), kept in maps at +0x1a4 of the main screen. Core
hooks where the constructor has made its last view and not yet indexed
them (0x400c2370, resuming at 0x400c2378): for each `core_view_pages`
entry it makes a parameter page view as stock makes view 28 (operator
new(0x1c4), the view's constructor 0x4002522c(obj, page, key), the shared
pointer 0x40171460, the views map's slot 0x40171da8 at +0x1bc, 0x401713e0,
0x40144764) and appends its index to the key's list (0x40172bae, 0x40171b9e,
then stock's push_back or 0x40171558). Stock's loop after the hook gives
every listed view its key, place and length. The page key's step
(0x400c4202, resuming at 0x400c420a) skips a core view whose `shown`
answers 0, then selects as stock does (0x400bea38).

The current view is kept for each track type and saved: a signed byte
each in the project settings (storage +8..+0xA, written by 0x4000aabe on
project save and SysEx, and kept over power-off). The setter (0x400add08)
keeps it to 0..28 and the loader (0x4000a6d2) takes anything above 28 to 0,
PERFORMANCE. Core widens both to 0..44 (the moveqs at 0x400add22,
0x400add28, 0x4000a758, 0x4000a774, 0x4000a790); an index with no view in
a build falls back as stock's do (0x400c0470), and stock alone opens such a
project on PERFORMANCE. Nothing else in the OS bounds a view index.

The FX track already keeps its page when it is shown again, and each view
shows its own title, so core-ar2's FX fix and title push have no
counterpart.

page_info asks `core_page_fns` first, as core-ar2 0.6 does:
`{page, info}`, where `info(page)` returns the `core_pages` entry to
answer, or 0 to pass on to `core_pages` and stock. A page whose knobs
change with the selected sound uses it. Every reader of a knob asks
page_info afresh (traced in the 1.56 image): the view's knob lookup
(0x400231be, slot 41 of the page view's vtable 0x4018d650) reads the id
from page_info on each call, and the knobs' draw (0x40024ff6), a knob's
touch key (0x400261da) and an encoder turn (0x4002598c) each call that
lookup, so the knobs follow at the next redraw. The view's constructor
(0x4002522c) copies only the page's name, once, when core makes the view
at boot, so the name stays the first entry's. Additive: no new sites, and
a build with no entries in it behaves as 0.2.

pages-check 0.2 (for core 0.3) answers CHECK through `core_page_fns`,
with knob B (AMP PAN again) shown while its count of PAN's turns is odd.
The count is one per delta call, that is per turn update (the bar's
length), not per value step.

In rytm-emu's A4 build (24 Oct 2026) [emu], core 0.3 alone (A403, with
analoghealth's remote keys) drew every screen of Check 4's stock steps
(a new project; track 1's OSC1, OSC2, FLTR, AMP, ENV and LFO pages, turned;
the FX track's and the CV track's pages; AMP pressed three times) identical
to stock's references, pixel for pixel. With pages-check 0.2 (PF56,
with the remote keys) the AMP key stayed on AMP until PAN turned, then
stepped AMP, CHECK, AMP, CHECK, with CHECK's title CHECK each time; PAN
read in hex (`7F` at the end stop) and the bar grew with each turn update.
On CHECK, knob A turned PAN, and knob B was PAN at counts 13, 27 and 41 and
empty at 14, 28 and 42, changing at the redraw after each turn; a turn of
the shown B moved PAN (41 to 42) and hid B; turns of the empty B changed
nothing on the screen and left the count as it was.

On a unit (OS 1.56, 24 Oct 2026) [hw], the same two plans passed. A403H
(core 0.3 alone, with analoghealth 1.2's remote keys) matched stock's
references on every screen of Check 4's stock steps. On PF56H (core 0.3,
pages-check 0.2, analoghealth 1.2) the AMP key stayed on AMP until PAN
turned, then stepped AMP, CHECK, AMP, CHECK with CHECK's title each time;
PAN read in hex (`7F` at the end stop, `75` after a turn back) and the bar
grew with each turn update. On CHECK, knob A turned PAN, and knob B was PAN
at counts 19, 21 and 53 and empty at 36, 38 and 54; a turn of the shown B
moved PAN (53 to 54) and hid B, and a turn of the empty B left the count
at 54.

In rytm-emu's A4 build (22 Oct 2026), core 0.2 alone (A402) drew every
screen checked (main, AMP, FILTERS, OSC1, an FX track's CHORUS, with
knobs turned) identical to stock's, pixel for pixel; with pages-check
(PC56), AMP PAN read its value in hex (centre `40`), the bar counted each
step, and every other knob was stock's. `core_audio_load` read about 0x7a
(48%) there, an emulator figure. With the views, A402 drew all 90 screens
of every page key (pressed one to three times, on synth, FX and CV tracks)
as stock does; on PC56 the AMP key stepped AMP, then CHECK once PAN had
turned, with CHECK's title and its knob A working; CHECK came back after a
power cycle and through a SysEx settings dump. The same project opened on
MIXER with core alone (the index kept, with no view: stock's own fallback,
until a page key is pressed) and on PERFORMANCE with stock. The OS uses a
view's page id only through page_info and to pick the parameter set
(0x400a647c, which singles out pages 1, 3, 4, 5 and 47), so mod pages from
48 are safe.

On a unit (OS 1.56) [hw], PA56 (core 0.2 with pages-check) passed the
emulator's plan, every screen identical. On fast turns the bar grows once
per accelerated turn update, not per value step. The SysEx settings round
trip and core alone were not run on the unit.

## Mod storage

Mods declare fields in their mod.json (`storage`) and read and write them
with `core_store_get` and `core_store_set`, which mask interrupts while they
do; elekloader places the fields from the build's lockfile.
`elekloader/sdk/include/analog-four-mk2/store.h` is the mod writer's header,
docs/ADAPTING.md ("Mod storage") the guide (core-ar2's design), and
`store.c`'s opening comment has the byte layout.

Every byte used is one the stock OS never reads, kept in stored bytes it
never writes or only ever writes as a copy of the live home or as 0; no
stock function gives up space:

| scope | live, while in use | saved in |
|---|---|---|
| sound | live sound +0xd4, +0xd5, +0xe0, +0xe1 (32 bits) | stored sound +0xdc, +0xdd, +0xe8, +0xe9 (stock's raw copy of the live bytes), the layout ID at +0xea, +0xeb, +0xf1, +0xf2 and the generation at +0xf3 |
| kit | live kit +0x950..+0x95f (128 bits) | stored kit +0x5b0..+0x5bf (its FX block's +0x18, never saved or loaded), the ID at +0x5c0, the generation at +0x5c4 |
| global | `core_store_globals` (8 bytes a slot) | stored slot: bytes +3..+7 of its first three 16-byte entries (+0xd1), which stock writes as 0 and never reads, with the ID and generation after the data |
| pattern track | live track +0x2ea (64 bytes) | the tagged store |
| pattern | live pattern +0x16eda (64 bytes) | the tagged store |
| project | the tagged store | the tagged store |

The tagged store is byte +0 of each of the 256 four-byte entries of the 17
songs in a project image (the working song and 16), 4,352 bytes, as on the
Rytm: the song save and load use bytes +1..+3 only. The stock project file
save serializes the whole project into a buffer, so core hooks the file
save and load and the save to a buffer, and carries the store across.

On the A4 the stored project (0x4045c974, which the OS keeps across power
cycles) takes stock's own edits part by part: an edited track, a pattern's
settings and locks (after a pattern edit in grid recording), each parameter
of the current kit (straight into the stored copy of that kit), a global
slot's settings. SAVE PROJECT AS writes a project file from the live
project and the stored project's name only. So:

- **Per-part saves**: each of stock's saves of a track, a pattern's settings,
  one lock or all locks also writes the pattern's track and pattern fields
  into the store, as core-ar2 0.3 does.
- **The current kit**: `core_store_set` writes a field of the current kit or
  of one of its sounds through to the stored copy of that kit, with its ID
  and generation, as stock writes each parameter edit there. Other kits and
  the pool reach the stored project through kit saves.
- **SAVE PROJECT AS**: stock writes only the project's name into the stored
  project, its own data being there already. After a whole save of the live
  project into another image (the file, a buffer), core copies the bytes it
  owns from that image into the stored project, part by part as the save
  wrote them (global slots, kits and their sounds, the pool, the tagged
  store), so a power cycle straight after the save keeps the mods' fields as
  it keeps stock's. Only core's bytes are written.

The 28 storage sites, by `jmp` unless marked, each with its displaced
instructions in `store.s`:

| site | A4 1.56 | why | Rytm 1.74 |
|---|---|---|---|
| pattern save / load | 0x4000a160 / 0x40009f60 | pattern tracks, patterns, and the project region with the working pattern | 0x4000937c / 0x40009146 |
| sound save / load | 0x400091bc / 0x40008808 | the sound's ID and generation; its 4 bytes | 0x4000843a / 0x400081f6 |
| kit save / load | 0x4000962a / 0x40008f2a | the kit's 16 bytes, ID and generation | 0x400088d6 / 0x4000863e |
| global save / load | 0x4000837c / 0x40007bc6 | the slot's 8 bytes, ID and generation | 0x40007a5c / 0x4000763c |
| pattern init, track init | 0x40015d70, 0x40015b74 | clear the fields (stock leaves the spare bytes as they were) | 0x40011ab8, 0x400118e8 |
| sound init, kit init | 0x40016890, 0x40015a58 | the same (the Rytm has no kit init site) | 0x400122b6, — |
| project init | 0x40016242 | CREATE NEW and the project reset: a fresh store | 0x40011d38 |
| project load | 0x40011054 | after any load: drop scopes another layout wrote, write this build's header | 0x4000cd2e |
| buffer, file and background saves (`keep2`) | 0x40020772, 0x400a77e4, 0x400a503c | which image is being saved | 0x40034d86, 0x400b0870 |
| file load (`keep2`) | 0x400209ec | which image is being loaded | 0x40035000 |
| SysEx song copy, write, swap (`keep2`) | 0x40154444, 0x40154d56, 0x40154f74 | copy whole stored songs but leave the target's store bytes | 0x4018b8b8, 0x4018c266, 0x4018c488 |
| SysEx pattern copy, write, swap (`keep2`) | 0x401547e8, 0x401553ea, 0x40155618 | clear the target pattern slot's fields, which can't follow the pattern | 0x4018bc5c, 0x4018c902, 0x4018cb34 |
| track save (`keep2`) | 0x400b40ee | an edited track saved by itself (0x40009b9c): the pattern's track and pattern fields too | 0x400bf818 |
| pattern settings save (`keep2`) | 0x400a19f6 | the same, for the settings (0x40009834) | 0x400aacc4 |
| locks save, one / all (`keep2`) | 0x400a2fda / 0x400a2ff6 | the same, for the parameter locks (0x4000a1e6, 0x4000a02c) | 0x400ac2ce / 0x400ac2ec |

After CREATE NEW and after every project load, core calls the mods'
`ev_project` handlers with what happened and which scopes came in as zeros.

What differs from core-ar2 0.3:

- **No velocity routes** (`route.s`): the A4's sound has spare bytes of its
  own, so its 32 bits need no stock field's unused low bytes.
- **Global slots carry their own layout ID**, as sounds and kits do: a
  global slot sent and received as SysEx keeps its mod data in the same
  layout (the Rytm's slot has no room for one and drops it).
- **The kit keeps 128 bits**, and a kit init site clears them.
- **The current kit's write-through, and the copy into the stored project
  after SAVE PROJECT AS** (above).
- **Six tracks a pattern**: the pattern-track scope is 129 x 6 items, so a
  track field of up to 2 bytes each fits the default split and 3 bytes fit
  too (the Rytm's 13 tracks don't).

What it does not do:

- **Fields of other kits and of the pool** set by a mod reach the stored
  project at the next stock save of that kit or sound, or the next SAVE
  PROJECT AS, not at once; the current kit's reach it at once.
- **Track and pattern fields** reach the stored project at the next stock
  save of that pattern's track, settings or locks, or the next SAVE PROJECT
  AS, as on the Rytm.
- **Patterns copied by the SysEx project commands** don't take their
  pattern-track and pattern fields with them; core clears the target slot's.
- The spare sound and FX words reach the engine through stock's parameter
  pushes, into the slot every unused parameter goes to; that nothing reads
  that slot is inferred from the code, and on a unit (21 Oct 2026) a
  pattern sounded the same by ear before and after store-check set
  every sound's markers.

## The OS upgrade's restart (0.4)

1.56 ends an OS upgrade as the Rytm's 1.74 does: SUCCESS! REBOOTING, a 3 s
wait, `move #0x2700,sr` at 0x4007db24, then 0x40002766, which waits while
bytes are pending behind the panel link's DMA (0x4133cfc0) or in the UART
queue (0x4133cfd0), counts only their interrupts move on. Core builds
core-ar2's `finish.s` with these addresses (`FINISH_PANEL`,
`FINISH_UART`, `FINISH_DRAIN` in `mod.json`): interrupts are masked only once
nothing is pending (core-ar2's README, "The OS upgrade's restart"). The A4's
routine is the Rytm's instruction for instruction; the hang itself was seen
in analog-emu on the Rytm only, and this site is not yet checked on the A4.

## Lockable parameters (0.5)

The lock service's first part (docs/ADAPTING.md, "Lockable parameters
(core-a4mk2 0.5, Analog Four MKII)"), core-ar2 0.8..0.10's `lock.c`, `rows.c`
and `rows.s` with the A4's pattern and track kinds. The linker writes
`{field, code}` for each lockable field into `core_lock_codes` (CV_FLAG,
0x800, in the code of a CV track's kit field), and mods add their entries to
`core_lock_params`. Codes are 0x70..0x7f per kind: synth (sound fields),
FX and CV (kit fields). A parameter's page knob is an id stock does not have
(above 0x126; stock's records end there and read record 0 for a higher id);
its pseudo id in stock's lists, from part 2, is 0x127 + 16 x kind + code -
0x70.

| site | 1.56 | what |
|---|---|---|
| lockable check | 0x4002327c (`jmp`) | a page view's "can this knob be locked?" reads the id's record's flag; a mod parameter's knob of the current track's kind can be |
| lock get, set, clear, turn | +0x48..+0x54 of the synth, FX and CV parameter sets (0x401a1694.., 0x401a1734.., 0x401a17d4.., `ptr`) | a mod parameter's knob with a trig held: the lock goes into the ledger (below); any other id goes to stock's routine. The three sets share stock's turn, 0x4009e74c |
| page value | page_get_value, 0x40023d54 (pages.s) | with a step, a mod parameter's knob shows the step's lock |
| live record | param_apply_delta, 0x400232e2 (pages.s) | a mod parameter's turn taken while live recording runs locks the value on the step the track plays, as stock's live record (0x400a690c) |

## Mod p-locks (0.5)

A pattern keeps its p-locks in 128 rows of 66 bytes, `[column][track][64
step bytes]`; stock's columns are 0..0x6f, a mod parameter's row carries
its code. Stock edits and plays an unpacked lock store, a 0xe4-byte step
record per step in each of the 129 unpacked patterns, and packs the rows
from it, dropping columns it doesn't know. So core keeps the mod locks in a
ledger, `{step record, code, value}`, that follows stock's moves of step
records by address, as core-ar2's does.

| site | 1.56 | what |
|---|---|---|
| memcpy | 0x40077fa0 (`jmp`) | a copy of 0xe4 bytes or more: the copied records' locks go with them, and the overwritten ones' go |
| memset, bzero, operator new | 0x40077fe8, 0x400778e0, 0x400653b8, 0x400653be (`jmp`) | the locks in the block go |
| store, track and step clear | 0x40015d10, 0x40015c2e, 0x400a35ac (`jmp`) | the locks there go |
| row unpack | 0x40009dbc (`jmp`) | the rows with codes 0x70..0x7f become the store's mod locks |
| row pack, row update | 0x4000a02c, 0x4000a1e6 (`jmp`) | after stock's rows, the mod locks as rows, in code then track order, while one fits under 127 rows; the per-column update runs with the mod rows taken out |
| trig copy | 0x400b905a (`jmp`) | after stock lists a held step's columns for a paste, core lists its mod locks |
| paste's lock set | 0x400b51f6 (`jmp`) | a listed column of 0x70 or more is a mod lock |
| trig fire | 0x40079fbe (`jmp`) | after stock builds a trig's lock list (the immediate list, three scheduled, the note path's), core fills its own slot for that list with the step's mod locks and Param Slide's deltas |
| event accept | 0x40101d58 (`jsr`), 0x40101fb2 (`jmp`) | when the audio interrupt copies an event's list to its channel's list (0x8000e670 + channel x 0x390, a channel per track), core keeps its slot for that channel; a slot filled before the ledger last changed is filled again first, as stock fills a stopped sequencer's first step at STOP and again on its own lock edits only |
| voice apply | 0x401025ac (`jsr`) | when the interrupt applies a channel's list (%a5) to a voice the allocator gave its note (%d2; on a poly track the voice it picks, in UNISON each poly voice in turn), core copies the channel's locks into `core_lock_now` and `core_slide_delta` for that voice |
| dump out | 0x4006b492 (`jmp`) | a pattern's SysEx dump: a marker row (`{0x7f, 0xfe}`, then the layout ID) for its mod rows, or, with no free row, no mod rows |
| dump in | 0x40011d44 (`jmp`) | a received pattern's unpack: its mod rows stay only with a marker holding this build's layout ID; the marker goes |

A fine parameter's second row has the header `{0x80, 0x81}`, never stock's
`{0x80, 0x80}`, which stock's unpack ORs into the last stock fine column
before it. A project load whose layout ID the build doesn't read
(`st_proj_load`) drops every pattern's mod locks; a project load, a kit
load and CREATE NEW clear the playing ones; a set of a field
(`core_store_set`) ends its slide on every voice playing that item.

## Destinations (0.5)

The lock service's second part. A lockable field declared `"mod": true`
(ROUTE in `core_lock_codes`) is a destination of every modulation source of
its track kind: the five routes (velocity, pitch bend, mod wheel, breath,
aftertouch, 5 each), the four Dest knobs (EnvF, Env2, LFO1, LFO2; A and B;
the FX and CV tracks' own), the ten performance macros and CONTROL IN 1
and 2 (5 targets each). The code goes raw in the bytes stock already has:
a route's and a macro target's index byte (signed, -1 none), a Dest word's
high byte. Stock's apply needs no change: an index of 0x70 or more
resolves to its sink slot (0x24b).

| site | 1.56 | what |
|---|---|---|
| list builder's end | 0x400a11a4 (`jmp`) | 0x400a1108(set, mask) builds every destination list (route screens, Dest popups, macro and CONTROL IN setup); core appends the pseudo id of each "mod" field of the set's kind (set +4: 1 synth, 7 FX, 8 CV), whatever the mask |
| id to index | 0x40007632 (`jmp`) | a "mod" pseudo id's engine index is its code |
| index to id | 0x400077d2 (`jmp`) | stock answers 0 (None) for an index above 0x6f; a "mod" code gives its pseudo id |
| Dest value check | 0x4009f6ca (`jmp`), in 0x4009f650 (vtable +0x28) | stock refuses any id above 0x126 (checked: without this site a Dest knob's step stops at the last stock entry); a "mod" pseudo id of the set's kind passes |
| short and long names | +0x68, +0x6c of the synth, FX and CV parameter sets (0x401a16b4.., 0x401a1754.., 0x401a17f4.., `ptr`) | a pseudo id's and a mod parameter's page knob's names: the knob's label and, with a trig held, the screen's title (stock: ERR, INVALID PARAMETER) |
| Dest value text | 0x400f3630, 0x400f368e, 0x400f36ec (`jmp`) | a Dest knob on a "mod" code shows its short name (synth, FX, CV) |
| list item | 0x40035a84 (`jmp`) | `CATEGORY:Name` (MOD when it has no category), stock's format |
| setup target | 0x40036a0a (`jmp`) | a route's, macro's or CONTROL IN's target on its setup screen: the short name and category |
| synth, FX, CV sound push | 0x40100b0e, 0x40100eb4, 0x401010dc (`jmp`, the returns of 0x40100a04, 0x40100d84, 0x40100fb8) | each Dest selector's code, per voice, and the item the voice was pushed with |
| route push | 0x40100cf4 (`jmp`, the return of 0x40100baa(sound, voice)) | each route to a code: {source, place, code, depth} |
| performance push | 0x401012e2 (`jmp`, the return of 0x401011e0(entry, n)) | each macro target on a code: {macro, track, code, depth} |
| knob push | 0x40100678 (`jmp`, the return of 0x401005b8) | a Dest knob's turn (or NRPN) on each voice of its track |

`core_dst_voice(voice, &routes, &dests)` and `core_dst_macros(&n)`
(`analog-four-mk2/lock.h`) hand part 3 what the pushes noted; every write
is made with interrupts masked, as the pushes run from the UI and from the
audio interrupt. A voice is 0..5 (the four synth voices, the FX track, the
CV track), the map rows 0x42878da0 + 0x200 x voice.

A Dest stored outside its list. The lists offer a Dest only the depths of
stages the audio interrupt applies after it (LFO2, Env2, LFO1, EnvF), but
an NRPN, a kit or SysEx can store any index, and stock then adds the term
to a depth its stage has already read: nothing hears it (trace item 8). At
each push core sends such a Dest to the sink, as stock sends an unknown
index, so a depth read at the end of the block (part 3) never carries a
term stock never used. The rule is stock's own list rule, applied to
depths only: a Dest whose target is a depth (a selector's word + 4) that the
selector's mask would not list.

Loads. Stock's sound and kit loads from storage (0x40008808, 0x40008f2a:
a project or kit load, a received kit or sound dump) write 0, OSC1 TUNE at
the route's depth, over a route or macro target index they do not know.
A sound's and a kit's load (`st_sound_load`, `st_kit_load`) first put a mod
code back from the storage (a sound's routes at storage +0xf4, the live
sound's +0xec; a kit's macros, CONTROL IN 1 and 2 at +0x798, live +0x764),
then turn a code this build has no "mod" parameter for into None, and every
code when the item's layout ID is not this build's. None is stock's own
entry of the kind (synth 96, FX 73, CV 68), as each list opens with it
(gap 7, 6 Nov 2026: it was a target's -1 and a Dest word 0, which a Dest
page showed as TUN, and a received kit's mod route targets came back as
OSC1 TUNE).

## The value read (0.5)

The lock service's third part (`value.c`): core-ar2 0.10's `smooth.c` with
the A4's stages. `core_param_values(track, handles, n, flags, out)` gives
each lockable parameter the value stock would play for a coarse parameter
of the same range, in 8.8 steps of the field (`CORE_PARAM_ENGINE`: the
engine's 0..0x7fff, the field's min..max as 0..0x7f00), computed in the
order of stock's audio interrupt (0x401013be), with a clamp to 0..0x7fff
after each term, as stock clamps:

| stage | stock | core |
|---|---|---|
| target | the pushed sound's word (kit block on FX, CV), the trig's lock in its place | the field in `core_voice_item[voice]` (part 2), the voice's `core_lock_now` (part 1) in its place; byte reads from offsets made once |
| smoother | 0x40102f0c: s = 0.97 s + 0.03 target a block, slots below 0x128; the state set to the target at a locked trig and at the next unlocked one (trace item 7) | smooth.c's lazy step and 0.97^k table per voice and code; a new accept on the voice whose trig is locked, or follows a locked one, jumps. `"smooth": false` skips it |
| Param Slide | 0x4010306a: phase and limit per voice (0x80002d54, 0x80002d3c), factor (phase / 2) x scale (0x80002d24) x 0x74822, copy - factor x delta (0x401030c6) | the same arithmetic, with part 1's delta in the engine's scale and the phase stock used in the block (it adds to it after use, and a slide trig resets it first) |
| routes | 0x400fff7a: source x depth / 32768, sources velocity 0x4287c4e8 (bipolar 0x800042dc), bend 0x80006a68 (x 2 - 0x8000), wheel 0x80006a5a, breath 0x80006a4c, aftertouch 0x80006a76, the track from 0x80006008 | the voice's noted routes (part 2) |
| macros, CONTROL IN | 0x40103314: copy[n] (bipolar from +0x498, CONTROL IN 0 while 0x80004804 is set) x depth / 32768 on the voices of the target's track (0x80005ff0) | every noted target on the voice's track |
| LFO2, Env2, LFO1, EnvF | 0x4007cbf2(1), 0x400ff230(1), 0x4007cbf2(0), 0x400ff230(0): ((depth - 0x4000) x out) >> 30 (LFO, out 0x80006b74 + 0x60 n + 0x30 p, depth slot 0xca + 0x10 n + 2 p + k) or >> 28 (envelope, out 0x80003b24 + 0x54 n + 0x1c e, depth slot 0xd0 + 0x10 n + 2 e + k) | each noted Dest by its place's stage, A then B |

The inputs of the last four stages are audio-side state stock holds for a
block. Core copies them once a block at `ev_render_out` (order 1, before
any mod's handler: `core_value_snapshot`), with per voice whether anything
modulates one of its mod parameters, so a read on a voice with nothing
pointing at its mod parameters skips the last three stages. A read in
`ev_render_out` sees the block's values, one in the next `ev_render_in` the
same, one block (0.67 ms) late, as on the Rytm. Part 2's push rule keeps a
Dest stored outside its list at the sink (trace item 8), so no depth in the
snapshot carries a term stock never used.

Voices (decided 2026-11-05): a track's read uses the voice its last trig
went to (part 1's record of the voice applies; on a poly track the
last-played voice, in UNISON the last of its voices, which all play the
same locks; before any trig, a voice stock gives the track), and
`core_param_values_voice(voice, ...)` reads one voice (0..3 synth, 4 FX, 5
CV). A handle is `code | kind << 8` (`core_param_handle(field)`): a build's
own numbering, never kept in a project. `core_param_value`,
`core_param_value_ex`, `core_param_int` and `core_param_value_h` are the
single reads, with core-ar2's names.

`core_store_get` reads a field a byte at a time (core-ar2 0.10's), and the
read does not call it: it reads the field's bytes from offsets made once.

Cost (dtboot, DMA timer 0 ticks, a block 88,284): a read of one parameter
129 on average with nothing modulating it, 179 with every source on it
(at most 531); four in one batch 426 and 528 (at most 1,238), so each
further parameter adds about 100 to 120. One read is 0.15% of a block, four
0.5% (1.4% at the worst seen).

## Mod parameters on MIDI (0.5)

The lock service's fourth part (`midi.c`, `midi.s`): core-ar2 0.8's
`midi.c` with the A4's numbering. An A4 NRPN is MSB x 128 + LSB, looked up
in a 484-entry table of its set's kind, and the set is the channel's
track's (dispatch 0x4006818e), never the MSB's; on a channel shared with
PERF it is the active track's. Stock's synth, FX and CV tables use MSBs 1,
2 and 3, and nothing at MSB 0, so (decided 2026-11-05):

- **NRPN**: a mod parameter is MSB 0, LSB its lock code (0x70..0x7f, the
  number its p-lock rows and destination bytes hold), on its track's
  channel: the same number on every track kind. The value is the data
  entry MSB for a parameter up to 127; one above 127 takes the whole
  14-bit value, as stock's fine parameters, clamped to its range.
- **CC**, opt-in per field (`"cc": true`), as on the Rytm: the lockfile
  gives the field a CC no stock table maps on a track's channel (none of
  the synth, FX, CV, MISC, TRACK and PERF kinds' nor bank select, read from
  the records at 0x40186af4): 12..15, 28..31, 35..37, 39..47, 60..63, 96,
  97, 100 and 101 for a synth field, of which a kit field (FX or CV) gets
  only those the FX and CV tables leave free too (all but 12..15 and
  28..31). A CC's step is the parameter's coarse step.

| what | stock | core |
|---|---|---|
| NRPN in | the three sets' vtable +0x8c, 0x4009ff56 (set, nrpn, value, track, flag): the table 0x4133dd64, then +0x38 | a jmp over its first 8 bytes: MSB 0, LSB 0x70..0x7f on a synth, FX or CV set is core's; core sets the field in the track's sound of the current kit (the kit on FX and CV) with `core_store_set`, which ends its slide (part 1), and the read follows from the next block (part 3) |
| CC in | +0x90, 0x400a0b62 (set, cc, value, track, flag): the table 0x4133ff6c | a jmp over its first 8 bytes: a CC in `core_lock_ccs` of the set's kind is core's |
| live record | +0x38 with the record flag (the caller's, or MIDI CONFIG's 0x400acd80) locks the value on the playing step through 0x400a690c | the same flag: `core_lock_record` (part 1) |
| out | +0x88, 0x4009fde4: when MIDI out applies (0x400ab768), PARAM OUTPUT (0x400ab6e4) NRPN 0x40068b22 or CC 0x400689ce on the channel of +0x94 (0x40068652) | `core_lock_turned` (a mod knob's turn, pages.s) sends the same way; a parameter with no CC sends nothing in CC mode |

Stock's tables are left alone: an unknown number finds no id there, so a
mod NRPN or CC never reaches stock's sets, hooked or not, and under stock
firmware a mod NRPN does nothing.

## Field defaults (0.6)

core-ar2 0.10's (its README, "Field defaults"): a storage field declared
with `"init": N` reads N where its bits are zero. The linker gives each such
field a slot in `core_field_inits` (1..63, the descriptor's bits 16..21),
and `core_store_get` and `core_store_set` XOR the value with it, so the
offset keeps bits 0..15 (the project scope, the largest, has 34,736 bits).
value.c's fast read XORs the same init; every other read and write in core
goes through the two calls. No new sites.

## The modified mark (0.6)

core-ar2 0.8's: after an edit of only mod fields, a project load asks
first, as after a stock edit. Stock marks the project modified with
0x400a5bba(model, 1) (the page edits' call is 0x400befce), keeping the mark
in the settings block's byte 0x4045c8c2; the load reads it (0x4003df98,
through 0x400a5bea) and asks ANY CHANGES WILL BE LOST for the active slot,
or SAVE ... BEFORE LOADING for another when the request asks for it.
`core_project_edited()` asks for the mark from any context and
`core_edit_tick` (`ev_tick`, order 0) makes it, with the project model in
the shared_ptr at 0x418717c8 (written only by its get-or-create,
0x401494de). Core calls it after a turn a mod's page knob takes and after
a lockable parameter's CC or NRPN; a mod calls it after its own keys,
encoders or settings change a field. Stock itself sets the mark on every
page or track key and after every load, and only a save clears it, so core's
mark matters after a save. RANDOMIZE and RELOAD PAGE on a mod knob are not
handled on the A4 yet.

## The project save (0.6)

core-ar2 0.10's `ev_project_save`, `void f(unsigned pre, unsigned how)`,
before (`pre` 1) and after (`pre` 0) every whole save of the live project
to a +Drive slot, with the same `how` (store.h). On 1.56 every such save
ends in 0x400a771a(project model, slot, progress), the Rytm's 0x400b07a4 in
shape and prologue, which holds the file save site 0x400a77e4: it
serializes the live project into the slot's image, writes it to the
+Drive, and on success makes the slot the active one (0x400a5b74) and
clears the modified mark; it returns 0 when the save failed. Core takes
its first 8 bytes (`jmp`, `st_proj_save`). Two jobs call it:

- **The save job** (0x4003da46, its call at 0x4003dacc), built with its
  SAVING box by 0x4003d448, which 0x4003ecbe queues after its OVERWRITE and
  WRITE PROTECTED asks (the Rytm's 0x40052908 and 0x4005417e in one).
  0x4003ecbe has six callers (0x4003eff8, 0x4003f07a, 0x40040380,
  0x40040444, 0x400bf7ea, 0x400e13a0): the front panel's saves.
- **The load job** (0x4003daf0): an empty slot's load runs CREATE NEW
  (0x400a9f1e(model, -1), so `ev_project` with `how` 0 first) and then the
  save, whose return address (0x4003dc26) tells `CORE_SAVE_NEW`. No panel
  action reaches it on 1.56: an empty slot offers INIT NEW (no save, no
  event), SAVE TO (`CORE_SAVE_OTHER`) and TOGGLE, and LOAD lists only full
  slots.

The active slot getter is 0x400a5b9c(model), -1 for none. Of core's other
file save sites, 0x400a503c is in the background job 0x400a5008 and
0x40020772 in the buffer save 0x40020732 (called only by 0x40069900):
neither writes a slot, and no other whole save does (the other calls of the
file save, 0x4001141e, copy the project into RAM: the boot's 0x400f1b18,
0x40096d90, 0x40020732). Not covered, as on the Rytm: a project received
over SysEx, and the project manager's changes to a slot without the live
project.

## Payloads and the mods' area (0.6)

core-ar2 0.11's payloads (`../core-ar2/payload.c`, `core_payloads`;
docs/ADAPTING.md, "Payloads (core-a4mk2 0.6, Analog Four MKII)"). With core
0.6 the linker lays the mods out from 0x45650000 to 0x47C00000 (the profile's
`core_ddr`, 37.7 MB): the gap above the OS's fixed buffers, which
ddr-canary checked untouched on a unit (21 Oct 2026), from its first
64 KB boundary, with the 128 KB area above it. A build with an older core
keeps the 128 KB. The flash budget is the tighter limit for a payload.

## Knob looks (0.6)

core-ar2 0.13's (`looks.c`, `looks.s`; its README, "Knob looks"): an entry
in `core_knob_looks` (`analog-four-mk2/pages.h`) gives a mod's own knob ids
(above 0x126) the look of a stock knob, a range of their own, or a UI
record of the mod's own; an id without one draws as record 0, as before.

| lookup | A4 1.56 | Rytm 1.74 | what differs |
|---|---|---|---|
| `uirec(id)` | 0x400f6232 (18 calls) | 0x400ff5c2 | 84-byte records in RAM from 0x41856c44, ids below 0x127; the jmp ends inside the bound's `cmpi`, so the site is 10 bytes |
| `range(id)` | 0x40007798 (81 calls) | 0x40006ff0 | 56-byte parameter records from 0x40186af4 (the Rytm's 52), the range at +8; the id is loaded into `%d1` |
| a knob's graphic | 0x4009eaaa, the parameter sets' vtable +0x84 (8 vtables) | 0x400a85e8, +88 | |

## Screen calls (0.6)

core-ar2 0.13's (`screen.c`, declared in
`elekloader/sdk/include/analog-four-mk2/screen.h`; docs/ADAPTING.md,
"Screen calls (core-a4mk2 0.6, Analog Four MKII)"). Core calls stock's own
routines; nothing of their code or glyphs is copied.

| call | stock's on 1.56 |
|---|---|
| `core_bitmap_init` | 0x40059222 |
| `core_text_draw`, `core_text_width`, `core_font_height`, `core_font_descent` | the 17 font objects at 0x41735764 + 8 i (in the Rytm's order: 0..3 and 5..11 compared), their data (0x4005b3dc), the text routine 0x4005ad40, the width 0x4005b3e6 over a `std::string` (0x4018471e, 0x40182c6e) |
| `core_key_native` | Brain::key 0x40098b36, with the brain `core_screen_key` keeps from `ev_key` |
| `core_stock_overlay` | the view controller's list, the Rytm's layout (its push, 0x4006293a, is the Rytm's byte for byte); HelpBubbleView (0x4018d228) is the main screen's last view |
| `core_level_readout`, `core_level_field_save`, `core_level_field_put` | `core_screen_enc` starts 47 ticks on the LEVEL / DATA encoder, 10 (11 on an Analog Keys: bit 20 of the boot word 0x401ff54c, the OS's own test) |
| `core_progress_open`, `_set`, `_close` | SAVE PROJECT's SAVING box (0x4003d448): a ProgressWindow of 0x88 bytes (ctor 0x40060462, fill 0x400603c8), its shared_ptr's count (new 0x4014f2e8, copy 0x401465dc, release 0x40144764), pushed onto the controller (0x4006293a), closed by its vtable +0x2c |

What differs from the Rytm:

- **Brain::key** drops the view controller's answer (0x4006304c) and
  returns its listeners' call's (0x40096aee), so `core_key_native` returns 0
  once stock had the event, not if a view took it.
- **The main screen's last view** is HelpBubbleView. TOTAL RECALL
  (TotalRecallMenuView) is pushed after it, so it counts as an overlay.
- **The views that draw nothing** (View's own draw) are KeyboardView,
  QuickPerformanceView, QuickPerformanceSelectionView, TrackSelectionView,
  PatternGridView and StepAttrView: the last two of the Rytm's overlays
  list are none here. The other overlays are the A4's classes that draw.
- **The encoders** are A..J, 0..9, and LEVEL / DATA is 10.
- **The level readout**: in the emulator stock draws none in the header
  after a LEVEL turn (it keeps the kit's name), so `core_level_readout`'s
  window and the kept field (rows 0..8, columns 0..69) are the Rytm's
  until a unit shows where, and for how long, the A4 shows the level.

## Standard events (0.7)

`std.c` serves std1 (docs/ADAPTING.md, "Standard events (std1)") at sites
core owns; A4 numbers are in `analog-four-mk2/keys.h`, `params.h`, `ports.h`
(`leds.h` is empty).

| std1 event | table | how |
|---|---|---|
| `ev_tick`, `ev_draw`, `ev_settings`, `ev_project`, `ev_render_in` | native | = or prefix, sorted by `order` |
| `ev_boot` | `std_boot` | `.boot` calls `core_boot_init` (`BOOT_INIT`) |
| `ev_key` | `std_key` | core's `ev_key` handler, order 0: the KeyEvent's code; bit 0 clear is `KEY_RELEASE` (none for a key a view took) |
| `ev_param_turn` | `std_param_turn` | `param_apply_delta` after `core_params`: a plain turn's sound edit, not a held trig's lock (std.c); `knob` its encoder |
| `ev_voice_start`, `ev_voice_end` | `std_voice_*` | apply 0x401025ac, frees 0x40102174, 0x40102e10; one end per start, steals too |

`core_post` (32 queued) and `core_timer` (16) run in core's first
`ev_tick` handler (UI task), `core_redraw` in its last.
Not here yet: `core_setting_add`, the draw helpers. Unconfirmed: `keys.h`'s
[emu] ids.

## Checked so far

The runs' logs are in their pull requests; what they established:

- **On a unit** (OS 1.56) [hw]: core with
  [hooks-check](../../examples/hooks-check-a4mk2) passed every event;
  with [store-check](../../examples/store-check-a4mk2) every scope's
  markers came back through SAVE PROJECT AS, loads, power cycles and a
  Transfer round trip, CREATE NEW cleared them, and the pattern sounded as
  before; [ddr-canary](../../examples/ddr-canary-a4mk2) found the mods'
  area and both gaps untouched by stock; recovery to stock 1.56 worked; the
  parameter routines, views and page functions passed with
  [pages-check](../../examples/pages-check-a4mk2) ("Parameter pages
  (0.2, 0.3)" above).
- **In rytm-emu's A4 build** [emu]: core alone runs as stock, frame for
  frame; every mod storage case above (saves, loads, power cycles, the
  write-through, per-part saves, `ev_project`); no stock access to the live
  homes other than core's.
- **The lock service (0.5)** [emu], with
  [lock-check](../../examples/lock-check-a4mk2) and
  [sound-check](../../examples/sound-check-a4mk2): p-locks set, copied,
  cleared, saved, loaded and played, with Param Slide, and stock 1.56
  playing such a project with no fault; destinations in every list of their
  kind, kept through SAVE KIT and LOAD KIT, and the Dest value check's site
  shown to be needed; the value read equal to stock's engine copy of a
  stock parameter under every modulation stage, on the synth, FX and CV
  tracks; NRPNs and CCs in and out, live recording and the PERF channel; a
  mod parameter heard driving a test tone.
- Not checked yet: the lock service on a unit; track clear, SysEx dump out
  and in, the kit change's and CREATE NEW's clear of locks; a fine
  parameter; RANDOMIZE landing on a mod parameter; the route screens'
  lists; a kit of another layout; a poly track's two voices, a sound-locked
  trig's pool sound, `"smooth": false`, CONTROL IN; an NRPN with a trig
  held, USB MIDI.
- **0.6 on a unit** (OS 1.56, 11-12 Nov 2026) [hw]: the check mods'
  plans passed as in the emulator: `ev_project_save` around saves onto the
  active slot, into another and of a new project into an empty slot (stock's
  own load request with create-if-empty set, made from a page knob inside
  the view dispatch: `CORE_SAVE_NEW`), the modified mark, payloads, knob
  looks and the screen calls (fonts, overlays, the LEVEL readout's window,
  `core_key_native`, the progress box); CPU about 73 % with analoghealth
  (the emulator's about 50 %), the heap flat.
- Statically, against the stock image: every site's stock bytes, that each
  covers whole instructions, that each body in `store.s` and `looks.s`
  resumes after the bytes its site displaced, and that the serializers
  never name the bytes mod storage uses (tests/test_analog_four_mk2.py).

## Building

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/store-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.lint examples/store-check-a4mk2/out/store-check-1.0.elemod \
    --stock Analog-Four_MKII_OS1.56.syx --with mods/core-a4mk2/out/core-0.5.elemod
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.5.elemod --mod examples/store-check-a4mk2/out/store-check-1.0.elemod \
    --out a4-store.syx --version SC56 --lockfile a4-store.lock.json
```
