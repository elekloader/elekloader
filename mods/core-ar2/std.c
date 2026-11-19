/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.17: the standard events (std1) at the sites core already owns,
 * and the helpers that go with them (README, "Standard events (0.17)";
 * elekloader/sdk/include/std1.h, analog-rytm-mk2/keys.h, params.h,
 * ports.h). mod.json's "std" map sends ev_tick, ev_draw, ev_settings,
 * ev_project, ev_render_in and ev_seq_tick to the native tables (the same
 * prototype, or its first part); the five below are thunks, a native
 * handler of core's (or a call from core's own site) that calls a std
 * table with the arguments converted:
 *
 *   ev_boot        std_boot, from .boot (core_boot_init, machines.c): f(void)
 *   ev_key         std_key, a native ev_key handler: f(key, flags)
 *   ev_enc         std_enc, a native ev_enc handler: f(enc, delta)
 *   ev_param_turn  std_param_turn, from pages.s's param_apply_delta when no
 *                  core_params entry took the turn: f(view, id, delta, knob)
 *   ev_sysex_in    std_sysex_in, a native ev_sysex_in handler: f(msg, len,
 *                  port), the MIDI task's iface as a PORT_*
 *
 * and core_redraw, core_timer and core_post, all of them run from core's
 * own ev_tick handlers, in the UI task; core_setting_add for ev_settings;
 * core_px, core_fill, core_text and core_blit for ev_draw. The A4's std.c
 * (core-a4mk2 0.7) is the model; what differs is said where it does.
 */

#include <stdint.h>
#include <std1.h>
#include <analog-rytm-mk2/keys.h>
#include <analog-rytm-mk2/ports.h>

typedef void (*fn_void)(void);
typedef int (*fn_key)(unsigned key, unsigned flags);
typedef int (*fn_enc)(unsigned enc, int delta);
typedef int (*fn_turn)(void *view, int id, int delta, int knob);
typedef int (*fn_msg)(const uint8_t *msg, unsigned len, unsigned port);

extern const fn_void std_boot[];
extern const fn_key std_key[];
extern const fn_enc std_enc[];
extern const fn_turn std_param_turn[];
extern const fn_msg std_sysex_in[];

/* the view controller's "compose again" byte (core.s's ev_tick reads it) */
#define CTRL_DIRTY 0x20
/* KeyEvent: +12 the key, +16 its flags; EncoderEvent: +12 the encoder
 * (0..7 A..H, 8 LEVEL / DATA), +16 the turn's steps (screen.c) */
#define EV_KEY 12
#define EV_FLAGS 16
#define EV_ENC 12
#define EV_DELTA 16

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

/* core_boot_init's last call (machines.c): the mods' RAM image is in place,
 * their .bss clear, core's storage and machines set up; the OS has not
 * started, so a handler calls no firmware */
void core_std_boot(void)
{
    const fn_void *f;
    for (f = std_boot; *f; f++)
        (*f)();
}

/* ================================ ev_key ================================== */

/* Rytm KeyEvent flags (+16): bit 0 down, 3 repeat, 2 double, 1 FUNC held,
 * as std1's; a release is 0x10, or 0 when the key was used with another
 * (TRK + a pad, smpl-machines' finding), so anything without bit 0, or with
 * 0x10, is std1's KEY_RELEASE. Bits 1..3 pass as they are. To confirm on a
 * unit: bit 2 and bit 3 on their own keys. */
#define AR2_KEY_DOWN 0x01u
#define AR2_KEY_UP 0x10u
#define AR2_KEY_SAME (KEY_FUNC | KEY_DOUBLE | KEY_REPEAT)

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
    flags = (flags & AR2_KEY_SAME)
            | ((flags & AR2_KEY_DOWN) && !(flags & AR2_KEY_UP) ? KEY_PRESS : KEY_RELEASE);
    for (f = std_key; *f; f++)
        if ((*f)(key, flags))
            return 1;
    return 0;
}

/* ============================ ev_enc, ev_param_turn ======================= */

/* the encoder of the last turn the panel sent: param_apply_delta runs inside
 * that turn's dispatch, after every ev_enc handler */
static volatile int last_enc = -1;

/* ev_enc (native), order 0: keeps the encoder for ev_param_turn's knob, then
 * the std handlers (enc 0..7 A..H, ENC_LEVEL; delta as the panel sent it);
 * the first nonzero takes the turn from every later handler and from stock */
int core_std_enc(void *brain, void *ev)
{
    const fn_enc *f;
    unsigned enc;
    int delta;
    (void)brain;
    if (!ev)
        return 0;
    enc = *(const uint32_t *)((const uint8_t *)ev + EV_ENC);
    last_enc = (int)enc;
    if (!std_enc[0] || enc >= ENC_COUNT)
        return 0;
    delta = *(const int32_t *)((const uint8_t *)ev + EV_DELTA);
    for (f = std_enc; *f; f++)
        if ((*f)(enc, delta))
            return 1;
    return 0;
}

/* pages.s, when no core_params entry took the turn: (view, id, delta 8.8,
 * func); knob is the turning encoder, ENC_* in keys.h. The same site as
 * core_params' delta (param_apply_delta 0x400378a6), so the turns it sees
 * are the page views' */
int core_std_param_turn(void *view, int32_t id, int32_t delta, int32_t func)
{
    const fn_turn *f;
    (void)func;
    for (f = std_param_turn; *f; f++)
        if ((*f)(view, id, delta, last_enc))
            return 1;
    return 0;
}

/* ============================== ev_sysex_in =============================== */

/* the MIDI task's iface bits: 0x08 / 0x10 DIN, 0x02 / 0x04 USB cable 0 (the
 * AR2 std1 reads of the SysEx router; USB cable 1 never reaches it) */
static unsigned in_port(int32_t iface)
{
    if (iface & 0x18)
        return PORT_DIN;
    if (iface & 0x06)
        return PORT_USB;
    return 0;
}

/* ev_sysex_in (native), order 0 */
int core_std_sysex_in(const uint8_t *msg, int32_t len, int32_t iface)
{
    const fn_msg *f;
    unsigned port;
    if (!std_sysex_in[0] || !msg || len <= 0)
        return 0;
    port = in_port(iface);
    for (f = std_sysex_in; *f; f++)
        if ((*f)(msg, (unsigned)len, port))
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

/* Both run from core's first ev_tick handler (UI task, CORE_TICK_HZ), as on
 * the A4: the Rytm kernel's deferred post and soft timer are not read yet.
 * core_timer takes the kernel timer's mask all the same: one bit, 1 << k,
 * runs fn every 2^k ticks. */
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

/* ev_tick (native), after every mod's (before core's LEDs): a redraw asked
 * since the last one */
void core_std_tick_end(void *ctrl)
{
    if (redraw && ctrl) {
        redraw = 0;
        *((volatile uint8_t *)ctrl + CTRL_DIRTY) = 1;
    }
}

/* ============================ core_setting_add ============================ */

/* A SETTINGS row of the firmware's own (a 0x54-byte MenuItem, as
 * core_additem builds it, with its addresses), whose label and select carry
 * the mod's struct core_setting as their std::function's payload: stock
 * calls each with the functor's address first, the payload in its first
 * word (drive-loader-ar2's MODS page rows). The label is stdui.s's
 * core_setting_label (it returns a std::string, so it is assembly). */
#define OP_NEW ((uintptr_t (*)(unsigned))0x40082a40)
#define ITEM_CTOR ((void (*)(void *, void *, void *, void *, void *, int, int))0x40072fbc)
#define MENU_ADD ((void (*)(void *, void *))0x400727f2)
#define FN_MGR 0x4019411au          /* std::function's manager for a plain pointer */

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

/* fb is ev_draw's bmp: stock's 28-byte bitmap record (+4 width, +8 height,
 * +16 the pixels; analog-rytm-mk2/screen.h). The pixels are columns of
 * height / 8 bytes, a column's top row in its first byte's top bit (row r
 * from the bottom, as stock's calls count, is byte height / 8 - 1 - r / 8,
 * bit r % 8: screen.c's LEVEL field). The helpers take rows from
 * the top, clip to the record's size and write the pixels themselves; the
 * mask, if the record has one, is left as it is. */
#define BMP_W(b) (*(const int32_t *)((const uint8_t *)(b) + 4))
#define BMP_H(b) (*(const int32_t *)((const uint8_t *)(b) + 8))
#define BMP_PX(b) (*(uint8_t *const *)((const uint8_t *)(b) + 16))

extern void core_text_draw(void *bmp, int font, int x, int y, int align, const char *str);
extern int core_text_width(int font, const char *str);
extern int core_font_height(int font);

void core_fill(void *fb, int x, int y, int w, int h, int on)
{
    int fw, fh, x1, y1, stride, c, r;
    uint8_t *px;
    if (!fb || w <= 0 || h <= 0)
        return;
    fw = BMP_W(fb);
    fh = BMP_H(fb);
    px = BMP_PX(fb);
    if (!px || fw <= 0 || fh <= 0 || (fh & 7))
        return;
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
    stride = fh / 8;
    for (c = x; c <= x1; c++) {
        uint8_t *col = px + c * stride;
        for (r = y; r <= y1; r++) {
            uint8_t m = (uint8_t)(0x80u >> (r & 7));
            if (on)
                col[r >> 3] |= m;
            else
                col[r >> 3] &= (uint8_t)~m;
        }
    }
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

/* TEXT_SMALL, _MEDIUM, _LARGE: stock's fonts 16 (5 rows, the main screen's),
 * 1 (9) and 6 (15, 2 below the line), as the A4's (screen.h's heights) */
static const uint8_t text_fonts[3] = {16, 1, 6};

/* aligned here from core_text_width, drawn left-aligned, as the A4's */
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
