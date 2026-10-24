| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4 0.2: parameter pages and parameter overrides for mods, core-ar2
| 0.4's (mods/core-ar2/pages.s) at the Analog Four MKII's routines (OS 1.56).
|
| A parameter page goes through four stock routines: page_info(page), its
| entry {name, ids A..J}; page_get_value(view, id, lock), a knob's value in
| 8.8; param_apply_delta(view, id, delta, func), a turn; and
| param_value_text(obj, id, value, buf), the value's text. Core takes the
| first instructions of all four and hands them on through two tables:
|
|   core_pages   a page of the mod's own: {page, name, ids A..J}. page_info
|                answers its entry for that page id (stock's are 0..47).
|   core_params  {first id, last id, get, delta, text}: answers for the ids
|                in the range, the mod's own pages' knobs or stock ones.
|
| C convention throughout (analog-four-mk2/pages.h), as on the Rytm:
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
| core_view_pages is the A4's own (below): each of its pages is a view of
| its own, and the page keys step through lists of views.
|
| Each stub is a jmp site's displaced instructions, reimplemented; the resume
| addresses come from mod.json's defsym (OS 1.56).
        .text
        .globl  core_page_info, core_param_get, core_param_delta, core_param_text
        .align  2

        .equ    P_FIRST, 0
        .equ    P_LAST, 4
        .equ    P_GET, 8
        .equ    P_DELTA, 12
        .equ    P_TEXT, 16

| page_info (0x400f6212): `moveq #47,%d1; movel %sp@(4),%d0`. A page in
| core_pages answers its entry, after the page id.
core_page_info:
        move.l  4(%sp),%d0
        lea     core_pages,%a1
1:      move.l  (%a1)+,%d1
        beq.s   9f
        move.l  %d1,%a0
        cmp.l   (%a0),%d0
        bne.s   1b
        addq.l  #4,%d1
        move.l  %d1,%d0
        rts
9:      moveq   #47,%d1
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

| page_get_value (0x40023d54): `lea %sp@(-12),%sp; moveml %d2-%d3/%a2,%sp@`.
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

| param_apply_delta (0x400232e2): `lea %sp@(-12),%sp; moveml %d2-%d3/%a2,%sp@`.
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

| param_value_text (0x4009ea68): `movel %d3,%sp@-; movel %d2,%sp@-;
| movel %sp@(20),%d2` (the Rytm's opens with lea and moveml). Frame: 0 the
| cursor, 8 obj, 12 id, 16 value, 20 buf; the handler gets the address of
| obj, the four as a struct core_text.
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
        move.l  %d3,-(%sp)
        move.l  %d2,-(%sp)
        move.l  20(%sp),%d2
        jmp     TEXT_RET


| Views behind the page keys (core_view_pages). Each A4 page is a view of
| its own: the main screen's constructor (0x400c1366) makes 29 of them,
| indices 0..28, and each page key steps through a list of indices kept for
| each (track type, key). An entry is {type, key, page, shown}: core makes a
| parameter page view for the page (a mod's own, from core_pages, or a stock
| one), as stock makes its last ones, gives it the next index from 29 on,
| and appends the index to that key's list, after stock's views and the
| entries before it. Stock's loop after the hook then gives every listed
| view its key, place and length. At most VIEW_MAX views; the rest are
| dropped. The current view's index is a signed byte of the project
| settings, kept 0..28 by the setter (0x400add08) and the loader
| (0x4000a6d2, which takes anything above to 0): mod.json's byte sites widen
| both to 28 + VIEW_MAX. An index with no view falls back as stock's do
| (0x400c0470), so a project saved with a view a build no longer has opens
| on MIXER.
        .equ    VIEW_MAX, 16
        .equ    VIEW_FIRST, 29
        .equ    V_TYPE, 0
        .equ    V_KEY, 4
        .equ    V_PAGE, 8
        .equ    V_SHOWN, 12

| The hook (0x400c2370): `lea %sp@(12),%sp; moveal %fp@(8),%a0`, after view
| 28's calls and before the loop. this is at 8(%fp); d0, d1 and a1 are free.
| Locals: 0 the shared pointer {obj, ctrl}, 8 the index, 12 the type, 16
| the key.
        .globl  core_views
core_views:
        lea     12(%sp),%sp
        lea     -40(%sp),%sp
        movem.l %d2-%d7/%a2-%a5,(%sp)
        lea     -20(%sp),%sp
        move.l  8(%fp),%a3
        lea     core_view_pages,%a4
        lea     view_ent,%a5
        moveq   #VIEW_FIRST,%d6
1:      move.l  (%a4)+,%d0
        beq.w   9f
        cmp.l   #VIEW_FIRST + VIEW_MAX,%d6
        bge.w   9f
        move.l  %d0,%a2
        pea     0x1c4                     | the view, as stock's: new(0x1c4)
        jsr     OP_NEW
        addq.l  #4,%sp
        move.l  %d0,%d7
        move.l  V_KEY(%a2),-(%sp)         | ParameterPageView(obj, page, key)
        move.l  V_PAGE(%a2),-(%sp)
        move.l  %d7,-(%sp)
        jsr     VIEW_CTOR
        lea     12(%sp),%sp
        move.l  %d7,-(%sp)                | the shared pointer
        pea     4(%sp)
        jsr     SP_MAKE
        addq.l  #8,%sp
        move.l  %d6,8(%sp)                | views[index] = it
        pea     8(%sp)
        pea     0x1bc(%a3)
        jsr     MAP_SLOT
        addq.l  #8,%sp
        pea     (%sp)
        move.l  %d0,-(%sp)
        jsr     SP_MOVE
        addq.l  #8,%sp
        pea     4(%sp)
        jsr     SP_RELEASE
        addq.l  #4,%sp
        move.l  V_TYPE(%a2),12(%sp)       | the key's list for the track type
        pea     12(%sp)
        pea     0x1a4(%a3)
        jsr     TYPE_MAP
        addq.l  #8,%sp
        move.l  V_KEY(%a2),16(%sp)
        pea     16(%sp)
        move.l  %d0,-(%sp)
        jsr     KEY_MAP
        addq.l  #8,%sp
        move.l  %d0,%a0                   | push_back(index), as stock's own
        move.l  4(%a0),%a1
        cmp.l   8(%a0),%a1
        beq.s   2f
        move.l  %d6,(%a1)+
        move.l  %a1,4(%a0)
        bra.s   3f
2:      pea     8(%sp)
        move.l  %a0,-(%sp)
        jsr     VEC_PUSH
        addq.l  #8,%sp
3:      move.l  %a2,(%a5)+                | the view's entry and object
        move.l  %d7,VIEW_MAX * 4 - 4(%a5)
        addq.l  #1,%d6
        bra.w   1b
9:      sub.l   #VIEW_FIRST,%d6
        move.l  %d6,view_n
        lea     20(%sp),%sp
        movem.l (%sp),%d2-%d7/%a2-%a5
        lea     40(%sp),%sp
        move.l  8(%fp),%a0
        jmp     VIEWS_RET

| The page key's step (0x400c4202): `movel %a0@,%sp@-; movel %a2,%sp@-;
| jsr 0x400bea38` (pc-relative), the view's selection. a0 is the next
| entry of the key's list, a4 the list {begin, end, cap}, d0 the current
| view's entry (or end). A core view whose shown answers 0 is stepped over,
| wrapping at the end; when every entry is, the view stays. d2, a2, a3 and
| a5 are stock's and kept.
        .globl  core_cycle
core_cycle:
        tst.l   view_n
        beq.s   8f
        lea     -12(%sp),%sp
        movem.l %d3-%d5,(%sp)
        move.l  %d0,%d3                   | the current entry
        move.l  %a0,%d5                   | the candidate
        move.l  4(%a4),%d4
        sub.l   (%a4),%d4
        asr.l   #2,%d4                    | tries: the list's length
1:      move.l  %d5,%a0
        move.l  (%a0),%d0
        sub.l   #VIEW_FIRST,%d0
        blt.s   7f                        | stock's: always shown
        cmp.l   view_n,%d0
        bge.s   7f
        lsl.l   #2,%d0
        lea     view_ent,%a1
        move.l  (%a1,%d0.l),%a1
        move.l  V_SHOWN(%a1),%d1
        beq.s   7f
        lea     view_obj,%a1
        move.l  (%a1,%d0.l),-(%sp)
        move.l  %d1,%a1
        jsr     (%a1)
        addq.l  #4,%sp
        tst.l   %d0
        bne.s   7f
        addq.l  #4,%d5                    | hidden: the next, wrapping
        cmp.l   4(%a4),%d5
        bne.s   2f
        move.l  (%a4),%d5
2:      subq.l  #1,%d4
        bgt.s   1b
        move.l  %d3,%d5                   | none shown: stay
        cmp.l   4(%a4),%d5
        bne.s   7f
        move.l  (%a4),%d5
7:      move.l  %d5,%a0
        movem.l (%sp),%d3-%d5
        lea     12(%sp),%sp
8:      move.l  (%a0),-(%sp)
        move.l  %a2,-(%sp)
        jsr     SET_VIEW
        jmp     CYCLE_RET

        .bss
        .align  2
view_n:
        .space  4
view_ent:
        .space  VIEW_MAX * 4              | each core view's entry
view_obj:
        .space  VIEW_MAX * 4              | and its object
