| SPDX-License-Identifier: GPL-2.0-or-later
| core 4.1 (Digitakt mk1): the master output and the master pages. Three
| things that mods which work on the whole mix all need, and which only one
| of them could hook for itself before: the master pair before it goes out
| (ev_render_master), pages of their own among the master pages FUNC+LFO
| shows (core_mpages), and rows of their own in SETTINGS > GLOBAL FX/MIX
| (ev_fxmix). Last in sources, so the code before it keeps its place. The
| addresses in the comments are OS 1.53's; the code takes them from
| mod.json's defsym, one set per OS:
|   MASTER_PAIR  the block's master pair (SRAM)
|   MV_KEY       the master view's key handler (its vtable's slot 2)
|   MV_DRAW      its draw (slot 4)
|   MV_TITLE     its title (slot 19)
|   MV_ENC       its knob handler, as its +4 subobject's vtable has it
|   MV_NAMES     the page kind 0's short and long name (two char pointers)
|   FXMIX_RET    where the GLOBAL FX/MIX item builder goes on after its site

        .section .run, "ax"
        .balign 4                       | after 4.0's .run, which it leaves as it was

| ---- ev_render_master: the master pair, before it goes out ----------------
| At 0x400721e6 (was: move.l #0x8000ea70,d4), the first instruction of the
| stage that spreads the master pair over the outputs: from there it goes to
| the converters for the main outputs and the headphones, and into the
| 12-channel bus the USB stream is built from. Handlers:
|   void f(int32_t *pair)
| pair is this block's master: 32 frames of {L, R}, 32-bit samples, which a
| handler may change in place. Every output then carries the change, and the
| scope, spectrum and tuner, which read the block further on, show it too.
| In the audio render, at interrupt level, once a block (1500 a second):
| keep it short. Handlers run in their order, each on what the one before it
| left. The stage's registers are live here (d0-d2 its mix coefficients, d3,
| a2, a3): core keeps d0-d1/a0-a2 around the handlers, which keep the rest
| as C does. With none: four instructions more than stock (the jsr, a test,
| a branch and the rts).
        .globl  core_render_master
core_render_master:
        tst.l   ev_render_master
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_render_master, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        pea     MASTER_PAIR
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      move.l  #MASTER_PAIR, %d4       | the instruction this replaced (and its CCR)
        rts

| ---- core_mpages: pages of the master view (FUNC+LFO) ---------------------
| The master view (its vtable at 0x401845dc) keeps the kinds of the pages it
| shows in a std::vector<int> at +124 (+128 its end, +132 its capacity) and
| the place of the one it shows at +144; FUNC+LFO steps through them, and
| the title counts them ("Compressor (1/3)"). Its stock
| kinds are 11 COMP, 12 internal mixer and 13 external mixer. Kind 0, the
| firmware's unused "None" page, has no knobs and no drawing of its own, so
| a mod page is a kind-0 entry in that list, told apart by its place.
|
| A mod adds a page by contributing to the table core_mpages a pointer to
| its descriptor (in .data: core writes its last two words):
|   +0  after   the stock kind it follows (11, 12 or 13; any other: last).
|               Pages that follow the same kind keep the table's order
|   +4  short   its short name, and +8 title its title: "EQ", "Master EQ"
|   +12 draw    void draw(void *view, void *bmp), or 0: after the stock
|               draw has drawn the screen as for kind 0 (the title bar, rows
|               53-63, and below it eight empty knob boxes, which a page
|               clears where it draws); bmp is the page's Bitmap, 128 x 64,
|               y 0 the bottom row
|   +16 key     int key(void *view, void *event), or 0: the view's keys
|               while the page is shown (event +12 the key's id, +16 its
|               flags, bit 0 pressed); nonzero takes it, 0 leaves it to the
|               stock handler
|   +20 enc     int enc(void *view, void *event), or 0: the view's knobs
|               (event +12 knob 1-8 = A-H, 9 LEVEL; +16 the turn, 16 a
|               notch; +20 set while the knob is pushed); nonzero takes it
|   +24 view    core writes the view whose list has the page
|   +28 pos     core writes its place in that list
| The first time the view is drawn with a list that has no kind 0 in it,
| core puts the pages in (and again if the firmware ever rebuilds the list),
| keeping the page shown where it was. While one is shown, core gives kind 0
| its names, for the title bar and the title pop-up, and calls its draw, key
| and enc. core_mpage_shown(view) says which page is shown, for a mod that
| reads the keys or knobs itself (an ev_key or ev_enc handler), and
| core_mview is the master view, core_mview_draws a count of its draws: a
| tick that sees the count move knows the view is on the screen. All of it
| runs in the UI task. With the table empty, each of the view's four
| entries goes straight to the stock one.
        .equ    MP_AFTER,  0
        .equ    MP_SHORT,  4
        .equ    MP_TITLE,  8
        .equ    MP_DRAW,   12
        .equ    MP_KEY,    16
        .equ    MP_ENC,    20
        .equ    MP_VIEW,   24
        .equ    MP_POS,    28
        .equ    MV_LIST,   124
        .equ    MV_END,    128
        .equ    MV_CAP,    132
        .equ    MV_CUR,    144
        .equ    MV_MAX,    32           | the longest list core makes

| struct core_mpage *core_mpage_shown(void *view): the mod page the view
| shows, or 0 (a stock page, or none). In d0 and a0.
        .globl  core_mpage_shown
core_mpage_shown:
        movea.l 4(%sp), %a0
        move.l  MV_CUR(%a0), %d0
        bmi.s   8f
        movea.l MV_LIST(%a0), %a1
        move.l  MV_END(%a0), %d1
        sub.l   %a1, %d1
        asr.l   #2, %d1
        cmp.l   %d1, %d0
        bge.s   8f
        tst.l   (%a1, %d0.l*4)          | kind 0: a mod's page
        bne.s   8f
        move.l  %a2, -(%sp)
        lea     core_mpages, %a1
1:      move.l  (%a1)+, %d1
        beq.s   7f
        movea.l %d1, %a2
        cmpa.l  MP_VIEW(%a2), %a0
        bne.s   1b
        cmp.l   MP_POS(%a2), %d0
        bne.s   1b
        movea.l %a2, %a0
        move.l  %a2, %d0
        movea.l (%sp)+, %a2
        rts
7:      movea.l (%sp)+, %a2
8:      moveq   #0, %d0
        movea.l %d0, %a0
        rts

| core_mpage_names(view): kind 0's names, the shown mod page's.
core_mpage_names:
        move.l  4(%sp), -(%sp)
        bsr.w   core_mpage_shown
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1f
        lea     MV_NAMES, %a1
        move.l  MP_SHORT(%a0), (%a1)+
        move.l  MP_TITLE(%a0), (%a1)
1:      rts

| core_mpage_add(view): the mod pages into a list that has none yet.
core_mpage_add:
        lea     -24(%sp), %sp
        movem.l %d2-%d5/%a2-%a3, (%sp)
        movea.l 28(%sp), %a2            | the view
        movea.l MV_LIST(%a2), %a0
        move.l  MV_END(%a2), %d2
        sub.l   %a0, %d2
        asr.l   #2, %d2                 | d2 = n, the pages it has
        ble.w   9f
        moveq   #0, %d0
1:      tst.l   (%a0, %d0.l*4)          | a kind 0 in it: done before
        beq.w   9f
        addq.l  #1, %d0
        cmp.l   %d2, %d0
        blt.s   1b
        lea     core_mpages, %a1        | d3 = m, the mod pages; their places unset
        moveq   #0, %d3
2:      move.l  (%a1)+, %d0
        beq.s   3f
        movea.l %d0, %a0
        moveq   #-1, %d1
        move.l  %d1, MP_POS(%a0)
        addq.l  #1, %d3
        bra.s   2b
3:      tst.l   %d3
        beq.s   9f
        move.l  %d2, %d0
        add.l   %d3, %d0
        cmpi.l  #MV_MAX, %d0
        bgt.s   9f
        asl.l   #2, %d0
        move.l  %d0, -(%sp)
        jsr     OP_NEW
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   9f
        movea.l %d0, %a3                | the new list
        moveq   #0, %d4                 | the old place i
        moveq   #0, %d1                 | the new place j
        moveq   #0, %d5                 | the page shown, at its new place
4:      cmp.l   MV_CUR(%a2), %d4
        bne.s   5f
        move.l  %d1, %d5
5:      movea.l MV_LIST(%a2), %a0
        move.l  (%a0, %d4.l*4), %d0     | the stock page ...
        move.l  %d0, (%a3, %d1.l*4)
        addq.l  #1, %d1
        bsr.s   20f                     | ... and the mod pages that follow it
        addq.l  #1, %d4
        cmp.l   %d2, %d4
        blt.s   4b
        moveq   #-1, %d0                | the pages whose kind it lacks: last
        bsr.s   20f
        move.l  %a3, MV_LIST(%a2)
        lea     (%a3, %d1.l*4), %a0
        move.l  %a0, MV_END(%a2)
        move.l  %a0, MV_CAP(%a2)
        move.l  %d5, MV_CUR(%a2)
        | The old list is left alone: the firmware's operator delete stops on
        | memory it did not hand out, and the list may be such.
9:      movem.l (%sp), %d2-%d5/%a2-%a3
        lea     24(%sp), %sp
        rts
| Each page not placed yet whose after is d0 (-1: any) goes in at j (d1),
| with the view (a2) and its place. Keeps d0, d2-d5, a2, a3.
20:     lea     core_mpages, %a1
21:     tst.l   (%a1)
        beq.s   24f
        movea.l (%a1)+, %a0
        tst.l   MP_POS(%a0)
        bpl.s   21b                     | placed already
        tst.l   %d0
        bmi.s   22f                     | -1: any
        cmp.l   MP_AFTER(%a0), %d0
        bne.s   21b
22:     move.l  %a2, MP_VIEW(%a0)
        move.l  %d1, MP_POS(%a0)
        clr.l   (%a3, %d1.l*4)          | kind 0
        addq.l  #1, %d1
        bra.s   21b
24:     rts

| ---- the master view's entries (pointers in its vtables) ------------------
| Draw, at 0x401845ec (slot 4, was 0x400395dc): draw(view, bmp).
        .globl  core_mview_draw
core_mview_draw:
        tst.l   core_mpages
        beq.s   9f
        lea     core_mview, %a1
        move.l  4(%sp), (%a1)+
        addq.l  #1, (%a1)               | core_mview_draws
        move.l  4(%sp), -(%sp)
        bsr.w   core_mpage_add
        bsr.w   core_mpage_names
        addq.l  #4, %sp
        move.l  8(%sp), -(%sp)          | the stock draw first
        move.l  8(%sp), -(%sp)
        jsr     MV_DRAW
        addq.l  #8, %sp
        move.l  4(%sp), -(%sp)
        bsr.w   core_mpage_shown
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1f
        move.l  MP_DRAW(%a0), %d0
        beq.s   1f
        movea.l %d0, %a0
        move.l  8(%sp), -(%sp)          | then the page's
        move.l  8(%sp), -(%sp)
        jsr     (%a0)
        addq.l  #8, %sp
1:      rts
9:      jmp     MV_DRAW

| Title, at 0x40184628 (slot 19, was 0x40030930): std::string title(view),
| built where a0 points. Kind 0's names first, then the stock getter.
        .globl  core_mview_title
core_mview_title:
        tst.l   core_mpages
        beq.s   9f
        move.l  %a0, -(%sp)
        move.l  8(%sp), -(%sp)
        bsr.w   core_mpage_names
        addq.l  #4, %sp
        movea.l (%sp)+, %a0
9:      jmp     MV_TITLE

| Keys, at 0x401845e4 (slot 2, was 0x40038b36): int key(view, event).
        .globl  core_mview_key
core_mview_key:
        move.l  4(%sp), -(%sp)
        bsr.w   core_mpage_shown
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   9f
        move.l  MP_KEY(%a0), %d0
        beq.s   9f
        movea.l %d0, %a0
        move.l  8(%sp), -(%sp)
        move.l  8(%sp), -(%sp)
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   1f
9:      jmp     MV_KEY
1:      rts

| Knobs, at 0x401846bc (the +4 subobject's knob listener, was 0x400388ee:
| this -= 4, then the view's handler): int enc(view + 4, event).
        .globl  core_mview_enc
core_mview_enc:
        move.l  4(%sp), %d0
        subq.l  #4, %d0                 | the view
        move.l  %d0, -(%sp)
        bsr.w   core_mpage_shown
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   9f
        move.l  MP_ENC(%a0), %d0
        beq.s   9f
        movea.l %d0, %a0
        move.l  8(%sp), -(%sp)          | the event
        move.l  8(%sp), %d0
        subq.l  #4, %d0
        move.l  %d0, -(%sp)             | the view
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   1f
9:      jmp     MV_ENC
1:      rts

| ---- ev_fxmix: the SETTINGS > GLOBAL FX/MIX menu's rows -------------------
| At 0x40044acc in its item builder (was: movea.l (a4),a0 ; pea 2.w), by
| jmp, after the last row and before the selection is restored; a4 = the
| menu. Handlers: f(menu), which add rows with core_additem(menu, row), as
| ev_settings's do. The UI task.
        .globl  core_fxmix
core_fxmix:
        move.l  %a3, -(%sp)
        lea     ev_fxmix, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  %a4, -(%sp)             | the menu
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movea.l (%sp)+, %a3
        movea.l (%a4), %a0              | the instructions this replaced
        pea     2.w
        jmp     FXMIX_RET

        .section .bss
        .balign 4
        .globl  core_mview, core_mview_draws
core_mview:       .skip 4               | the master view, once drawn with a page in core_mpages
core_mview_draws: .skip 4               | its draws since
