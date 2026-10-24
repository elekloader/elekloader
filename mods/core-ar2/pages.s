| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.4: parameter pages and parameter overrides for mods.
|
| A parameter page goes through four stock routines: page_info(page), its
| entry {name, ids A..H}; page_get_value(view, id, lock), a knob's value in
| 8.8; param_apply_delta(view, id, delta, func), a turn; and
| param_value_text(obj, id, value, buf), the value's text. Core takes the
| first instructions of all four and hands them on through two tables:
|
|   core_pages   a page of the mod's own: {page, name, ids A..H}. page_info
|                answers its entry for that page id (stock's are 0..11).
|   core_page_fns  (0.6) {page, info}: page_info asks info(page) first and
|                answers the core_pages entry it returns, so a page's knobs
|                can follow the sound (0 passes, to core_pages and stock).
|   core_params  {first id, last id, get, delta, text}: answers for the ids
|                in the range, the mod's own pages' knobs or stock ones.
|
| C convention throughout (analog-rytm-mk2/pages.h):
|   const struct core_page *info(int32_t page)
|       the entry to answer, in d0, or 0 to pass
|   int get(int32_t id, int32_t *value)
|       1 and *value (8.8) when it answers, 0 to pass
|   int delta(void *view, int32_t id, int32_t delta, int32_t func)
|       delta in 8.8, func 1 while FUNC is held; 1 when it took the turn (core
|       then redraws the view, and stock makes no lock), 0 to pass
|   int text(struct core_text *t)
|       t is the routine's own arguments {obj, id, value, buf}: 0 to pass, 1
|       when it wrote buf, 2 when stock should go on with t's id and value,
|       which it may have changed (a page knob shown as a stock one)
| Entries are asked in the table's order; the first answer wins.
|
| Each stub is a jmp site's displaced instructions, reimplemented; the resume
| addresses come from mod.json's defsym (OS 1.74).
        .text
        .globl  core_page_info, core_param_get, core_param_delta, core_param_text
        .align  2

        .equ    P_FIRST, 0
        .equ    P_LAST, 4
        .equ    P_GET, 8
        .equ    P_DELTA, 12
        .equ    P_TEXT, 16

| page_info (0x400ff5e0): `moveq #11,%d1; movel %sp@(4),%d0`. A page in
| core_page_fns answers the entry its info returns, then a page in core_pages
| its entry, after the page id.
core_page_info:
        lea     core_page_fns,%a1
1:      move.l  4(%sp),%d0
        move.l  (%a1)+,%d1
        beq.s   3f
        move.l  %d1,%a0
        cmp.l   (%a0),%d0
        bne.s   1b
        move.l  %a1,-(%sp)                      | the cursor
        move.l  %d0,-(%sp)
        move.l  4(%a0),%a0
        jsr     (%a0)
        addq.l  #4,%sp
        move.l  (%sp)+,%a1
        tst.l   %d0
        beq.s   1b
        addq.l  #4,%d0
        rts
3:      lea     core_pages,%a1
1:      move.l  (%a1)+,%d1
        beq.s   9f
        move.l  %d1,%a0
        cmp.l   (%a0),%d0
        bne.s   1b
        addq.l  #4,%d1
        move.l  %d1,%d0
        rts
9:      moveq   #11,%d1
        jmp     PAGE_INFO_RET

| param_next(d0 = id, a1 = cursor in core_params) -> a0 = the next entry whose
| range holds the id, a1 past it; Z set when there is none. Clobbers d1.
param_next:
1:      move.l  (%a1)+,%d1
        beq.s   9f
        move.l  %d1,%a0
        cmp.l   P_FIRST(%a0),%d0
        blt.s   1b
        cmp.l   P_LAST(%a0),%d0
        bgt.s   1b
        moveq   #1,%d1
9:      rts

| page_get_value (0x40038462): `lea %sp@(-12),%sp; moveml %d2-%d3/%a2,%sp@`.
| Frame: 0 the cursor, 4 the answer, 12 view, 16 id, 20 lock.
core_param_get:
        lea     core_params,%a1
        subq.l  #8,%sp
1:      move.l  16(%sp),%d0
        bsr.w   param_next
        beq.s   9f
        move.l  P_GET(%a0),%d1
        beq.s   1b
        move.l  %a1,(%sp)
        pea     4(%sp)
        move.l  %d0,-(%sp)
        move.l  %d1,%a0
        jsr     (%a0)
        addq.l  #8,%sp
        move.l  (%sp),%a1
        tst.l   %d0
        beq.s   1b
        move.l  4(%sp),%d0
        addq.l  #8,%sp
        rts
9:      addq.l  #8,%sp
        lea     -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp     GET_RET

| param_apply_delta (0x400378a6): `lea %sp@(-12),%sp; moveml %d2-%d3/%a2,%sp@`.
| Frame: 0 the cursor, 8 view, 12 id, 16 delta, 20 func (its low byte).
core_param_delta:
        lea     core_params,%a1
        subq.l  #4,%sp
1:      move.l  12(%sp),%d0
        bsr.w   param_next
        beq.s   9f
        move.l  P_DELTA(%a0),%d1
        beq.s   1b
        move.l  %a1,(%sp)
        mvz.b   23(%sp),%d0
        move.l  %d0,-(%sp)                | func
        move.l  20(%sp),-(%sp)            | delta
        move.l  20(%sp),-(%sp)            | id
        move.l  20(%sp),-(%sp)            | view
        move.l  %d1,%a0
        jsr     (%a0)
        lea     16(%sp),%sp
        move.l  (%sp),%a1
        tst.l   %d0
        beq.s   1b
        move.l  8(%sp),-(%sp)             | taken: redraw the view now
        jsr     VIEW_INVALIDATE
        addq.l  #8,%sp
        rts
9:      addq.l  #4,%sp
        lea     -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp     DELTA_RET

| param_value_text (0x400a864c): `lea %sp@(-20),%sp; moveml
| %d2-%d4/%a2-%a3,%sp@`. Frame: 0 the cursor, 8 obj, 12 id, 16 value, 20 buf;
| the handler gets the address of obj, the four as a struct core_text.
core_param_text:
        lea     core_params,%a1
        subq.l  #4,%sp
1:      move.l  12(%sp),%d0
        bsr.w   param_next
        beq.s   9f
        move.l  P_TEXT(%a0),%d1
        beq.s   1b
        move.l  %a1,(%sp)
        pea     8(%sp)
        move.l  %d1,%a0
        jsr     (%a0)
        addq.l  #4,%sp
        move.l  (%sp),%a1
        subq.l  #1,%d0
        bmi.s   1b                        | 0: not its id
        bne.s   9f                        | 2: stock goes on with t's id and value
        addq.l  #4,%sp                    | 1: the text is written
        rts
9:      addq.l  #4,%sp
        lea     -20(%sp),%sp
        movem.l %d2-%d4/%a2-%a3,(%sp)
        jmp     TEXT_RET

| Page-key lists (core_view_pages). Each view's constructor hands its page
| list to a setter: `moveq #n,%d1; movel %d1,%sp@-; movel #list,%d0;
| movel %d0,%sp@-`. Core takes the first three of those per view and
| hands back a list of its own: stock's pages first, in stock's order, then
| the pages mods add to that view, in core_view_pages' order. The page key
| steps through it as through stock's. Entries are {view, page}, view one of
| CORE_VIEW_* (analog-rytm-mk2/pages.h). Several mods may add pages to one
| view; a view holds at most VIEW_MAX pages and drops the rest. An entry is
| {view, page, shown}: shown, when not 0, is `int shown(void *view)`, asked
| on each press of the page key, and the key steps over the page while it
| answers 0.
        .equ    VIEW_MAX, 16
        .equ    VIEWS, 10
        .equ    VIEW_SHOWN, VIEWS * VIEW_MAX * 4  | each page's shown, after the lists
        .globl  core_view_0, core_view_1, core_view_2, core_view_3, core_view_4
        .globl  core_view_5, core_view_6, core_view_7, core_view_8, core_view_9

        .macro  VIEWSTUB n
core_view_\n:
        moveq   #\n,%d0
        bsr.w   view_list                 | d0 = the list, d1 = its length
        move.l  %d1,-(%sp)
        jmp     VIEW\n\()_RET
        .endm
        VIEWSTUB 0
        VIEWSTUB 1
        VIEWSTUB 2
        VIEWSTUB 3
        VIEWSTUB 4
        VIEWSTUB 5
        VIEWSTUB 6
        VIEWSTUB 7
        VIEWSTUB 8
        VIEWSTUB 9

| view_list(d0 = view) -> d0 = the view's list, d1 = its length. Keeps every
| register but d0, d1, a0 and a1, as the constructor's code around it needs.
view_list:
        lea     -16(%sp),%sp
        movem.l %d2-%d4/%a2,(%sp)
        move.l  %d0,%d2                   | the view
        lea     view_buf,%a2
        move.l  %d2,%d0
        lsl.l   #6,%d0                    | VIEW_MAX longs a view
        add.l   %d0,%a2
        lea     view_stock,%a0
        move.l  %d2,%d0
        lsl.l   #3,%d0
        add.l   %d0,%a0
        move.l  (%a0)+,%a1                | stock's list
        move.l  (%a0),%d3                 | and its length
        lea     VIEW_SHOWN(%a2),%a0
        moveq   #0,%d1
1:      cmp.l   %d3,%d1
        bge.s   2f
        move.l  (%a1)+,%d4
        move.l  %d4,(%a2,%d1.l*4)
        clr.l   (%a0,%d1.l*4)             | stock's pages are always shown
        addq.l  #1,%d1
        bra.s   1b
2:      lea     core_view_pages,%a1
3:      move.l  (%a1)+,%d0
        beq.s   9f
        move.l  %d0,%a0
        cmp.l   (%a0),%d2
        bne.s   3b
        moveq   #VIEW_MAX,%d4
        cmp.l   %d4,%d1
        bge.s   3b                        | full
        move.l  4(%a0),%d4
        move.l  %d4,(%a2,%d1.l*4)
        move.l  8(%a0),%d4
        lea     VIEW_SHOWN(%a2),%a0
        move.l  %d4,(%a0,%d1.l*4)
        addq.l  #1,%d1
        bra.s   3b
9:      lea     view_len,%a0
        move.l  %d1,(%a0,%d2.l*4)
        move.l  %a2,%d0
        movem.l (%sp),%d2-%d4/%a2
        lea     16(%sp),%sp
        rts

| The page key's cycle (core_cycle), over `movel %a2@(140),%d1; addql #1,%d1;
| remsl %d0,%d2:%d1; movel %d2,%a2@(140); movel %a2,%sp@-; jsr VIEW_INVALIDATE`,
| reached when the view a2 lists more than one page, d0 of them. The next
| page whose shown answers non-0 (or has none) is selected; when none does,
| the page stays. The view is invalidated as stock does, a2 left pushed for
| stock's addql #8, and then its new page's title goes to the header, as
| stock's page_view_activate pushes it: stock pushes it before the cycle, so
| the header showed the page left. The view keeps a copy of its list, so
| it is told by its pages: the first of core's lists with the same pages
| (LFO's and FX LFO's are the same while no mod adds to either, and then
| neither has a shown). A view with none of core's lists steps as stock's.
| d3 is the key handler's answer and kept; d2 is free here, as stock
| overwrites it.
        .equ    VIEW_SEL, 140
        .equ    VIEW_PAGES, 124
        .equ    VIEW_UI, 108
        .equ    VT_TITLE, 0x4c
        .globl  core_cycle
core_cycle:
        lea     -16(%sp),%sp
        movem.l %d3-%d5/%a3,(%sp)
        move.l  %d0,%d4                   | the length
        move.l  VIEW_SEL(%a2),%d5
        moveq   #0,%d2                    | which of core's lists the view holds:
        lea     view_len,%a1              | the view keeps a copy, so by its pages
6:      cmp.l   (%a1,%d2.l*4),%d4
        bne.s   8f
        move.l  VIEW_PAGES(%a2),%a0
        lea     view_buf,%a3
        move.l  %d2,%d0
        lsl.l   #6,%d0
        add.l   %d0,%a3
        move.l  %d4,%d1
7:      move.l  (%a0)+,%d0
        cmp.l   (%a3)+,%d0
        bne.s   8f
        subq.l  #1,%d1
        bne.s   7b
        lea     view_buf + VIEW_SHOWN,%a3 | the same pages: that view's shown
        move.l  %d2,%d0
        lsl.l   #6,%d0
        add.l   %d0,%a3
        bra.s   1f
8:      addq.l  #1,%d2
        moveq   #VIEWS,%d0
        cmp.l   %d0,%d2
        blt.s   6b
        sub.l   %a3,%a3                   | none: stock's steps
1:      move.l  %d4,%d3                   | tries
2:      addq.l  #1,%d5
        cmp.l   %d4,%d5
        blt.s   3f
        moveq   #0,%d5
3:      move.l  %a3,%d0
        beq.s   5f
        move.l  (%a3,%d5.l*4),%d0
        beq.s   5f
        move.l  %d0,%a0
        move.l  %a2,-(%sp)
        jsr     (%a0)
        addq.l  #4,%sp
        tst.l   %d0
        bne.s   5f
        subq.l  #1,%d3
        bne.s   2b                        | after d4 tries d5 is the page again
5:      move.l  %d5,VIEW_SEL(%a2)
        movem.l (%sp),%d3-%d5/%a3
        lea     16(%sp),%sp
        move.l  %a2,-(%sp)
        jsr     VIEW_INVALIDATE
        lea     -8(%sp),%sp               | the string at 0, its allocator's byte at 7
        move.l  %a2,-(%sp)
        move.l  (%a2),%a0
        move.l  VT_TITLE(%a0),%a0
        jsr     (%a0)                     | d0 = the title
        addq.l  #4,%sp
        pea     7(%sp)
        move.l  %d0,-(%sp)
        pea     8(%sp)
        jsr     STR_FROM_CSTR
        lea     12(%sp),%sp
        move.l  %sp,-(%sp)
        move.l  VIEW_UI(%a2),-(%sp)
        jsr     UI_SET_TITLE
        addq.l  #8,%sp
        move.l  %sp,-(%sp)
        jsr     STR_DTOR
        lea     12(%sp),%sp               | the string and its argument
        jmp     CYCLE_RET

| stock's list and length, per view (mod.json's defsym)
        .align  2
view_stock:
        .long   VIEW0_LIST, VIEW0_N, VIEW1_LIST, VIEW1_N, VIEW2_LIST, VIEW2_N
        .long   VIEW3_LIST, VIEW3_N, VIEW4_LIST, VIEW4_N, VIEW5_LIST, VIEW5_N
        .long   VIEW6_LIST, VIEW6_N, VIEW7_LIST, VIEW7_N, VIEW8_LIST, VIEW8_N
        .long   VIEW9_LIST, VIEW9_N

        .bss
        .align  2
view_buf:
        .space  VIEWS * VIEW_MAX * 4 * 2  | the lists, then each page's shown
view_len:
        .space  VIEWS * 4
