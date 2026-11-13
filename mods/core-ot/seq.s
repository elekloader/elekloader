| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the sequencer's moments (core-ot 0.10). ColdFire V4 (MCF5445x);
| assemble with -mcpu=54455.
|
| Four routines' entries, each a place octabam modules detour (MIT; the census is theirs, the code is
| our own): VARISPEED and VERB PARTS the Part apply, KITS the chain append, MIDI LOOPBACK the panic.
|
|   0x40009e00  a pattern's Part is applied (bank, part)          ev_part_apply
|   0x4009c634  a pattern is added to the chain (pattern)          ev_chain_add
|   0x400a10c8  the sequencer's stop and reset                     ev_seq_reset
|   0x4009f288  the all-notes-off on the MIDI tracks' channels     ev_midi_panic
|
| Each stub calls the handlers (observers, void), then replays the entry it displaced and goes on.

        .section .run, "ax"

| Calls every handler of the table \tab with \nargs longs taken from the caller's frame: the stub was
| entered by a jmp from the routine's first instruction, so 4(sp) is its first argument.
        .macro  OBSERVE tab, nargs
        tst.l   \tab
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     \tab, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        .if \nargs >= 2
        move.l  20+8(%sp), -(%sp)
        move.l  24+4(%sp), -(%sp)       | (the push moved sp by 4)
        .elseif \nargs == 1
        move.l  20+4(%sp), -(%sp)
        .endif
        movea.l %d1, %a0
        jsr     (%a0)
        .if \nargs
        lea     4*\nargs(%sp), %sp
        .endif
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:
        .endm

| ---- 0x40009e00 (was: lea -76(sp),sp ; movem.l d2-d7/a2-a6,(sp)): apply(bank, part), from the pattern
| schedule (0x400a05f0, a pattern change) and from the frame builder (0x4000b1d8, the frame interrupt).
| Handlers: void f(unsigned bank, unsigned part), before the Part reaches the engine.
        .globl  core_part_apply
core_part_apply:
        OBSERVE ev_part_apply, 2
        lea     -76(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     0x40009e08

| ---- 0x4009c634 (was: move.l d2,-(sp) ; move.l 8(sp),d2): append(pattern), from the [PTN] key's two
| handlers (0x40056b9a, 0x4007b3ae). The routine may still leave it out: the chain is full (16) or has
| it. Handlers: void f(unsigned pattern).
        .globl  core_chain_add
core_chain_add:
        OBSERVE ev_chain_add, 1
        move.l  %d2, -(%sp)
        move.l  8(%sp), %d2
        jmp     0x4009c63a

| ---- 0x400a10c8 (was: lea -36(sp),sp ; movem.l d2-d5/a2-a6,(sp)): the stop and reset (STOP while
| stopped, the menus that stop the sequencer first, MIDI). Handlers: void f(void), before it.
        .globl  core_seq_reset
core_seq_reset:
        OBSERVE ev_seq_reset, 0
        lea     -36(%sp), %sp
        movem.l %d2-%d5/%a2-%a6, (%sp)
        jmp     0x400a10d0

| ---- 0x4009f288 (was: lea -20(sp),sp ; movem.l d2-d4/a2-a3,(sp)): CC 123 on each channel the MIDI tracks
| use, called by the stop and reset when 0x46c7a9fe is set. Handlers: void f(void), before it.
        .globl  core_midi_panic
core_midi_panic:
        OBSERVE ev_midi_panic, 0
        lea     -20(%sp), %sp
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     0x4009f290

| ---- the sequencer's tick (core-ot 0.12) -------------------------------------------------------------------
| The tick is a timer interrupt (0x400a1e0c, every register saved, the mask raised): handlers here run in
| it and must be short. octabam's TRACK PATTERNS (MIT, proven on a unit) reads these places.
|
| 0x400a3d88 / 0x400a3ea8 (was: mvz.b d1,d1 ; mvs.b d0,d0 ; cmp.l d1,d0), each track's advance: d1 the step
| counter just incremented, d0 the length it wraps at (stock's: the track's under PER TRACK, the
| pattern's else), d3 the track (0-7 audio, 8-15 MIDI). Handlers: int f(unsigned trk, int len) -> the
| length (chained). The compare and the bgt after it use what they return.
        .macro  TLEN addr
        .globl  core_tlen_\addr
core_tlen_\addr:
        mvz.b   %d1, %d1
        mvs.b   %d0, %d0
        tst.l   ev_track_length
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d1/%a0-%a2, (%sp)
        lea     ev_track_length, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  %d0, -(%sp)             | the length so far
        move.l  %d3, -(%sp)             | the track
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d1/%a0-%a2
        lea     16(%sp), %sp
9:      cmp.l   %d1, %d0
        jmp     \addr + 6
        .endm

        TLEN    0x400a3d88
        TLEN    0x400a3ea8

| 0x400a44f4 (was: lea 0x80006634,a1), the master pattern's wrap, after a queued pattern (0x800065c0, if
| any) has become the playing one: 0x800065bd the bank, 0x800065be the pattern; 0x800065c2 / 0x800065c1
| the ones that just ended. Not reached when the wrap stops the sequencer (no next pattern). Handlers:
| void f(unsigned bank, unsigned pattern, unsigned old_bank, unsigned old_pattern).
        .globl  core_pattern_wrap
core_pattern_wrap:
        tst.l   ev_pattern_wrap
        beq.s   9f
        lea     -24(%sp), %sp
        movem.l %d0-%d1/%a0-%a2/%d2, (%sp)
        lea     ev_pattern_wrap, %a2
1:      move.l  (%a2)+, %d2
        beq.s   2f
        lea     -16(%sp), %sp
        moveq   #0, %d0
        move.b  0x800065bd, %d0
        move.l  %d0, (%sp)
        move.b  0x800065be, %d0
        move.l  %d0, 4(%sp)
        move.b  0x800065c2, %d0
        move.l  %d0, 8(%sp)
        move.b  0x800065c1, %d0
        move.l  %d0, 12(%sp)
        movea.l %d2, %a0
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2/%d2
        lea     24(%sp), %sp
9:      lea     0x80006634, %a1
        jmp     0x400a44fa

| ---- the recorder's length (core-ot 0.13) -----------------------------------------------------------------
| 0x40006e18 (was: move.l 128(sp),d3 ; beq.s 0x40006e24), in the recorder routine 0x400068e4 (the frame
| interrupt's, through a3 at 0x4000d322): every way the length of a take is worked out (RLEN 1-64, MAX,
| the free and the sequencer-armed takes) joins here with the length in samples in d4; 136(sp) is the
| track (the routine's first argument). octabam's RLEN PLEN and RECORDER LOOP FIX (MIT) set it on these
| paths. Handlers: int f(unsigned trk, int len) -> the length (chained).
        .globl  core_rec_length
core_rec_length:
        tst.l   ev_rec_length
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %a2, -(%sp)
        lea     ev_rec_length, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  %d4, -(%sp)             | the length so far
        move.l  20+136+4(%sp), -(%sp)   | the track (20 saved, 4 pushed)
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        move.l  %d0, %d4
        bra.s   1b
2:      move.l  (%sp)+, %a2
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
9:      move.l  128(%sp), %d3
        beq.s   8f
        jmp     0x40006e1e
8:      jmp     0x40006e24

| ---- the pattern each track plays (core-ot 0.14) -----------------------------------------------------------
| Where stock passes the playing pattern to a track's step routine: the tick's audio and MIDI passes (in the
| sequencer's interrupt) and the working-set rebuild after an edit, audio and MIDI (in the sys task). The
| registers are those octabam's TRACK PATTERNS (MIT, proven on a unit) reads. Handlers:
|   int f(unsigned trk, int pattern, unsigned step, unsigned peek) -> the pattern (chained)
| trk 0-7 audio, 8-15 MIDI; step the track's counter; peek 1 at the rebuilds, which must change nothing.

| core_tpat(trk, pattern, step, peek) -> d0 the pattern; keeps every other register.
core_tpat:
        move.l  8(%sp), %d0
        tst.l   ev_track_pattern
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d1/%a0-%a2, (%sp)
        lea     ev_track_pattern, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  32(%sp), -(%sp)         | peek (16 saved + ret + trk, pattern, step)
        move.l  32(%sp), -(%sp)         | step
        move.l  %d0, -(%sp)             | the pattern so far
        move.l  32(%sp), -(%sp)         | trk
        movea.l %d1, %a0
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d1/%a0-%a2
        lea     16(%sp), %sp
9:      rts

| 0x400a2d76 (was: move.l d3,-(sp) ; move.l 86(sp),-(sp)): the tick's audio pass, pushing
| 0x4009d1e8(track, bank, pattern, step, n); the step is pushed, the pattern in d3, the track in d5,
| the bank at 82(sp). d0/d1/a0/a1 are not read again before the call.
        .globl  core_tpat_atick
core_tpat_atick:
        clr.l   -(%sp)                  | peek 0
        lea     0x800064d0, %a0
        moveq   #0, %d0
        move.b  %a0@(0,%d5:l), %d0
        move.l  %d0, -(%sp)             | step: the track's counter
        move.l  %d3, -(%sp)
        move.l  %d5, -(%sp)
        jsr     core_tpat
        lea     16(%sp), %sp
        move.l  %d0, -(%sp)             | the pattern argument
        move.l  86(%sp), -(%sp)         | the bank, as stock
        jmp     0x400a2d7c

| 0x400a39c0 (was: move.l d4,-(sp) ; move.l d3,-(sp) ; move.l d7,-(sp)): the tick's MIDI pass, pushing
| 0x4009cf4c(track, bank, pattern, step, n): pattern d4, bank d3, MIDI track d7 (0-7), its counter
| at 0x800064d8 + d7.
        .globl  core_tpat_mtick
core_tpat_mtick:
        clr.l   -(%sp)
        lea     0x800064d8, %a0
        moveq   #0, %d0
        move.b  %a0@(0,%d7:l), %d0
        move.l  %d0, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d7, %d0
        addq.l  #8, %d0
        move.l  %d0, -(%sp)             | trk 8-15
        jsr     core_tpat
        lea     16(%sp), %sp
        move.l  %d0, -(%sp)
        move.l  %d3, -(%sp)
        move.l  %d7, -(%sp)
        jmp     0x400a39c6

| 0x4009dc8c (was: mvs.b 0x800065be,d4 ; move.l d4,-(sp)): the rebuild 0x4009da20 for an audio track:
| step d2, track d6; d4 keeps stock's pattern, only the pushed argument changes.
        .globl  core_tpat_arebuild
core_tpat_arebuild:
        mvs.b   0x800065be, %d4
        pea     1                       | peek
        move.l  %d2, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d6, -(%sp)
        jsr     core_tpat
        lea     16(%sp), %sp
        move.l  %d0, -(%sp)
        jmp     0x4009dc94

| 0x4009e3e2 (was: mvs.b 0x800065be,d2 ; move.l d2,-(sp)): its MIDI branch, d6 8-15, the step in d2
| until the replayed load.
        .globl  core_tpat_mrebuild
core_tpat_mrebuild:
        pea     1
        move.l  %d2, -(%sp)             | the step, before the load takes d2
        mvs.b   0x800065be, %d2
        move.l  %d2, -(%sp)
        move.l  %d6, -(%sp)
        jsr     core_tpat
        lea     16(%sp), %sp
        mvs.b   0x800065be, %d2         | d2 as stock leaves it
        move.l  %d0, -(%sp)
        jmp     0x4009e3ea

| ---- the UI task's tick (core-ot 0.15) --------------------------------------------------------------------
| 0x40056c28 (was: move.l 0x460d1e6c,d0), the countdown the UI task (0x40056c40, the task that draws the
| screen) calls for each tick message (type 1 from its queue 0x460d1664), before its timers (0x40056ab8).
| octabam's FREE LOOP and RELOAD FROM PROJECT take this routine, TUNER and CC FEEDBACK the loop's head
| (MIT). Handlers: void f(void), in the UI task.
        .globl  core_ui_tick
core_ui_tick:
        OBSERVE ev_ui_tick, 0
        move.l  0x460d1e6c, %d0
        jmp     0x40056c2e
