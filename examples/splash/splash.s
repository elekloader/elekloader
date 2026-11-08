/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A boot splash: your picture, or your animation, over the boot intro of the
 * Digitakt mk1 and the Digitone mk1 / Keys. The site's Splash screen maker
 * (web/splash.js) and elekloader.splash write this code's bytes, assembled,
 * with your frames after it; tests/test_units.py checks they are this file's.
 *
 * Time: the intro's PIT3 handler runs 15 times a second and posts the intro's
 * semaphore. Its call to the post goes to splash_tick instead, which moves the
 * sequence on one entry and then posts, as the handler did.
 *
 * Pictures: the intro draws its frames, several between two ticks, and
 * presents each with one of two stock calls (a delta present and a full
 * present). Each of the intro's calls goes to splash_diff or splash_flush
 * instead: while the intro still owns the PIT3 vector, the sequence's current
 * frame is copied over the composed frame, and then the stock present runs.
 * Once the display module takes the vector, nothing is drawn. The present
 * sites and the PIT3 gate are DigiSplash's
 * (github.com/DigiAlchemydsp/DigiSplash, GPL-2.0-or-later).
 *
 * The code has no relocations: the data follows it, found PC-relative.
 *
 *   splash_data + 0    the sequence's length, in ticks
 *                + 4   the position in it (starts at 0)
 *                + 8   where the frames start, from splash_data
 *                + 12  1: loop the sequence; 0: stop on its last entry
 *                + 16  the sequence: a 16-bit frame number per tick
 *   then, from a multiple of 4, the frames: 1024 bytes each, the panel's
 *   format (byte = page + 8 * column, page = 7 - y / 8, bit y % 8).
 *
 * Built for one OS by the symbols it is given:
 *   ISR    the intro's PIT3 handler     FB     the front buffer pointer
 *   DIFF   the delta present            FLUSH  the full present
 *   POST   the semaphore post the handler calls
 */
        .section .run, "ax"

        .globl  splash_diff
        .globl  splash_flush
        .globl  splash_tick

splash_diff:
        bsr.w   splash_draw
        jmp     DIFF

splash_flush:
        bsr.w   splash_draw
        jmp     FLUSH

/* in the PIT3 handler, which saved d0-d1/a0-a1; the post's argument is on the stack */
splash_tick:
        lea     splash_data(%pc),%a1
        move.l  4(%a1),%d0              /* the next position */
        addq.l  #1,%d0
        cmp.l   (%a1),%d0
        bcs.s   1f
        clr.l   %d0                     /* past the end: loop, */
        tst.l   12(%a1)
        bne.s   1f
        move.l  (%a1),%d0               /* or stay on the last entry */
        subq.l  #1,%d0
1:      move.l  %d0,4(%a1)
        jmp     POST

splash_draw:
        move.l  0x40000340,%d0          /* the PIT3 vector: still the intro's? */
        cmpi.l  #ISR,%d0
        bne.s   9f
        move.l  FB,%a0                  /* the frame the intro composed */
        tst.l   %a0
        beq.s   9f
        lea     splash_data(%pc),%a1
        move.l  4(%a1),%d0
        moveq   #0,%d1
        move.w  16(%a1,%d0.l*2),%d1     /* the frame for this tick */
        moveq   #10,%d0
        lsl.l   %d0,%d1                 /* * 1024 */
        add.l   8(%a1),%d1
        adda.l  %d1,%a1
        move.l  #256,%d0
2:      move.l  (%a1)+,(%a0)+
        subq.l  #1,%d0
        bne.s   2b
9:      rts

        .balign 4
splash_data:
