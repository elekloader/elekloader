; SPDX-License-Identifier: GPL-2.0-or-later
; USB audio in, the DSP side: on the DSP bus's ev_dsp_rx (mods/dspbus-ot), DSP core 0, at the head
; of every frame, before anything reads the current RX block. The host's samples go over the
; inputs' slots of the block; a part the CPU did not send leaves its inputs to the jacks.
;
; The CPU's block (usbin.c, through usb-ot's DSP feed) is at r6 + $320, in the bank this frame works
; in. It is two parts, C/D then A/B: a flag word and, when its bit 0 is set, 16 samples x 2 channels
; as (hi, lo) words, hi = sample[23:8], lo = sample[7:0]. Each word came over the host port as 16
; bits, so its top byte is stale. A clear flag has nothing after it: the next part's flag follows.
;
; The RX block is 16 samples x 4 slots at x:>$202: slots 0/1 are inputs C/D, 2/3 are A/B.
; A sample is joined from its two words with one insert: lo is loaded into a (its bits 7:0 land in
; a1's low byte), then hi's 16 bits go over a1 bits 23:8 (offset 32 of the accumulator, width 16),
; so neither stale byte survives. (sambanks/octabam's rx_inject.asm, MIT, found the bank, the
; slots and that join.)
;
; The bus's contract: a, x1, r0 and r1 may change, nothing else.

usbi_inject:
        move    r6,a
        add     #>$320,a
        move    a,r0                    ; r0: the CPU's block, in this frame's bank
        move    x:>$202,r1              ; r1: the RX block, slot 0 (input C)
        bsr     usbi_part               ; C/D
        lua     (r1+2),r1               ; slot 2 (input A)
        bsr     usbi_part               ; A/B
        rts

usbi_part:
        move    x:(r0)+,x1              ; the part's flag
        jclr    #0,x1,usbi_none        ; bit 0: the stale top byte is not looked at
        do      #16,usbi_end
        move    x:(r0)+,x1              ; the first channel's sample[23:8]
        move    x:(r0)+,a               ; sample[7:0]
        insert  #$010020,x1,a
        move    a1,x:(r1)+              ; slot 0 or 2
        move    x:(r0)+,x1              ; the second channel's
        move    x:(r0)+,a
        insert  #$010020,x1,a
        move    a1,x:(r1)+              ; slot 1 or 3
        lua     (r1+2),r1               ; the next sample's pair
usbi_end:
        lua     (r1-64),r1              ; back to the part's first slot
usbi_none:
        rts
