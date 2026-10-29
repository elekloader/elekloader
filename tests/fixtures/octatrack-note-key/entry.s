| SPDX-License-Identifier: GPL-2.0-or-later
| The entry of the ev_midi_note_key probe record (tests/fixtures/octatrack-note-key): entered
| from intercepts-ot's stub with every register and the stack as at 0x4009fb58, d7 the track and
| a2 pointing at the note byte. Not a mod to ship.

        .section .run, "ax"
        .globl  nk_entry
        .extern nk_cfg, nk_log, nk_n

nk_entry:
        lea     -12(%sp), %sp
        movem.l %d0-%d1/%a0, (%sp)
        move.l  nk_n, %d0
        moveq   #64, %d1
        cmp.l   %d1, %d0
        bcc.s   1f                      | the log is full
        lsl.l   #2, %d0
        lea     nk_log, %a0
        adda.l  %d0, %a0
        move.b  %d7, (%a0)              | the track
        move.b  (%a2), %d1
        move.b  %d1, 1(%a0)             | the note
        addq.l  #1, nk_n
1:      move.l  nk_cfg+4, %d0
        subq.l  #1, %d0
        beq.s   m1
        subq.l  #1, %d0
        beq.s   m2
        subq.l  #1, %d0
        beq.s   m3
        subq.l  #1, %d0
        beq.s   m4
        bra.s   replay                  | 0: change nothing
m1:     move.b  (%a2), %d1              | the note up two, then stock corrects it
        bmi.s   replay                  | -1: no note here
        addq.l  #2, %d1
        move.b  %d1, (%a2)
        bra.s   replay
m2:     moveq   #-1, %d1                | the note dropped
        move.b  %d1, (%a2)
        bra.s   send
m3:     move.b  (%a2), %d1              | the note up two, sent as it is
        bmi.s   send
        addq.l  #2, %d1
        move.b  %d1, (%a2)
        bra.s   send
m4:     move.l  nk_cfg+8, %d1           | stock's KEY root offset set, then stock corrects the note
        move.l  %d1, -64(%fp)
replay: movem.l (%sp), %d0-%d1/%a0
        lea     12(%sp), %sp
        .word   0x4aae, 0xffc0          | tst.l   -64(%fp)
        bgt.s   2f
        jmp     0x4009fb80              | the ble.s
2:      jmp     0x4009fb5e
send:   movem.l (%sp), %d0-%d1/%a0
        lea     12(%sp), %sp
        jmp     0x4009fb80