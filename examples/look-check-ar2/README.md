# look-check (Analog Rytm MKII, core-ar2 0.13)

A test for core-ar2 0.13's knob looks (`core_knob_looks`, docs/ADAPTING.md
"Knob looks (core-ar2 0.13, Analog Rytm MKII)").

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

## The pages

Which knob shows what. The own ids are the ones a fresh lockfile gives
(555 up, in the order of `looks.h`); a build whose lockfile already holds
other mods' knobs may have them elsewhere (its lockfile says). Written by
`page_table.py` from `looks.h`. To see each stock id's name and range, run
`looks_table.py` on your own dump and OS image (it is read from the stock OS,
so it is not in the repository).

| page | key | pair | stock id | own id | note |
|---|---|---|---|---|---|
| 1 | LFO | A/B | 1 | 555 |  |
| 1 | LFO | C/D | 29 | 556 |  |
| 1 | LFO | E/F | 30 | 557 |  |
| 1 | LFO | G/H | 31 | 558 |  |
| 2 | LFO | A/B | 32 | 559 |  |
| 2 | LFO | C/D | 33 | 560 |  |
| 2 | LFO | E/F | 34 | 561 |  |
| 2 | LFO | G/H | 38 | 562 |  |
| 3 | LFO | A/B | 39 | 563 |  |
| 3 | LFO | C/D | 41 | 564 |  |
| 3 | LFO | E/F | 42 | 565 |  |
| 3 | LFO | G/H | 43 | 566 |  |
| 4 | LFO | A/B | 45 | 567 |  |
| 4 | LFO | C/D | 46 | 568 |  |
| 4 | LFO | E/F | 47 | 569 |  |
| 4 | LFO | G/H | 48 | 570 |  |
| 5 | LFO | A/B | 49 | 571 |  |
| 5 | LFO | C/D | 54 | 572 |  |
| 5 | LFO | E/F | 57 | 573 |  |
| 5 | LFO | G/H | 58 | 574 |  |
| 6 | LFO | A/B | 60 | 575 |  |
| 6 | LFO | C/D | 62 | 576 |  |
| 6 | LFO | E/F | 64 | 577 |  |
| 6 | LFO | G/H | 65 | 578 |  |
| 7 | LFO | A/B | 66 | 579 |  |
| 7 | LFO | C/D | 67 | 580 |  |
| 7 | LFO | E/F | 68 | 581 |  |
| 7 | LFO | G/H | 69 | 582 |  |
| 8 | LFO | A/B | 70 | 583 |  |
| 8 | LFO | C/D | 71 | 584 |  |
| 8 | LFO | E/F | 72 | 585 |  |
| 8 | LFO | G/H | 73 | 586 |  |
| 9 | LFO | A/B | 75 | 587 |  |
| 9 | LFO | C/D | 76 | 588 |  |
| 9 | LFO | E/F | 78 | 589 |  |
| 9 | LFO | G/H | 80 | 590 |  |
| 10 | LFO | A/B | 81 | 591 |  |
| 10 | LFO | C/D | 83 | 592 |  |
| 10 | LFO | E/F | 84 | 593 |  |
| 10 | LFO | G/H | 95 | 594 |  |
| 11 | LFO | A/B | 96 | 595 |  |
| 11 | LFO | C/D | 97 | 596 |  |
| 11 | LFO | E/F | 98 | 597 |  |
| 11 | LFO | G/H | 100 | 598 |  |
| 12 | LFO | A/B | 106 | 599 |  |
| 12 | LFO | C/D | 115 | 600 |  |
| 12 | LFO | E/F | 126 | 601 |  |
| 12 | LFO | G/H | 159 | 602 |  |
| 13 | AMP | A/B | 190 | 603 |  |
| 13 | AMP | C/D | 191 | 604 |  |
| 13 | AMP | E/F | 207 | 605 |  |
| 13 | AMP | G/H | 224 | 606 |  |
| 14 | AMP | A/B | 243 | 607 |  |
| 14 | AMP | C/D | 244 | 608 |  |
| 14 | AMP | E/F | 251 | 609 |  |
| 14 | AMP | G/H | 252 | 610 |  |
| 15 | AMP | A/B | 267 | 611 |  |
| 15 | AMP | C/D | 282 | 612 |  |
| 15 | AMP | E/F | 289 | 613 |  |
| 15 | AMP | G/H | 290 | 614 |  |
| 16 | AMP | A/B | 291 | 615 |  |
| 16 | AMP | C/D | 294 | 616 |  |
| 16 | AMP | E/F | 295 | 617 |  |
| 16 | AMP | G/H | 296 | 618 |  |
| 17 | AMP | A/B | 298 | 619 |  |
| 17 | AMP | C/D | 322 | 620 |  |
| 17 | AMP | E/F | 323 | 621 |  |
| 17 | AMP | G/H | 324 | 622 |  |
| 18 | AMP | A/B | 327 | 623 |  |
| 18 | AMP | C/D | 328 | 624 |  |
| 18 | AMP | E/F | 382 | 625 |  |
| 18 | AMP | G/H | 401 | 626 |  |
| 19 | AMP | A/B | 403 | 627 |  |
| 19 | AMP | C/D | 406 | 628 |  |
| 19 | AMP | E/F | 408 | 629 |  |
| 19 | AMP | G/H | 414 | 630 |  |
| 20 | AMP | A/B | 420 | 631 |  |
| 20 | AMP | C/D | 431 | 632 |  |
| 20 | AMP | E/F | 434 | 633 |  |
| 20 | AMP | G/H | 444 | 634 |  |
| 21 | AMP | A/B | 450 | 635 |  |
| 21 | AMP | C/D | 457 | 636 |  |
| 21 | AMP | E/F | 461 | 637 |  |
| 21 | AMP | G/H | 463 | 638 |  |
| 22 | AMP | A/B | 464 | 639 |  |
| 22 | AMP | C/D | 467 | 640 |  |
| 22 | AMP | E/F | 468 | 641 |  |
| 22 | AMP | G/H | 457 | 642 | ui callback: a copy of 457's record |
| 23 | AMP | A/B | 94 | 643 | COMP MIX's look with min 0, max 12 (stock partner shows the value on 0..127) |

## The record

After the magic `LKC1` (the symbol `lk`): own knobs, the first own id,
ticks, then for each own knob what core's two lookups answer, read at the
first tick: `{int16 look, int16 ok, u32 uirec, u32 want, int32 range[3],
int32 want_range[2]}`; ok is 1 when uirec and the range are look's (the
callback's record, the entry's range).

`looks.h`, `pages.c` and `mod.json` are written by `gen_looks.py` from a dump
of a stock 1.74 boot's RAM 0x419a4000..0x419ae800 (analog-emu's dtpeek after
booting to the main screen); the dump is derived from the stock OS, so it is
not in the repository.
