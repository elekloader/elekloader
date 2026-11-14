| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: each track's renderer time (core-ot 0.38).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The frame builder calls each track's renderers through its slots (current, then
| next, 0x400d61d0/0x400d61f0), d3 = the track. Core times both calls on DMA timer
| 3, as the frame meter does, into ot_render_meter (core.h): a budget per track for
| machines and mods (Modwerk's base guards CPU at the same two calls).
|
|   0x4000d43e  jsr (a0) ; lea 16(sp),sp          the current renderer
|   0x4000d52c  jsr (a0) ; move.b (a2),d0 ; moveq #15,d4    the next one
        .section .run, "ax"

        .globl  core_render_a, core_render_b, ot_render_meter
core_render_a:
        move.l  0xfc07c00c, %d0         | d0 is free before the call (the args are pushed)
        move.l  %d0, core_r_t0
        jsr     (%a0)
        bsr.s   core_render_count
        lea     16(%sp), %sp
        jmp     0x4000d444
core_render_b:
        move.l  0xfc07c00c, %d0         | d0 is free before the call (the args are pushed)
        move.l  %d0, core_r_t0
        jsr     (%a0)
        bsr.s   core_render_count
        move.b  (%a2), %d0
        moveq   #15, %d4
        jmp     0x4000d532

| d3 = the track. Keeps every register.
core_render_count:
        lea     -12(%sp), %sp
        movem.l %d0-%d1/%a0, (%sp)
        moveq   #7, %d0
        cmp.l   %d3, %d0
        bcs.s   9f                      | not a track 0-7
        move.l  0xfc07c00c, %d1
        sub.l   core_r_t0, %d1          | this call's counts
        move.l  %d3, %d0
        lsl.l   #2, %d0
        lea     ot_render_meter, %a0
        move.l  %d1, (%a0,%d0.l)        | last[t]
        cmp.l   32(%a0,%d0.l), %d1
        bls.s   1f
        move.l  %d1, 32(%a0,%d0.l)      | worst[t]
1:      addq.l  #1, 64(%a0,%d0.l)       | calls[t]
        add.l   %d1, 96(%a0,%d0.l)      | total[t]
9:      movem.l (%sp), %d0-%d1/%a0
        lea     12(%sp), %sp
        rts

        .section .bss
        .balign 4
ot_render_meter: .skip 128
core_r_t0: .skip 4
