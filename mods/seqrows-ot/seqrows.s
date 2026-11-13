| SPDX-License-Identifier: GPL-2.0-or-later
| seqrows, an Octatrack companion of core-ot: rows mods add to PROJECT > CONTROL >
| SEQUENCER. ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| SEQUENCER keeps three parallel arrays of 3 (CHAIN AFTER, SILENCE TRACKS, LFO AUTO
| CHANGE): labels 0x400b27d0, value texts 0x400b27dc (const char *get(void)) and
| setters 0x400b282c (void set(delta, press)). They become ot_seq_label, ot_seq_get,
| ot_seq_set (stock's three first); the six operands naming them are ptr sites (the
| setters' fourth, the encoder's at 0x40065cc4, is one octabam's QUANTIZER leaves); the
| list's count gets the mods' rows; and the draw loop starts at the list's scroll
| (stock's starts at row 0, which only works while every row fits).
|
|   0x40065c7c  pea 3 ; pea 3   (the list's count and its visible rows)
|   0x40065bca  clr.l d2 ; lea 32(sp),sp   (the draw loop's first row)
|
| A companion, not core: octabam's SCALE QUANTIZER (v1.0 in the shop) patches these
| seven places to add its own three rows, so the two refuse each other.
        .section .run, "ax"

        .globl  seqrows_count, seqrows_draw
seqrows_count:
        pea     ot_seq_get_n            | the count: stock's 3 and the mods' rows
        pea     3                       | three visible, as stock
        jmp     0x40065c84

seqrows_draw:
        move.l  0x460e43d8, %d2         | the list's scroll: the first row drawn
        lsl.l   #2, %d2
        lea     32(%sp), %sp
        jmp     0x40065bd0
