/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.15: pad strikes before stock plays them, and pad and
 * key LEDs from mods (docs/ADAPTING.md, "Pads and LEDs (core-ar2 0.15,
 * Analog Rytm MKII)"; elekloader/sdk/include/
 * analog-rytm-mk2/pads.h). Every address is OS 1.74's; nothing of stock's
 * code or tables is copied, core calls it and reads its RAM.
 *
 *   - ev_pad: the UI loop hands a pad's strike, release and pressure to
 *     the view tree (0x400a0398, 0x400a03b4), and the view under it plays
 *     the pad's sound, sends its note and selects its track there (the pad
 *     handler 0x400c78c0). Core takes those three calls (0x400a47ce,
 *     0x400a4804, 0x400a4784) and asks the mods first. A strike a handler
 *     takes never reaches stock, and neither do its pressure and release,
 *     whatever the handlers answer to those: stock's view tree pairs a
 *     release with the press it saw (0x40079c4e), so the two always go the
 *     same way.
 *   - LEDs: stock sets each logical LED's colour into one of four slots on
 *     the panel (`Bs id c`, 0x40095d98 led_set and 0x40095fc0 led_colour,
 *     their only senders of slot colours), keeps the four in RAM
 *     (0x42fb6f49 + 4 x LED) and picks a slot per LED with the selectors
 *     (blink, glow, on/off). A LED a mod owns gets its colour in all four
 *     slots, so every selector shows it, and core drops stock's slot
 *     colours for it at those two calls (0x40095e68, 0x40096088); stock's
 *     own copy still updates. When the mod lets it go, core sends stock's
 *     four slots back.
 *
 * All of it runs in the UI task.
 */

#include <stdint.h>

/* ---- stock's ---- */
#define PAD_DISPATCH ((int (*)(void *, void *))0x400a0398)   /* Brain: a pad strike or release to the views */
#define TOUCH_DISPATCH ((int (*)(void *, void *))0x400a03b4) /* Brain: a pad's pressure to the views */
#define KEY_HELD ((uint32_t (*)(int))0x40081c28)              /* 1 while key code k is down */
#define K_FUNC 0x80
#define PANEL_SEND ((void (*)(int, const uint8_t *))0x400019e4) /* the panel link's writer */
#define MUTEX_TAKE ((int (*)(void *))0x400010bc)
#define MUTEX_GIVE ((void (*)(void *))0x400011ee)
#define LED_MUTEX ((void *)0x41920560)                        /* stock's, around every LED send */
#define LED_HW ((const volatile int32_t *)0x4192056c)         /* logical LED -> panel LED id, < 0 none */
#define LED_SLOTS ((const volatile uint8_t *)0x42fb6f49)      /* stock's 4 slot colours a logical LED */
#define LEDS 193                                              /* stock's logical LEDs (0x400a0508) */

/* the pad events (0x4007607a, 0x4006ceba): +16 1 a strike / 0 a release
 * (2 pressure), +20 the pad 0..11, +24 the velocity (pressure: value << 7) */
#define EV_I(ev, o) (*(const volatile int32_t *)((const uint8_t *)(ev) + (o)))
#define PADS 12

/* ---- the SDK's (pads.h) ---- */
#define CORE_PAD_DOWN 1
#define CORE_PAD_FUNC 2
#define CORE_PAD_UP 0x10
#define CORE_PAD_PRESSURE 0x20
#define CORE_LED_STOCK (-1)

typedef int32_t (*ev_pad_t)(int32_t pad, int32_t value, int32_t flags);
typedef void (*ev_leds_t)(void);
extern const ev_pad_t ev_pad[];
extern const ev_leds_t ev_leds[];

/* ================================ pads ==================================== */

static volatile uint32_t pads_taken;   /* bit p: pad p's strike was taken, until its release */

static int32_t ask(int32_t pad, int32_t value, int32_t flags)
{
    const ev_pad_t *f;
    int32_t r = 0;
    if (KEY_HELD(K_FUNC))
        flags |= CORE_PAD_FUNC;
    for (f = ev_pad; *f; f++)
        if ((*f)(pad, value, flags)) {
            r = 1;
            if (flags & CORE_PAD_DOWN)
                break;          /* a strike is taken by the first that says so */
        }
    return r;
}

/* in place of the UI loop's jsr 0x400a0398 (strike 0x400a47ce, release
 * 0x400a4804): (brain, the pad event) */
int core_pad(void *brain, void *ev)
{
    int32_t pad = EV_I(ev, 20), bit;
    if ((uint32_t)pad >= PADS)
        return PAD_DISPATCH(brain, ev);
    bit = 1 << pad;
    if (EV_I(ev, 16) & 0xff) {
        if (pads_taken & bit) {
            /* a strike with no release seen for the last: still the mod's */
            ask(pad, EV_I(ev, 24), CORE_PAD_DOWN);
            return 0;
        }
        if (ask(pad, EV_I(ev, 24), CORE_PAD_DOWN)) {
            pads_taken |= bit;
            return 0;
        }
        return PAD_DISPATCH(brain, ev);
    }
    ask(pad, EV_I(ev, 24), CORE_PAD_UP);
    if (pads_taken & bit) {
        pads_taken &= ~bit;
        return 0;
    }
    return PAD_DISPATCH(brain, ev);
}

/* in place of the UI loop's jsr 0x400a03b4 (0x400a4784): a pad's pressure */
int core_pad_touch(void *brain, void *ev)
{
    int32_t pad = EV_I(ev, 20);
    if ((uint32_t)pad >= PADS)
        return TOUCH_DISPATCH(brain, ev);
    if (ask(pad, EV_I(ev, 24), CORE_PAD_PRESSURE) || (pads_taken & (1 << pad)))
        return 0;
    return TOUCH_DISPATCH(brain, ev);
}

/* the pads whose strike a mod holds now (bit p = pad p) */
uint32_t core_pads_taken(void)
{
    return pads_taken;
}

/* ================================ LEDs ==================================== */

static int16_t want[LEDS];      /* this frame's: colour 0..255, or CORE_LED_STOCK */
static int16_t shown[LEDS];     /* what core last sent: colour, or CORE_LED_STOCK */
static uint8_t owned_hw[32];    /* bit per panel LED id: core drops stock's slot colours */
static uint8_t leds_ready;

static int hw_of(int led)
{
    int32_t h = LED_HW[led];
    return h < 0 ? -1 : (h & 0xff);
}

static void send_slots(int hw, const uint8_t c[4])
{
    uint8_t m[3];
    int s;
    m[1] = (uint8_t)hw;
    for (s = 0; s < 4; s++) {
        m[0] = (uint8_t)(0xb0 | s);
        m[2] = c[s];
        PANEL_SEND(3, m);
    }
}

/* in place of led_set's and led_colour's jsr 0x400019e4 (0x40095e68,
 * 0x40096088): (3, "Bs id c"), inside stock's LED mutex */
void core_led_send(int len, const uint8_t *msg)
{
    if (len == 3 && (msg[0] & 0xfc) == 0xb0 && (owned_hw[msg[1] >> 3] & (1 << (msg[1] & 7))))
        return;         /* a LED a mod owns: stock's copy (0x42fb6f49) is kept */
    PANEL_SEND(len, msg);
}

/* a LED's colour for this frame: colour 0..255 (the panel's palette, 0
 * dark), CORE_LED_STOCK to leave it to stock. From an ev_leds handler; a LED
 * no handler sets in a frame goes back to stock. 0 done, -1 no such LED. */
int core_led(int led, int colour)
{
    if ((unsigned)led >= LEDS || hw_of(led) < 0)
        return -1;
    want[led] = colour < 0 ? CORE_LED_STOCK : (int16_t)(colour & 0xff);
    return 0;
}

/* ev_tick, after every mod's (order 1000000): ev_leds, then the changes to
 * the panel */
void core_leds_tick(void *ctrl)
{
    const ev_leds_t *f;
    int i, hw;
    (void)ctrl;
    if (!ev_leds[0] && !leds_ready)
        return;
    if (!leds_ready) {
        for (i = 0; i < LEDS; i++)
            shown[i] = CORE_LED_STOCK;
        leds_ready = 1;
    }
    for (i = 0; i < LEDS; i++)
        want[i] = CORE_LED_STOCK;
    for (f = ev_leds; *f; f++)
        (*f)();
    for (i = 0; i < LEDS; i++) {
        if (want[i] == shown[i])
            continue;
        hw = hw_of(i);
        if (hw < 0)
            continue;
        MUTEX_TAKE(LED_MUTEX);
        if (want[i] >= 0) {
            uint8_t c[4];
            c[0] = c[1] = c[2] = c[3] = (uint8_t)want[i];
            owned_hw[hw >> 3] |= (uint8_t)(1 << (hw & 7));
            send_slots(hw, c);
        } else {
            uint8_t c[4];
            int s;
            owned_hw[hw >> 3] &= (uint8_t)~(1 << (hw & 7));
            for (s = 0; s < 4; s++)
                c[s] = LED_SLOTS[4 * i + s];
            send_slots(hw, c);
        }
        MUTEX_GIVE(LED_MUTEX);
        shown[i] = want[i];
    }
}
