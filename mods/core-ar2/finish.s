| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 and core-a4: an OS upgrade's restart that cannot hang on the
| panel link.
|
| After writing a new OS, stock waits 3 s, masks every interrupt
| (move #0x2700,sr) and calls a routine (FINISH_DRAIN) that waits for its
| serial output to drain: while bytes are pending behind the panel link's DMA
| (FINISH_PANEL), then while the UART queue holds bytes (FINISH_UART), then
| for the UART to be empty. Only the DMA's and the UART's interrupts move
| those counts on, so bytes the panel task queued while a DMA was still
| running are never sent: the unit stays on SUCCESS! REBOOTING until it is
| switched off (the new OS is written; it boots then). Seen in analog-emu on
| the Rytm's 1.74; the A4's 1.56 has the same code.
|
| core_finish_drain takes the place of the mask and the call: it masks
| interrupts only once nothing is pending, unmasking again to let the
| interrupts send whatever was queued, then goes on to FINISH_DRAIN as stock
| does, with interrupts masked.
|
| The device's mod.json gives FINISH_PANEL, FINISH_UART and FINISH_DRAIN.

        .section .run, "ax"
        .globl  core_finish_drain
core_finish_drain:
        move.w  %sr, %d1
1:      move.w  #0x2700, %sr
        tst.l   FINISH_PANEL
        bgt.s   2f
        tst.l   FINISH_UART
        bgt.s   2f
        jmp     FINISH_DRAIN            | its rts returns to the site's caller
2:      move.w  %d1, %sr                | let the interrupts send it
3:      tst.l   FINISH_PANEL
        bgt.s   3b
        tst.l   FINISH_UART
        bgt.s   3b
        bra.s   1b
