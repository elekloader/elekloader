        .ifdef CORE_NATIVE_UI
/* SPDX-License-Identifier: GPL-2.0-or-later */
        .section .run,"ax"
        .balign 4,0
/* Adapt the existing name callback's D0 pointer result to an integer C call. */
        .globl core_param_name_call
core_param_name_call:
        movea.l 4(%sp),%a0
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr (%a0)
        addq.l #8,%sp
        rts

/* Native std::string long_name(set,id), hidden result in A0, D0 return.
 * Match4001fb92's callee-saved/allocator ABI and use the stock constructor. */
        .macro PARAM_NAME name,field
        .globl \name
\name:
        link.w %fp,#-4
        move.l %d2,-(%sp)
        move.l %a0,%d2
        pea \field
        move.l 12(%fp),-(%sp)
        jsr core_param_name
        addq.l #8,%sp
        pea -1(%fp)
        move.l %d0,-(%sp)
        move.l %d2,-(%sp)
        jsr fw_string_ctor
        lea 12(%sp),%sp
        move.l %d2,%d0
        move.l -8(%fp),%d2
        unlk %fp
        rts

        .endm
        PARAM_NAME core_param_long_name,0x28.w
        PARAM_NAME core_param_short_native,0x30.w

/* Notification value call at40045674. FM retains the original formatter. */
        .globl core_param_notification_value
core_param_notification_value:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_param_value_text
        addq.l #8,%sp
        tst.l %d0
        bne.s 1f
        jmp fw_param_value_text
1:      rts

/* Replace two native pushes with a JSR. Leave the same two stack words
 * and restore every register; the synthetic return occupies the new gap. */
        .macro LFO_NAME name,index,extra,mode=0
        .globl \name
\name:
        lea -24(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l 24(%sp),16(%sp)
        moveq #\mode,%d0
        move.l %d0,-(%sp)
        move.l \index,-(%sp)
        jsr core_param_lfo_name
        addq.l #8,%sp
        move.l %d0,24(%sp)
        .ifc \extra,%d0
        move.l (%sp),20(%sp)
        .else
        move.l \extra,20(%sp)
        .endif
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        rts
        .endm
        LFO_NAME core_param_lfo_list_name,%d1,%d6
        LFO_NAME core_param_lfo_value_name,%d2,%d0

        LFO_NAME core_param_lfo_list_fallback,%d2,%d6,2

/* The compact DEST control's original short-name and format pushes. */
        .globl core_param_lfo_knob_name
core_param_lfo_knob_name:
        lea -24(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l 24(%sp),16(%sp)
        pea 1.w
        move.l %d2,-(%sp)
        jsr core_param_lfo_name
        addq.l #8,%sp
        move.l %d0,24(%sp)
        move.l #fw_lfo_name_format,%d0
        move.l %d0,20(%sp)
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        rts

/* Preserve the native hidden-result ABI and filter only its fresh UI vector. */
        .globl core_param_lfo_choices
core_param_lfo_choices:
        subq.l #4,%sp
        move.l %a2,(%sp)
        move.l %a0,%a2
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr fw_lfo_choices
        addq.l #8,%sp
        move.l %a2,-(%sp)
        jsr core_param_lfo_filter
        addq.l #4,%sp
        move.l %a2,%d0
        move.l (%sp),%a2
        addq.l #4,%sp
        rts

/* Common return from native relative stepping and UI-specific stepping.
 * Normalize only user edits before the native apply/set path. */
        .globl core_param_lfo_step_bridge
core_param_lfo_step_bridge:
        move.l %d3,-(%sp)
        move.l %d0,-(%sp)
        move.l %d2,-(%sp)
        move.l %a2,-(%sp)
        jsr core_param_lfo_step
        lea 16(%sp),%sp
        mvzb %d6,%d6
        mvzb %d5,%d5
        move.l (%sp),24(%sp)
        lea 24(%sp),%sp
        rts
        .endif
