| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the held-step editor, the pattern data's copies and the MIDI
| tracks' notes (core-ot 0.11). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| octabam's EVENTS module holds these places for PLOCKS P2 and MIDI FOLLOW (MIT; the census and the
| contracts are theirs, the code is our own):
|
|   0x400508e4   the held-step editor (a trig held, an encoder turned): its six   ev_edit_held
|                encoder records (0x400beddc + 22 n) point at it
|   22 places    the pattern and track data's copies through memcpy (0x40020898):  ev_copy
|                the clipboard, the undo buffer, the bank; 11 calls, and 11
|                `lea memcpy,aN` whose register the routine calls
|   0x4009fbb2,  a MIDI track's note has gone out (after midi_send 0x40010bc8)      ev_midi_note_sent
|   0x4009fcfa

        .section .run, "ax"

| ---- the held-step editor ------------------------------------------------------------------------------
| 0x400508e4 (was: lea -80(sp),sp ; movem.l d2-d7/a2-a6,(sp)), reached by a jsr through an encoder
| record: 4(sp) the encoder, 8(sp) the turn. Handlers: int f(unsigned enc, int delta); the first that
| returns nonzero has taken the turn (it did the edit), and the stock editor does not run: it returns
| nothing, so the stub returns to its caller.
        .globl  core_edit_held
core_edit_held:
        tst.l   ev_edit_held
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_edit_held, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  28(%sp), -(%sp)         | the turn
        move.l  28(%sp), -(%sp)         | the encoder (24 + 4 for the push)
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2  | taken
        lea     20(%sp), %sp
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      lea     -80(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     0x400508ec

| ---- the pattern data's copies ---------------------------------------------------------------------------
| Reached instead of memcpy(dst, src, len) by a jsr, so 4(sp) is dst. Handlers: void f(void *dst,
| const void *src, unsigned len), before the copy; then memcpy runs once with the caller's arguments.
        .globl  core_copy
core_copy:
        tst.l   ev_copy
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_copy, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  32(%sp), -(%sp)         | len
        move.l  32(%sp), -(%sp)         | src
        move.l  32(%sp), -(%sp)         | dst
        movea.l %d1, %a0
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      jmp     0x40020898

| ---- a MIDI track's note -----------------------------------------------------------------------------------
| After midi_send returned, d7 = the MIDI track; the message is at 0x400d807c (status, note, velocity);
| the track's note pool kind is the byte at 0x46c77b1e + 10 track (1: the arpeggiator's). Handlers:
| void f(unsigned trk, unsigned note, unsigned vel, unsigned pool). A site replaces
| `move.b 0x400d807c,dN ; extb.l dN` (mvs.b, 6 bytes) and replays it last.
        .macro  NOTE addr, reg
        .globl  core_note_\addr
core_note_\addr:
        tst.l   ev_midi_note_sent
        beq.s   9f
        moveq   #7, %d1                 | (d1 is free here: midi_send has just returned)
        cmp.l   %d7, %d1
        bcs.s   9f                      | not a MIDI track
        lea     -32(%sp), %sp
        movem.l %d0-%d4/%a0-%a2, (%sp)
        moveq   #10, %d0
        mulu.l  %d7, %d0
        lea     0x46c77b1e, %a0
        moveq   #0, %d4
        move.b  %a0@(0,%d0:l), %d4      | pool
        moveq   #0, %d2
        move.b  0x400d807e, %d2         | velocity
        moveq   #0, %d3
        move.b  0x400d807d, %d3         | note
        lea     ev_midi_note_sent, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        lea     -16(%sp), %sp
        move.l  %d7, (%sp)
        move.l  %d3, 4(%sp)
        move.l  %d2, 8(%sp)
        move.l  %d4, 12(%sp)
        movea.l %d0, %a0
        jsr     (%a0)                   | keeps d2-d4, a2 (the C ABI)
        lea     16(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d4/%a0-%a2
        lea     32(%sp), %sp
9:      mvs.b   0x400d807c, %\reg
        jmp     \addr + 6
        .endm

        NOTE    0x4009fbb2, d1
        NOTE    0x4009fcfa, d0
