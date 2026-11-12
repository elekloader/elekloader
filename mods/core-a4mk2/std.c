/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.7: the standard events (std1) at the sites core already owns,
 * and the helpers that go with them (README, "Standard events (0.7)";
 * elekloader/sdk/include/std1.h, analog-four-mk2/keys.h, leds.h, params.h,
 * ports.h). mod.json's "std" map sends ev_tick, ev_draw, ev_settings,
 * ev_project and ev_render_in to the native tables (the same prototype, or
 * its first part); the three below are thunks, a native handler of core's
 * that calls a std table with the arguments converted:
 *
 *   ev_boot        std_boot, from .boot (BOOT_INIT): f(void)
 *   ev_key         std_key, from a native ev_key handler: f(key, flags)
 *   ev_param_turn  std_param_turn, from pages.s's param_apply_delta when no
 *                  core_params entry took the turn: f(view, id, delta, knob)
 *
 * and core_redraw, core_timer and core_post, all of them run from core's
 * own ev_tick handlers, in the UI task.
 */

#include <stdint.h>
#include <std1.h>

typedef void (*fn_void)(void);
typedef int (*fn_key)(unsigned key, unsigned flags);
typedef int (*fn_turn)(void *view, int id, int delta, int knob);

extern const fn_void std_boot[];
extern const fn_key std_key[];
extern const fn_turn std_param_turn[];

/* the view controller's "compose again" byte (core.s's ev_tick reads it) */
#define CTRL_DIRTY 0x20
/* KeyEvent: +12 the key, +16 its flags; EncoderEvent: +12 the encoder
 * (analog-four-mk2/screen.h) */
#define EV_KEY 12
#define EV_FLAGS 16
#define EV_ENC 12

static unsigned short irq_mask(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    return sr;
}

static void irq_restore(unsigned short sr)
{
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

/* ================================ ev_boot ================================= */

/* .boot's BOOT_INIT call: the mods' RAM image is in place and their .bss
 * clear; the OS has not started, so a handler calls no firmware */
void core_boot_init(void)
{
    const fn_void *f;
    for (f = std_boot; *f; f++)
        (*f)();
}

/* ================================ ev_key ================================== */

/* A4 KeyEvent flags (+16), read in 1.56: bit 0 down (kept on repeats), so a
 * release is bit 0 clear; bits 1 FUNC held, 2 double and 3 repeat are std1's.
 * Bit 4 is set only on a clean tap's release (a combo's release has 0 or 2),
 * so it is not std1's KEY_RELEASE; bits 4..6 are not passed. A key a view
 * consumed gets no release from stock. */
#define A4_KEY_DOWN 0x01u
#define A4_KEY_SAME (KEY_FUNC | KEY_DOUBLE | KEY_REPEAT)

/* ev_key (native), order 0: the std handlers, in their own order; the first
 * nonzero takes the key from every later handler and from stock */
int core_std_key(void *brain, void *ev)
{
    const fn_key *f;
    unsigned key, flags;
    (void)brain;
    if (!std_key[0] || !ev)
        return 0;
    key = *(const uint32_t *)((const uint8_t *)ev + EV_KEY);
    flags = *(const uint32_t *)((const uint8_t *)ev + EV_FLAGS);
    flags = (flags & A4_KEY_SAME) | ((flags & A4_KEY_DOWN) ? KEY_PRESS : KEY_RELEASE);
    for (f = std_key; *f; f++)
        if ((*f)(key, flags))
            return 1;
    return 0;
}

/* ============================= ev_param_turn ============================== */

/* the encoder of the last turn the panel sent: ev_enc (native) before every
 * other handler; param_apply_delta runs inside that turn's dispatch */
static volatile int last_enc = -1;

int core_std_enc(void *brain, void *ev)
{
    (void)brain;
    if (ev)
        last_enc = *(const int32_t *)((const uint8_t *)ev + EV_ENC);
    return 0;
}

/* pages.s, when no core_params entry took the turn: (view, id, delta 8.8,
 * func); knob is the turning encoder, 0..9 A..J (ENC_* in keys.h). The page
 * view's turn (0x4002598c) calls it only for a plain turn's sound edit: a
 * held trig's p-lock and a held track key's multi-track edit go elsewhere,
 * as std1 asks (no steps held), and a turn below the accelerator's step
 * makes no call. */
int core_std_param_turn(void *view, int32_t id, int32_t delta, int32_t func)
{
    const fn_turn *f;
    (void)func;
    for (f = std_param_turn; *f; f++)
        if ((*f)(view, id, delta, last_enc))
            return 1;
    return 0;
}

/* ============================ core_redraw ================================= */

static volatile int redraw;

void core_redraw(void)
{
    redraw = 1;
}

/* ===================== core_post and core_timer =========================== */

/* Both run from core's first ev_tick handler (UI task, CORE_TICK_HZ), not
 * through the kernel's: its deferred post (0x4000163c) calls fn in the
 * deferred task, not the UI task, with no check for a full ring; its soft
 * timer (0x40002e14) has 16 slots shared with stock and drops a 17th
 * silently. core_timer keeps the kernel timer's mask: one bit, 1 << k, runs
 * fn every 2^k ticks. */
#define POSTS 32                    /* a power of two */
#define TIMERS 16

static struct { void (*fn)(void *); void *arg; } posts[POSTS];
static volatile unsigned post_head, post_tail;

int core_post(void (*fn)(void *), void *arg)
{
    unsigned short sr;
    unsigned h;
    if (!fn)
        return -1;
    sr = irq_mask();
    h = post_head;
    if (h - post_tail >= POSTS) {
        irq_restore(sr);
        return -1;
    }
    posts[h & (POSTS - 1)].fn = fn;
    posts[h & (POSTS - 1)].arg = arg;
    post_head = h + 1;
    irq_restore(sr);
    return 0;
}

static struct { void (*fn)(void); unsigned mask; } timers[TIMERS];
static unsigned ticks;

int core_timer(void (*fn)(void), unsigned mask)
{
    unsigned short sr;
    int i, free_ = -1, r = -1;
    if (!fn || (mask != CORE_TIMER_STOP && (!mask || (mask & (mask - 1)))))
        return -1;                  /* one bit, as the kernel timer's */
    sr = irq_mask();
    for (i = 0; i < TIMERS; i++) {
        if (timers[i].fn == fn) {
            if (mask == CORE_TIMER_STOP)
                timers[i].fn = 0;
            else
                timers[i].mask = mask;
            r = 0;
            goto out;
        }
        if (!timers[i].fn && free_ < 0)
            free_ = i;
    }
    if (mask == CORE_TIMER_STOP)
        r = 0;
    else if (free_ >= 0) {
        timers[free_].mask = mask;
        timers[free_].fn = fn;
        r = 0;
    }
out:
    irq_restore(sr);
    return r;
}

/* ev_tick (native), before every mod: the posted calls, then the timers due */
void core_std_tick(void *ctrl)
{
    unsigned t, end = post_head;    /* calls posted meanwhile wait for the next tick */
    int i;
    (void)ctrl;
    while ((t = post_tail) != end) {
        void (*fn)(void *) = posts[t & (POSTS - 1)].fn;
        void *arg = posts[t & (POSTS - 1)].arg;
        post_tail = t + 1;
        fn(arg);
    }
    for (i = 0; i < TIMERS; i++) {
        void (*fn)(void) = timers[i].fn;
        if (fn && !(ticks & (timers[i].mask - 1)))
            fn();
    }
    ticks++;
}

/* ev_tick (native), after every mod: a redraw asked since the last one */
void core_std_tick_end(void *ctrl)
{
    if (redraw && ctrl) {
        redraw = 0;
        *((volatile uint8_t *)ctrl + CTRL_DIRTY) = 1;
    }
}
