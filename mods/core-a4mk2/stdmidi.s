| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.7: the sites of std1's MIDI events and ev_seq_tick (stdmidi.c),
| OS 1.56. Each resumes after the stock bytes it displaced, or skips stock's
| work when a handler took the message.
        .text
        .globl  core_std_in_din_site, core_std_in_usb_site, core_std_sysex_site
        .globl  core_std_cc_site, core_std_pc_site
        .globl  core_std_send_site, core_std_bits_site, core_std_rt_site
        .globl  core_std_seq_midi_site, core_std_seq_tick_site
        .align  2

| 0x4006578a (DIN) and 0x40065818 (USB), jsr from the MIDI task: `movea.l
| (0,%a2,%dN.l*4),%a0; jsr (%a0)`, the status handler of (data, len, iface)
| on the stack, %a2 the table and %d0 (DIN) or %d1 (USB) the status's top
| nibble. Not taken: on to the handler, which returns to the task as before.
core_std_in_din_site:
        lea     -8(%sp),%sp
        movem.l %d0-%d1,(%sp)
        pea     1.w                     | PORT_DIN
        move.l  20(%sp),-(%sp)          | len
        move.l  20(%sp),-(%sp)          | data
        jsr     core_std_midi_in
        lea     12(%sp),%sp
        tst.l   %d0
        movem.l (%sp),%d0-%d1
        lea     8(%sp),%sp
        bne.s   1f
        movea.l (%a2,%d0.l*4),%a0
        jmp     (%a0)
1:      rts

core_std_in_usb_site:
        lea     -8(%sp),%sp
        movem.l %d0-%d1,(%sp)
        pea     2.w                     | PORT_USB
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        jsr     core_std_midi_in
        lea     12(%sp),%sp
        tst.l   %d0
        movem.l (%sp),%d0-%d1
        lea     8(%sp),%sp
        bne.s   1f
        movea.l (%a2,%d1.l*4),%a0
        jmp     (%a0)
1:      rts

| 0x40066320, the SysEx router (data, len, iface), MIDI task: `lea
| -16(%sp),%sp; movem.l %d2-%d4/%a2,(%sp)`.
core_std_sysex_site:
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     core_std_sysex_in
        lea     12(%sp),%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      lea     -16(%sp),%sp
        movem.l %d2-%d4/%a2,(%sp)
        jmp     0x40066328

| 0x40068632, one CC to one track (track, cc, value, auto, perf), MIDI task:
| `tst.l 20(%sp); sne %d0`.
core_std_cc_site:
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        jsr     core_std_midi_cc
        lea     20(%sp),%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      tst.l   20(%sp)
        sne     %d0
        jmp     0x40068638

| 0x40065e9e, the program-change handler (data, len, iface), MIDI task: `lea
| -48(%sp),%sp; move.l 0x412aa57a,%d0`.
core_std_pc_site:
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     core_std_midi_pc
        lea     12(%sp),%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      lea     -48(%sp),%sp
        move.l  0x412aa57a,%d0
        jmp     0x40065ea8

| 0x400687aa, the general send (len, data, dest), tasks: `lea -12(%sp),%sp;
| movem.l %d2-%d4,(%sp)`.
core_std_send_site:
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     core_std_out_send
        lea     12(%sp),%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      lea     -12(%sp),%sp
        movem.l %d2-%d4,(%sp)
        jmp     0x400687b2

| 0x40068698, the OUTPUT TO send (len, data): the same prologue.
core_std_bits_site:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_std_out_bits
        addq.l  #8,%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      lea     -12(%sp),%sp
        movem.l %d2-%d4,(%sp)
        jmp     0x400686a0

| 0x40068704, a real-time byte (byte), the clock-out interrupt or a task:
| `link.w %a6,#-4; move.b 11(%a6),-4(%a6)`.
core_std_rt_site:
        move.l  4(%sp),-(%sp)
        jsr     core_std_out_rt
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      link.w  %a6,#-4
        move.b  11(%a6),-4(%a6)
        jmp     0x4006870e

| 0x40101acc, the audio interrupt queueing a note record (%a0) for the note-out
| task: `move.l %a0,-(%sp); pea 0x417f73d0`, then jsr 0x40001f4a and addq
| #8 at 0x40101ada. Taken: the record is not queued (its ring slot is simply
| reused), on at 0x40101adc.
core_std_seq_midi_site:
        move.l  %a0,-(%sp)
        move.l  %a0,-(%sp)
        jsr     core_std_seq_note
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        addq.l  #4,%sp
        jmp     0x40101adc
1:      pea     0x417f73d0
        jmp     0x40101ad4

| 0x4007badc, the sequencer tick interrupt's (vector 121) only exit: `movem.l
| (%sp),%d0-%d7/%a0-%a6; lea 76(%sp),%sp`, then rte. Every register is saved
| here, so the handlers run before they come back.
core_std_seq_tick_site:
        jsr     core_std_seq_tick
        movem.l (%sp),%d0-%d7/%a0-%a6
        lea     76(%sp),%sp
        rte
