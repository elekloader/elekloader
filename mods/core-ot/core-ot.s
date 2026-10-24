| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack (MKI and MKII), OS 1.40C: the boot copier. The
| hook bus (0.2) is in bus.s.
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| What every Octatrack mod needs is RAM and a way to get its code into it,
| and that is this file:
|
| - mod.json moves the audio page arena's base past the pages the linked
|   mods' RAM fills (from 0.3; 0.2 took a fixed 1,707), so the OS never
|   uses them. Those are octabam's arena writes for the same reservation;
|   the linker computes their values from the page count (the profile's
|   `reserve`): arena_base, __arena_pages, __arena_fill, __arena_clear.
| - .boot runs where the bootstrap unpacks it (appended to MAIN OS at
|   0x4010fdf0), from the boot site 0x4000050c. That site called
|   BOOT_CONTINUE (0x40001e50), which is replayed first, as octabam's loader
|   does. Then .boot copies the RAM image the linker built into the reserve,
|   hashes what it copied, zeroes .bss, and returns with every register as
|   BOOT_CONTINUE left it. Both writes go through the uncached alias
|   (+0x08000000), so no cache line can hold the reserve's old contents when
|   the code there first runs; the hash reads the copy back through it.
| - The hash (0.7.1) is the linker's __run_hash over the RAM image (link.py,
|   run_hash). The sites already point into that image, so a copy that
|   differs from what was linked cannot be skipped around: .boot halts in
|   `core_boot_fatal` instead of running it. The reserve's .bss is not yet cleared
|   then, so ot_boot_info (core.h) records the two hashes there before the
|   loop, with the state OT_BOOT_FAIL; a copy that matches leaves
|   OT_BOOT_OK and the same two numbers (equal) there.
|
| The linker defines __run_load, __run_start, __run_words, __run_hash,
| __bss_start and __bss_words, and arena_base: the arena's base after the
| reserve, for a mod that compares against it (octabam's RECORDER HOLD: a
| fetch past a recording returns the base). BOOT_CONTINUE and UNCACHED come
| from mod.json (defsym).

        .include "meter.inc"

| link.py's HASH_SEED: h = seed; h = h * 33 + word, per 32-bit word
        .equ    RUN_HASH_SEED, 5381
| ot_boot_info's states (core.h's struct ot_boot_info: state, want, got)
        .equ    OT_BOOT_OK,   0x424f4f54      | 'BOOT'
        .equ    OT_BOOT_FAIL, 0x4641494c      | 'FAIL'

| ============================ .boot =======================================
        .section .boot, "ax"
        .globl  boot
boot:
        jsr     BOOT_CONTINUE           | the call the boot site made, first
        lea     -24(%sp), %sp
        movem.l %d0-%d3/%a0-%a1, (%sp)
        lea     __run_load, %a0         | where the RAM image was unpacked
        movea.l #__run_start + UNCACHED, %a1
        move.l  #__run_words, %d0
        beq.s   2f                      | nothing to copy (core alone)
1:      move.l  (%a0)+, (%a1)+
        subq.l  #1, %d0
        bne.s   1b
2:      movea.l #__run_start + UNCACHED, %a0    | the copy, read back
        move.l  #RUN_HASH_SEED, %d1
        move.l  #__run_words, %d0
        beq.s   6f                      | nothing copied: the seed alone
5:      move.l  (%a0)+, %d2
        move.l  %d1, %d3
        lsl.l   #5, %d1
        add.l   %d3, %d1                | h * 33
        add.l   %d2, %d1
        subq.l  #1, %d0
        bne.s   5b
6:      move.l  #__run_hash, %d2
        cmp.l   %d2, %d1
        bne.w   core_boot_fatal
        movea.l #__bss_start + UNCACHED, %a1
        move.l  #__bss_words, %d0
        beq.s   4f
3:      clr.l   (%a1)+
        subq.l  #1, %d0
        bne.s   3b
4:      movea.l #ot_boot_info + UNCACHED, %a1
        move.l  #OT_BOOT_OK, (%a1)+
        move.l  %d2, (%a1)+
        move.l  %d1, (%a1)
        movea.l #ot_meter + M_TPF + UNCACHED, %a1
        move.l  #FRAME_COUNTS, (%a1)    | the frame meter's first guess
        moveq   #1, %d0                 | the draw gate may call into .run now
        move.b  %d0, core_up
        movem.l (%sp), %d0-%d3/%a0-%a1
        lea     24(%sp), %sp
        rts

| The copy is not what the linker built: stop, with interrupts masked, rather
| than run it. d1 = the hash of the copy, d2 = the linked one, as ot_boot_info
| records. The start-up goes no further from here; the stock file in its
| place recovers the unit (the bootloader is never changed).
        .globl  core_boot_fatal
core_boot_fatal:
        move.w  #0x2700, %sr
        movea.l #ot_boot_info + UNCACHED, %a1
        move.l  #OT_BOOT_FAIL, (%a1)+
        move.l  %d2, (%a1)+
        move.l  %d1, (%a1)
7:      bra.s   7b

| ============================ .bss ========================================
        .section .bss
        .balign 4
| What a weak import resolves to when no mod provides it: reads as zero.
        .globl  core_zero
core_zero:  .skip 16
| What .boot found (core.h's struct ot_boot_info): its state and the two hashes.
        .globl  ot_boot_info
ot_boot_info: .skip 12
| The frame budget meter (bus.s, core.h's struct ot_frame_meter).
        .globl  ot_meter
ot_meter:   .skip OT_METER_SIZE
