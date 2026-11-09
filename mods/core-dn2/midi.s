| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn2 0.4: ev_midi_cc, the MIDI CCs the Digitone II receives, as core's
| (Digitakt mk1, 4.3) and core-dn1's.
|   ROUTER_EXIT  the CC router's exit (its registers back, d0 = 1, rts)
|
| The firmware's CC router (0x4003f0ec) gets each control change the MIDI
| dispatcher passes on, f(this, cc, value, track, flags): track 0-15 the
| tracks, 16 the FX track. At 0x4003f106 it loads flags, `move.l 40(sp),d5`,
| and checks for CC 95, `cmp.l d3,d0` with d0 = 95. Core takes those six
| bytes: the same load, then each handler of ev_midi_cc,
|   int f(int track, int cc, int value, int flags)
| flags bit 0 (CORE_MIDI_CC_AUTO): it came on the auto channel. A handler
| that returns nonzero takes the CC: the router stops there and the stock
| does not apply it. With none taking it, the compare, and the router goes
| on as stock. Not at interrupt level: the MIDI input's task.

        .section .run, "ax"
        .globl  core_midi_cc
core_midi_cc:
        move.l  44(%sp), %d5            | flags: the load the jsr replaced
        tst.l   ev_midi_cc
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d1/%a0-%a2, (%sp)
        lea     ev_midi_cc, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        moveq   #1, %d1
        and.l   %d5, %d1
        move.l  %d1, -(%sp)             | flags: the auto channel
        move.l  %d4, -(%sp)             | value
        move.l  %d3, -(%sp)             | cc
        move.l  %d2, -(%sp)             | track
        jsr     (%a0)
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d1/%a0-%a2      | taken
        lea     16(%sp), %sp
        move.l  #ROUTER_EXIT, (%sp)     | the router's exit, in place of the return
        rts
2:      movem.l (%sp), %d1/%a0-%a2      | nobody took it: on into the router
        lea     16(%sp), %sp
9:      moveq   #95, %d0
        cmp.l   %d3, %d0                | the compare the jsr replaced; rts keeps the flags
        rts
