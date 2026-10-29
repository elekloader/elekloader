| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 1.1 and core-dn2 0.3: ev_frame_params, each frame's per-track
| parameter values before the stock modulators ride on them. ColdFire V4;
| assemble with -mcpu=54455.
|
| The frame interrupt (vector 191) rebuilds a block of per-track values from
| the smoother every frame, then calls the per-track modulators (velocity, a
| second source) with the block pushed: the site is that jsr (DT2 0x4002e90c,
| MODS_STOCK 0x400d9508, block 0x80005b50, 71 values a track; DN2 0x40027196,
| 0x400db390 with the mirror as a second argument, block 0x800068e4, 101).
| Track t's values are int16 at block + 0x22 + 2 FRAME_STRIDE t, value index j
| (the Sound array's, the modulation destinations') at + 2 j.
|
| Handlers: void f(int16_t *values, int stride), at interrupt level, once a
| frame: they add to the values (held in each parameter's range by the
| handler), and the stock modulators and LFOs then add theirs on top. They
| change neither the kit nor what the pages show. With no handler the stock
| modulators run as they did.
        .text
        .globl  core_frame
core_frame:
        move.l  %a2, -(%sp)
        lea     ev_frame_params, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        pea     FRAME_STRIDE            | the stride, in values
        move.l  12(%sp), %d1            | the block (after a2 and the stride)
        addi.l  #0x22, %d1
        move.l  %d1, -(%sp)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movea.l (%sp)+, %a2
        jmp     MODS_STOCK              | the stock modulators, the stack as it came
