| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 5.0.1 (Digitone mk1): the sequencer's MIDI out as an event, and a MIDI
| message played into the unit as if it came in. ColdFire V4; assemble with
| -mcpu=54455. Beside core.s.
|
|   SEQ_MIDI_A, SEQ_MIDI_B   the sequencer's MIDI engine's two append routines
|   SEQ_MIDI_A_LEN, ..._B_LEN  the length bytes of their buffers
|   MIDI_TABLE   the MIDI task's dispatch table: a handler per status nibble
|   AUTO_CHAN    the auto channel (0-15)
|
| Addresses are 1.43's; the 1.44 port in mod.json gives its own.

        .section .run, "ax"

| ---- ev_seq_midi ----------------------------------------------------------------
| ev_seq_midi: int f(const uint8_t *msg, int len, int buffer), may take. Each
| message the sequencer's MIDI engine queues for its output: the MIDI tracks'
| notes, CCs, program changes, pitch bend and aftertouch, and the All Sound
| Off it sends each MIDI track at STOP. The engine appends a whole message to
| one of two buffers (0x400f3bfc, buffer 0; 0x400f39a6, buffer 1) and later
| writes each buffer straight to DIN and USB (0x400f355a, by MIDI CONFIG's
| OUTPUT TO): these messages do not pass the port switch, so ev_midi_out never
| sees them. A handler that returns nonzero takes the message: it is not
| appended, so it is not sent. Interrupt level (the engine's tick).
        .macro  SEQ_MIDI_STUB buffer, lenbyte, routine
        tst.l   ev_seq_midi
        beq.s   8f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_seq_midi, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        pea     \buffer
        move.l  28(%sp), -(%sp)         | len (4(sp) at entry)
        move.l  36(%sp), -(%sp)         | msg (8(sp) at entry)
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2  | taken: the message is not queued
        lea     20(%sp), %sp
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
8:      mvz.b   \lenbyte, %d1           | the two instructions this replaced
        move.l  %d2, -(%sp)
        jmp     \routine + 8
        .endm

        .globl  core_seq_midi_a, core_seq_midi_b
core_seq_midi_a:
        SEQ_MIDI_STUB 0, SEQ_MIDI_A_LEN, SEQ_MIDI_A
core_seq_midi_b:
        SEQ_MIDI_STUB 1, SEQ_MIDI_B_LEN, SEQ_MIDI_B

| ---- core_midi_receive -----------------------------------------------------------
| int core_midi_receive(const uint8_t *msg, int len): a whole MIDI message
| (a channel message, or real time) played into the unit as if it had come in
| on DIN: the handlers of ev_midi_in first (a nonzero return takes it, and
| core_midi_receive returns 1), then the MIDI task's own handler for its
| status, as its dispatch calls it, so ev_midi_cc and ev_midi_pc see it and the
| stock applies it by MIDI CONFIG's channels. MIDI CONFIG's INPUT FROM is not
| asked: the message did not come in on a port. Source 0x10 when its channel
| is the auto channel, 8 otherwise, as the MIDI task passes them. From a task
| (the UI task's ev_tick, say), never at interrupt level. -> 0.
        .globl  core_midi_receive
core_midi_receive:
        lea     -12(%sp), %sp
        movem.l %d2-%d3/%a2, (%sp)
        movea.l 16(%sp), %a0            | msg
        moveq   #0, %d2
        move.b  (%a0), %d2              | the status
        moveq   #8, %d3                 | the source: DIN
        cmpi.l  #0xF0, %d2
        bcc.s   1f
        moveq   #15, %d0
        and.l   %d2, %d0
        cmp.l   AUTO_CHAN, %d0
        bne.s   1f
        moveq   #0x10, %d3              | DIN, on the auto channel
1:      lea     ev_midi_in, %a2
2:      move.l  (%a2)+, %d0
        beq.s   3f
        movea.l %d0, %a1
        move.l  %d3, -(%sp)
        move.l  24(%sp), -(%sp)         | len
        move.l  24(%sp), -(%sp)         | msg
        jsr     (%a1)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   2b
        moveq   #1, %d0                 | taken by a handler
        bra.s   9f
3:      lsr.l   #4, %d2
        lea     MIDI_TABLE, %a1
        movea.l (0, %a1, %d2.l*4), %a1
        move.l  %d3, -(%sp)
        move.l  24(%sp), -(%sp)
        move.l  24(%sp), -(%sp)
        jsr     (%a1)
        lea     12(%sp), %sp
        moveq   #0, %d0
9:      movem.l (%sp), %d2-%d3/%a2
        lea     12(%sp), %sp
        rts

