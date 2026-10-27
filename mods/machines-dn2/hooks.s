| SPDX-License-Identifier: GPL-2.0-or-later
    .text
    .globl dn2_stock_names, dn2_stock_permit, dn2_stock_descriptors
    .globl dn2_unknown_name, dn2_unknown_label
    .set dn2_stock_names, STOCK_NAMES
    .set dn2_stock_permit, STOCK_PERMIT
    .set dn2_stock_descriptors, STOCK_DESCRIPTORS
    .set dn2_unknown_name, UNKNOWN_NAME
    .set dn2_unknown_label, UNKNOWN_LABEL
    .globl dn2_stock_filter_names, dn2_stock_filter_descriptors, dn2_pixel, dn2_shown
    .set dn2_pixel, PIXEL
    .set dn2_shown, SHOWN
    .set dn2_stock_filter_names, FILTER_NAMES
    .set dn2_stock_filter_descriptors, FILTER_DESCRIPTORS

    .macro SAVE
    lea -16(%sp),%sp
    movem.l %d0-%d1/%a0-%a1,(%sp)
    .endm
    .macro RESTORE
    movem.l (%sp),%d0-%d1/%a0-%a1
    lea 16(%sp),%sp
    .endm

| The cached vector has exactly the registered entries, without unused ID slots.
    .globl dn2_list_alloc
dn2_list_alloc:
    SAVE
    jsr dn2_ensure
    RESTORE
    move.l dn2_list_bytes,-(%sp)
    clr.l CACHE_BEGIN
    jmp LIST_ALLOC_CONTINUE

    .globl dn2_list_end
dn2_list_end:
    subq.l #4,%sp
    SAVE
    move.l dn2_list_bytes,%d0
    add.l #dn2_list,%d0
    move.l %d0,16(%sp)
    RESTORE
    adda.l dn2_list_bytes,%a0
    jmp LIST_END_CONTINUE

    .globl dn2_group
dn2_group:
    cmpi.l #5,%d0
    bcs.s 1f
    cmpi.l #32,%d0
    bcc.s 1f
    moveq #0,%d0
1:  moveq #3,%d1
    cmp.l %d0,%d1
    bge.s 2f
    jmp GROUP_CONTINUE
2:  jmp GROUP_SYNTH

| Map only the stock function's own argument slot. Keep the live page/slot registers.
    .macro MAP off,fn=dn2_host_type
    SAVE
    move.l (16+\off,%sp),-(%sp)
    jsr \fn
    move.l %d0,(20+\off,%sp)
    addq.l #4,%sp
    RESTORE
    .endm

    .globl dn2_page_count
dn2_page_count:
    SAVE
    move.l 20(%sp),-(%sp)
    jsr dn2_voice_page_count
    addq.l #4,%sp
    tst.l %d0
    beq.s 1f
    move.l %d0,(%sp)
    RESTORE
    rts
1:  RESTORE
    MAP 4
    moveq #4,%d1
    move.l 4(%sp),%d0
    jmp PAGE_COUNT_CONTINUE

    .globl dn2_page
dn2_page:
    SAVE
    move.l 28(%sp),-(%sp)
    move.l 28(%sp),-(%sp)
    jsr dn2_voice_page
    addq.l #8,%sp
    tst.l %d0
    beq.s 1f
    move.l %d0,(%sp)
    RESTORE
    move.l (%sp)+,%d2
    rts
1:  RESTORE
    MAP 8
    move.l 8(%sp),%d1
    move.l 12(%sp),%d0
    jmp PAGE_CONTINUE

    .globl dn2_parameter
dn2_parameter:
    SAVE
    move.l 28(%sp),-(%sp)
    jsr dn2_is_voice
    addq.l #4,%sp
    tst.l %d0
    beq.s 1f
    RESTORE
    move.l %a0,%d0
    subi.l #25,%d0
    cmpi.l #39,%d0
    bhi.s 2f
    SAVE
    move.l %a0,-(%sp)
    move.l 32(%sp),-(%sp)
    jsr dn2_voice_code
    addq.l #8,%sp
    move.l %d0,(%sp)
    RESTORE
    move.l (%sp)+,%d2
    rts
1:  RESTORE
2:  SAVE                         | a custom filter's G: a slot its host has no row for
    move.l 32(%sp),-(%sp)
    move.l %a0,-(%sp)
    jsr dn2_filter_code
    addq.l #8,%sp
    tst.l %d0
    beq.s 3f
    move.l %d0,(%sp)
    RESTORE
    move.l (%sp)+,%d2
    rts
3:  RESTORE
    MAP 12
    MAP 16,dn2_filter_host_type
    move.l 12(%sp),%d0
    movea.l 16(%sp),%a1
    jmp PARAM_CONTINUE

| The stock CC and NRPN lookups, past the entry dn2_cc_code and dn2_nrpn_code took.
    .globl dn2_stock_cc, dn2_stock_nrpn
dn2_stock_cc:
    lea -12(%sp),%sp
    movem.l %d2-%d4,(%sp)
    jmp CC_CONTINUE
dn2_stock_nrpn:
    lea -12(%sp),%sp
    movem.l %d2-%d4,(%sp)
    jmp NRPN_CONTINUE

| The value-class accessor: an added parameter code uses its template's object.
    .globl dn2_t2
dn2_t2:
    SAVE
    move.l 20(%sp),-(%sp)
    jsr dn2_value_code
    addq.l #4,%sp
    move.l %d0,(%sp)
    RESTORE
    cmpi.l #T1_ROWS,%d0
    jmp T2_CONTINUE

| Audio mirror identity stays real; only the transmitted DSP type is mapped (dn2_dsp_type).
    .globl dn2_frame_type
dn2_frame_type:
    lea -12(%sp),%sp
    movem.l %d1/%a0-%a1,(%sp)
    mvs.b 0xd8c(%a0),%d0
    move.l %d0,-(%sp)
    jsr dn2_dsp_type
    addq.l #4,%sp
    movem.l (%sp),%d1/%a0-%a1
    lea 12(%sp),%sp
    jmp FRAME_CONTINUE

| This entry preserves the original reader for identity callers without copying firmware.
    .globl dn2_real_reader
dn2_real_reader:
    move.l 4(%sp),-(%sp)
    jsr READER_FN
dn2_real_return:
    addq.l #4,%sp
    rts

    .globl dn2_reader
dn2_reader:
    mvs.b 0xde(%a0),%d0
    move.l %d1,-(%sp)
    move.l 8(%sp),%d1
    .irp caller,RC0,RC1,RC2,RC3,RC4,RC5,RC6,RC7,RC8,dn2_real_return
    cmpi.l #\caller,%d1
    beq.w 1f
    .endr
    lea -8(%sp),%sp
    movem.l %a0-%a1,(%sp)
    move.l %d0,-(%sp)
    jsr dn2_is_voice
    tst.l %d0
    beq.s 2f
    move.l (%sp),%d0
    bra.s 3f
2:  jsr dn2_host_type
3:
    addq.l #4,%sp
    movem.l (%sp),%a0-%a1
    lea 8(%sp),%sp
1:  move.l (%sp)+,%d1
    jmp READER_EPILOGUE

    .globl dn2_owner
dn2_owner:
    SAVE
    mvs.b 0xde(%a0),%d0
    move.l %d0,-(%sp)
    jsr dn2_host_type
    addq.l #4,%sp
    move.l %d0,%d3
    RESTORE
    move.l %d2,-(%sp)
    jmp OWNER_CONTINUE

    .globl dn2_filter_list_alloc
dn2_filter_list_alloc:
    SAVE
    jsr dn2_filters_ensure
    RESTORE
    move.l dn2_filter_list_bytes,-(%sp)
    clr.l FILTER_CACHE_BEGIN
    jmp FILTER_LIST_ALLOC_CONTINUE

    .globl dn2_filter_list_end
dn2_filter_list_end:
    subq.l #4,%sp
    SAVE
    move.l dn2_filter_list_bytes,%d0
    add.l #dn2_filter_list,%d0
    move.l %d0,16(%sp)
    RESTORE
    adda.l dn2_filter_list_bytes,%a0
    jmp FILTER_LIST_END_CONTINUE

    .globl dn2_filter_frame_type
dn2_filter_frame_type:
    lea -12(%sp),%sp
    movem.l %d1/%a0-%a1,(%sp)
    mvs.b 0xd8d(%a0),%d0
    move.l %d0,-(%sp)
    jsr dn2_filter_dsp_type
    addq.l #4,%sp
    movem.l (%sp),%d1/%a0-%a1
    lea 12(%sp),%sp
    movea.l %d6,%a0
    jmp FILTER_FRAME_CONTINUE

    .globl dn2_filter_real_reader
dn2_filter_real_reader:
    move.l 4(%sp),-(%sp)
    jsr FILTER_READER_FN
dn2_filter_real_return:
    addq.l #4,%sp
    rts

| The MIDI filter parameter owner reads the sound directly, outside the identity readers. Its
| only callers pass the type to dn2_cc_code and dn2_nrpn_code, which map a custom filter (G).
    .globl dn2_filter_parameter_type
dn2_filter_parameter_type:
    SAVE
    mvs.b 0xdf(%a0),%d0
    move.l %d0,-(%sp)
    jsr dn2_filter_dsp_type
    addq.l #4,%sp
    move.l %d0,(%sp)
    RESTORE
    rts

| Pages, defaults and parameters use the host type; missing IDs use MULTI-MODE's.
| FRC9 and FRC10 are the FLTR page's knob lookups: a custom filter's own descriptor has G.
    .globl dn2_filter_reader
dn2_filter_reader:
    mvs.b 0xdf(%a0),%d0
    move.l %d1,-(%sp)
    move.l 8(%sp),%d1
    .irp caller,FRC0,FRC1,FRC2,FRC3,FRC4,FRC5,FRC6,FRC7,FRC8,FRC9,FRC10,dn2_filter_real_return
    cmpi.l #\caller,%d1
    beq.w 1f
    .endr
    lea -8(%sp),%sp
    movem.l %a0-%a1,(%sp)
    move.l %d0,-(%sp)
    jsr dn2_filter_host_type
    addq.l #4,%sp
    movem.l (%sp),%a0-%a1
    lea 8(%sp),%sp
1:  move.l (%sp)+,%d1
    jmp FILTER_READER_EPILOGUE

| The FLTR page's curve dispatch: stock types keep their curves; others draw a custom filter's own.
| D4 is the saved ID: a missing filter's becomes MULTI-MODE, whose curve it then draws.
    .globl dn2_filter_curve
dn2_filter_curve:
    SAVE
    move.l %d4,-(%sp)
    jsr dn2_filter_dsp_type
    addq.l #4,%sp
    move.l %d0,%d4
    RESTORE
    moveq #5,%d0
    cmp.l %d4,%d0
    bcs.s 1f
    jmp CURVE_STOCK
1:  SAVE
    move.l %a2,-(%sp)
    move.l %d7,-(%sp)
    move.l %d6,-(%sp)
    move.l %d3,-(%sp)
    move.l %d4,-(%sp)
    jsr dn2_filter_draw_curve
    lea 20(%sp),%sp
    RESTORE
    jmp CURVE_SKIP
