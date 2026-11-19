/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pad-check: core-ar2's ev_pad and LEDs on the Analog Rytm MKII (OS 1.74),
 * on screen.
 *
 *   - BD's strikes are taken (ev_pad answers 1): BD makes no sound, sends
 *     no note and doesn't select its track. Every other pad is stock's.
 *   - RS's releases are answered 1, its strikes 0: stock keeps the release
 *     of a strike it played (no voice left hanging).
 *   - After an odd number of BD strikes the mod owns two LEDs: the BD pad
 *     (colour 15) and trig key 1 (colour 47); after an even number it owns
 *     none, and stock's colours come back.
 *   - A bottom bar (rows 55..63, inverted): "B<down>/<up>/<pressure>
 *     S<down>/<up> R<down>/<up> T<track> F<flags> V<velocity> K<taken>",
 *     the counts per pad, core_current_track, the last strike's flags and
 *     velocity, and core_pads_taken.
 */
#include <stdint.h>
#include "analog-rytm-mk2/pads.h"
#include "analog-rytm-mk2/screen.h"
#include "analog-rytm-mk2/moments.h"

#define BD 0
#define SD 1
#define RS 2

static volatile unsigned down[CORE_PADS], up[CORE_PADS], touch[CORE_PADS];
static volatile int last_flags, last_vel;

int32_t pc_pad(int32_t pad, int32_t value, int32_t flags)
{
    if ((uint32_t)pad >= CORE_PADS)
        return 0;
    if (flags & CORE_PAD_DOWN) {
        down[pad]++;
        last_flags = flags;
        last_vel = value;
        return pad == BD;
    }
    if (flags & CORE_PAD_UP) {
        up[pad]++;
        return pad == RS;
    }
    if (flags & CORE_PAD_PRESSURE)
        touch[pad]++;
    return 0;
}

void pc_leds(void)
{
    if (down[BD] & 1) {
        core_led(CORE_LED_PAD(BD), 15);
        core_led(CORE_LED_TRIG(0), 47);
    }
}

/* ---- the bar ---- */
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

void pc_draw(void *bmp, void *ctrl)
{
    char t[64], *o = t;
    uint8_t *p = *(uint8_t **)((uint8_t *)bmp + 16);
    int x;
    (void)ctrl;
    for (x = 0; x < 128; x++)
        p[8 * x] |= 0xff;   /* glass rows 56..63: byte 7 - 63 / 8 = 0 */
    o = put(o, "B");
    o = num(o, (int)down[BD]);
    o = put(o, "/");
    o = num(o, (int)up[BD]);
    o = put(o, "/");
    o = num(o, (int)touch[BD]);
    o = put(o, " S");
    o = num(o, (int)down[SD]);
    o = put(o, "/");
    o = num(o, (int)up[SD]);
    o = put(o, " R");
    o = num(o, (int)down[RS]);
    o = put(o, "/");
    o = num(o, (int)up[RS]);
    o = put(o, " T");
    o = num(o, core_current_track());
    o = put(o, " F");
    o = num(o, last_flags);
    o = put(o, " V");
    o = num(o, last_vel);
    o = put(o, " K");
    o = num(o, (int)core_pads_taken());
    *o = 0;
    core_text_draw(bmp, CORE_FONT_MAIN, 1, 1, CORE_ALIGN_LEFT | CORE_TEXT_INVERT, t);
}
