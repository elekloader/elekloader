| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.15: the sound a trig plays (sound.c). OS 1.74.
|
| 0x4011f5c4 (item, track) pushes a sound to the engine: a kit's sound on a
| plain trig or a kit apply, a pool sound on a sound lock. It is called from
| the audio interrupt (0x4011fb66) and from the UI's kit apply. At 0x4011f5f6
| the machine check has passed, a2 is the sound item and d2 the track, and
| stock pushes the push's arguments: `move.l %d2,-(%sp); pea 20(%a2)`, then
| `jsr 0x4011dace` (PC-relative, left in place). Core's routine pushes the
| same two arguments under its return address and files the item for the
| track in core_trig_items. Every register is kept.
        .text
        .globl  core_trig_sound
        .align  2

core_trig_sound:
        subq.l  #8,%sp
        move.l  %a0,-(%sp)
        move.l  12(%sp),%a0
        move.l  %a0,4(%sp)
        move.l  %d2,12(%sp)
        lea     20(%a2),%a0
        move.l  %a0,8(%sp)
        cmpi.l  #12,%d2
        bcc.s   1f
        lea     core_trig_items,%a0
        move.l  %a2,(%a0,%d2.l*4)
1:      move.l  (%sp)+,%a0
        rts
