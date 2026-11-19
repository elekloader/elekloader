| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.16: a submenu from a SETTINGS row (analog-rytm-mk2/settings.h).
|
| A row a mod adds in ev_settings (core_additem) opens a menu of its own
| rows with core_submenu_open(menu, sm), from the row's select. The menu is
| stock's own checkbox menu view, the one MIDI CONFIG > SYNC and the +Drive's
| FORMAT are (0x400780e8, the whole screen), so it draws, scrolls, closes on
| NO and goes back to its parent as theirs do. Built the first time a row
| opens it and kept, as stock keeps its submenus, so the cursor stays where
| it was; built again only if the parent is another menu.
|
| struct core_submenu (settings.h):
|   +0  title     C string, the header
|   +4  rows      the rows (core_additem's four callbacks each), in order
|   +8  n         how many
|   +12 icon      the header's icon, a stock bitmap or 0 (none)
|   +16 parent    core's: the menu it was built for
|   +20 view      core's: a shared_ptr<View> to it (pointer, control block)
|   +24 ctrl
|
| The stock routines (OS 1.74):
|   SUBMENU_CTOR  0x400780e8 (this, name, rows reserved, std::string *title,
|                 icon, height, width): the checkbox menu view
|   VIEW_LEVEL    0x40078974 (view, level): its depth, one more than its
|                 parent's (GLOBAL 2, its submenus 3, theirs 4)
|   VIEW_SHARED   0x4007762c (parent, view), result through a0: the
|                 shared_ptr, with the parent kept to go back to
|   MENU_OPEN     0x40077a16 (parent, shared_ptr *): shows it, as stock's
|                 GLOBAL rows do (0x400dae7a)
|   SP_RELEASE    0x40170bd8 (&control block): drops one reference

        .section .run, "ax"
        .globl  core_submenu_open

SM_TITLE  = 0
SM_ROWS   = 4
SM_N      = 8
SM_ICON   = 12
SM_PARENT = 16
SM_VIEW   = 20
SM_CTRL   = 24

VIEW_SIZE = 0x1a0                       | stock's are 0x1b0..0x1b4 with members of their own; the base ends at 0x194

core_submenu_open:
        link    %a6, #-8                | -4: the title's std::string; -5: its allocator byte
        lea     -20(%sp), %sp
        movem.l %d2-%d3/%a2-%a4, (%sp)
        movea.l 8(%a6), %a2             | the parent menu
        movea.l 12(%a6), %a3            | the submenu
        cmpa.l  SM_PARENT(%a3), %a2
        bne.s   1f
        tst.l   SM_VIEW(%a3)
        bne     9f                      | built for this parent: show it again
1:      tst.l   SM_CTRL(%a3)            | built for another: let that one go
        beq.s   2f
        clr.l   SM_VIEW(%a3)
        pea     SM_CTRL(%a3)
        jsr     SP_RELEASE
        addq.l  #4, %sp
        clr.l   SM_CTRL(%a3)
2:      move.l  %a2, SM_PARENT(%a3)
        pea     VIEW_SIZE.w
        jsr     OP_NEW
        addq.l  #4, %sp
        move.l  %d0, %d2                | the view
        beq     8f

        pea     -5(%a6)
        move.l  SM_TITLE(%a3), -(%sp)
        pea     -4(%a6)
        jsr     STR_FROM_CSTR
        lea     12(%sp), %sp
        pea     0x80.w                  | width
        pea     0x40.w                  | height
        move.l  SM_ICON(%a3), -(%sp)
        pea     -4(%a6)                 | the title (copied)
        move.l  SM_N(%a3), -(%sp)       | rows reserved
        pea     view_name
        move.l  %d2, -(%sp)
        jsr     SUBMENU_CTOR
        lea     28(%sp), %sp
        pea     -4(%a6)
        jsr     STR_DTOR
        addq.l  #4, %sp

        | its depth: one more than the parent's last (stock lists one)
        moveq   #3, %d0                 | GLOBAL's submenus' depth, if the parent lists none
        movea.l 0x38+0x1c(%a2), %a0     | the parent's levels: begin
        movea.l 0x38+0x20(%a2), %a1     | end
        cmpa.l  %a0, %a1
        beq.s   3f
        move.l  -4(%a1), %d0
        addq.l  #1, %d0
3:      move.l  %d0, -(%sp)
        move.l  %d2, %d0
        addi.l  #0x38, %d0
        move.l  %d0, -(%sp)
        jsr     VIEW_LEVEL
        addq.l  #8, %sp

        movea.l SM_ROWS(%a3), %a4
        move.l  SM_N(%a3), %d3
        bra.s   5f
4:      move.l  (%a4)+, -(%sp)          | the row
        move.l  %d2, -(%sp)             | the submenu: its rows' payload
        jsr     core_additem
        addq.l  #8, %sp
5:      subq.l  #1, %d3
        bge.s   4b

        move.l  %d2, %d0                | shared_ptr to its View part (+0x38)
        addi.l  #0x38, %d0
        move.l  %d0, -(%sp)
        move.l  %a2, -(%sp)
        lea     SM_VIEW(%a3), %a0
        jsr     VIEW_SHARED
        addq.l  #8, %sp

9:      pea     SM_VIEW(%a3)
        move.l  %a2, -(%sp)
        jsr     MENU_OPEN
        addq.l  #8, %sp
8:      movem.l -28(%a6), %d2-%d3/%a2-%a4
        unlk    %a6
        rts

view_name:
        .asciz  "ModMenuView"
        .balign 2
