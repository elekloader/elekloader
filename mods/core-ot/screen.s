| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: what the screen shows of a track and a knob
| (core-ot 0.8). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| Octabam's REC THRESHOLD and REC TRIG MUTE both detour the track icons'
| renderer (0x4004bd48) at one place and cannot be in a build together; PLOCKS
| P2, SCENES P2 and VARISPEED each detour the SETUP windows' knob loop
| (MIT; the census is theirs, the code is our own). Here each place is an event:
|
|   0x4004bee2  an audio track's box: the state it shows      ev_track_icon
|   0x4004c00c  the box's glyph, for a state stock has none    ev_track_icon_draw
|   0x4003786a  FX2 SETUP's knob, before the widget call       ev_setup_dial
|   0x40037c06  FX1 SETUP's                                    ev_setup_dial
|   0x4003690c  AMP SETUP's                                    ev_setup_dial
|
| None of them is a byte any mod of irpina/octabam2elemod v1.0 patches (SCENES
| P2's is the Part byte's load before each knob site, and stays its own). They
| run in the task that draws the screen.

        .section .run, "ax"

| ---- the track icons ------------------------------------------------------------
| 0x4004bee2 (was: lea 0x400c0cb8,a0) in the audio tracks' loop: d3 = the track,
| a4 = the state its box shows (bit 0 it plays, bits 1-2 its recorder), which
| the next instructions compare with the cached one to decide on a redraw.
| Handlers: unsigned f(unsigned trk, unsigned state) -> the state (chained).
        .globl  core_icon, core_icon_draw
core_icon:
        tst.l   ev_track_icon
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a1-%a2, (%sp)
        lea     ev_track_icon, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  %a4, -(%sp)             | the state so far
        move.l  %d3, -(%sp)             | the track
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        movea.l %d0, %a4
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a1-%a2
        lea     16(%sp), %sp
9:      lea     ICON_CACHE, %a0         | the instruction this replaced
        jmp     ICON_NEXT

| 0x4004c00c (was: moveq #3,d0 ; cmp.l a4,d0 ; bne.s 0x4004c02a), the last case of
| the glyph switch: state 3 has stock's "+>" glyph, any other state above 3 none.
| The box is cleared already. a3 = its x, a2 + 49 the glyph's y. Handlers: int
| f(unsigned trk, unsigned state, int x, int y), nonzero when one drew (ot_blit):
| the rest are not asked. d0-d1/a0-a1 are free on both of stock's ways on.
core_icon_draw:
        moveq   #3, %d0
        cmp.l   %a4, %d0
        bne.s   1f
        jmp     ICON_GLYPH3
1:      move.l  %a2, -(%sp)
        move.l  %a5, -(%sp)
        lea     ev_track_icon_draw, %a5
2:      move.l  (%a5)+, %d1
        beq.s   3f
        pea     49(%a2)                 | y
        move.l  %a3, -(%sp)             | x
        move.l  %a4, -(%sp)             | the state
        move.l  %d3, -(%sp)             | the track
        movea.l %d1, %a0
        jsr     (%a0)
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   2b
3:      movea.l (%sp)+, %a5
        movea.l (%sp)+, %a2
        jmp     ICON_DONE

| ---- the SETUP windows' knobs -----------------------------------------------------
| The SETUP windows of FX2, FX1 and AMP draw their six page-2 knobs in a loop: for
| knob d4 (0-5) whose enable bit is set, a3 -> its formatter slot (the widget is at
| 48(a3), 0 for the stock one), d6 = its value byte, a1 = the widget's flags (0 or
| 8), then the widget(x, y, knob, value, flags, formatter, canvas) call. At each
| site (was: movea.l 48(a3),a0 ; tst.l a0 ; bne.s +6 ; lea 0x400479b4,a0 ; move.l
| CANVAS,-(sp) ; move.l (a3),-(sp) ; move.l a1,-(sp)) the stub resolves the
| widget as stock, lets screen.c's otd_dial change the value, the flags and the
| widget, and pushes the call's first three arguments. d0-d1 are free there.
        .macro  DIAL page, canvas, next
        movea.l 48(%a3), %a0
        tst.l   %a0
        bne.s   1f
        lea     DIAL_WIDGET, %a0
1:      tst.l   ev_setup_dial
        beq.s   2f
        lea     -20(%sp), %sp           | struct ot_dial
        move.b  #\page, (%sp)
        move.b  %d4, 1(%sp)             | the knob
        mvs.b   %d6, %d0
        move.l  %d0, 4(%sp)             | the value, as the widget gets it
        move.l  %a1, 8(%sp)             | the flags
        move.l  (%a3), 12(%sp)          | the formatter
        move.l  %a0, 16(%sp)            | the widget
        pea     (%sp)
        jsr     otd_dial
        addq.l  #4, %sp
        move.l  4(%sp), %d6
        movea.l 8(%sp), %a1
        movea.l 16(%sp), %a0
        lea     20(%sp), %sp
2:      move.l  \canvas, -(%sp)
        move.l  (%a3), -(%sp)
        move.l  %a1, -(%sp)
        jmp     \next
        .endm

        .globl  core_dial_fx2, core_dial_fx1, core_dial_amp
core_dial_fx2:
        DIAL    3, %d3, DIAL_FX2_NEXT   | OT_P2_FX2
core_dial_fx1:
        DIAL    2, %d3, DIAL_FX1_NEXT   | OT_P2_FX1
core_dial_amp:
        DIAL    1, %d5, DIAL_AMP_NEXT   | OT_P2_AMP

| ---- two more SETUP windows (core-ot 0.22) -------------------------------------------------------------
| The SRC / NOTE SETUP window (0x40036548, "CHAN": MIDI NOTE SETUP, FUNC + PLAYBACK in MIDI mode, and the
| audio tracks' setup octabam's VARISPEED reaches; MIDI FOLLOW too, MIT) and MIDI ARP
| SETUP (0x40079d48) draw their knobs with the same widget call. In each the knob's formatter slot is at
| \base (widget at 48(\base)), the knob in \knob, its value byte in \val (unsigned), the canvas in
| \canvas. The SRC window's flags are a1; ARP SETUP's are 0. Same contract as DIAL above; the value
| goes back into \val's low byte, where the widget call takes it from.
        .macro  DIAL2 page, base, knob, val, flags, canvas, next
        movea.l 48(%\base), %a0
        tst.l   %a0
        bne.s   1f
        lea     DIAL_WIDGET, %a0
1:      .ifc    \flags, zero
        suba.l  %a1, %a1
        .endif
        tst.l   ev_setup_dial
        beq.s   2f
        lea     -20(%sp), %sp
        move.b  #\page, (%sp)
        move.b  %\knob, 1(%sp)
        moveq   #0, %d0
        move.b  %\val, %d0
        move.l  %d0, 4(%sp)
        move.l  %a1, 8(%sp)
        move.l  (%\base), 12(%sp)
        move.l  %a0, 16(%sp)
        pea     (%sp)
        jsr     otd_dial
        addq.l  #4, %sp
        move.l  4(%sp), %d0
        move.b  %d0, %\val
        movea.l 8(%sp), %a1
        movea.l 16(%sp), %a0
        lea     20(%sp), %sp
2:      move.l  %\canvas, -(%sp)
        move.l  (%\base), -(%sp)
        move.l  %a1, -(%sp)
        jmp     \next
        .endm

        .globl  core_dial_src, core_dial_arp
core_dial_src:
        DIAL2   0, a4, d4, d3, a1, d5, 0x400366ac       | OT_P2_PLAYBACK
core_dial_arp:
        DIAL2   4, a3, d3, d4, zero, d6, 0x4007a200     | OT_SETUP_ARP

| ---- the track tabs (core-ot 0.17) ---------------------------------------------------------------------
| 0x40040146 (was: moveq #4,d0 ; cmp.l a4,d0 ; bne.s 0x400401c6), in the track tabs' renderer 0x4003fed8
| (the UI task's display loop): stock has just drawn track d4's machine letter at x d3, y a2 + 1 (a4 the
| track's machine). octabam's REC TRIG MUTE draws its dots here (MIT). Handlers: void f(unsigned trk,
| int x, int y), after stock's letter: draw beside it (ot_blit). The tab's other marks come after.
        .globl  core_tab_draw
core_tab_draw:
        tst.l   ev_track_tab_draw
        beq.s   9f
        lea     -24(%sp), %sp
        movem.l %d0-%d2/%a0-%a2, (%sp)
        move.l  %a2, %d2
        addq.l  #1, %d2                 | the letter's y (d2: the handlers keep it)
        lea     ev_track_tab_draw, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  %d2, -(%sp)
        move.l  %d3, -(%sp)
        move.l  %d4, -(%sp)
        movea.l %d1, %a0
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d2/%a0-%a2
        lea     24(%sp), %sp
9:      moveq   #4, %d0
        cmp.l   %a4, %d0
        bne.s   8f
        jmp     0x4004014c
8:      jmp     0x400401c6

| ---- the knob widget (core-ot 0.23) ---------------------------------------------------------------------
| 0x400467a4 (was: lea -48(sp),sp ; movem.l d2-d7/a2-a3,(sp)), the knob widget: the page parameter
| descriptors name it (their widget field, 0x400d3438 on), RECORDING SETUP calls it (0x4002fa1a, 0x4002faba,
| 0x4002fb5e) and two drawers reach it by a tail branch (0x40047c76, 0x40047d04). Its arguments, at 4(sp) on entry: x, y, mode, value, flags, formatter, canvas (struct
| ot_knob, core.h). octabam's RLEN PLEN and REC THRESHOLD (MIT) swap RECORDING SETUP's formatters at the
| calls. Handlers: void f(struct ot_knob *k): read it, or change value, flags or the formatter; the
| widget reads them after. In the task that draws the screen.
        .globl  core_knob_draw
core_knob_draw:
        tst.l   ev_knob_draw
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_knob_draw, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        pea     24(%sp)                 | the arguments (20 saved + the return address)
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      lea     -48(%sp), %sp
        movem.l %d2-%d7/%a2-%a3, (%sp)
        jmp     0x400467ac
