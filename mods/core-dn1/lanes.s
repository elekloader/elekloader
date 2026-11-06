| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 5.1: the sites of lanes.c (Digitone mk1). ColdFire V4; assemble
| with -mcpu=54455. Beside core.s; the Digitakt's core does not have it.
|
|   ROWS_SAVE   rows(out, locks): a live pattern's locks (pattern + 0x1e80)
|               as the 80 stored lock rows; 1 done
|   ROWS_LOAD   locks(locks, in): the stored rows into a live pattern's locks
|   BUILD_SYN   the synth tracks' note builder (track, pattern, kit, step,
|               reuse, &time) -> the note event, or 0
|   BUILD_MIDI  the MIDI tracks'
|   BUILT_A, BUILT_B  where the builders' two callers go on: the prepare
|               (0x40094d84) and the sequencer's tick (0x40095b40)
|   HELD_STEPS  held(mgr): steps are held on a page's trig-hold manager
|   REARM       rearm(track): the sequencer prepares the track's first step again
        .section .run, "ax"

| ---- the stored rows --------------------------------------------------------------
| core_rows_save: at ROWS_SAVE's entry, by jmp (its first two instructions
| are run at 1: below). Stock's rows, then the pattern's lanes after them.
        .globl  core_rows_save
core_rows_save:
        move.l  8(%sp), -(%sp)          | locks
        move.l  8(%sp), -(%sp)          | out
        bsr.s   1f
        addq.l  #8, %sp
        tst.b   %d0
        beq.s   9f
        move.l  %d0, -(%sp)
        move.l  12(%sp), -(%sp)         | locks
        move.l  12(%sp), -(%sp)         | out
        jsr     core_lanes_save
        addq.l  #8, %sp
        move.l  (%sp)+, %d0
9:      rts
1:      lea     -32(%sp), %sp
        movem.l %d2-%d4/%a2-%a6, (%sp)
        jmp     ROWS_SAVE + 8

| core_rows_load: at ROWS_LOAD's entry, by jmp. Stock's load, then the
| lanes from the rows.
        .globl  core_rows_load
core_rows_load:
        move.l  8(%sp), -(%sp)          | in
        move.l  8(%sp), -(%sp)          | locks
        bsr.s   1f
        addq.l  #8, %sp
        move.l  %d0, -(%sp)
        move.l  %d0, -(%sp)             | ok
        move.l  16(%sp), -(%sp)         | in
        move.l  16(%sp), -(%sp)         | locks
        jsr     core_lanes_load
        lea     12(%sp), %sp
        move.l  (%sp)+, %d0
        rts
1:      lea     -32(%sp), %sp
        movem.l %d2-%d5/%a2-%a5, (%sp)
        jmp     ROWS_LOAD + 8

| ---- each note built ---------------------------------------------------------------
| At the builders' four calls, by jmp: the call (and the push before a MIDI
| call, which the site took), then core_note_built with the event, and on
| where the caller went. The arguments stay on the stack, as the caller
| pops them: track at 0(sp), pattern 4, step 12.
        .globl  core_build_syn_a, core_build_syn_b, core_build_midi_a, core_build_midi_b
core_build_syn_a:
        jsr     BUILD_SYN
        bsr.s   built
        jmp     BUILT_A
core_build_syn_b:
        jsr     BUILD_SYN
        bsr.s   built
        jmp     BUILT_B
core_build_midi_a:
        move.l  %d2, -(%sp)
        jsr     BUILD_MIDI
        bsr.s   built
        jmp     BUILT_A
core_build_midi_b:
        move.l  %d2, -(%sp)
        jsr     BUILD_MIDI
        bsr.s   built
        jmp     BUILT_B
| d0 = the event, 4(sp) the builder's arguments. Keeps every register.
built:
        tst.l   %d0
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  32(%sp), -(%sp)         | step
        move.l  28(%sp), -(%sp)         | pattern
        move.l  28(%sp), -(%sp)         | track
        move.l  %d0, -(%sp)             | the event
        jsr     core_note_built
        lea     16(%sp), %sp
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
9:      rts

| ---- for lanes.c --------------------------------------------------------------------
| void core_rearm(int track): REARM(track), the sequencer's preparation of
| the track's first step (what it plays when PLAY starts it).
        .globl  core_rearm
core_rearm:
        jmp     REARM

| int core_held(void *mgr): HELD_STEPS's answer (a byte).
        .globl  core_held
core_held:
        move.l  4(%sp), -(%sp)
        jsr     HELD_STEPS
        addq.l  #4, %sp
        rts
