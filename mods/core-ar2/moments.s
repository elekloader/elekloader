| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.11: the sites of the moments (moments.c), OS 1.74. Each body is
| the routine's displaced first instructions, then a jump to the next one,
| called like the stock routine.
        .text
        .globl  core_sysex_site
        .globl  core_kit_select_body, core_kit_push_body, core_slot_clear_body, core_slot_paste_body
        .align  2

| 0x40083dd8, the SysEx router (data, len, iface), MIDI task: `lea
| -16(%sp),%sp; movem.l %d2-%d3/%a2-%a3,(%sp)`. A message a mod takes
| (ev_sysex_in nonzero) goes no further; any other goes on to stock.
core_sysex_site:
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     core_sysex_in
        lea     12(%sp),%sp
        tst.l   %d0
        beq.s   1f
        rts
1:      lea     -16(%sp),%sp
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp     0x40083de0

core_kit_select_body:                   | 0x400a65f6 (working kits, kit, a, b)
        lea     -32(%sp),%sp
        movem.l %d2-%d5/%a2-%a5,(%sp)
        jmp     0x400a65fe
core_kit_push_body:                     | 0x4011f90a (kit data)
        lea     -24(%sp),%sp
        movem.l %d2-%d3/%a2-%a5,(%sp)
        jmp     0x4011f912
core_slot_clear_body:                   | 0x40034bd2 (store, slot); the cmp after it sets the flags
        moveq   #-1,%d1
        move.l  8(%sp),%d0
        jmp     0x40034bd8
core_slot_paste_body:                   | 0x400351d2 (store, slot, from a file)
        link.w  %a6,#-56
        movem.l %d2-%d6/%a2,(%sp)
        jmp     0x400351da
