| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1: the voice note-on event (Digitone mk1, OS 1.43). ColdFire V4;
| assemble with -mcpu=54455. Beside core.s, which is shared with the
| Digitakt mk1; this event exists on the Digitone only.
|
|   VOICE_PITCH  the voices' pitch words: one long per voice, the note in
|                the high word (note << 16), which the render reads again
|                every block
|
| ---- ev_voice_on: a voice starts a note ------------------------------------
| At 0x4009e928 in the audio render (vector 191), in its note-on loop (was:
| lea VOICE_PITCH,a0, followed by move.l d0,(0,a0,a1.l*4) at 0x4009e92e,
| which stores the voice's pitch word). Here a1 = the voice (0-7), d2 = the
| track, d0 = the pitch word and a3 = the note event. core_voice_on makes
| that store itself, runs the handlers and returns past it, so a handler
| sees the pitch word written and may change it.
|
| Handlers: f(int voice, int track, void *event). Interrupt level, inside the
| render: keep them short and free of firmware calls that might block. The
| voice's sound and the step's parameter locks are loaded after this point,
| in the same pass: read the voice's parameters from ev_render_out, not here.
| Every register is as the render left it when this returns, a0 included.
        .section .run, "ax"
        .globl  core_voice_on
core_voice_on:
        lea     VOICE_PITCH, %a0        | the instruction this replaced
        move.l  %d0, (0,%a0,%a1.l*4)    | and the store after it, at 0x4009e92e
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)  | 0 d0, 4 d1, 8 a0, 12 a1, 16 a2
        lea     ev_voice_on, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  %a3, -(%sp)             | the note event
        move.l  %d2, -(%sp)             | the track
        move.l  20(%sp), -(%sp)         | the voice: the saved a1, 8 bytes up
        movea.l %d0, %a0
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        addq.l  #4, (%sp)               | return past the store at 0x4009e92e
        rts

| ---- ev_voice_ready: a starting voice, with its sound and its locks (4.2) -
| At 0x4009eadc (1.44 0x4009eafc), the note-on loop's last instruction for a
| voice (was: clr.l READY_FLAG), in the same pass as ev_voice_on. By now the
| voice holds its sound in fw_voice_params (loaded whole for another sound,
| or for the same one only the slots in fw_voice_locked again) and the
| event's p-locks over it, each set by the firmware's fw_voice_locks: the
| target, the smoother's value (so it starts there) and the slot's bit in
| fw_voice_locked, which makes the voice's next note load that slot from its
| sound again. core makes the clear itself and runs the handlers. A handler
| that puts values into the voice with fw_voice_locks starts the note with
| them as with p-locks; what the voice has at this point is what plays.
|
| Handlers: f(int voice, int track, void *event), as ev_voice_on's.
| Interrupt level, inside the render: keep them short. Here -128(a6) = the
| voice, d2 = the track and a3 = the event; every register is as the render
| left it when this returns.
        .globl  core_voice_ready
core_voice_ready:
        clr.l   READY_FLAG              | the instruction this replaced
        tst.l   ev_voice_ready
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_voice_ready, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  %a3, -(%sp)             | the note event
        move.l  %d2, -(%sp)             | the track
        move.l  -128(%a6), -(%sp)       | the voice
        movea.l %d0, %a0
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts
