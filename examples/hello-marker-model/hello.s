| SPDX-License-Identifier: GPL-2.0-or-later
| hello-marker for Model:Cycles and Model:Samples (OS 1.13): the smallest mod
| that runs code on them. It subscribes to core's ev_draw (mc/mod.json,
| ms/mod.json): after the firmware has drawn each frame, core calls
| hello_draw(bmp, ctrl), and this fills a 3 x 3 square in a right-hand
| corner of the 128 x 64 screen.
|
|   FILL    the firmware's fill(bmp, x0, y0, x1, y1, colour, step): every
|           step-th row from y0 to y1, x0..x1 each, clipped to the bitmap;
|           colour 0 clears, 1 sets, negative inverts
|
| No gcc is needed: the device's toolchain is binutils (m68k-elf-as).

        .section .run, "ax"
        .globl  hello_draw
hello_draw:
        pea     1.w                     | step
        pea     1.w                     | colour: set
        pea     2.w                     | y1
        pea     127.w                   | x1
        clr.l   -(%sp)                  | y0
        pea     125.w                   | x0
        move.l  28(%sp), -(%sp)         | bmp
        jsr     FILL
        lea     28(%sp), %sp
        rts
