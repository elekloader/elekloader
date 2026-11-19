| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.16: the sample-layer skip (SKIPS.md,
| "The sample-layer skip (0.16)"). Every address is OS 1.74's.
|
| 0x4011c6b8, called once per render block, runs the sample layer of all 8
| voice slots (struct 0x80005b30 + 0x5c x slot): for each, a 64-step
| resample from the slot's DMA fetch, a 2:1 filter (FIR history +0x2c..+0x4b,
| IIR history +0x4c..+0x5b) and an output stage writing 32 longs at
| 0x800067f8 + 0x80 x slot, every block, playing or not. A slot that is not
| playing (+0x28 0) fetches from a zero buffer, so with its filter history at
| 0 every one of those steps gives exactly 0 and leaves the history at 0.
| The skip leaves those steps out for such a slot and writes what they would
| have: the phase words, the gain word, an output of zeros. The sound is
| unchanged, bit for bit.
|
| Two sites in the slot loop. Live at both: %a3 the slot's struct, the
| loop's frame at (%sp) (0x58: the slot); stock writes every data register
| and %a1/%a2 after the first, before reading them.
|
|   0x4011cc08 (h1), `move.l 0x24(%a3),%d0; lea (%a0,%d0.l*2),%a0`, after
|     the slot's fetch is in and before the resample. Checks the slot; to
|     skip, writes the phase words and goes on at 0x4011ccc2 (the wait for
|     the next slot's fetch and its start, which must run).
|   0x4011cd46 (h2), `lea 0x2c(%a3),%a0; move.l %a0,-(%sp)`, before the
|     filter. For a skipped slot, writes the gain word and the zeros and goes
|     on at 0x4011cf5e (the next slot).
|
| Off (core_sample_skip_want 0, the boot's value, or core_voice_skip_hold
| above 0) both sites are stock's instructions and a few tests.

        .equ    MASK_NOW, 0x8000586c    | this block's trig bits, by slot
        .equ    MASK_LAST, 0x80005868   | last block's (the setup acts on these)
        .equ    OWNERS, 0x40291138      | + slot: the owner track
        .equ    SOUNDS, 0x8000f7ca      | + track x 0x54: +0x0c loop, +0x0e level
        .equ    OUT, 0x800067f8         | + slot x 0x80: 32 longs, read by the mixer
        .equ    AFTER_RESAMPLE, 0x4011ccc2
        .equ    NEXT_SLOT, 0x4011cf5e

        .text
        .globl  core_sample_h1, core_sample_h2
        .align  2

| %d1, %d2, %d3, %d5 and %a1 are free here (stock writes them before it reads
| them); %d0 and %a0 are stock's.
core_sample_h1:
        tst.b   core_sample_skip_want
        beq.w   9f
        tst.l   core_voice_skip_hold
        bne.w   9f
        tst.b   0x28(%a3)               | playing: its fetch is the sample's
        bne.w   9f
        move.l  0x58+4(%sp),%d1         | the slot
        move.l  MASK_NOW,%d2
        or.l    MASK_LAST,%d2
        btst    %d1,%d2                 | a trig on it this block or the last
        bne.w   9f
        move.l  %macsr,%d2
        andi.l  #0xf0,%d2
        moveq   #0x20,%d3               | fractional, no saturation, truncating:
        cmp.l   %d3,%d2                 | the mode stock's steps run in
        bne.w   9f
        move.l  0x2c(%a3),%d2           | the filter history, all 12 longs, 0
        or.l    0x30(%a3),%d2
        or.l    0x34(%a3),%d2
        or.l    0x38(%a3),%d2
        or.l    0x3c(%a3),%d2
        or.l    0x40(%a3),%d2
        or.l    0x44(%a3),%d2
        or.l    0x48(%a3),%d2
        or.l    0x4c(%a3),%d2
        or.l    0x50(%a3),%d2
        or.l    0x54(%a3),%d2
        or.l    0x58(%a3),%d2
        bne.w   9f

| skipped: the resample's phase, as 0x4011cc1a..0x4011ccbe leaves it, which
| adds 64 x (rate << 4) to (0 : +0x1c) in 64 bits: rate << 10, with its high
| word rate asr 22 (the rate +0x0c is already negated for reverse)
        move.l  0x0c(%a3),%d2
        move.l  %d2,%d3
        moveq   #22,%d5
        asr.l   %d5,%d3                 | high
        lsl.l   #8,%d2
        lsl.l   #2,%d2                  | low
        move.l  0x1c(%a3),%d5
        clr.l   %d1
        add.l   %d2,%d5
        addx.l  %d1,%d3
        move.l  %d5,0x1c(%a3)           | fractional phase
        move.l  %d3,0x20(%a3)           | steps this block
        add.l   %d3,0x04(%a3)           | position
        moveq   #1,%d1
        move.b  %d1,core_sample_skip_now
        addq.l  #1,core_sample_skips
        addq.l  #4,%sp
        jmp     AFTER_RESAMPLE

9:      clr.b   core_sample_skip_now
        move.l  0x24(%a3),%d0
        lea     (%a0,%d0.l*2),%a0
        rts

core_sample_h2:
        tst.b   core_sample_skip_now
        bne.s   1f
        lea     0x2c(%a3),%a0           | stock's two instructions, the push
        move.l  (%sp),-(%sp)            | under the return address
        move.l  %a0,4(%sp)
        rts

| skipped: the gain word as 0x4011ce5e..0x4011ceb8 writes it for a slot not
| playing (+0x28 0, so no trig mask): level^2 x 2, or while the slot fades
| (+0x29) without loop, the last gain asr 2
1:      addq.l  #4,%sp
        move.l  0x58(%sp),%d0           | the slot
        lea     OWNERS,%a0
        mvz.b   (%a0,%d0.l),%d1
        moveq   #0x54,%d2
        muls.l  %d2,%d1
        movea.l %d1,%a1
        adda.l  #SOUNDS,%a1
        mvs.w   0x0e(%a1),%d5
        muls.l  %d5,%d5
        add.l   %d5,%d5
        tst.b   0x29(%a3)
        beq.s   2f
        tst.b   0x0c(%a1)
        bne.s   2f
        move.l  0x10(%a3),%d5
        asr.l   #2,%d5
2:      move.l  %d5,0x10(%a3)
| the output, which stock overwrites with zeros
        lsl.l   #7,%d0
        movea.l %d0,%a6
        adda.l  #OUT,%a6
        moveq   #8,%d1
3:      clr.l   (%a6)+
        clr.l   (%a6)+
        clr.l   (%a6)+
        clr.l   (%a6)+
        subq.l  #1,%d1
        bne.s   3b
| the accumulators as stock leaves them, and %a0/%d6 as its last movclr
        movclr.l %acc0,%d1
        movclr.l %acc1,%d1
        movclr.l %acc2,%d1
        movclr.l %acc3,%d1
        suba.l  %a0,%a0
        moveq   #0,%d6
        jmp     NEXT_SLOT

        .bss
        .balign 4
        .globl  core_sample_skip_want, core_sample_skips
core_sample_skips:
        .space  4
core_sample_skip_now:
        .space  1
core_sample_skip_want:
        .space  1
