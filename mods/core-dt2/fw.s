| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 1.1 and 1.2: firmware routines as exports. Shared with core-dn2,
| which defines its own addresses in its mod.json; a block whose defsym a
| device lacks is left out.
|
| 1.1 (Digitakt II only), for core's own C (rows.c) and ev_key_leds handlers:
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
|
| 1.2:
| elekloader/sdk/include/digitakt-mk2/core.h and digitone-mk2/core.h declare
| them.
|
| The UI (its task):                                  DT2 1.17     DN2 1.12
|   fw_fillrect   fillrect(bmp, x0, y0, x1, y1, colour) 0x401131ae 0x40114b22
|   fw_textf      textf(bmp, font, x, y, flags, fmt, ...) -> the advance: printf
|                 text (128 bytes); flags 0 plain (bit 1 centre, 2 right-align;
|                 not the mk1's maxlen)           0x40113dc8   0x4011573c
|   fw_font5      the stock small font (the page views' own)
|                                                 0x44d2e70c   0x44508ef8
|   fw_blit       blit(dst, src, x, y, centre)    0x40114168   0x40115adc
|   fw_op_new     operator new(size) -> d0        0x4011ebd0   0x40120544
|   fw_invalidate invalidate(view): drawn again   0x4011b126   0x4011ca9a
| The Digitakt II's +Drive file system (FS_OPEN and the rest), from
| ev_fs_ready on. A task, never an interrupt; its mutex (0x46f4deb0) is
| recursive. Paths from "/", at most 255 bytes. A handle is four words {node,
| position, writing, open}, as on the mk1; set the position word to seek.
|   fw_fs_open    0x40158902  open(path, mode, handle[4]) -> 0 or -1: mode "r", or "w"
|                             (the file made if it is not there; never truncated)
|   fw_fs_read    0x4015860a  read(buf, len, handle) -> bytes read, or -1
|   fw_fs_write   0x4015866a  write(buf, len, handle) -> len, or -1 (grows the file)
|   fw_fs_close   0x401588c0  close(handle) -> 0
|   fw_fs_unlink  0x40157078  unlink(path) -> 0 or -1 (files only, not open)
|   fw_fs_mkdir   0x4015746e  mkdir(path) -> 0 or -1 (there already, too)
|   fw_fs_rename  0x40156f6c  rename(from, to) -> 0 or -1
| The card below the projects' store, 512-byte sectors (CARD_READ and
| CARD_WRITE: the Digitone II's), from ev_card_ready on, from a task:
|   fw_card_read  0x4012c87a  read(sector, bytes, buf) -> 0, or < 0
|   fw_card_write 0x4012ca60  write(sector, bytes, buf) -> 0, or < 0
| fw_sysex_send is core-dn2's sysex.s's, which both cores build.

        .macro  FW name, value
        .globl  \name
        .set    \name, \value
        .endm

        .ifdef  FW_LED_SET
        FW      fw_led_set, FW_LED_SET
        FW      fw_textbox, FW_TEXTBOX
        FW      fw_font_menu, FW_FONT_MENU
        .endif
        FW      fw_fillrect, FW_FILLRECT
        FW      fw_textf, FW_TEXTF
        FW      fw_font5, FW_FONT5
        FW      fw_blit, FW_BLIT
        FW      fw_op_new, OP_NEW
        FW      fw_invalidate, FW_INVALIDATE
        .ifdef  FS_OPEN
        FW      fw_fs_open, FS_OPEN
        FW      fw_fs_read, FS_READ
        FW      fw_fs_write, FS_WRITE
        FW      fw_fs_close, FS_CLOSE
        FW      fw_fs_unlink, FS_UNLINK
        FW      fw_fs_mkdir, FS_MKDIR
        FW      fw_fs_rename, FS_RENAME
        .endif
        .ifdef  CARD_READ
        FW      fw_card_read, CARD_READ
        FW      fw_card_write, CARD_WRITE
        .endif
