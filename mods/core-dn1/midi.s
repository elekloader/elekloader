| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 3.2: ev_midi_cc, the MIDI CCs the Digitone receives, and from 4.0
| ev_midi_pc, its program changes (Digitone mk1).
| ColdFire V4; assemble with -mcpu=54455. Beside core.s.
|
|   ROUTER_EXIT  the CC router's exit (its registers back, rts)
|   PC_RECV      MIDI CONFIG's receive program changes (a long, nonzero: on)
|   PC_CHAN      its PROG CH IN channel (0-15; negative: the auto channel's)
|   AUTO_CHAN    the auto channel (0-15)
|   PC_EXIT      the program change handler's exit (its registers back, rts)
|
| ---- ev_midi_cc ---------------------------------------------------------------
| The firmware's CC router (0x400ed94e; Tone+FX patches its first six bytes)
| gets each control change it receives: d2 the track (MIDI channel 1-9 is
| 0-8; the auto channel is the active track, with d5 = 1), d3 the CC number,
| d4 the value. At 0x400ed96e (0x400edbe2 in 1.44) it checks the
| track, `cmp.l d2,d0; blt.w exit` with d0 = 8. Core takes those six bytes:
| the same check, then each handler of ev_midi_cc,
|   int f(int track, int cc, int value, int flags)
| flags bit 0: it came on the auto channel. A handler that returns nonzero
| takes the CC: the router stops there and the stock does not apply it. With
| none taking it, the router goes on as stock. Not at interrupt level: the
| MIDI input's task. A mod that patches the router's entry (Tone+FX) sees a
| CC first and, if it lets it through, core sees it next.

        .section .run, "ax"
        .globl  core_midi_cc
core_midi_cc:
        cmp.l   %d2, %d0                | the router's check: a track past 8 -> out
        blt.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
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
        movem.l (%sp), %d0-%d1/%a0-%a2  | taken
        lea     20(%sp), %sp
9:      move.l  #ROUTER_EXIT, (%sp)     | the router's exit, in place of the return
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2  | nobody took it: on into the router
        lea     20(%sp), %sp
        rts

| ---- ev_midi_pc (4.0) -----------------------------------------------------------
| The firmware's program change handler (0x400ea19c) gets a pointer to the
| message (status, program) in a0. At 0x400ea1a8 (0x400ea41c in 1.44) it tests
| PC_RECV, `tst.l PC_RECV`: off, it ignores the change; on, it takes the one
| on PROG CH IN's channel (or the auto channel's) and has the UI task change
| the pattern. Core takes those six bytes: each handler of ev_midi_pc,
|   int f(int channel, int program, int flags)
| sees every program change first, on any channel: flags bit 0 (as
| CORE_MIDI_CC_AUTO): it came on the auto channel; bit 1 (CORE_MIDI_PC_STOCK):
| the stock would apply it. A handler that returns nonzero takes it: the
| handler's exit, and the pattern stays. With none taking it, the test it
| replaced, and the stock goes on. The MIDI input's task.

        .globl  core_midi_pc
core_midi_pc:
        lea     -20(%sp), %sp
        movem.l %d2-%d4/%a0/%a2, (%sp)
        moveq   #0, %d3
        move.b  (%a0), %d3
        moveq   #15, %d0
        and.l   %d0, %d3                | the channel, 0-15
        moveq   #0, %d4
        move.b  1(%a0), %d4             | the program, 0-127
        moveq   #0, %d2
        cmp.l   AUTO_CHAN, %d3
        bne.s   1f
        moveq   #1, %d2                 | on the auto channel
1:      tst.l   PC_RECV
        beq.s   3f                      | receiving them is off: the stock ignores it
        move.l  PC_CHAN, %d0
        bge.s   2f
        tst.l   %d2                     | PROG CH IN is the auto channel: is this it?
        beq.s   3f
        bra.s   4f
2:      cmp.l   %d0, %d3
        bne.s   3f
4:      addq.l  #2, %d2                 | the stock would apply it
3:      lea     ev_midi_pc, %a2
5:      move.l  (%a2)+, %d0
        beq.s   6f
        movea.l %d0, %a0
        move.l  %d2, -(%sp)             | flags
        move.l  %d4, -(%sp)             | program
        move.l  %d3, -(%sp)             | channel
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   5b
        movem.l (%sp), %d2-%d4/%a0/%a2  | taken
        lea     20(%sp), %sp
        move.l  #PC_EXIT, (%sp)         | the handler's exit, in place of the return
        rts
6:      movem.l (%sp), %d2-%d4/%a0/%a2  | nobody took it: the test the site replaced (its
        lea     20(%sp), %sp            | flags are the stock's next branch's), then on
        tst.l   PC_RECV
        rts
