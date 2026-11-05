| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 1.1: firmware routines as symbols, for core's own C (rows.c) and
| for ev_key_leds handlers. OS 1.17's addresses are in mod.json (defsym).
|
|   fw_led_set      0x400308e0  set(mgr, logical, layer, value): layer 2 lights
|                               with value (its high nibble a colour), layer 1
|                               turns off; for ev_key_leds (leds.s)
|   fw_invalidate   0x4011b126  View::invalidate(view): redraw (a menu: menu + 0x38)
|   fw_textbox      0x40113fd4  textbox(bmp, font, x, y, align, deco, template, fmt, ...):
|                               align 2 right; a menu row's value is drawn with it
|   fw_font_menu    0x402a9be0  the menus' value font
|   fw_op_new       0x4011ebd0  operator new(bytes) -> d0
|
| The firmware returns a pointer in d0, where GCC's m68k ABI reads a pointer
| result from a0: declare a routine that returns a pointer as returning a
| number (uintptr_t), or its result is a stale a0.

        .macro  FW name, value
        .globl  \name
        .set    \name, \value
        .endm

        FW      fw_led_set, FW_LED_SET
        FW      fw_invalidate, FW_INVALIDATE
        FW      fw_textbox, FW_TEXTBOX
        FW      fw_font_menu, FW_FONT_MENU
        FW      fw_op_new, OP_NEW
