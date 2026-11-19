; SPDX-License-Identifier: GPL-2.0-or-later
; The guard's stub (0.2): not built. pump.c holds its words (stub[]) and puts the four addresses in
; for each block it loads: LO and HI the guard words before and after the code, CODE the code's
; entry, BOUND the slot's BOUND word. tests/test_octatrack.py (test_dsp_load) assembles this file
; and checks pump.c's words against it. Relative branches only, so the words are the same anywhere.

stub:
        move    x:>$123456,a            ; LO
        cmp     #>$c0ffee,a
        bne     out
        move    x:>$234567,a            ; HI
        cmp     #>$c0ffee,a
        bne     out
        move    #>$345678,r0            ; CODE
        nop
        jmp     (r0)
out:
        clr     a
        move    a,x:>$033da3            ; BOUND: the slot is never called again
        rts
