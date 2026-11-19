| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.17: the sites of std1's MIDI events (stdmidi.c), OS 1.74. Each
| resumes after the stock bytes it displaced, or skips stock's work when a
| handler took the message. core-a4mk2's stdmidi.s is the model.
        .text
        .globl  core_std_in_din_site, core_std_in_usb_site
        .globl  core_std_cc_site, core_std_pc_site
        .globl  core_std_send_site, core_std_bits_site, core_std_rt_site
        .globl  core_std_seq_midi_site
        .align  2

| 0x40082e18 (DIN) and 0x40082e9e (USB), jsr from the MIDI task: `movea.l
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

| 0x40085652, one CC to one track (track, cc, value, auto, perf), MIDI task:
| `tst.l 20(%sp); sne %d0`; stock then goes on to 0x400852ba.
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
        jmp     0x40085658

| 0x40083384, the program-change handler (data, len, iface), MIDI task: `lea
| -48(%sp),%sp; move.l 0x413189af,%d0`.
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
        move.l  0x413189af,%d0
        jmp     0x4008338e

| 0x400857ca, the general send (len, data, dest), tasks: `lea -12(%sp),%sp;
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
        jmp     0x400857d2

| 0x400856b8, the ports setting's send (len, data): the same prologue.
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
        jmp     0x400856c0

| 0x40085724, a real-time byte (byte), the clock interrupt or a task: `link.w
| %a6,#-4; move.b 11(%a6),-4(%a6)`.
core_std_rt_site:
        move.l  4(%sp),-(%sp)
        jsr     core_std_out_rt
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      link.w  %a6,#-4
        move.b  11(%a6),-4(%a6)
        jmp     0x4008572e

| 0x401200b2, the audio interrupt queueing a note record (%a0) for the
| note-out task: `move.l %a0,-(%sp); pea 0x41908de0`, then jsr 0x400013b2
| (mbox_post) and addq #8 at 0x401200c0. Taken: the record is not queued
| (its ring slot is simply reused), on at 0x401200c2. mbox_post's own call
| clobbers %d0-%d1/%a0-%a1, so the C call here does no more.
core_std_seq_midi_site:
        move.l  %a0,-(%sp)
        move.l  %a0,-(%sp)
        jsr     core_std_seq_note
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        addq.l  #4,%sp
        jmp     0x401200c2
1:      pea     0x41908de0
        jmp     0x401200ba
