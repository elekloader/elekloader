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
    cmpi.l #128,%d0                 | machines.c COUNT
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
    move.l 20(%sp),%d0
    cmpi.l #SYN_TITLE_RETURN,%d0
    bne.s 3f
    move.l %a2,dn2_syn_view         | the title accessor's live view, not a fixed heap address
3:
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

| The range accessor, {min, max, default} of a parameter code: a0 is where its result goes, 4(sp)
| the code; it returns a0 in d0, as stock does. machines.c's dn2_range gives the shared rows the
| active track's machine's defaults.
    .globl dn2_range_entry
dn2_range_entry:
    move.l %a0,-(%sp)
    move.l 8(%sp),-(%sp)
    move.l %a0,-(%sp)
    jsr dn2_range
    addq.l #8,%sp
    move.l (%sp)+,%d0
    rts

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

| A loaded sound's synth type: stock keeps 0..31 (with the bound machines-dn2 0.1 set), and 0xff;
| 0..127 here (machines.c COUNT): anything else becomes FM TONE, as stock does. The stock code's d1 is
| dead after the type; d2 gets the filter's bound, 7, as it would.
    .globl dn2_load_type
dn2_load_type:
    move.b 0xf4(%a3),%d0
    moveq #0,%d1
    move.b %d0,%d1
    cmpi.l #128,%d1                 | machines.c COUNT
    bcs.s 1f
    cmpi.l #0xff,%d1
    beq.s 1f
    moveq #0,%d0
1:  move.b #7,%d2
    jmp LOAD_TYPE_CONTINUE

| SDK machines (sdk_alpha.c): where OS 1.12 keeps the panel's track and page, the tracks' sounds and
| SYN's page; the bitmap's fill. Found with digikit's emulator and checked on a unit (DigiHealth).
    .globl dn2_active_track, dn2_shown_page, dn2_sounds, dn2_fillrect
    .set dn2_active_track, ACTIVE_TRACK
    .set dn2_shown_page, SHOWN_PAGE
    .set dn2_sounds, SOUNDS
    .set dn2_fillrect, FILLRECT
| The Brain's pointer, which stock reads at 0x4003b7c6 (tst.l): the ev_key/ev_enc brain, already set
| at power-on, so a glyph draws before the first key (checked on a unit with DigiHealth).
    .globl dn2_brain, dn2_syn_methods
    .set dn2_brain, BRAIN
    .set dn2_syn_methods, SYN_METHODS
| TRK SETUP's PLAY MODE and MONO NOTE PRIO (sdk_alpha.c): the owner of the tracks' settings objects, made
| once at boot (0x4018e89e returns it), the object of track t (0x40030e66(*(owner + TRACK_LIST), t)), and
| the menu's own getters and setters, which also tell the voice allocator.
    .globl dn2_tracks_root, dn2_track_object
    .globl dn2_play_mode, dn2_set_play_mode, dn2_mono_prio, dn2_set_mono_prio
    .set dn2_tracks_root, TRACKS_ROOT
    .set dn2_track_object, TRACK_OBJECT
    .set dn2_play_mode, PLAY_MODE_GET
    .set dn2_set_play_mode, PLAY_MODE_SET
    .set dn2_mono_prio, MONO_PRIO_GET
    .set dn2_set_mono_prio, MONO_PRIO_SET
