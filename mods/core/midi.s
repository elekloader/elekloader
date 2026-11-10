| SPDX-License-Identifier: GPL-2.0-or-later
| core 4.3 (Digitakt mk1): ev_midi_cc, the MIDI CCs the Digitakt receives, as
| core-dn1's (3.2) on the Digitone. Last in sources, so the code before it
| keeps its place. The addresses are the same in OS 1.53 and 1.54:
|   ROUTER_EXIT  the CC router's exit (its registers back, d0 = 1, rts)
|
| The firmware's CC router (0x400158d2) gets each control change the MIDI
| dispatcher passes on, f(this, cc, value, track, flags): track 0-7 the
| audio tracks, 8-15 the MIDI tracks, 16 the FX track. At 0x400158ec it
| loads flags, `move.l 44(sp),d6`, and checks for the FX track, `cmp.l
| d2,d0` with d0 = 16. Core takes those six bytes: the same load, then each
| handler of ev_midi_cc,
|   int f(int track, int cc, int value, int flags)
| flags bit 0 (CORE_MIDI_CC_AUTO): it came on the auto channel. A handler
| that returns nonzero takes the CC: the router stops there and the stock
| does not apply it. With none taking it, the compare, and the router goes
| on as stock. Not at interrupt level: the MIDI input's task. The
| dispatcher keeps some CCs for itself (the NRPN and data entry CCs among
| them): those never come here.

        .section .run, "ax"
        .globl  core_midi_cc
core_midi_cc:
        move.l  48(%sp), %d6            | flags: the load the jsr replaced
        tst.l   ev_midi_cc
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d1/%a0-%a2, (%sp)
        lea     ev_midi_cc, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        moveq   #1, %d1
        and.l   %d6, %d1
        move.l  %d1, -(%sp)             | flags: the auto channel
        move.l  %d3, -(%sp)             | value
        move.l  %d4, -(%sp)             | cc
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
9:      moveq   #16, %d0
        cmp.l   %d2, %d0                | the compare the jsr replaced; rts keeps the flags
        rts
