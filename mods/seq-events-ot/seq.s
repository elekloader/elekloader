| SPDX-License-Identifier: GPL-2.0-or-later
| seq-events (Octatrack, OS 1.40C): the sequencer's two record builders, offered to other mods as
| the events in elekloader/sdk/include/seq-events.h (fire.c, ../seq-events/seq.c, with dev.h).
| ColdFire V4e (MCF5445x); assemble with -mcpu=54455.
|
| The sequencer's tick (an interrupt) builds each track's next step: for an audio track
| 0x4009d1e8(track, bank, pattern, step, n), for a MIDI track 0x4009cf4c(the same). Both are C
| calls: the arguments at 4(sp)..20(sp) above the return address, d2-d7/a2-a6 kept, and each
| begins with a stack frame and a movem that a jmp site replaces. Each calls the trig condition
| 0x400a5164 and tests its answer; that is the second site (the call's four arguments sit the
| same in both builders: track 0-15 first, then the step).
|
|   sq_site_abuild, sq_site_mbuild   the entries. enter (fire.c) may replace the step argument and
|                                    the return address; the displaced instructions run, and the
|                                    builder goes on at its third instruction
|   sq_site_acond, sq_site_mcond     the condition's answer, and the stages that need the step to
|                                    fire; the builder's return address, which names its frame
|   sq_tramp0..2                     the builder's return, when enter put it there: sq_exit puts
|                                    back what was written, and the real return address follows

        .section .run, "ax"

| 0x4009d1e8, 8 bytes: lea -104(sp),sp; movem.l d2-d7/a2-a6,(sp). sp -> [return][track][bank]
| [pattern][step][n]. d0-d1/a0-a1 are the builder's own.
        .globl  sq_site_abuild
sq_site_abuild:
        movea.l %sp, %a0
        move.l  %a0, -(%sp)             | enter's argument: the frame
        jsr     sq_a_enter
        addq.l  #4, %sp
        .word   0x4fef, 0xff98          | lea -104(sp),sp
        .word   0x48d7, 0x7cfc          | movem.l d2-d7/a2-a6,(sp)
        jmp     0x4009d1f0

| 0x4009cf4c, 8 bytes: lea -40(sp),sp; movem.l d2-d7/a2-a5,(sp)
        .globl  sq_site_mbuild
sq_site_mbuild:
        movea.l %sp, %a0
        move.l  %a0, -(%sp)
        jsr     sq_m_enter
        addq.l  #4, %sp
        .word   0x4fef, 0xffd8          | lea -40(sp),sp
        .word   0x48d7, 0x3cfc          | movem.l d2-d7/a2-a5,(sp)
        jmp     0x4009cf54

| 0x4009d354 (audio), 0x4009d036 (MIDI), 6 bytes over `lea 16(sp),sp; tst.l d0` after the
| condition's call: d0 = its answer (0 passes), the four words it was called with at 4(sp)..:
| track (MIDI: +8), step, condition, immediate. The branch after reads the flags. The builder's
| return address is above its frame (104 bytes for the audio builder, 40 for the MIDI) and the
| call's four words: if enter put a sq_tramp there, it names the build's frame.
        .macro  COND name, retslot
        .globl  \name
\name:
        lea     \retslot(%sp), %a1      | where the builder returns to
        move.l  (%sp), 16(%sp)          | our return, over the call's last argument
        movea.l 4(%sp), %a0             | track
        move.l  8(%sp), %d1             | step
        lea     16(%sp), %sp
        move.l  %a1, -(%sp)
        move.l  %d0, -(%sp)             | the answer
        move.l  %d1, -(%sp)
        move.l  %a0, -(%sp)
        jsr     sq_cond
        lea     16(%sp), %sp
        tst.l   %d0
        rts
        .endm

        COND    sq_site_acond, 124      | 4 (our return) + 16 + 104
        COND    sq_site_mcond, 60       | 4 + 16 + 40

| The builder returned to here, to the trampoline of the frame enter gave it: sq_exit(frame) ->
| d0, the real return address, put under the registers the caller may expect from the builder
| (it keeps what a C call keeps, and so do we).
        .macro  TRAMP k
        .globl  sq_tramp\k
sq_tramp\k:
        lea     -20(%sp), %sp           | the registers, and room for the return address
        movem.l %d0-%d1/%a0-%a1, (%sp)
        moveq   #\k, %d0
        bra     sq_tramp_common
        .endm

        TRAMP   0
        TRAMP   1
        TRAMP   2

sq_tramp_common:
        move.l  %d0, -(%sp)
        jsr     sq_exit
        addq.l  #4, %sp
        move.l  %d0, 16(%sp)
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        rts
