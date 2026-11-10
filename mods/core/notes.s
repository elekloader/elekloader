| SPDX-License-Identifier: GPL-2.0-or-later
| core 4.2 (Digitakt mk1) and core-dn1 5.3 (Digitone mk1): the played notes.
| A note played on a track, before its voice, as an event a handler may take
| (ev_live_note), and the stock routines past it (fw_note_on, fw_note_off), so
| a mod that took a note can play another without the event seeing it. Last in
| sources, so the code before it keeps its place. The code takes the addresses
| and the routines' frame sizes from mod.json's defsym, one set per device and
| OS; the comments give the Digitakt's 1.53 and the Digitone's 1.44:
|   NOTE_ON   0x400d53dc, 0x400eae5c   note_on(track, note, vel, source, skip, sound[, flag])
|   NOTE_OFF  0x400d575e, 0x400eabb4   note_off(track, note, source)
| source is where the note came from (0x40 the trig keys, keyboard mode and the
| preview, 2 MIDI on the Digitakt); skip nonzero counts the key without playing
| it (a note recorded live); the trig keys pass sound (and on the Digitakt
| flag) -1. Both count the keys held on each track and each note (Digitakt
| 0x4219c0e0, 0x4219bc98; Digitone 0x41994e80, 0x419952a8) before they send
| the voice its message. Each starts with link.w %fp, #NOTE_ON_LINK (or
| NOTE_OFF_LINK) and moveq #127, %d0, the 6 bytes its site replaces.
| digipoly, digislicer and ob1 hook the Digitakt's note submits inside these
| routines for themselves; the sites here are the routines' entries, so they
| still link.

        .section .run, "ax"
        .balign 4                       | after the .run before it, which it leaves as it was

| ev_live_note: f(int track, int note, int vel, int on, int source), may take.
| on 1 (NOTE_ON's entry): a press, vel its velocity; on 0 (NOTE_OFF's entry):
| its release (vel: the routine's third argument, the source). The trig keys,
| keyboard mode, the preview and a MIDI note on a track's channel pass here,
| in the UI task or the MIDI task; the sequencer's trigs do not. A handler
| that takes it (nonzero): the routine does nothing, the key is not counted
| and the track does not play (or release) it. With none: a test and a branch
| more than stock.
        .globl  core_note_on, core_note_off, fw_note_on, fw_note_off
core_note_on:
        moveq   #1, %d0
        move.l  16(%sp), %d1            | source
        bsr.s   live_note
        bne.s   8f
fw_note_on:
        link.w  %fp, #NOTE_ON_LINK      | NOTE_ON's first two instructions
        moveq   #127, %d0
        jmp     NOTE_ON + 6
8:      rts
core_note_off:
        moveq   #0, %d0
        move.l  12(%sp), %d1            | source
        bsr.s   live_note
        bne.s   8f
fw_note_off:
        link.w  %fp, #NOTE_OFF_LINK     | NOTE_OFF's first two instructions
        moveq   #127, %d0
        jmp     NOTE_OFF + 6
8:      rts
| d0 = on, d1 = source; 8(sp) track, 12 note, 16 vel. -> Z clear when taken.
live_note:
        tst.l   ev_live_note
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_live_note, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  4(%sp), -(%sp)          | source: the saved d1
        move.l  4(%sp), -(%sp)          | on: the saved d0
        move.l  44(%sp), -(%sp)         | vel
        move.l  44(%sp), -(%sp)         | note
        move.l  44(%sp), -(%sp)         | track
        jsr     (%a0)
        lea     20(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        moveq   #1, %d1                 | taken: Z clear
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      cmp.l   %d0, %d0
        rts
