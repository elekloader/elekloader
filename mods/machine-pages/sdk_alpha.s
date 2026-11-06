| SPDX-License-Identifier: GPL-2.0-or-later
| machine-pages' SDK alpha (OS 1.54): what sdk_alpha.c and pages.c need in assembly.
        .section .run, "ax"

| Redraw presentation without resetting the native temporary value-display timers.
        .globl sdk_alpha_redraw
sdk_alpha_redraw:
        move.l  %a2, -(%sp)
        movea.l 8(%sp), %a2
        move.l  %a2, -(%sp)
        jsr     SRC_DIRTY_FN
        move.l  108(%a2), (%sp)
        jsr     SRC_INVALIDATE_FN
        addq.l  #4, %sp
        movea.l (%sp)+, %a2
        rts
