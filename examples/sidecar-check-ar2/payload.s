| SPDX-License-Identifier: GPL-2.0-or-later
| sidecar-check (Analog Rytm MKII), part 6: a 1 MB payload of a known
| pattern (README): 4,096 blocks of 64 words. Block i is the word
| i ^ 0x5041594c ('PAYL'), the next 48 words of a xorshift32 stream seeded
| with 0x5041594c, then the 15 words of payload.c's table. Partly random
| on purpose: the main OS is unpacked in place, so a payload that packs
| very small overtakes its own input (README, "Payloads").

        .ifndef BLOCKS
        .equ    BLOCKS, 4096                    | 1 MB; mod.json's defsym may set fewer
        .endif
        .equ    RANDOM, 48

        .macro  table
        .long   0xe15d9d01
        .long   0x8c73fe28
        .long   0x8e85bde5
        .long   0x459dd5a5
        .long   0x4bda3694
        .long   0x4a421a4e
        .long   0x32edf4c4
        .long   0xf2fd80c7
        .long   0xfad6503f
        .long   0x0ea70bdd
        .long   0x22906a7a
        .long   0xe152535d
        .long   0x8144ee59
        .long   0xc5b3986e
        .long   0x15f6f9c3
        .endm

        .section .payload, "a"
        .align  4
        .globl  sc6_payload
sc6_payload:
        .set    i, 0
        .set    x, 0x5041594c
        .rept   BLOCKS
        .long   i ^ 0x5041594c
        .rept   RANDOM
        .set    x, (x ^ (x << 13)) & 0xffffffff
        .set    x, x ^ (x >> 17)
        .set    x, (x ^ (x << 5)) & 0xffffffff
        .long   x
        .endr
        table
        .set    i, i + 1
        .endr
        .globl  sc6_payload_end
sc6_payload_end:
