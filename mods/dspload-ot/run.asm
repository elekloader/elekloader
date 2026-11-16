; SPDX-License-Identifier: GPL-2.0-or-later
; DSPLOAD's dispatcher, DSP core 0 (payload A), on the DSP bus's ev_dsp_rx (the head of every
; audio frame): each slot the copier (copier.asm, core 1) has bound is called as an ev_dsp_rx
; handler. A loaded handler has ev_dsp_rx's contract: it may change a, x1, r0, r1 and the
; condition codes, keeps every other register, and ends with rts. The slots are in the shared
; window, x:$33da3 + 2 k: +0 BOUND ($b0a5ed), +1 the entry.

dla_run:
        move    x:>$33da3,a
        cmp     #>$b0a5ed,a
        bne     dla_one
        move    x:>$33da4,r0
        nop
        jsr     (r0)
dla_one:
        move    x:>$33da5,a
        cmp     #>$b0a5ed,a
        bne     dla_two
        move    x:>$33da6,r0
        nop
        jsr     (r0)
dla_two:
        move    x:>$33da7,a
        cmp     #>$b0a5ed,a
        bne     dla_three
        move    x:>$33da8,r0
        nop
        jsr     (r0)
dla_three:
        move    x:>$33da9,a
        cmp     #>$b0a5ed,a
        bne     dla_end
        move    x:>$33daa,r0
        nop
        jsr     (r0)
dla_end:
        rts
