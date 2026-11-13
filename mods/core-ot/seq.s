| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the sequencer's moments (core-ot 0.10). ColdFire V4 (MCF5445x);
| assemble with -mcpu=54455.
|
| Four routines' entries, each a place octabam modules detour (MIT; the census is theirs, the code is
| our own): VARISPEED and VERB PARTS the Part apply, KITS the chain append, MIDI LOOPBACK the panic.
|
|   0x40009e00  a pattern's Part is applied (bank, part)          ev_part_apply
|   0x4009c634  a pattern is added to the chain (pattern)          ev_chain_add
|   0x400a10c8  the sequencer's stop and reset                     ev_seq_reset
|   0x4009f288  the all-notes-off on the MIDI tracks' channels     ev_midi_panic
|
| Each stub calls the handlers (observers, void), then replays the entry it displaced and goes on.

        .section .run, "ax"

| Calls every handler of the table \tab with \nargs longs taken from the caller's frame: the stub was
| entered by a jmp from the routine's first instruction, so 4(sp) is its first argument.
        .macro  OBSERVE tab, nargs
        tst.l   \tab
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     \tab, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        .if \nargs >= 2
        move.l  20+8(%sp), -(%sp)
        move.l  24+4(%sp), -(%sp)       | (the push moved sp by 4)
        .elseif \nargs == 1
        move.l  20+4(%sp), -(%sp)
        .endif
        movea.l %d1, %a0
        jsr     (%a0)
        .if \nargs
        lea     4*\nargs(%sp), %sp
        .endif
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:
        .endm

| ---- 0x40009e00 (was: lea -76(sp),sp ; movem.l d2-d7/a2-a6,(sp)): apply(bank, part), from the pattern
| schedule (0x400a05f0, a pattern change) and from the frame builder (0x4000b1d8, the frame interrupt).
| Handlers: void f(unsigned bank, unsigned part), before the Part reaches the engine.
        .globl  core_part_apply
core_part_apply:
        OBSERVE ev_part_apply, 2
        lea     -76(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     0x40009e08

| ---- 0x4009c634 (was: move.l d2,-(sp) ; move.l 8(sp),d2): append(pattern), from the [PTN] key's two
| handlers (0x40056b9a, 0x4007b3ae). The routine may still leave it out: the chain is full (16) or has
| it. Handlers: void f(unsigned pattern).
        .globl  core_chain_add
core_chain_add:
        OBSERVE ev_chain_add, 1
        move.l  %d2, -(%sp)
        move.l  8(%sp), %d2
        jmp     0x4009c63a

| ---- 0x400a10c8 (was: lea -36(sp),sp ; movem.l d2-d5/a2-a6,(sp)): the stop and reset (STOP while
| stopped, the menus that stop the sequencer first, MIDI). Handlers: void f(void), before it.
        .globl  core_seq_reset
core_seq_reset:
        OBSERVE ev_seq_reset, 0
        lea     -36(%sp), %sp
        movem.l %d2-%d5/%a2-%a6, (%sp)
        jmp     0x400a10d0

| ---- 0x4009f288 (was: lea -20(sp),sp ; movem.l d2-d4/a2-a3,(sp)): CC 123 on each channel the MIDI tracks
| use, called by the stop and reset when 0x46c7a9fe is set. Handlers: void f(void), before it.
        .globl  core_midi_panic
core_midi_panic:
        OBSERVE ev_midi_panic, 0
        lea     -20(%sp), %sp
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     0x4009f290
