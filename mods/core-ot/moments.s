| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: tempo settings, pattern picks and edits, the
| arrangement, live trigs (core-ot 0.42). ColdFire V4 (MCF5445x); -mcpu=54455.
|
| ev_tempo_set(tempo, source, ra): the tempo set outside a commit (ev_tempo's),
| bpm*24 after stock's clamp; source 0 set_tempo 0x4009c708 (TAP, PICKUP takes,
| an arrangement row's pattern tempo, project load/new), 1 the bpm.tenths setter
| 0x4009c7c4 (the TEMPO window's knob and arrows, an arrangement row's own tempo),
| 2 the MIDI clock follower (watch only); ra = the setter's caller. Nonzero = the
| tempo to set instead (clamped 720..7200). 0 and 1 also set the stored tempo.
| ev_pattern_select(bank, pattern, ra, queued): a pattern picked through the
| wrapper 0x400a1030 (PTN+TRIG, the BANK window, MIDI program change; also stop
| and loads); nonzero refuses the three user picks, the others only watch.
| ev_pattern_edited(trk, bank, pattern, load): the rebuild 0x4009da20 has ended:
| trk 0-7 audio, 8-15 MIDI, -1 all; load 1 for the nine load/stop callers, 0 for
| edits and a track change (select_track rebuilds the new track: seen in ot_emu).
| ev_arr_mode(on): ARRANGEMENT mode entered (refusable) or left (after).
| ev_arr_row(row, how): the arranger took a row (0x4004a100, after its pattern
| pick): how 0 set/cue, 1 start, 2 advance (in the sequencer interrupt), 3 the
| end or a chained arrangement; row >= the row count = the end.
| ev_track_play(trk, slot, flags, mode): a live trig (trig keys, SLOTS, SLICES,
| CHROMATIC, MIDI notes, TRACK+PLAY, recorder keys) at 0x40005030, slot -1 = the
| Part's; nonzero refuses it (stock's "done" answer, as for an empty slot).
|
|   0x4009c72e  move.w sr,d3 ; move.w #0x2700,sr   ev_tempo_set 0
|   0x4009c822  the same                           ev_tempo_set 1
|   0x40005c4a  move.l d0,0x80001814               ev_tempo_set 2
|   0x400a1030  clr.l -(sp) ; pea -1               ev_pattern_select
|   0x4009e4be  movem.l (sp),.. ; lea 144(sp),sp   ev_pattern_edited
|   0x4005200c  move.l a2,-(sp) ; tst.l 0x460d1aec ev_arr_mode(1)
|   0x40055fcc  jsr 0x400131c8                     ev_arr_mode(0)
|   0x4004a658  moveq #-1,d0 ; lea 20(sp),sp       ev_arr_row
|   0x40005030  lea -24(sp),sp ; movem.l ..        ev_track_play
        .section .run, "ax"

        .macro  SAVE4
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        .endm
        .macro  REST4
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        .endm

| d2: the clamped tempo; d3 is free (it takes sr next); ra at RA(sp).
        .macro  TSET name, src, ra, cont
        .globl  \name
\name:
        tst.l   ev_tempo_set
        beq.s   1f
        move.l  \ra(%sp), %d3
        SAVE4
        move.l  %d3, -(%sp)
        pea     \src
        move.l  %d2, -(%sp)
        jsr     core_tempo_set_c        | moments.c
        lea     12(%sp), %sp
        move.l  %d0, %d2
        REST4
1:      move.w  %sr, %d3
        move.w  #0x2700, %sr
        jmp     \cont
        .endm
        TSET    core_tempo_set0, 0, 8, 0x4009c734
        TSET    core_tempo_set1, 1, 12, 0x4009c828

        .globl  core_tempo_clock
core_tempo_clock:
        tst.l   ev_tempo_set
        beq.s   1f
        SAVE4
        clr.l   -(%sp)
        pea     2
        move.l  %d0, -(%sp)
        jsr     core_tempo_set_c        | watch: the answer is not used
        lea     12(%sp), %sp
        REST4
1:      move.l  %d0, 0x80001814
        jmp     0x40005c50

| The wrapper's entry: bank 4(sp), pattern 8(sp), its caller at (sp).
        .globl  core_pattern_select
core_pattern_select:
        tst.l   ev_pattern_select
        beq.s   1f
        move.l  (%sp), -(%sp)           | ra
        move.l  12(%sp), -(%sp)         | pattern
        move.l  12(%sp), -(%sp)         | bank
        jsr     core_pattern_select_c
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1f
        rts                             | refused
1:      clr.l   -(%sp)
        pea     -1
        jmp     0x400a1036

| The rebuild's exit: its frame at (sp), the caller at 144(sp), trk at 148(sp).
        .globl  core_pattern_edited
core_pattern_edited:
        tst.l   ev_pattern_edited
        beq.s   1f
        move.l  %d0, -(%sp)
        move.l  148(%sp), -(%sp)        | ra
        move.l  156(%sp), -(%sp)        | trk
        jsr     core_pattern_edited_c
        addq.l  #8, %sp
        move.l  (%sp)+, %d0
1:      movem.l (%sp), %d2-%d7/%a2-%a6
        lea     144(%sp), %sp
        rts

        .globl  core_arr_enter, core_arr_leave
core_arr_enter:
        tst.l   ev_arr_mode
        beq.s   1f
        tst.l   0x460d1aec
        bne.s   1f                      | already in: stock does nothing
        pea     1
        jsr     core_arr_mode_c
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1f
        rts                             | refused: nothing has changed
1:      move.l  %a2, -(%sp)
        tst.l   0x460d1aec              | last: its flags go on to stock's bne
        jmp     0x40052014
| led_off's argument is already pushed; d0-d1/a0-a1 are its to take.
core_arr_leave:
        tst.l   ev_arr_mode
        beq.s   1f
        clr.l   -(%sp)
        jsr     core_arr_mode_c
        addq.l  #4, %sp
1:      jsr     0x400131c8
        jmp     0x40055fd2

| After set_pattern in 0x4004a100: its 5 arguments at (sp), the caller at 52(sp),
| the row at 56(sp).
        .globl  core_arr_row
core_arr_row:
        tst.l   ev_arr_row
        beq.s   1f
        SAVE4
        move.l  16+52(%sp), -(%sp)      | ra
        move.l  20+56(%sp), -(%sp)      | row
        jsr     core_arr_row_c
        addq.l  #8, %sp
        REST4
1:      moveq   #-1, %d0
        lea     20(%sp), %sp
        jmp     0x4004a65e

| The entry: trk 4(sp), flags 8(sp), mode 12(sp), slot 16(sp); d0-d1/a0-a1 free.
        .globl  core_track_play
core_track_play:
        tst.l   ev_track_play
        beq.s   1f
        move.l  12(%sp), -(%sp)         | mode
        move.l  12(%sp), -(%sp)         | flags
        move.l  24(%sp), -(%sp)         | slot
        move.l  16(%sp), -(%sp)         | trk
        jsr     core_track_play_c
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   1f
        moveq   #1, %d0                 | refused: "done", as for an empty slot
        rts
1:      lea     -24(%sp), %sp
        movem.l %d2-%d6/%a2, (%sp)
        jmp     0x40005038
