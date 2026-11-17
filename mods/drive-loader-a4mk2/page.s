| SPDX-License-Identifier: GPL-2.0-or-later
| drive-loader (Analog Four MKII, OS 1.56): the MODS row's label and icon
| (page.c, dl_settings), the two of its callbacks that can't be C.
|
| The label: stock's MenuItem calls a label as a std::function returning a
| std::string: the string to build is at %a0 (the hidden result), and %d0
| returns %a0. std::string(%a0, text, allocator) is stock's 0x4018471e, as
| the A4's own SETTINGS labels and core-a4mk2's core_setting_label build
| theirs.

        .text
        .align  2
        .globl  dl_mods_label, dl_mods_icon

dl_mods_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        move.l  %a0, %d2
        pea     -1(%a6)                 | the allocator, a byte on the frame
        pea     dl_mods_name
        move.l  %d2, -(%sp)
        jsr     0x4018471e
        lea     12(%sp), %sp
        move.l  %d2, %d0
        movea.l %d2, %a0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts

| The MODS row's icon: the row's draw callback, as stock's SETTINGS > GLOBAL
| rows have it (SYSTEM's is at 0x400cf0f4), called with (functor, item,
| canvas, x, y): a thunk that moves its arguments down and tail-calls
| stock's bitmap blit 0x4005aed4 (canvas, bitmap, x, y, centred = 0). The
| Bitmap is stock's object layout (vptr 0x401973f4, +4 width, +8 height,
| +0xc words a column, +0x10 pixels, +0x14 mask, +0x18 owns = 0): 9 x 9,
| one column a long, the top row the lowest bit of the field; the same
| lightning bolt as drive-loader-ar2's MODS row, the elekloader web icon's.
dl_mods_icon:
        move.l  12(%sp), %d0
        move.l  %d0, 4(%sp)
        move.l  16(%sp), %d0
        move.l  %d0, 12(%sp)
        move.l  20(%sp), %d0
        clr.l   20(%sp)
        move.l  %d0, 16(%sp)
        move.l  #dl_icon_bitmap, %d0
        move.l  %d0, 8(%sp)
        jmp     0x4005aed4

        .align  2
dl_icon_bitmap:
        .long   0x401973f4
        .long   9, 9, 1
        .long   dl_icon_pixels, dl_icon_mask
        .long   0

dl_icon_mask:
        .rept   9
        .long   0xff800000
        .endr
dl_icon_pixels:
        .long   0x00000000
        .long   0x00000000
        .long   0x08000000
        .long   0x0e000000
        .long   0x7f000000
        .long   0x38000000
        .long   0x08000000
        .long   0x00000000
        .long   0x00000000

dl_mods_name:
        .asciz  "MODS"
