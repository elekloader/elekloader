| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.7: core_setting_add's label (std.c). Stock's MenuItem calls a
| label as a std::function returning a std::string: the string to build is
| at %a0 (the hidden result), the functor's address is the first stack
| argument, its first word core_setting_add's payload, the mod's struct
| core_setting, whose first word is the name; %d0 returns %a0.
| std::string(%a0, text, allocator) is stock's 0x4018471e, as the A4's own
| labels and std-check-a4mk2's label.s build theirs.

        .text
        .align  2
        .globl  core_setting_label
core_setting_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        move.l  %a0, %d2
        movea.l 8(%a6), %a1             | the functor
        movea.l (%a1), %a1              | the struct core_setting
        pea     -1(%a6)                 | the allocator, a byte on the frame
        move.l  (%a1), -(%sp)           | its name
        move.l  %d2, -(%sp)
        jsr     0x4018471e
        lea     12(%sp), %sp
        move.l  %d2, %d0
        movea.l %d2, %a0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts
