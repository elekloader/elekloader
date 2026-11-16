| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2: the sites of the moments (moments.c), OS 1.56. Each body is
| the routine's displaced first instructions, then a jump to the next one,
| called like the stock routine.
        .text
        .globl  core_kit_select_body, core_kit_push_body
        .align  2

core_kit_select_body:                   | 0x4016796c (this, index): the active-item setter
        move.l  %a2,-(%sp)
        move.l  %d2,-(%sp)
        moveq   #0x7f,%d2
        jmp     0x40167972
core_kit_push_body:                     | 0x401012ec (kit data)
        lea     -24(%sp),%sp
        movem.l %d2/%a2-%a6,(%sp)
        jmp     0x401012f4
