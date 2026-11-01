| SPDX-License-Identifier: GPL-2.0-or-later
| The machine menu's filter tab: the sites and calls menu.c cannot make in C.
        .section .run, "ax"

| The view's constructor, at its one call (was: jsr 0x4002a736), by keep2: a
| menu opens on its machines.
        .globl  mp_menu_open
mp_menu_open:
        clr.l   mp_menu_filter
        clr.l   mp_menu_back
        jmp     MENU_OPEN_FN

| FUNC+SRC in the page views' key handler (was: clr.l -(sp) ; pea 1.w, the
| page step's arguments, at 0x4002eb92), by jmp, a2 the views' owner.
        .globl  mp_menu_src_key_s
mp_menu_src_key_s:
        move.l  %a2, -(%sp)
        jsr     mp_menu_src_key
        addq.l  #4, %sp
        clr.l   -(%sp)
        pea     1.w
        jmp     MENU_SRC_KEY_ON

| The panel beside the list, in place of its wave, rules and MACHINE (was:
| the first of those calls' arguments, at 0x4002b4ba), by jmp, d2 the screen.
        .globl  mp_menu_panel_s
mp_menu_panel_s:
        move.l  %d2, -(%sp)
        jsr     mp_menu_panel
        addq.l  #4, %sp
        jmp     MENU_PANEL_DONE

| A row's id for the draw's "the current one" compare (was: jsr 0x400c3fb8, a2
| the view), by keep2: the stock filter is always the current one.
        .globl  mp_menu_row_id
mp_menu_row_id:
        tst.l   mp_menu_filter
        beq.s   1f
        move.l  416(%a2), %d0
        rts
1:      jmp     MENU_ROW_ID

| mp_menu_list(&vector, track): the firmware's list builder takes its result
| in a0.
        .globl  mp_menu_list
mp_menu_list:
        movea.l 4(%sp), %a0
        move.l  8(%sp), -(%sp)
        jsr     MENU_LIST_FN
        addq.l  #4, %sp
        rts

| The step's invoker on the filter tab.
        .globl  mp_menu_no_step
mp_menu_no_step:
        rts
