| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.9: the engine push's two index sites for velocity and
| aftertouch routes to a mod parameter (routes.c). OS 1.74.
|
| The engine push (0x4011f60a, sound, track) writes track t's route entries
| at 0x8000ea3c + 0x20 t: 4 velocity {s16 amount, s16 index} at +0x00, then
| 4 aftertouch at +0x10, index = t x 42 + 26 + code into the engine's
| parameter words (0x8000f774); the audio interrupt adds g x amount to each
| (0x4011de2a). A code above 41 would index another track's parameter, so
| here such an entry gets stock's empty one (index 25, amount 0, as stock's
| scene and performance builds give it), and core keeps the route's amount
| and code in core_routes, laid out as the engine's entries (one long per
| route, {s16 amount, s16 code}, 0 for a stock route), for core_mod_offset.
| Each site is 6 bytes of whole instructions with a free register.
        .text
        .globl  core_vel_index, core_at_index
        .align  2

        .equ    ENTRIES, 0x8000ea3c

| 0x4011f6b2, velocity: `mvs.b 111(%a1),%d3; add.l %d0,%d3`, the route's
| code (a1 already moved on by 3) plus t x 42 + 26 (d0, kept); the entry's
| amount is at 1216(%a0), the index goes to 1218(%a0) at 0x4011f6b8. d3 is
| free; a2 (the sound) is kept.
core_vel_index:
        mvs.b   111(%a1),%d3
        move.l  %a2,-(%sp)
        move.l  %a0,%a2
        adda.l  #1216-ENTRIES,%a2
        adda.l  #core_routes,%a2
        cmpi.l  #41,%d3
        bgt.s   1f
        clr.l   (%a2)
        move.l  (%sp)+,%a2
        add.l   %d0,%d3
        jmp     0x4011f6b8
1:      move.w  1216(%a0),(%a2)
        move.w  %d3,2(%a2)
        move.l  (%sp)+,%a2
        clr.w   1216(%a0)
        moveq   #25,%d3
        jmp     0x4011f6b8

| 0x4011f6d8, aftertouch: `mvs.b 139(%a2),%d2; adda.l %d2,%a1`, the route's
| code (a2 already moved on by 3) added to a1 = t x 42 + 26; the entry's
| amount is at -4(%a0) (a0 moved on by 4), the index goes to -2(%a0) at
| 0x4011f6de. d2 is free; a3, which the push does not use, is kept.
core_at_index:
        mvs.b   139(%a2),%d2
        move.l  %a3,-(%sp)
        move.l  %a0,%a3
        adda.l  #-4-ENTRIES,%a3
        adda.l  #core_routes,%a3
        cmpi.l  #41,%d2
        bgt.s   1f
        clr.l   (%a3)
        move.l  (%sp)+,%a3
        adda.l  %d2,%a1
        jmp     0x4011f6de
1:      move.w  -4(%a0),(%a3)
        move.w  %d2,2(%a3)
        move.l  (%sp)+,%a3
        clr.w   -4(%a0)
        movea.w #25,%a1
        jmp     0x4011f6de
