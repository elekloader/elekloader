/* SPDX-License-Identifier: GPL-2.0-or-later */
/* THRU: the smallest exclusive-audio mod for the Digitakt mk1 (core 4.2). THRU in the Mod Menu takes the audio: the
 * Digitakt's engine stops, and its audio inputs go straight to the output at knob A's level, with an echo at knob B's
 * (0: none) in the memory core lends while THRU has the audio (the delay's line). NO gives the audio back. Every other
 * key and knob stays the Digitakt's. A template for a mod that wants the whole audio engine (docs/ADAPTING.md,
 * "Exclusive audio and the Mod Menu (core 4.2, Digitakt mk1)"). */
#include "digitakt-mk1/core3.h"

#define KEY_NO      13
#define EV_KEY(ev)   (*(uint32_t *)((char *)(ev) + 12))
#define EV_FLAGS(ev) (*(uint32_t *)((char *)(ev) + 16))
#define EV_DELTA(ev) (*(int32_t *)((char *)(ev) + 16))

typedef void (*rect_fn)(void *bmp, int x0, int y0, int x1, int y1, int colour);
typedef void (*text_fn)(void *bmp, const void *font, int x, int y, int maxlen, const char *fmt, ...);

#define ECHO        ((int16_t *)CORE_LEND_LO)  /* the echo's line: 12000 frames L,R (250 ms), lent by core */
#define ECHO_FRAMES 12000

static volatile int32_t thru_level = 64;    /* knob A: 0-127, 64 unity */
static volatile int32_t thru_echo;          /* knob B: 0-127, the echo's level (0 none) */
static int32_t thru_counts[2];
static uint32_t at;                         /* the echo's write position, frames */

struct core_audio_owner thru_owner;

/* Interrupt level, every block THRU has the audio: 32 frames L,R in, 32 out, Q1.31. The line is THRU's only while
 * core_audio_live is its record: core clears it before then, so it starts silent. */
static void thru_render(int32_t *out, const int32_t *in)
{
    int32_t g = thru_level * 16384 / 64, e = thru_echo * 16384 / 127;     /* Q14 */
    int echo = core_audio_live == &thru_owner && e;
    int i;
    for (i = 0; i < 64; i++) {
        int32_t x = in[i] >> 16, v = x * g;    /* Q15 x Q14: Q29 */
        if (echo) {                         /* the line's sample, out and fed back with the input */
            int16_t *p = ECHO + 2 * at + (i & 1);
            int32_t d = *p, fb = x + ((d * e) >> 15);
            v += d * e;
            *p = (int16_t)(fb > 32767 ? 32767 : fb < -32768 ? -32768 : fb);
            if (i & 1)
                at = at + 1 >= ECHO_FRAMES ? 0 : at + 1;
        }
        if (v > 0x1fffffff)
            v = 0x1fffffff;
        else if (v < -0x20000000)
            v = -0x20000000;
        out[i] = v << 2;
    }
}

struct core_audio_owner thru_owner = { 0, thru_render, 0 };

static void thru_open(void *brain, void *event, int32_t track)
{
    (void)brain, (void)event, (void)track;
    thru_owner.on = 1;                      /* the audio is THRU's from the next block */
}

const struct core_menu_item thru_menu = { "THRU", thru_open, 0, 0 };

int thru_key(void *brain, void *ev)
{
    (void)brain;
    if (!thru_owner.on || EV_KEY(ev) != KEY_NO || !(EV_FLAGS(ev) & 1))
        return 0;
    thru_owner.on = 0;                      /* the Digitakt's audio, once core has cleared the line */
    return 1;
}

int thru_enc(void *brain, void *ev)
{
    int32_t k = (int32_t)EV_KEY(ev) - 1, v;
    volatile int32_t *p;
    (void)brain;
    if (!thru_owner.on || k < 0 || k > 1)
        return 0;
    p = k ? &thru_echo : &thru_level;
    thru_counts[k] += EV_DELTA(ev);         /* 4 counts a detent */
    v = *p + thru_counts[k] / 4;
    thru_counts[k] %= 4;
    *p = v < 0 ? 0 : v > 127 ? 127 : v;
    return 1;
}

void thru_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (!thru_owner.on)
        return;
    ((rect_fn)fw_fillrect)(bmp, 64, 54, 127, 63, 0);
    ((text_fn)fw_textf)(bmp, (const void *)fw_font5, 67, 57, -1, "THRU %d E%d", (int)thru_level, (int)thru_echo);
    ((rect_fn)fw_fillrect)(bmp, 64, 54, 127, 63, -1);
}
