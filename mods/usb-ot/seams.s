| SPDX-License-Identifier: GPL-2.0-or-later
| usb-ot for the Octatrack, OS 1.40C: the places in the stock USB device stack
| that ask the functions (usb.c). ColdFire V4 (MCF5445x); assemble with
| -mcpu=54455.
|
| Each stub replays what its site displaced and goes back to stock, or, when
| a function answered the request, to the end of stock's request handler
| (0x4001de74: it restores d2/d3 and returns). The C calls keep every
| register the code after them reads: C keeps d2-d7/a2-a6, the stubs save
| d0-d1/a0-a1 where stock still needs them.
|
| Each USB file of octabam2elemod v1.0 writes one of these sites too, so the
| linker refuses usb-ot beside it: a build takes either those files or usb-ot
| and its functions.

        .section .run, "ax"

        .set    UNCACHED, 0x08000000    | the controller does not see the data cache

| CALLC fn: fn(), every register kept.
        .macro  CALLC fn
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        jsr     \fn
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        .endm

| CALLR fn: d0 = fn(), d1/a0/a1 kept.
        .macro  CALLR fn
        lea     -12(%sp), %sp
        movem.l %d1/%a0-%a1, (%sp)
        jsr     \fn
        movem.l (%sp), %d1/%a0-%a1
        lea     12(%sp), %sp
        .endm

| ---- the controller's init ------------------------------------------------------
| 0x4001e030 jmp (move.l #0x08000000,%d0): the composition, the crossbar, each
| function's init.
        .globl  otu_seam_init
otu_seam_init:
        CALLC   otu_init
        move.l  #0x08000000, %d0
        jmp     0x4001e036

| ---- bus reset, session end ------------------------------------------------------
| 0x4001e91c jmp (jsr 0x4001d6b8 ; moveq #64,%d0): USBSTS.URI, after stock's own
| reset handling. 0x4001e952 jmp (move.l 0xfc0b0140,%d0): OTGSC.BSVIS, before
| stock stops the controller. Every function goes down.
        .globl  otu_seam_reset, otu_seam_sessend
otu_seam_reset:
        jsr     0x4001d6b8
        CALLC   otu_down
        moveq   #64, %d0
        jmp     0x4001e922
otu_seam_sessend:
        CALLC   otu_down
        move.l  0xfc0b0140, %d0
        jmp     0x4001e958

| ---- SET_CONFIGURATION -------------------------------------------------------------
| 0x4001d9ca jmp (pea 0x400b9868): after stock primed EP0's status and brought EP1
| up. Every function goes down, then up at the speed the port came up at.
        .globl  otu_seam_setcfg
otu_seam_setcfg:
        CALLC   otu_configured
        pea     0x400b9868
        jmp     0x4001d9d0

| ---- SET_INTERFACE, GET_INTERFACE --------------------------------------------------
| 0x4001dd04 jmp (move.l 0xfc0b01c4,%d0 ; ori.l #0x00400040,%d0): a function's
| interface is its own; the mass-storage interface goes on to stock.
        .globl  otu_seam_setiface
otu_seam_setiface:
        CALLR   otu_set_interface
        tst.l   %d0
        beq.s   1f
        jmp     0x4001de74
1:      move.l  0xfc0b01c4, %d0
        ori.l   #0x00400040, %d0
        jmp     0x4001dd10

| 0x4001d824 jmp (pea 0x400e20a1, stock's one-byte 0): the reply's address, then
| stock's send (0x4001d81e pushes wLength and calls usb_ep0_send).
        .globl  otu_seam_getiface
otu_seam_getiface:
        CALLR   otu_get_interface
        move.l  %d0, -(%sp)
        jmp     0x4001d81e

| ---- CLEAR_FEATURE(ENDPOINT_HALT) ----------------------------------------------
| 0x4001daec jmp (move.w 0x46c8ce0c,%d0): a function's endpoint is answered here;
| stock answers EP1 (and stalls the rest).
        .globl  otu_seam_clrfeat
otu_seam_clrfeat:
        CALLR   otu_clear_feature
        tst.l   %d0
        beq.s   1f
        jmp     0x4001de74
1:      move.w  0x46c8ce0c, %d0
        jmp     0x4001daf2

| ---- requests stock does not answer --------------------------------------------
| 0x4001de64 jmp (move.l 0xfc0b01c0,%d0): stock's unknown-request tail, before its
| STALL. 0x4001de6e jmp (move.l %d0,0xfc0b01c0): the STALL's store, which stock's
| refusals of standard requests reach too (d0: ENDPTCTRL0 with its stall bits).
        .globl  otu_seam_unknown, otu_seam_stall
otu_seam_unknown:
        CALLR   otu_setup_unknown
        tst.l   %d0
        beq.s   1f
        jmp     0x4001de74
1:      move.l  0xfc0b01c0, %d0
        jmp     0x4001de6a
otu_seam_stall:
        move.l  %d0, -(%sp)
        CALLR   otu_setup_stall
        tst.l   %d0
        beq.s   1f
        addq.l  #4, %sp
        jmp     0x4001de74
1:      move.l  (%sp)+, %d0
        move.l  %d0, 0xfc0b01c0
        jmp     0x4001de74

| ---- GET_DESCRIPTOR(CONFIGURATION / OTHER_SPEED) ----------------------------------
| 0x4001d880 (pea 0x400e201c ; bra.s), 0x4001d888 (pea 0x400e203c), 0x4001d8be
| (pea 0x400e207c ; bra.s), 0x4001d8c6 (pea 0x400e205c ; bra.s), each jmp: the
| composed table in place of stock's, through the alias, then stock's shared
| send at 0x4001d88e (move.l %d1,-(%sp) ; jsr (%a0)).
        .globl  otu_seam_cfg_fs, otu_seam_cfg_hs, otu_seam_cfg_ohs, otu_seam_cfg_ofs
otu_seam_cfg_fs:
        pea     otu_cfg + UNCACHED
        jmp     0x4001d88e
otu_seam_cfg_hs:
        pea     otu_cfg + UNCACHED + 512
        jmp     0x4001d88e
otu_seam_cfg_ofs:
        pea     otu_cfg + UNCACHED + 1024
        jmp     0x4001d88e
otu_seam_cfg_ohs:
        pea     otu_cfg + UNCACHED + 1536
        jmp     0x4001d88e

| 0x4001d858, 0x4001d896 jmp (moveq #32,%d0 ; cmp.l %d2,%d0 ; bcs.s +4): d1 =
| min(wLength, the table's length), where stock fixed 32; on past stock's
| two-way move (12 bytes on). d0 is loaded again there.
        .globl  otu_seam_clamp1, otu_seam_clamp2
otu_seam_clamp1:
        moveq   #0, %d1
        bra.s   1f
otu_seam_clamp2:
        moveq   #1, %d1
1:      lea     -8(%sp), %sp
        movem.l %a0-%a1, (%sp)
        move.l  %d1, -(%sp)             | 1: OTHER_SPEED
        move.l  %d2, -(%sp)             | wLength
        jsr     otu_clamp
        addq.l  #4, %sp
        move.l  (%sp)+, %d1
        move.l  %d0, %a0                | d1 = the length; d0 = which (no exg on ColdFire)
        move.l  %d1, %d0
        move.l  %a0, %d1
        movem.l (%sp), %a0-%a1
        addq.l  #8, %sp
        tst.l   %d0
        bne.s   2f
        jmp     0x4001d864
2:      jmp     0x4001d8a2

| ---- usb_ep0_send ----------------------------------------------------------------
| 0x4001d4b2 jmp (move.l (%a0),0x4ec95028: the EP0 dTD's buffer page 0): page 1 too,
| so a reply that crosses a 4 KB page is sent whole (stock's sends a truncated one).
        .globl  otu_seam_ep0page
otu_seam_ep0page:
        move.l  %d1, -(%sp)
        move.l  (%a0), %d1
        move.l  %d1, 0x4ec95028
        andi.l  #0xfffff000, %d1
        addi.l  #0x1000, %d1
        move.l  %d1, 0x4ec9502c
        move.l  (%sp)+, %d1
        jmp     0x4001d4b8

| ---- the USB interrupt ------------------------------------------------------------
| 0x4001e606 jmp (move.l 0xfc0b01ac,%d0: ENDPTSETUPSTAT), on USBSTS.UI: each
| function's endpoint completions first.
        .globl  otu_seam_irq
otu_seam_irq:
        CALLC   otu_irq
        move.l  0xfc0b01ac, %d0
        jmp     0x4001e60c

| ---- for the functions -------------------------------------------------------------
        .globl  ot_usb_irq_off, ot_usb_irq_restore
ot_usb_irq_off:
        move.w  %sr, %d0
        mvz.w   %d0, %d0
        move.w  #0x2700, %sr
        rts
ot_usb_irq_restore:
        move.l  4(%sp), %d0
        move.w  %d0, %sr
        rts
