| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: pages, the current track, windows and the
| crossfader (core-ot 0.42). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| set_page 0x400554e0(kind) makes a main page current (0x460d1684: SRC 0, LFO 1,
| AMP 2, FX1 3, FX2 4): ev_page(old, new) sees it, watch only (the SETUP openers
| call it before opening the page's SETUP, so a replaced page would not fit).
| select_track 0x40083bf8(trk) (TRACK keys, FUNC+TRACK, the sys task's MIDI-in
| previous/next track): int ev_track_select(old, new, how) nonzero = refused,
| before anything changes; how 1 is the AUDIO <-> MIDI toggle's swap, after
| it (not refusable).
| window_create 0x4005829c returns a window from its 5-slot pool, or 0; a window
| has no name of its own, its opener does: ev_window(0, window, level, ra) after
| a create (ra = the opener's return address), ev_window(1, window, holder, ra)
| before window_destroy 0x40055db4(holder) frees one in use.
| The crossfader 0x460d16c8 (0..127) is stored by the sys task from the panel
| (message 4) and from MIDI CC 48 (message 0x44): int ev_crossfader(value,
| source 0 panel / 1 MIDI) returns the value to store (0..127) or -1 for stock's.
|
|   0x400554ec  move.l d3,0x460d1684                ev_page
|   0x40083bf8  move.l a2,-(sp) ; move.l d2,-(sp) ; move.l 12(sp),d2   ev_track_select
|   0x400555d8  move.b d1,0x100b14cc                ev_track_select (toggle)
|   0x40058382  move.l a2,d0 ; movem.l (sp),..      ev_window (create)
|   0x40055db4  move.l a2,-(sp) ; movea.l 8(sp),a2  ev_window (destroy)
|   0x40061e2e  move.l d2,0x460d16c8                ev_crossfader (panel)
|   0x400626a8  move.l d4,0x460d16c8                ev_crossfader (MIDI)
        .section .run, "ax"

        .macro  SAVE4
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        .endm
        .macro  REST4
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        .endm

| d3: the new page.
        .globl  core_page
core_page:
        tst.l   ev_page
        beq.s   1f
        SAVE4
        move.l  %d3, -(%sp)
        move.l  0x460d1684, -(%sp)
        jsr     core_page_c             | ui.c
        addq.l  #8, %sp
        REST4
1:      move.l  %d3, 0x460d1684
        jmp     0x400554f2

| The entry: the track at 4(sp); d0-d1/a0-a1 free.
        .globl  core_track_select, core_track_toggle
core_track_select:
        tst.l   ev_track_select
        beq.s   1f
        clr.l   -(%sp)                  | how 0
        move.l  8(%sp), -(%sp)          | new (as asked)
        moveq   #0, %d0
        move.b  0x100b14cc, %d0
        move.l  %d0, -(%sp)             | old
        jsr     core_track_select_c
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1f
        rts                             | refused: nothing has changed
1:      move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        move.l  12(%sp), %d2
        jmp     0x40083c00
| The toggle: d0 the old track's byte, d1 the new one's (kept, stored below).
core_track_toggle:
        tst.l   ev_track_select
        beq.s   1f
        SAVE4
        pea     1                       | how 1
        moveq   #0, %d0
        move.b  %d1, %d0
        move.l  %d0, -(%sp)             | new
        move.l  8(%sp), %d0             | old: d0 as saved (below the two pushes)
        and.l   #0xff, %d0
        move.l  %d0, -(%sp)
        jsr     core_track_select_c
        lea     12(%sp), %sp
        REST4
1:      move.b  %d1, 0x100b14cc
        jmp     0x400555de

| Create: a2 the window (0: the pool is full), d5 the level, the opener's return
| address at 40(sp). Every register comes back from the movem after.
        .globl  core_window_create, core_window_destroy
core_window_create:
        tst.l   ev_window
        beq.s   1f
        tst.l   %a2
        beq.s   1f
        move.l  40(%sp), -(%sp)         | ra
        move.l  %d5, -(%sp)             | level
        move.l  %a2, -(%sp)
        clr.l   -(%sp)                  | op 0: created
        jsr     core_window_c
        lea     16(%sp), %sp
1:      move.l  %a2, %d0
        movem.l (%sp), %d2-%d6/%a2-%a5
        jmp     0x40058388
| Destroy, the entry: the holder at 4(sp), the opener's return address at (sp).
core_window_destroy:
        tst.l   ev_window
        beq.s   1f
        movea.l 4(%sp), %a0             | holder
        move.l  (%a0), %d0              | window
        beq.s   1f
        movea.l %d0, %a1
        moveq   #0x20, %d1
        and.l   0x20(%a1), %d1          | in use
        beq.s   1f
        move.l  (%sp), -(%sp)           | ra
        move.l  %a0, -(%sp)             | holder
        move.l  %d0, -(%sp)             | window
        pea     1                       | op 1: destroyed
        jsr     core_window_c
        lea     16(%sp), %sp
1:      move.l  %a2, -(%sp)
        movea.l 8(%sp), %a2
        jmp     0x40055dba

| The crossfader: d2 (panel) or d4 (MIDI) the value, kept in its register.
        .globl  core_xfade_panel, core_xfade_midi
core_xfade_panel:
        tst.l   ev_crossfader
        beq.s   1f
        SAVE4
        clr.l   -(%sp)                  | source 0: panel
        move.l  %d2, -(%sp)
        jsr     core_xfade_c
        addq.l  #8, %sp
        tst.l   %d0
        bmi.s   2f
        move.l  %d0, %d2
2:      REST4
1:      move.l  %d2, 0x460d16c8
        jmp     0x40061e34
core_xfade_midi:
        tst.l   ev_crossfader
        beq.s   1f
        SAVE4
        pea     1                       | source 1: MIDI
        move.l  %d4, -(%sp)
        jsr     core_xfade_c
        addq.l  #8, %sp
        tst.l   %d0
        bmi.s   2f
        move.l  %d0, %d4
2:      REST4
1:      move.l  %d4, 0x460d16c8
        jmp     0x400626ae
