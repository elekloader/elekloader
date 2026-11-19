| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.16: the UI skip, a memo on stock's __dynamic_cast (libstdc++,
| 0x401b82f2: (src, src_type, dst_type, src2dst) -> the cast pointer or 0).
|
| The UI asks "is view X open?" about twenty times per main-screen refresh
| (0x400c9272 and its finders 0x401a1ed6..0x401a2d02), each by casting every
| entry of the open-view list. The views use multiple inheritance, so each
| cast walks the class tree and compares type names with strcmp: about two
| thirds of the CPU the UI uses outside the audio render.
|
| A cast's answer depends only on the source subobject's vtable pointer (it
| fixes the complete type and where the subobject sits in it), the two types
| and the hint; the cast pointer is the source pointer plus a fixed offset.
| Vtables and type infos are in flash, so the memo's answer is always stock's.
| Construction and destruction vtables are other pointers, so they are other
| keys.
|
| The memo is 256 sets of 8 entries of 16 bytes (32 KB): the vtable pointer, the two
| types, and a word with the hint (high half) and the offset (low half;
| 0x8000 for "no answer"). The idle main screen alone casts with over 200
| keys, so the sets are 8-way: a new answer goes in front of its set and the
| set's oldest drops out. A cast whose hint or offset does not fit 16 bits
| runs stock's and is not kept. Lookup and store run with interrupts masked
| (at most about 80 instructions), so tasks that cast at the same time never
| see half an entry.
|
| On while core_ui_skip_want is nonzero and core_ui_skip_hold is 0
| (analog-rytm-mk2/ui.h); off, the site is stock's cast after two tests.
|
| Site: 0x401b82f2, stock `link.w %a6,#-40; movem.l %d2-%d5/%a2,(%sp)`
| (4e56ffd8 48d7043c), replaced by a jmp here.

        .equ    CAST_REST, 0x401b82fa
        .equ    SETS_LOG, 8
        .equ    WAYS, 8
        .equ    GOLD, 0x9e3779b1
        .equ    NONE, 0x8000            | the low half of a cast with no answer

        .text
        .globl  core_cast_site, core_cast_stock
        .globl  core_ui_skip_hold_add, core_ui_skip_hold_release
        .align  2
core_cast_site:
        tst.b   core_ui_skip_want
        beq.w   9f
        tst.l   core_ui_skip_hold
        bne.w   9f
        lea     -28(%sp),%sp
        movem.l %d2-%d6/%a2-%a3,(%sp)
        | the arguments: 32(%sp) src, 36 src_type, 40 dst_type, 44 src2dst
        move.l  44(%sp),%d2             | the hint must fit 16 bits
        move.w  %d2,%d3
        ext.l   %d3
        cmp.l   %d2,%d3
        bne.w   8f
        swap    %d2
        clr.w   %d2                     | the hint in the high half
        movea.l 32(%sp),%a0
        move.l  (%a0),%d6               | the vtable pointer
        move.l  40(%sp),%d0             | dst_type
        movea.l 36(%sp),%a3             | src_type
        | the set: (dst * GOLD ^ vptr ^ hint) * GOLD, top SETS_LOG bits
        move.l  #GOLD,%d4
        move.l  %d0,%d3
        mulu.l  %d4,%d3
        eor.l   %d6,%d3
        eor.l   %d2,%d3
        mulu.l  %d4,%d3
        moveq   #32-SETS_LOG,%d4
        lsr.l   %d4,%d3
        lsl.l   #7,%d3                  | x 128 bytes a set
        lea     core_cast_memo,%a2
        adda.l  %d3,%a2
        movea.l %a2,%a1
        moveq   #WAYS,%d4
        move.w  %sr,%d3
        move.w  #0x2700,%sr
1:      cmp.l   (%a1),%d6
        bne.s   2f
        cmp.l   8(%a1),%d0
        bne.s   2f
        cmpa.l  4(%a1),%a3
        bne.s   2f
        move.l  12(%a1),%d5
        move.l  %d5,%d1
        clr.w   %d1
        cmp.l   %d2,%d1
        beq.w   3f
2:      lea     16(%a1),%a1
        subq.l  #1,%d4
        bne.s   1b
        | a miss: stock's cast, then keep its answer in front of the set
        addq.l  #1,core_ui_skip_misses
        move.w  %d3,%sr
        move.l  44(%sp),-(%sp)
        move.l  44(%sp),-(%sp)
        move.l  44(%sp),-(%sp)
        move.l  44(%sp),-(%sp)
        bsr.w   9f
        lea     16(%sp),%sp
        move.l  #NONE,%d1
        tst.l   %d0
        beq.s   4f
        move.l  %d0,%d1
        sub.l   32(%sp),%d1             | the offset must fit 16 bits, not NONE
        move.w  %d1,%d5
        ext.l   %d5
        cmp.l   %d1,%d5
        bne.w   7f
        cmpi.l  #-0x8000,%d1
        beq.w   7f
        andi.l  #0xffff,%d1
4:      or.l    %d2,%d1                 | the stored word
        move.w  %sr,%d3
        move.w  #0x2700,%sr
        lea     (WAYS-1)*16(%a2),%a1    | ways 0..6 move down one
        moveq   #WAYS-1,%d4
5:      move.l  -16(%a1),(%a1)
        move.l  -12(%a1),4(%a1)
        move.l  -8(%a1),8(%a1)
        move.l  -4(%a1),12(%a1)
        lea     -16(%a1),%a1
        subq.l  #1,%d4
        bne.s   5b
        move.l  %d6,(%a2)
        move.l  %a3,4(%a2)
        move.l  40(%sp),%d5
        move.l  %d5,8(%a2)
        move.l  %d1,12(%a2)
        move.w  %d3,%sr
        bra.w   7f

        | a hit
3:      addq.l  #1,core_ui_skip_hits
        move.w  %d3,%sr
        ext.l   %d5                     | the offset
        cmpi.l  #-0x8000,%d5
        beq.s   6f
        move.l  %d5,%d0
        add.l   %a0,%d0
        bra.w   7f
6:      moveq   #0,%d0
7:      movem.l (%sp),%d2-%d6/%a2-%a3
        lea     28(%sp),%sp
        rts

        | a hint stock's search takes but the memo can't key: stock's cast
8:      movem.l (%sp),%d2-%d6/%a2-%a3
        lea     28(%sp),%sp
        | fall through

| stock's cast: the two instructions the site replaced, then the rest
core_cast_stock:
9:      link.w  %a6,#-40
        movem.l %d2-%d5/%a2,(%sp)
        jmp     CAST_REST

| a mod that needs stock's exact UI load (one that measures it) holds the
| skip off while it does; one instruction each, so any context may call them
core_ui_skip_hold_add:
        addq.l  #1,core_ui_skip_hold
        rts
core_ui_skip_hold_release:
        subq.l  #1,core_ui_skip_hold
        rts

        .bss
        .align  4
        .globl  core_ui_skip_want, core_ui_skip_hold
        .globl  core_ui_skip_hits, core_ui_skip_misses
core_cast_memo:
        .space  (1 << SETS_LOG) * WAYS * 16
core_ui_skip_hold:
        .space  4
core_ui_skip_hits:
        .space  4
core_ui_skip_misses:
        .space  4
core_ui_skip_want:
        .space  1
