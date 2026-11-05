| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.11: the length and scale setters, for ev_steps' LENGTH (steps.c).
| Each is a jmp site at the setter's entry: core runs the setter (its
| displaced instructions, then a jump back into it), then tells steps.c
| which setter ran, its object and the value it was given (OS 1.74).
        .text
        .globl  core_len_track, core_speed_track, core_len_pattern, core_speed_pattern
        .globl  core_scale_mode, core_len_change
        .align  2

| after the setter: core_steps_length(which, object, value), d0 kept
        .macro  after which
        move.l  %d0,-(%sp)
        move.l  12(%sp),-(%sp)            | value
        move.l  12(%sp),-(%sp)            | object
        pea     \which
        jsr     core_steps_length
        lea     12(%sp),%sp
        move.l  (%sp)+,%d0
        rts
        .endm

| the setter with its first four arguments again
        .macro  call body
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     \body
        lea     16(%sp),%sp
        .endm

| 0x400c0ba2, a track's length (track, length): `lea -20(%sp),%sp; movem.l
| %d2-%d3/%a2/%a6,(%sp)`.
core_len_track:
        call    1f
        after   0
1:      lea     -20(%sp),%sp
        movem.l %d2-%d3/%a2/%a6,(%sp)
        jmp     0x400c0baa

| 0x400c0c8c, a track's speed (track, speed): the same.
core_speed_track:
        call    1f
        after   1
1:      lea     -20(%sp),%sp
        movem.l %d2-%d3/%a2/%a6,(%sp)
        jmp     0x400c0c94

| 0x400ab8d0, the pattern's (master) length (pattern, length): `lea
| -12(%sp),%sp; movem.l %d2-%d3/%a2,(%sp)`.
core_len_pattern:
        call    1f
        after   2
1:      lea     -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp     0x400ab8d8

| 0x400aba02, the pattern's speed (pattern, speed): the same.
core_speed_pattern:
        call    1f
        after   3
1:      lea     -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp     0x400aba0a

| 0x400aba7e, the scale mode (pattern, mode): the same.
core_scale_mode:
        call    1f
        after   4
1:      lea     -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp     0x400aba86

| 0x400ab096, the change length (pattern, length): `move.l %a2,-(%sp);
| move.l %d2,-(%sp); movea.l 12(%sp),%a2`.
core_len_change:
        call    1f
        after   5
1:      move.l  %a2,-(%sp)
        move.l  %d2,-(%sp)
        movea.l 12(%sp),%a2
        jmp     0x400ab09e
