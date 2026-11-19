# core-ar2: the skips and submenus (0.16)

Work the Analog Rytm MKII's OS 1.74 does every block or every UI tick that
changes nothing, skipped while a mod asks for it, and submenus from a
SETTINGS row. Every address is OS 1.74's.

## The silent-voice skip (0.16)

`idle.c` and `idle.s`, at 0x40121208 in the render's slot loop: stock calls
each of the 8 voice slots' machine routines every block from the slot's
first SYN trig on, whether the voice sounds or not, and that cost never
leaves (a power cycle is the only thing that clears it: no STOP, kit load or
project load does). With the skip on, core stops making that call once the
slot is silent and the call changes nothing the voice board gets, and
returns the call to stock on a trig of the slot's track (SYN on or off), a
new owner, sequence state or idle bit at once, and on a knob, an LFO or a
kit's values at its next check.

**Off at boot: on only if asked and nobody holds it off**
(`analog-rytm-mk2/voice.h`):

| symbol | what |
|---|---|
| `core_voice_skip_want` | `uint8_t`, 0 at boot: the user's choice, written by the one mod that offers it as a setting; any context, any time (read once per slot per block) |
| `core_voice_skip_hold` | `uint32_t`, 0 at boot: the count of holds; the skip runs only while it is 0 |
| `core_voice_skip_hold_add()`, `core_voice_skip_hold_release()` | a mod that needs stock's exact render while it works (one that reads a voice's machine state, or measures stock's load) adds a hold when it starts and releases it when it ends, once each; one instruction each, any context |
| `core_voice_skips` | the calls skipped since boot |

Turned off either way, a slot that was skipped gets its machine call again
from the next block, and its digital state still goes back to the boot's at
its next SYN trig. Off, the site costs a few instructions more than stock's
two.

**It changes the sound slightly.** At the next SYN trig of a slot that was
skipped, just before stock's call for that note, the slot's digital state
(its noise generator and filters, gains, resonator buffer and the machine's
private record words) goes back to what the boot left, so the note starts
as the slot's first after a power-on does, not from wherever the generator
had run to. While a slot is skipped, its digital output differs only by the
machine's leftover gain, below 2^-19 of full scale (under -114 dB).

A slot is silent when, every block:

| test | why |
|---|---|
| the machine's gain words (0x80007000 + 8 x slot) are both within ±0xfff | its noise and click are gone; ten machines leave a small negative gain stuck for good, so the test is a level, not zero |
| the machine is a public one (0..33), not CP CLASSIC (6) | machines mods add keep state core doesn't know; CP CLASSIC's generator is shared |
| no trig of the owner, and the machine's own sequence (0x8000fef8 + 0x20 x slot) is done (state < 0) or not waiting for its timer (timer below 0x10) | no timed step of the machine is still to come |
| the slot's amp envelope (0x42fb49b4 + 0x20 x slot) is in its decay or release, for twice the time stock's rate routine (0x4011e7d6) gives for that rate, plus 0.25 s | the note has ended, not just its digital part |
| the call's inputs (the owner's parameter block, pitch, owner, idle bit, pointers, sequence state) are unchanged; inputs that move every block (an LFO) are looked at again 16 blocks later | anything stock would react to keeps the slot |
| the call changes nothing in the control frame or the slot's sequence record, leaves the gains as they were and the click buffer at 0, for 16 calls in a row, and again every 256th block while skipped | proof that skipping it changes nothing sent; the frame bytes the render clears after each send (byte +4 of entries 0x15..0x4c) may be set, the same each call, and core sets them again in each skipped block; a call that changes anything else is left to stock for 256 blocks |

Let off, and frozen while skipped: BD PLASTIC, SD NATURAL, BD
SHARP and BD ACOUSTIC's private record words (an envelope that keeps
decaying), and XT CLASSIC's ring-out words on slots 3 and 5 (frame
+0x7a/+0x7c, +0x7e/+0x80). SY CHIP (its arpeggio counter) never passes. A
SYN-off trig gives the slot back to stock without a reset, as stock does,
from the frozen words.

Each slot's cost at the site, per block, in the emulator's count: stock 2;
off 11; on, a sounding voice 14, a skipped slot 110 to 160 (with its call
made and checked every 256th block), a watched slot about 120, against 1100
to 1400 for an idle machine call. A checked call (core making the call and
comparing the 0x3d8-byte frame) costs about 5 % of a block, so at most one
slot makes one in a block; the turn starts at slot `core_block & 7`, and a
watched slot without it gets stock's call. Without that limit, a STOP (a trig
to every track) or the skip's switch-on put the slots in step, and every
256th block made five or six checked calls and ran past the block's time
(T238). A knob, an LFO or a kit's values on a skipped slot reach the voice at
its next check (at most 264 blocks, 176 ms; a machine change in the next
block); the voice is silent by then.

## The sample-layer skip (0.16)

`sample.s`, at 0x4011cc08 and 0x4011cd46 in the sample layer's slot loop
(0x4011c6b8): stock resamples, filters (2:1) and scales every voice slot's
sample every block, 32 frames into 0x800067f8 + 0x80 x slot, whether a
sample plays or not. A slot that plays none fetches from a zero buffer, so
with its filter history at zero all of that gives zeros and leaves the
history at zero. The skip leaves it out for such a slot and writes what it
would have written: the phase words (+0x1c, +0x20, +0x04), the gain word
(+0x10) and 32 zeros. **The sound is unchanged, bit for bit.**

**Off at boot: on only if asked and nobody holds it off**
(`analog-rytm-mk2/voice.h`):

| symbol | what |
|---|---|
| `core_sample_skip_want` | `uint8_t`, 0 at boot: the user's choice, written by the one mod that offers it as a setting; any context, any time (read once per slot per block) |
| `core_voice_skip_hold` | the silent-voice skip's hold count (above) holds this skip off too |
| `core_sample_skips` | the slot-blocks skipped since boot |

A slot is skipped in a block when all of these hold:

| test | why |
|---|---|
| the slot is not playing (+0x28 is 0) | its fetch is the zero buffer |
| its bit is clear in this block's and the last block's trig masks (0x8000586c, 0x80005868) | a trig sets the slot up one block late and ramps its gain to 0 in the trig block |
| the filter history, 12 longs at +0x2c..+0x5b, is all 0 | zero in and zero history give exactly zero out and keep the history at zero; a trig clears it, a sample that has ended may leave it nonzero until the next trig |
| the MAC unit is in stock's mode (MACSR & 0xf0 is 0x20) | the mode the steps are exact in |

What still runs for every slot: the fetch and its DMA (shared by all
slots in one pipeline), the per-slot rate (0x4011c1e0) and the amp envelope
(0x40109940, not part of the sample layer). In the emulator the three steps
the skip leaves out cost about 1 point of render load per idle slot.

## The UI skip (0.16)

`cast.s`, at 0x401b82f2, the entry of stock's `__dynamic_cast` (libstdc++).
The main screen's refresh (0x400c9272) asks "is view X open?" about twenty
times, each by casting every entry of the open-view list (the finders
0x401a1ed6..0x401a2d02). The views use multiple inheritance, so every cast
walks the class tree and compares type names with strcmp. On the emulator
that is about two thirds of the CPU the UI uses outside the audio render,
on every page and whether or not Overbridge is linked.

With the skip on, the cast keeps its answers in a memo: 256 sets of 8
entries (32 KB), keyed by the source subobject's vtable pointer, the source
and target types and the hint, holding the offset from the source to the
answer (or "no answer"). The idle main screen alone casts with over 200
keys (11 view types x 20 targets x 2 hints), so the sets are 8-way: a new
answer goes in front of its set and the set's oldest drops out. A cast
whose hint or offset doesn't fit 16 bits always runs stock's search. The vtable pointer fixes the complete type and
where the subobject sits in it, and vtables and type infos are in flash, so
the memo's answer is always the one stock's search gives. A miss runs
stock's search and keeps its answer. Lookup and store run with interrupts
masked for at most about 80 instructions, so casts from different tasks never see
half an entry. **It changes nothing the unit does or shows**, only the CPU.

**Off at boot: on only if asked and nobody holds it off**
(`analog-rytm-mk2/ui.h`):

| symbol | what |
|---|---|
| `core_ui_skip_want` | `uint8_t`, 0 at boot: the user's choice, written by the one mod that offers it as a setting; any context, any time (read once per cast) |
| `core_ui_skip_hold` | `uint32_t`, 0 at boot: the count of holds; the skip runs only while it is 0 |
| `core_ui_skip_hold_add()`, `core_ui_skip_hold_release()` | a mod that measures stock's UI load adds a hold when it starts and releases it when it ends, once each; one instruction each, any context |
| `core_ui_skip_hits`, `core_ui_skip_misses` | the casts answered from the memo and those that ran stock's search, since boot, while the skip is on |

Off, the site costs two tests more than stock's cast. The memo keeps its
entries while the skip is off; they stay right, because they never depend
on anything that changes.

## The FX skip (0.16)

`fx.s` and `fx.c`, in the FX block 0x4010e6bc: stock builds both send buses
from all 8 voice slots, and calls the delay (0x4010da24) and the reverb
(0x4010d434), every block at full cost, with every send at 0 too.

**Off at boot: on only if asked and nobody holds it off**
(`analog-rytm-mk2/fx.h`):

| symbol | what |
|---|---|
| `core_fx_skip_want` | `uint8_t`, 0 at boot: the send trim and the delay skip (exact); written by the one mod that offers it as a setting, any context, any time |
| `core_reverb_clear_want` | `uint8_t`, 0 at boot: the reverb clear and skip (changes the sound slightly); runs only with `core_fx_skip_want` on |
| `core_delay_clear_want` | `uint8_t`, 0 at boot: the delay clear (changes the sound slightly); runs only with `core_fx_skip_want` on |
| `core_fx_skip_hold` | `uint32_t`, 0 at boot: the count of holds; every part runs only while it is 0 |
| `core_fx_skip_hold_add()`, `core_fx_skip_hold_release()` | a mod that needs stock's exact FX render while it works adds a hold and releases it, once each; one instruction each, any context |
| `core_fx_skips` | the blocks a delay or reverb call was left out of, since boot |

| part | site | rule |
|---|---|---|
| send trim | 0x4010e75e (jmp), in place of the send loop 0x4010e780..0x4010e7aa | a slot whose two gains and two increments are 0 adds exactly 0 and keeps its gains, so the loop runs only the slots that send, in stock's order; none: the bus is written as 32 frames of 0; more than 5: stock's loop |
| delay skip | 0x4010e7c4 (10 bytes: its `jsr (pc)` and the `move.w 0x8000fbb6,%d3` after it) | no call while its bus is 0, its parameters (0x8000fbaa..0x8000fbbb) are those of its last call, that call changed none of its state (0x8000ca00..0x8000ca0f, 0x8000dff0..0x8000e12b) and wrote 0 to its output and its staging block, and no call has written anything but 0 into its 4 MB line for 16,400 blocks (a whole turn of the write position, about 11 s); the eDMA then moves zeros, as a call would |
| delay clear | the same site | once fed, stock's delay never decays to exactly 0 either (its output keeps a few hundred of 2^31), so the skip alone ends the first time the delay is used. Input 0, and nothing at or above 1024 (4 LSB of 24 bits) written into the line or the output, for 16,400 blocks, so the whole line is that quiet: core clears 0x2000 bytes of the line (0x4b400000, 4 MB) a block, with the gate (0x8000dff0..0x8000e007), the tap's scratch and the filter (0x8000e01c..0x8000e12b), the staging block and the output, without the call (512 blocks); then the skip's rule takes over. A first call after a clear that writes anything but 0 is a failure; after 2, not until the input moves |
| reverb clear and skip | 0x4010e81e (10 bytes: its `jsr (pc)` and the `move.l #0x1000,%d4` after it) | once excited, stock's reverb never decays to 0 (a limit cycle of about ±117 of 2^31, which leaves main L at -1). Input 0 for 1500 blocks (1 s) with the output below 256 (1 LSB of 24 bits) all along: core clears its state words and SRAM blocks every block and 0x2000 bytes of its lines (0x4b000000, 0x98000 bytes) a block, without the call (76 blocks); then calls it from the cleared state for 1024 blocks, which must leave the output exactly 0 (else stock again; after 2 failures, not until the input moves); then no call while its input is 0 |

Input that is not 0 gives each part back to stock in that block. After a
clear, the next delay or reverb tail starts from silence, as after a power-on. Both routines leave
the MAC unit as they found it, so a call left out does too.

## Submenus (0.16)

`submenu.s` (`analog-rytm-mk2/settings.h`): a row a mod adds to the GLOBAL
menu in `ev_settings` (`core_additem`) can open a menu of rows of its own.
From the row's select, `core_submenu_open(menu, &sm)` shows stock's own
checkbox menu view, the one MIDI CONFIG > SYNC and the +Drive's FORMAT are
(0x400780e8, the whole screen): `sm.title` in the header, `sm.icon` (a stock
bitmap, or 0 for none, as FORMAT has) beside it, and `sm.rows`, each added
with `core_additem` as in SETTINGS, so their select and change get the
submenu as payload. It is shown as stock's GLOBAL rows show theirs
(0x40077a16, from 0x400dae7a), one level deeper than its parent (0x40078974:
GLOBAL 2, its submenus 3), and NO goes back to the parent. Core builds it the
first time and keeps it, as GLOBAL keeps its submenus, so the cursor stays
where it was; it is built again only if the parent is another menu. On its
own it adds nothing to any menu.

