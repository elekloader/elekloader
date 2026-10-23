| SPDX-License-Identifier: GPL-2.0-or-later
        .section .run, "ax"
        .globl core_keys_lock, core_keys_unlock, core_stock_buttons
        .globl fw_key_state, fw_ui_queue
        .set fw_key_state, KEY_STATE
        .set fw_ui_queue, UI_QUEUE

core_keys_lock:
        moveq   #0, %d0
        move.w  %sr, %d0
        move.w  #0x2700, %sr
        rts

core_keys_unlock:
        move.l  4(%sp), %d0
        move.w  %d0, %sr
        rts

| Reproduce only the displaced prologue, then use the original driver.
core_stock_buttons:
        lea     -KEY_STACK(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     KEY_CONTINUE

        .globl fw_panel_encoder, fw_sysex_send
        .set fw_panel_encoder, PANEL_ENCODER
        .set fw_sysex_send, SYSEX_SEND
        .globl core_millis, core_panel_clock
core_millis:
        move.l core_ms, %d0
        rts

| The stock 120 Hz panel callback, registered with period mask 1 at
| 0x401200c4. PIT2: PRE=6, PMR=17187, 132 MHz bus.
| Add 25 ms per three callbacks; resolution 8/9 ms. Keep stock unchanged.
core_panel_clock:
        addq.l #8, core_ms
        subq.l #1, core_fraction
        bpl.s 1f
        move.l %d0, -(%sp)
        moveq #2, %d0
        move.l %d0, core_fraction
        move.l (%sp)+, %d0
        addq.l #1, core_ms
1:
        addq.l #1, PANEL_COUNTER
        rts
        .section .bss
        .balign 4
core_ms: .skip 4
core_fraction: .skip 4
