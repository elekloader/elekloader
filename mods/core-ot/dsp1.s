| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: one more block a frame to DSP core 1 (core-ot 0.39).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The frame transfer chain runs in eDMA channel 0's interrupt (0x40004840, level 6:
| d0-d1/a0-a1 saved, then a jump by the state at 0x46104d3e through 0x400ab61a).
| State 2 pushes 672 words to core 1 ($6080, host command 0x88); state 3 starts as
| that completes and pushes to core 0. At state 3's entry ev_dsp_feed1's handlers
| may start one more transfer to core 1 (ot_dsp_feed1, dsp1.c); the state stays 3,
| so its completion comes back here, and stock's state 3 then runs. usb-ot's
| ev_dsp_feed does the same for core 0 at state 7. Modwerk's DSP loader sends core 1
| its packets here; the code is our own.
|
|   0x400049ca  clr.b d0 ; move.b d0,0xfc0a400c (state 3: the host port to core 0)
        .section .run, "ax"

        .globl  core_dsp_state3
core_dsp_state3:
        tst.b   ot_dsp1_feeding
        bne.s   3f                      | our transfer's completion
        tst.l   ev_dsp_feed1
        beq.s   4f
        move.l  %a2, -(%sp)
        moveq   #1, %d0
        move.b  %d0, ot_dsp1_in_feed
        lea     ev_dsp_feed1, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   1b
2:      clr.b   ot_dsp1_in_feed
        movea.l (%sp)+, %a2
        tst.b   ot_dsp1_feeding
        beq.s   4f
        jmp     0x40004bc8              | a transfer is on its way: state 3 again at its end
3:      clr.b   ot_dsp1_feeding
        addq.l  #1, ot_dsp1_fed
4:      clr.b   %d0                     | stock's state 3
        move.b  %d0, 0xfc0a400c
        jmp     0x400049d2
