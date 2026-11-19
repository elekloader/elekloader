| SPDX-License-Identifier: GPL-2.0-or-later
| int32_t fc_pitch_word(uint32_t u, int32_t c0, int32_t c1)
| Stock 1.56's oscillator frame word for pitch state u and calibration pair
| (c0, c1), with stock's own EMAC instruction (msac.l %a0,%d2,%acc0) in the
| same registers, so the result follows the MACSR stock runs with. The
| assembler is run for -mcpu=54455 (no EMAC), so the EMAC words are given as
| data. ACC0 is saved and put back.
	.text
	.globl	fc_pitch_word
	.type	fc_pitch_word, @function
fc_pitch_word:
	move.l	%d2,-(%sp)
	.short	0xa181			| move.l %acc0,%d1 (save)
	move.l	8(%sp),%d2		| u
	lsl.l	#8,%d2
	movea.l	12(%sp),%a0		| c0
	.short	0xa1c0			| movclr.l %acc0,%d0 (clear)
	.short	0xa408,0x0900		| msac.l %a0,%d2,%acc0 (stock's words)
	.short	0xa1c2			| movclr.l %acc0,%d2
	sub.l	16(%sp),%d2		| - c1
	addi.l	#0x80,%d2
	asr.l	#8,%d2
	neg.l	%d2
	move.l	%d2,%d0
	.short	0xa101			| move.l %d1,%acc0 (restore)
	move.l	(%sp)+,%d2
	rts
	.size	fc_pitch_word, .-fc_pitch_word
