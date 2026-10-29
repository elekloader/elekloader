| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 1.1 and core-dn2 0.3: firmware locations as exports, so a mod that
| names these instead of addresses builds for both units (and each OS the
| cores port to) unchanged. Each target's mod.json gives the values (defsym
| FW_*); the C declarations are elekloader/sdk/include/mk2/core.h. The values
| below are the Digitakt II 1.17's, then the Digitone II 1.12's.
|
| The firmware returns a pointer in d0, where GCC's m68k ABI reads a pointer
| result from a0: declare a routine that returns a pointer as returning a
| number (uintptr_t), or its result is a stale a0.
|
| Drawing (the bitmap's y counts from the bottom):
|   fw_fillrect     0x401131ae 0x40114b22  fillrect(bmp, x0, y0, x1, y1, colour): 1 set, 0 clear
|   fw_textf        0x40113dc8 0x4011573c  textf(bmp, font, x, y, flags, fmt, ...): y the text's
|                                           bottom row; flags bit 1 centres on x
|   fw_textbox      0x40113fd4 0x40115948  textbox(bmp, font, x, y, align, deco, template, fmt, ...):
|                                           align 2 right; a menu row's value is drawn with it
|   fw_font         0x44d2e70c 0x44508ef8  the main font object (capitals 5 pixels high)
|   fw_font_narrow  0x44d2e6f4 0x44508ee0  the narrow one (3-pixel letters): lists, page boxes
|   fw_font_menu    0x402a9be0 0x40294710  the menus' value font
|   fw_invalidate   0x4011b126 0x4011ca9a  View::invalidate(view): redraw (a menu: menu + 0x38)
| Keys, knobs, LEDs:
|   fw_key_down     0x4011e150 0x4011fac4  int key_down(code): bit 0 while the key is down
|   fw_led_set      0x400308e0 0x4002c318  set(mgr, logical, layer, value): layer 2 lights with
|                                           value (its high nibble a colour), layer 1 turns off
|   fw_enc_init     0x40111b68 0x401134dc  the encoder filter's block (252 bytes), constructed
|   fw_enc_filter   0x40111cda 0x4011364e  filter(block, knob, count, cfg, pushed) -> steps
|   fw_enc_list     0x40111e42 0x401137b6  list_filter(block, event, cfg) -> steps (x8 pushed)
|   fw_enc_param    0x44d2e654 0x44508e40  cfg: the 0-127 parameters, 256 a step (x2 pushed)
|   fw_enc_depth    0x44d2e670 0x44508e5c  cfg: the modulation depths, 1/256 steps (x12 pushed)
|   fw_enc_steps    0x44d2e600 0x44508dec  cfg: a list's steps (MODULATION SETUP's destinations)
| The header, views, strings:
|   fw_ui_states    0x401998c0 0x4018b2a4  -> UIStates (d0): its message, a std::string, at +72
|   fw_toast        0x4005b23a 0x400562e6  toast(ui, fmt, ...): the header's message, as a page's title
|   fw_brain        0x40198c24 0x4018cbbc  -> the Brain (d0): its view controller at +0x40
|   fw_view_push    0x4011c08a 0x4011d9fe  push(Brain + 0x40, &shared_ptr<View>, 0)
|   fw_name_view    0x40117c70 0x401195e4  NameView(view, &title, &name, 0, len, 0, &done): the
|                                           naming screen; done a std::function<void(std::string)>
|   fw_op_new       0x4011ebd0 0x40120544  operator new(bytes) -> d0
|   fw_str_new      0x401e7e7a 0x401ceab8  std::string(&str, cstr, &alloc): one pointer to the chars
|   fw_str_free     0x401e63ca 0x401cd008  ~std::string(&str)
|   fw_sp_release   0x40197d26 0x401883ac  a shared_ptr's release(&control block)
|   fw_sp_inplace   0x401ef190 0x401e0fd8  the vtable of make_shared's control block (view after it)
|   fw_vt_main      0x40216f14 0x402008e8  the top view's vtable on the main screen and the pages
|   fw_vt_func      0x401f9208 0x401e1eb4  ... while FUNC is held (QuickMuteMenuView)
|   fw_vt_trk       0x401f9ec8 0x401e2b68  ... while TRK is held (TrackSelectionView)
| The card and memory:
|   fw_card_read    0x4012e1b6 0x4012c87a  int read(sector, bytes, buf), 0 = ok (eMMC blocks)
|   fw_card_write   0x4012e39c 0x4012ca60  int write(sector, bytes, buf), 0 = ok
|   fw_memcpy       0x40136388 0x40134770  memcpy(dst, src, n) (a kit's size: core_kit_copied)
| The project in RAM (core_kit_on() gives the kit on):
|   fw_project      0x41776c9c 0x41219970  the loaded project
|   fw_patterns     0x41776cb8 0x4121998c  its 128 patterns: stride fw_pattern_size
|   fw_kits         0x426532b8 0x4210d08c  its kits, kit n pattern n's: stride fw_kit_size

        .macro  FW name, value
        .globl  \name
        .set    \name, \value
        .endm

        FW      fw_fillrect, FW_FILLRECT
        FW      fw_textf, FW_TEXTF
        FW      fw_textbox, FW_TEXTBOX
        FW      fw_font, FW_FONT
        FW      fw_font_narrow, FW_FONT_NARROW
        FW      fw_font_menu, FW_FONT_MENU
        FW      fw_invalidate, FW_INVALIDATE
        FW      fw_key_down, FW_KEY_DOWN
        FW      fw_led_set, FW_LED_SET
        FW      fw_enc_init, FW_ENC_INIT
        FW      fw_enc_filter, FW_ENC_FILTER
        FW      fw_enc_list, FW_ENC_LIST
        FW      fw_enc_param, FW_ENC_PARAM
        FW      fw_enc_depth, FW_ENC_DEPTH
        FW      fw_enc_steps, FW_ENC_STEPS
        FW      fw_ui_states, FW_UI_STATES
        FW      fw_toast, FW_TOAST
        FW      fw_brain, FW_BRAIN
        FW      fw_view_push, FW_VIEW_PUSH
        FW      fw_name_view, FW_NAME_VIEW
        FW      fw_op_new, OP_NEW
        FW      fw_str_new, FW_STR_NEW
        FW      fw_str_free, FW_STR_FREE
        FW      fw_sp_release, FW_SP_RELEASE
        FW      fw_sp_inplace, FW_SP_INPLACE
        FW      fw_vt_main, FW_VT_MAIN
        FW      fw_vt_func, FW_VT_FUNC
        FW      fw_vt_trk, FW_VT_TRK
        FW      fw_card_read, FW_CARD_READ
        FW      fw_card_write, FW_CARD_WRITE
        FW      fw_memcpy, MEMCPY
        FW      fw_project, FW_PROJECT
        FW      fw_patterns, FW_PATTERNS
        FW      fw_kits, FW_KITS
        FW      fw_pattern_size, FW_PATTERN_SIZE
        FW      fw_kit_size, KIT_SIZE
