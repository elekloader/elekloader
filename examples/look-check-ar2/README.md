# look-check (Analog Rytm MKII, core-ar2 0.12)

A test for core-ar2 0.12's knob looks (`core_knob_looks`, docs/ADAPTING.md
"Knob looks (core-ar2 0.12, Analog Rytm MKII)").

23 LOOK CHECK pages: 1..12 after the LFO key's stock page, 13..23 after the
AMP key's. Each has four pairs of knobs, A|B, C|D, E|F and G|H: on the left
a stock id L, on the right a knob id of the mod's own whose entry names L.
This mod's `core_params` answers both knobs of a pair with the same value
and asks stock to write L's text for both, and its `core_param_locks` names
the own knob with L's label and name, so the two cells of a pair are the
same pixel for pixel when core gives the look. The pairs take one stock id
of each of the 87 different UI records a stock 1.74 boot builds (`looks.h`):
every knob display stock has. The last page adds a knob whose record comes
from a `ui` callback (a copy of 457's) and one with COMP MIX's look and a
range of 0..12, whose dial core spans over 0..12: its stock partner shows
the value moved onto 0..127, so the pair is the same when core does.

While it is in, its `core_params` entry for 1..468 also answers the 87
stock ids on stock's own pages. It is a test, not for a project you keep.

After the magic `LKC1` (the symbol `lk`): own knobs, the first own id,
ticks, then for each own knob what core's two lookups answer, read at the
first tick: `{int16 look, int16 ok, u32 uirec, u32 want, int32 range[3],
int32 want_range[2]}`; ok is 1 when uirec and the range are look's (the
callback's record, the entry's range).

`looks.h`, `pages.c` and `mod.json` are written by `gen_looks.py` from a dump
of a stock 1.74 boot's RAM 0x419a4000..0x419ae800 (analog-emu's dtpeek after
booting to the main screen); the dump is derived from the stock OS, so it is
not in the repository.
