| SPDX-License-Identifier: GPL-2.0-or-later
| seq-events (Digitakt mk1): the step fires' sites (1.53 and 1.54: the audio
| tracks' fire at 0x4006f4be, the MIDI tracks' at 0x4006f846, the same
| addresses and bytes in both), offered to other mods as the events in
| elekloader/sdk/include/seq-events.h (../seq-events/seq.c, with dev.h).
| Each jsr site covers whole stock instructions; its routine does the
| displaced work and changes only what those instructions change, saving the
| C calls' scratch registers (d0-d1/a0-a1) where the stock code still needs
| them. The C calls no firmware: interrupt level.
| The audio fire (tracks 0-7): d5 = track, d4 = step, a3 = pattern_t,
| d3 = 911 * track, a4 = the note, a5 = the lock list. The MIDI fire (8-15):
| d6 = track, d4 = step, a2 = pattern_t, d3 = 911 * track, d7 = the notes'
| count, the notes as longs at the fire's 52(sp). Inside a routine the
| fire's frame is 4 bytes further away (the return address).
| A word handed to C carries the track's default SYN trig as bit 20
| (dev.h WORD_DEF_SYN); what goes back to the fire is its low 16 bits.

        .section .run, "ax"

| ---- the step read (ev_fire_step; the step clock) -----------------------

| 0x4006f4dc, 8 bytes over `move.l 76(sp),d4; movea.l 80(sp),a2`: the fire
| loads its step and event once; everything after reads the step in d4.
| d0 (an offset) and d1 (1) are live.
        .globl  sq_site_sread
sq_site_sread:
        move.l  80(%sp), %d4
        movea.l 84(%sp), %a2
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %a3, -(%sp)             | pattern_t
        move.l  %d4, -(%sp)             | the real step
        move.l  %d5, -(%sp)             | track
        jsr     sq_on_step              | -> the step to read
        lea     12(%sp), %sp
        move.l  %d0, %d4
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        rts

| 0x4006f864 (MIDI), 8 bytes over `move.l 84(sp),d4; movea.l 88(sp),a3`
        .globl  sq_site_mread
sq_site_mread:
        move.l  88(%sp), %d4
        movea.l 92(%sp), %a3
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %a2, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d6, -(%sp)
        jsr     sq_on_step
        lea     12(%sp), %sp
        move.l  %d0, %d4
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        rts

| ---- the step word (ev_gen_trig, ev_fire_trig) --------------------------

| a0 = the track's data: d0 |= the track's default SYN trig as bit 20
        .macro  defsyn
        btst    #7, 899(%a0)            | track_t + 0x382, a u16: its 0x80 in the low byte
        beq.s   1f
        bset    #20, %d0
1:
        .endm

| d0 = a step word, d1 = 1 on the first read of a step, 0 on the second;
| 4(sp) = the step, 8(sp) = the track (pushed by the caller).
| Out: d0, the word as sq_on_trig decides (the device layer maps the
| Digitakt's bits: trig = bit 0 with the SYN trig, lock trig = bit 0
| without). Keeps all but d0.
apply:
        lea     -12(%sp), %sp
        movem.l %d1/%a0-%a1, (%sp)
        move.l  %d0, -(%sp)             | word
        move.l  %d1, -(%sp)             | first
        move.l  24(%sp), -(%sp)         | step
        move.l  32(%sp), -(%sp)         | track
        jsr     sq_on_trig
        lea     16(%sp), %sp
        movem.l (%sp), %d1/%a0-%a1
        lea     12(%sp), %sp
        rts

| 0x4006f51a, over `move.w (a3,d0.l),d0; and.l d0,d1` (d0 = 2 * step + 911 *
| track, d1 = 1): the trig test's read. The stock then moves d0 to the CCR
| and skips the step on bit 2 (0x0004), before testing d1: a word with that
| bit never fires, whatever a handler answers (a documented limit).
        .globl  sq_site_gate
sq_site_gate:
        move.w  (%a3,%d0.l), %d0
        lea     (%a3,%d3.l), %a0
        defsyn
        move.l  %d5, -(%sp)
        move.l  %d4, -(%sp)
        moveq   #1, %d1
        bsr     apply
        addq.l  #8, %sp
        moveq   #1, %d1
        and.l   %d0, %d1
        rts

| 0x4006f582, over `add.l d3,d2; move.w (a3,d2.l),d1` (d2 = 2 * step): the
| fire path's second read; d1's low word is the word, d0 is dead.
        .globl  sq_site_word
sq_site_word:
        add.l   %d3, %d2
        move.w  (%a3,%d2.l), %d0
        lea     (%a3,%d3.l), %a0
        defsyn
        move.l  %d5, -(%sp)
        move.l  %d4, -(%sp)
        moveq   #0, %d1
        bsr     apply
        addq.l  #8, %sp
        move.w  %d0, %d1
        rts

| 0x4006f8a4 (MIDI), over `move.w (a2,d0.l),d0; and.l d0,d1` (d1 = 1)
        .globl  sq_site_mgate
sq_site_mgate:
        move.w  (%a2,%d0.l), %d0
        lea     (%a2,%d3.l), %a0
        defsyn
        move.l  %d6, -(%sp)
        move.l  %d4, -(%sp)
        moveq   #1, %d1
        bsr     apply
        addq.l  #8, %sp
        moveq   #1, %d1
        and.l   %d0, %d1
        rts

| 0x4006f90c (MIDI), over `add.l d3,d2; move.w (a2,d2.l),d2`; d0, d1 dead
        .globl  sq_site_mword
sq_site_mword:
        add.l   %d3, %d2
        move.w  (%a2,%d2.l), %d0
        lea     (%a2,%d3.l), %a0
        defsyn
        move.l  %d6, -(%sp)
        move.l  %d4, -(%sp)
        moveq   #0, %d1
        bsr     apply
        addq.l  #8, %sp
        move.w  %d0, %d2
        rts

| ---- trig conditions and probability (ev_fire_cond) ---------------------

| d0 = the condition call's answer (0 passes), d1 = the step, 4(sp) = the
| track. Out: d0 as sq_on_cond decides. Keeps all but d0.
cond:
        lea     -12(%sp), %sp
        movem.l %d1/%a0-%a1, (%sp)
        move.l  %d0, -(%sp)
        move.l  %d1, -(%sp)
        move.l  24(%sp), -(%sp)
        jsr     sq_on_cond
        lea     12(%sp), %sp
        movem.l (%sp), %d1/%a0-%a1
        lea     12(%sp), %sp
        rts

| 0x4006f564, over `lea 16(sp),sp; tst.l d0` (after the trig condition's
| call 0x400ead38, 1.53 0x400eab10; the branches after read the flags)
        .globl  sq_site_scond
sq_site_scond:
        move.l  (%sp), 16(%sp)          | our return, over the call's last argument
        lea     16(%sp), %sp
        move.l  %d1, -(%sp)
        move.l  %d5, -(%sp)
        move.l  %d4, %d1
        bsr     cond
        addq.l  #4, %sp
        move.l  (%sp)+, %d1
        tst.l   %d0
        rts

| 0x4006f8ee (MIDI), the same instructions
        .globl  sq_site_mcond
sq_site_mcond:
        move.l  (%sp), 16(%sp)
        lea     16(%sp), %sp
        move.l  %d1, -(%sp)
        move.l  %d6, -(%sp)
        move.l  %d4, %d1
        bsr     cond
        addq.l  #4, %sp
        move.l  (%sp)+, %d1
        tst.l   %d0
        rts

| d1 = a probability (track_t + 0x38c), d0 = the step, 4(sp) = the track.
| Out: d1, the probability 0x400eacf0 (1.53 0x400eaac8) is asked with.
| Keeps all but d1.
prob:
        lea     -12(%sp), %sp
        movem.l %d0/%a0-%a1, (%sp)
        move.l  %d1, -(%sp)
        move.l  %d0, -(%sp)
        move.l  24(%sp), -(%sp)
        jsr     sq_on_prob
        lea     12(%sp), %sp
        move.l  %d0, %d1
        movem.l (%sp), %d0/%a0-%a1
        lea     12(%sp), %sp
        rts

| 0x4006f56e, over `mvs.b 908(a0),d0; move.l d0,-(sp)` (a0 = track_t: the
| argument of 0x400eacf0). The push lands under our return: the return is
| copied down and the argument written where it was. d1 is kept.
        .globl  sq_site_sprob
sq_site_sprob:
        mvs.b   908(%a0), %d0
        move.l  (%sp), -(%sp)           | our return, once more
        move.l  %d1, -(%sp)
        move.l  %d5, -(%sp)
        move.l  %d0, %d1
        move.l  %d4, %d0
        bsr     prob
        addq.l  #4, %sp
        move.l  %d1, %d0
        move.l  (%sp)+, %d1
        move.l  %d0, 4(%sp)             | the argument
        rts

| 0x4006f8f8 (MIDI), over `mvs.b 908(a0),d1; move.l d1,-(sp)`; d0 is kept
        .globl  sq_site_mprob
sq_site_mprob:
        mvs.b   908(%a0), %d1
        move.l  (%sp), -(%sp)
        move.l  %d0, -(%sp)
        move.l  %d6, -(%sp)
        move.l  %d4, %d0
        bsr     prob
        addq.l  #4, %sp
        move.l  (%sp)+, %d0
        move.l  %d1, 4(%sp)
        rts

| ---- notes (ev_gen_notes, ev_fire_notes) --------------------------------

| 0x4006f5be, over `lea (a3,d3.l),a0; moveq #-1,d6`: a4 = the note the
| audio track plays (note lock 1, or NOTE at +0x384), d2 = the word as fired
| (bit 16 set when the note is locked). src (dev.h struct dev_notes) on our
| stack: the track's data, no list, the note. A changed note goes to a4 and
| is applied as a locked one (bit 16). d0-d1 kept: d1 holds the word still.
        .globl  sq_site_notes
sq_site_notes:
        lea     -8(%sp), %sp
        movem.l %d0-%d1, (%sp)
        move.l  %a4, -(%sp)             | src+8: the note
        clr.l   -(%sp)                  | src+4: no list
        pea     (%a3,%d3.l)             | src+0: the track's data
        move.l  %sp, %a1                | src
        move.l  (%sp), %a0
        move.l  %d2, %d0
        defsyn
        move.l  %a1, -(%sp)
        move.l  %d0, -(%sp)             | the word as fired
        move.l  %d4, -(%sp)             | step
        move.l  %d5, -(%sp)             | track
        jsr     sq_on_notes             | -> src, or 0: the stock note
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   9f
        move.l  8(%sp), %a4             | the note
        bset    #16, %d2
9:      lea     12(%sp), %sp
        movem.l (%sp), %d0-%d1
        lea     8(%sp), %sp
        lea     (%a3,%d3.l), %a0
        moveq   #-1, %d6
        rts

| 0x4006f98a (MIDI), over `lea (a2,d3.l),a0; moveq #-1,d1`: the step's
| notes, absolute, as longs at the fire's 52(sp) (56 here), d7 of them
| (0-4). src: the track's data, the list, the count; a new count goes to d7.
| d0, d1, a0 and a1 are free here.
        .globl  sq_site_mnotes
sq_site_mnotes:
        move.l  %d7, -(%sp)             | src+8: the count
        pea     60(%sp)                 | src+4: the list, the fire's 52(sp)
        pea     (%a2,%d3.l)             | src+0: the track's data
        move.l  %sp, %a1
        move.l  (%sp), %a0
        move.l  %d2, %d0
        defsyn
        move.l  %a1, -(%sp)
        move.l  %d0, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d6, -(%sp)
        jsr     sq_on_notes
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   9f
        move.l  8(%sp), %d7             | the count
9:      lea     12(%sp), %sp
        lea     (%a2,%d3.l), %a0
        moveq   #-1, %d1
        rts

| ---- velocity (ev_fire_vel) ---------------------------------------------

| 0x4006f772, over `move.l 48(sp),d0; adda.l d4,a3` (the velocity, shifted
| into the event after); d1, a0, a1 are reloaded after
        .globl  sq_site_svel
sq_site_svel:
        move.l  52(%sp), -(%sp)         | the velocity (the fire's 48(sp))
        move.l  %d4, -(%sp)
        move.l  %d5, -(%sp)
        jsr     sq_on_vel
        lea     12(%sp), %sp
        adda.l  %d4, %a3
        rts

| 0x4006f9a6 (MIDI), over `lea (a2,d3.l),a0; moveq #-1,d1`: a5 = the velocity
        .globl  sq_site_mvel
sq_site_mvel:
        move.l  %a5, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d6, -(%sp)
        jsr     sq_on_vel
        lea     12(%sp), %sp
        move.l  %d0, %a5
        lea     (%a2,%d3.l), %a0
        moveq   #-1, %d1
        rts

| ---- length (ev_fire_len) ----------------------------------------------

| 0x4006f77c (audio, OS 1.53), over `lea 0x40191290,a0` (LEN's table, which
| the fire indexes with its 44(sp), the LEN index, two instructions on); d0 is
| the velocity, shifted after; a1 and d1 are reloaded after
        .globl  sq_site_slen
sq_site_slen:
        move.l  %d0, -(%sp)
        move.l  52(%sp), -(%sp)         | LEN (the fire's 44(sp))
        move.l  %d4, -(%sp)
        move.l  %d5, -(%sp)
        jsr     sq_on_len
        move.l  %d0, 64(%sp)
        lea     12(%sp), %sp
        move.l  (%sp)+, %d0
        lea     0x40191290, %a0
        rts

| 0x4006fb5a (MIDI, OS 1.53), over `lea 0x40191290,a0`: a4 = LEN, d6 = track
| - 8 by now (the fire's subq), d0 and d1 set again after
        .globl  sq_site_mlen
sq_site_mlen:
        move.l  %a4, -(%sp)
        move.l  %d4, -(%sp)
        move.l  %d6, -(%sp)
        addq.l  #8, (%sp)
        jsr     sq_on_len
        lea     12(%sp), %sp
        movea.l %d0, %a4
        lea     0x40191290, %a0
        rts

| ---- locks (ev_fire_locks) ----------------------------------------------

| 0x4006f750, over `move.l #1500,d0` (after the lock resolve 0x4006f46e, and
| the target of the branch that skips it when a5, the lock list, is 0; the
| list as the Digitone's: count +8, {u16 slot, s16 value} from +0x14, 8
| apart; 53 slots)
        .globl  sq_site_slocks
sq_site_slocks:
        lea     -12(%sp), %sp
        movem.l %d1/%a0-%a1, (%sp)
        move.l  %a5, -(%sp)             | the lock list, or 0
        move.l  %d4, -(%sp)
        move.l  %d5, -(%sp)
        jsr     sq_on_locks
        lea     12(%sp), %sp
        movem.l (%sp), %d1/%a0-%a1
        lea     12(%sp), %sp
        move.l  #1500, %d0
        rts
