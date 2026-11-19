| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.16: the FX skip's sites and switches (fx.c has the delay's and
| the reverb's rules).
|
| 0x4010e75e, in the FX block 0x4010e6bc, between the send gains' update and
| the send loop 0x4010e780..0x4010e7aa: `move.l %d3,%d1; move.l %d3,%d0;
| lsl.l #6,%d1`. The loop runs 32 frames of 8 voice slots for one bus (%d3: 0
| delay, 1 reverb): per slot, ACC1 += gain L x the voice's sample, ACC0 +=
| gain R x it, and both gains move by their increments (0x419c9f98 + bus x
| 0x40 + slot x 8, and %d7 = 0x419c9f58 + slot x 8); ACC0 and ACC1 go to the
| bus (0x80007a44 + bus x 0x100) after each frame. Live here: %d3 the bus,
| %d4 the per-slot levels, %d7, %a4, %a5, and the receive frame at (0x34,%sp);
| %d0-%d2, %d5, %d6, %a0-%a3 and %a6 are stock's scratch until 0x4010e7ac.
|
| A slot whose two gains and two increments are 0 adds exactly 0 to both
| sums and its gains stay 0, so leaving it out changes nothing. On
| (core_fx_skip_want, no core_fx_skip_hold), the site lists the slots that
| send: none, and the bus is 32 frames of 0 (written, because the reverb
| works on its bus in place); up to TRIM_MAX, and core's loop runs only those,
| in stock's order; more, and stock's loop runs.
        .equ    GAINS, 0x419c9f98
        .equ    BUSES, 0x80007a44
        .equ    STOCK_LOOP, 0x4010e764
        .equ    AFTER_LOOP, 0x4010e7ac
        .equ    TRIM_MAX, 5
        .equ    ENTRY, 24               | gain L, gain R, inc L, inc R, sample offset, gains' address

        .text
        .globl  core_fx_send_site, core_fx_skip_hold_add, core_fx_skip_hold_release
        .align  2
core_fx_send_site:
        tst.b   core_fx_skip_want
        beq     9f
        tst.l   core_fx_skip_hold
        bne     9f
        move.l  %d3,%d0
        lsl.l   #6,%d0
        movea.l #GAINS,%a1
        adda.l  %d0,%a1                 | this bus's gains, by slot
        movea.l %d7,%a0                 | the increments, by slot
        lea     fx_trim,%a6
        movea.l %a6,%a3
        moveq   #8,%d2                  | slot 0's sample, in a receive frame
        moveq   #8,%d6
1:      movem.l (%a1),%d0/%d1
        move.l  %d0,%d5
        or.l    %d1,%d5
        or.l    (%a0),%d5
        or.l    (4,%a0),%d5
        beq.s   2f                      | sends nothing
        move.l  %d0,(%a3)+
        move.l  %d1,(%a3)+
        move.l  (%a0),(%a3)+
        move.l  (4,%a0),(%a3)+
        move.l  %d2,(%a3)+
        move.l  %a1,(%a3)+
2:      addq.l  #8,%a1
        addq.l  #8,%a0
        addq.l  #4,%d2
        subq.l  #1,%d6
        bgt.s   1b
        movea.l %a3,%a1                 | the list's end
        move.l  %a3,%d0
        sub.l   %a6,%d0
        beq     6f
        cmpi.l  #TRIM_MAX*ENTRY,%d0
        bhi     9f

| the slots that send, in stock's order, one frame at a time
        move.l  %d3,%d0
        lsl.l   #8,%d0
        movea.l #BUSES,%a3
        adda.l  %d0,%a3
        movea.l (0x34,%sp),%a2
        moveq   #32,%d5
3:      movea.l %a6,%a0
4:      move.l  (16,%a0),%d6
        move.l  (0,%a2,%d6.l),%d6       | the voice's sample
        movem.l (%a0),%d0/%d1
        mac.l   %d0,%d6,%acc1
        mac.l   %d1,%d6,%acc0
        add.l   (8,%a0),%d0
        add.l   (12,%a0),%d1
        movem.l %d0/%d1,(%a0)
        lea     (ENTRY,%a0),%a0
        cmpa.l  %a1,%a0
        bcs.s   4b
        movclr.l %acc0,%d0
        move.l  %d0,(%a3)+
        movclr.l %acc1,%d0
        move.l  %d0,(%a3)+
        lea     (48,%a2),%a2
        subq.l  #1,%d5
        bgt.s   3b
        movea.l %a6,%a0                 | the gains back
5:      movea.l (20,%a0),%a2
        move.l  (%a0),(%a2)+
        move.l  (4,%a0),(%a2)
        lea     (ENTRY,%a0),%a0
        cmpa.l  %a1,%a0
        bcs.s   5b
        jmp     AFTER_LOOP

| nothing sends: the bus is silence
6:      move.l  %d3,%d0
        lsl.l   #8,%d0
        movea.l #BUSES,%a3
        adda.l  %d0,%a3
        moveq   #16,%d6
7:      clr.l   (%a3)+
        clr.l   (%a3)+
        clr.l   (%a3)+
        clr.l   (%a3)+
        subq.l  #1,%d6
        bgt.s   7b
        jmp     AFTER_LOOP

| stock: the three instructions the site displaced, then stock's loop
9:      move.l  %d3,%d1
        move.l  %d3,%d0
        lsl.l   #6,%d1
        jmp     STOCK_LOOP

| 0x4010e7c4 and 0x4010e81e: stock reaches the delay and the reverb with a
| 4-byte `jsr (pc)`, so each site also takes the instruction after it, which
| its wrapper makes after core's routine (fx.c): `move.w 0x8000fbb6,%d3`
| (the delay's send into the reverb) and `move.l #0x1000,%d4`. %d3 (0x80
| at the reverb) passes through the C routine, which keeps %d2-%d7/%a2-%a6.
        .globl  core_fx_delay_site, core_fx_reverb_site
core_fx_delay_site:
        jsr     core_fx_delay
        move.w  0x8000fbb6,%d3
        rts
core_fx_reverb_site:
        jsr     core_fx_reverb
        move.l  #0x1000,%d4
        rts

| a mod that needs stock's exact FX render holds the skip off while it does
core_fx_skip_hold_add:
        addq.l  #1, core_fx_skip_hold
        rts
core_fx_skip_hold_release:
        subq.l  #1, core_fx_skip_hold
        rts

        .bss
        .align  2
        .globl  core_fx_skip_hold, core_fx_skip_want, core_reverb_clear_want
        .globl  core_delay_clear_want
fx_trim:
        .space  8*ENTRY
core_fx_skip_hold:
        .space  4
core_fx_skip_want:
        .space  1
core_reverb_clear_want:
        .space  1
core_delay_clear_want:
        .space  1
