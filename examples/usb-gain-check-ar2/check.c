/* SPDX-License-Identifier: GPL-2.0-or-later */
/* usb-gain-check: core-ar2's held USB input gain on the Analog Rytm MKII
 * (OS 1.74), on screen.
 *
 *   - At the first tick, with nobody holding it, ten checks of the hold's
 *     rules; each that passes adds 1 to C (C10 is all of them):
 *       1 a hold of CORE_USB_IN_NONE by A is taken     5 B's release is refused
 *       2 the holder is A                              6 a hold by 0 is refused
 *       3 the step in force is NONE                    7 A's release is taken
 *       4 a hold by B is refused, A still holds        8 nobody holds it
 *       9 the step in force is the setting's again (the one read first)
 *      10 a step of 4 is refused
 *   - PLAYMODE (taken) steps the hold: none (the setting), NONE (no cut),
 *     CUT18 (the most cut), none again.
 *   - A bottom bar (rows 56..63, inverted): "USB S<step> H<held> C<checks>
 *     G<setting's step at boot>", core_usb_in_step, 1 while this mod holds
 *     it, the checks passed. Only on the main screen with nothing over it
 *     (core_stock_overlay 0), so a menu's rows (AUDIO ROUTING's USB TO
 *     MAIN) stay readable.
 */
#include <stdint.h>
#include "analog-rytm-mk2/usbin.h"
#include "analog-rytm-mk2/screen.h"

#define K_PLAYMODE 0xb5
#define KEY(ev) (*(const int32_t *)((const uint8_t *)(ev) + 12))
#define FLAGS(ev) (*(const int32_t *)((const uint8_t *)(ev) + 16))
#define DOWN 1

static const char A = 'A', B = 'B';
static int checked, passed, boot_step = -1, mode;

void ug_tick(void *ctrl)
{
    (void)ctrl;
    if (checked)
        return;
    checked = 1;
    boot_step = core_usb_in_step();
    passed += core_usb_in_gain(&A, CORE_USB_IN_NONE) == 0;
    passed += core_usb_in_holder() == &A;
    passed += core_usb_in_step() == CORE_USB_IN_NONE;
    passed += core_usb_in_gain(&B, CORE_USB_IN_CUT6) == -1 && core_usb_in_holder() == &A;
    passed += core_usb_in_gain(&B, CORE_USB_IN_RELEASE) == -1;
    passed += core_usb_in_gain(0, CORE_USB_IN_CUT6) == -1;
    passed += core_usb_in_gain(&A, CORE_USB_IN_RELEASE) == 0;
    passed += core_usb_in_holder() == 0;
    passed += core_usb_in_step() == boot_step;
    passed += core_usb_in_gain(&A, 4) == -1 && core_usb_in_holder() == 0;
}

int ug_key(void *brain, void *ev)
{
    static const int32_t steps[3] = {CORE_USB_IN_RELEASE, CORE_USB_IN_NONE, CORE_USB_IN_CUT18};
    (void)brain;
    if (KEY(ev) != K_PLAYMODE)
        return 0;
    if (FLAGS(ev) & DOWN) {
        mode = (mode + 1) % 3;
        core_usb_in_gain(&A, steps[mode]);
    }
    return 1;
}

static char *put(char *o, const char *s)
{
    while (*s)
        *o++ = *s++;
    return o;
}

static char *num(char *o, int v)
{
    char b[12];
    int n = 0;
    unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    if (v < 0)
        *o++ = '-';
    do
        b[n++] = (char)('0' + u % 10);
    while ((u /= 10) && n < 11);
    while (n)
        *o++ = b[--n];
    return o;
}

void ug_draw(void *bmp, void *ctrl)
{
    char t[48], *o = t;
    uint8_t *p = *(uint8_t **)((uint8_t *)bmp + 16);
    int x;
    (void)ctrl;
    if (core_stock_overlay())
        return;
    for (x = 0; x < 128; x++)
        p[8 * x] |= 0xff;   /* rows 56..63: byte 7 - 63 / 8 = 0 */
    o = put(o, "USB S");
    o = num(o, core_usb_in_step());
    o = put(o, " H");
    o = num(o, core_usb_in_holder() == &A);
    o = put(o, " C");
    o = num(o, passed);
    o = put(o, " G");
    o = num(o, boot_step);
    *o = 0;
    core_text_draw(bmp, CORE_FONT_MAIN, 1, 1, CORE_ALIGN_LEFT | CORE_TEXT_INVERT, t);
}
