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

/* ============================ core_setting_add ============================ */

/* A SETTINGS row of the firmware's own (a 0x54-byte MenuItem, as
 * core_additem builds it), whose label and select carry the mod's struct
 * core_setting as their std::function's payload: stock calls each with the
 * functor's address first, the payload in its first word (drive-loader-ar2's
 * MODS page rows, the same MenuItem on the Rytm). The label is stdui.s's
 * core_setting_label (it returns a std::string, so it is assembly). */
#define OP_NEW ((uintptr_t (*)(unsigned))0x400653b8)
#define ITEM_CTOR ((void (*)(void *, void *, void *, void *, void *, int, int))0x4005c7c4)
#define MENU_ADD ((void (*)(void *, void *))0x4005bffa)
#define FN_MGR 0x40164940u          /* std::function's manager for a plain pointer */

struct fn { const void *payload[2]; uint32_t mgr; const void *invoke; };

extern void core_setting_label(void);   /* stdui.s */

static void setting_select(const struct core_setting *const *f)
{
    const struct core_setting *s = *f;
    if (s && s->select)
        s->select(s);
}

static int setting_nothing(void)
{
    return 0;
}

void core_setting_add(void *menu, const struct core_setting *s)
{
    struct fn l = {{s, 0}, FN_MGR, (const void *)core_setting_label};
    struct fn c = {{s, 0}, FN_MGR, (const void *)setting_select};
    struct fn d = {{0, 0}, FN_MGR, (const void *)setting_nothing};
    struct fn h = {{menu, 0}, FN_MGR, (const void *)setting_nothing};
    void *it;
    if (!menu || !s || !s->name)
        return;
    it = (void *)OP_NEW(0x54);
    if (!it)
        return;
    ITEM_CTOR(it, &l, &c, &d, &h, -1, 8);
    MENU_ADD(menu, it);
}

/* ============================= draw helpers =============================== */

/* fb is ev_draw's bmp: stock's bitmap record (+4 width, +8 height), whose
 * rows count from the bottom. The helpers take rows from the top, clip to
 * the record's size and draw through stock's fill (0x40059f1a: x0, y0, x1,
 * y1 inclusive, rows from the bottom, colour 0 or 1) and screen.c's text. */
#define FILL ((void (*)(void *, int, int, int, int, int))0x40059f1a)
#define BMP_W(b) (*(const int32_t *)((const uint8_t *)(b) + 4))
#define BMP_H(b) (*(const int32_t *)((const uint8_t *)(b) + 8))

extern void core_text_draw(void *bmp, int font, int x, int y, int align, const char *str);
extern int core_text_width(int font, const char *str);
extern int core_font_height(int font);

void core_fill(void *fb, int x, int y, int w, int h, int on)
{
    int fw, fh, x1, y1;
    if (!fb || w <= 0 || h <= 0)
        return;
    fw = BMP_W(fb);
    fh = BMP_H(fb);
    x1 = x + w - 1;
    y1 = y + h - 1;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 >= fw)
        x1 = fw - 1;
    if (y1 >= fh)
        y1 = fh - 1;
    if (x > x1 || y > y1)
        return;
    FILL(fb, x, fh - 1 - y1, x1, fh - 1 - y, on ? 1 : 0);
}

void core_px(void *fb, int x, int y, int on)
{
    core_fill(fb, x, y, 1, 1, on);
}

/* one row's runs of equal bits, each a fill */
void core_blit(void *fb, int x, int y, int w, int h, const uint8_t *bits)
{
    int stride = (w + 7) / 8, r, c, start;
    if (!fb || !bits || w <= 0 || h <= 0)
        return;
    for (r = 0; r < h; r++) {
        const uint8_t *row = bits + r * stride;
        start = 0;
        for (c = 1; c <= w; c++) {
            int prev = (row[(c - 1) >> 3] >> (7 - ((c - 1) & 7))) & 1;
            if (c == w || ((row[c >> 3] >> (7 - (c & 7))) & 1) != prev) {
                core_fill(fb, x + start, y + r, c - start, 1, prev);
                start = c;
            }
        }
    }
}

/* TEXT_SMALL, _MEDIUM, _LARGE: stock's fonts 16 (5 rows), 1 (9) and 6 (15,
 * 2 below the line), all with lower case (screen-check-a4mk2's table) */
static const uint8_t text_fonts[3] = {16, 1, 6};

/* aligned here from core_text_width, drawn left-aligned: stock's right
 * alignment ends the text one column before x (std-check 0.2's gate) */
int core_text(void *fb, int x, int y, unsigned style, const char *str)
{
    unsigned size = (style >> 8) & 3;
    int font, w;
    if (!fb || !str || size > 2)
        return 0;
    font = text_fonts[size];
    w = core_text_width(font, str);
    if ((style & 3) == TEXT_RIGHT)
        x -= w - 1;
    else if ((style & 3) == TEXT_CENTRE)
        x -= w / 2;
    core_text_draw(fb, font, x, BMP_H(fb) - core_font_height(font) - y,
                   0 | (int)(style & TEXT_INVERT), str);
    return w;
}
