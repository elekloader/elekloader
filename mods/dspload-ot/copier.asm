; SPDX-License-Identifier: GPL-2.0-or-later
; DSPLOAD's copier, DSP core 1 (payload B), at the head of every audio frame (ev_dsp_b, P:0x76).
; After sambanks/octabam's DLCOPY (MIT, Sam Banks: modules/dlcopy/dlcopy.asm), cut down to one
; message kind: a block of code for a slot, which it copies into the shared window and binds.
;
; Staging, in this core's X, written by the ColdFire's host transfer (16 bits a word: the byte
; above them is whatever the port last held, so every word is read through a mask):
;   x:$2900  doorbell, written last: $a55a when the block is complete
;   x:$2901  the slot k (0..3)          x:$2902  the destination, an offset into the window
;   x:$2903  the number of code words N x:$2904  the entry, an offset from the destination
;   x:$2905  2 N + 2 words: each 24-bit code word as its high 16 bits, then its low 8; the last
;            pair is the sum of the N words, modulo 2^24, in the same form
; The slots, in the window (one memory for both cores), x:$33da3 + 2 k:
;   +0 BOUND  $b0a5ed while the code is resident   +1 the entry's address
; BOUND is cleared before any word of the code changes and set last, so core 0's dispatcher sees
; the code whole or not at all. A block the slot already holds (BOUND, the same entry) is consumed
; without a copy: the ColdFire sends every block three times, and core 0 may be running it.
; A block whose sum does not match is not bound.
;
; Registers: a, x0, x1, r0, r1, and b kept in x:$28f0..$28f2 (the stock code after the hook site
; writes the others before it reads them, as DLCOPY measured).

dlb_copy:
        move    x:>$2900,a
        and     #>$ffff,a
        cmp     #>$a55a,a
        bne     dlb_exit
        move    #>$2901,r0              ; the header words, cleaned the same way
        do      #4,dlb_hdr
        move    x:(r0),a
        and     #>$ffff,a
        move    a1,x:(r0)+
dlb_hdr:
        move    x:>$2901,a              ; the slot
        asl     a
        move    #>$33da3,x0
        add     x0,a
        move    a1,r1
        move    x:>$2902,a              ; the destination
        move    #>$30000,x0
        add     x0,a
        move    a1,r0
        move    x:>$2904,x0
        add     x0,a
        move    a1,x1                   ; the entry's address
        move    x:(r1),a                ; a repeat of what the slot holds
        cmp     #>$b0a5ed,a
        bne     dlb_fresh
        move    x:(r1+1),a
        cmp     x1,a
        beq     dlb_done
dlb_fresh:
        clr     a
        move    a,x:(r1)                ; unbound while its words change
        move    x1,x:>$28f3
        move    b2,x:>$28f0             ; b is the running sum
        move    b1,x:>$28f1
        move    b0,x:>$28f2
        clr     b
        move    x:>$2903,x0
        move    #>$2905,r1
        do      x0,dlb_words
        move    x:(r1)+,a               ; the high 16 bits: the shift moves the byte above them into a2
        asl     #$8,a,a
        move    a1,x1
        move    x:(r1)+,a
        and     #>$ff,a
        add     x1,a
        add     a,b
        move    a1,x0
        move    x0,p:(r0)+
dlb_words:
        move    x:(r1)+,a               ; the staged sum
        asl     #$8,a,a
        move    a1,x1
        move    x:(r1)+,a
        and     #>$ff,a
        add     x1,a
        move    b1,x0
        move    x:>$28f0,b2
        move    x:>$28f1,b1
        move    x:>$28f2,b0
        sub     x0,a
        move    a1,a
        tst     a
        bne     dlb_done                ; not bound: a later send replaces it
        move    x:>$2901,a              ; the slot again
        asl     a
        move    #>$33da3,x0
        add     x0,a
        move    a1,r1
        move    x:>$28f3,x1
        move    x1,x:(r1+1)             ; the entry
        move    #>$b0a5ed,x0
        move    x0,x:(r1)               ; BOUND, last
dlb_done:
        clr     a
        move    a,x:>$2900              ; consumed
dlb_exit:
        rts
