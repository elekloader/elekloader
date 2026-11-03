| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2: the audio interrupt's load, for mods with work they can skip.
|
| Core subscribes to its own ev_render_in first (order 0) and ev_render_out
| last (order 100), and reads DMA timer 0's counter (DTCN0, the bus clock,
| which core starts at boot) at both. core_audio_load is the last block's
| time from the one to the other as a share of the time between two blocks'
| entries, in 256ths: 256 is the whole period. The interrupt's prologue and
| epilogue, before and after core's hooks, are not in it, so the real share
| is a little higher. 0 until two blocks have run.
|
| A mod with optional work in the interrupt (a meter, an estimate) skips it
| while the load is high, so the stock audio and the UI keep their time:
|     if (core_audio_load > CORE_AUDIO_BUSY) return;
|
| core_block (0.9) counts the blocks, from 1 at the first: the smoothed
| value (smooth.c) steps its smoothers by it.
        .equ    DTCN0, 0xfc07000c

        .section .run, "ax"
        .globl  core_load_in, core_load_out
core_load_in:
        addq.l  #1, core_block
        move.l  DTCN0, %d0
        move.l  load_t0, %d1
        move.l  %d0, load_t0
        tst.l   %d1
        beq.s   1f                      | the first block: no period yet
        sub.l   %d1, %d0
        move.l  %d0, load_period
1:      rts

core_load_out:
        move.l  load_period, %d1
        beq.s   9f
        move.l  DTCN0, %d0
        sub.l   load_t0, %d0            | this block's time
        cmp.l   %d1, %d0
        bcc.s   1f                      | as long as the period, or longer
        lsl.l   #8, %d0                 | < 2^24 at any rate the OS runs at
        divu.l  %d1, %d0
        move.l  %d0, core_audio_load
        rts
1:      move.l  #256, %d0
        move.l  %d0, core_audio_load
9:      rts

        .bss
        .align  2
        .globl  core_audio_load, core_block
core_audio_load:
        .space  4
core_block:
        .space  4
load_t0:
        .space  4
load_period:
        .space  4
