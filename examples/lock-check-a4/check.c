/* SPDX-License-Identifier: GPL-2.0-or-later */
/* lock-check: the smallest user of core-a4 0.5's lockable parameters and
 * their p-locks on the Analog Four MKII (OS 1.56).
 *
 * mod.json declares four lockable fields, so the lockfile gives each a lock
 * code of its kind, and registers them in core_lock_params:
 *   A  a sound field, 0..127, the synth tracks' (knob A of LOCK CHECK);
 *   C  a sound field, 0..1000, above 127, so its locks take two rows
 *      (knob B of LOCK CHECK);
 *   K  a kit field, 0..127, the FX track's (knob A of LOCK CHECK FX);
 *   V  a kit field with "track": "cv", 0..127, the CV track's (knob A of
 *      LOCK CHECK CV).
 * LOCK CHECK sits behind the AMP key on a synth track, after stock's AMP
 * page; LOCK CHECK FX and LOCK CHECK CV behind the LFO key on the FX and CV
 * tracks. The synth knobs show the current track's sound of the current kit,
 * the FX and CV knobs the current kit. With a trig held a knob sets that
 * trig's p-lock instead, and shows it.
 *
 * Every audio block (ev_render_out) lk_seen records, after the magic LKCK:
 * for voices 0..5 the playing lock of A and C (core_lock_value_voice) and
 * C's slide delta, and core_lock_value of A, C, K and V on tracks 1, 1, 5
 * and 6, for the emulator's checks; and, since a lock lasts only while its
 * trig plays, for each of A, C, K and V the blocks it was seen in and the
 * last value seen. Two bars along the bottom of the screen
 * show track 1's lock of A (half a pixel a step) and of C (a pixel per 8),
 * none while its trig has none.
 */
#include "analog-four-mk2/store.h"
#include "analog-four-mk2/lock.h"
#include "analog-four-mk2/pages.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#define FILLRECT ((fillrect_t)FILLRECT_AT)

STORE_FIELD(lk_a);
STORE_FIELD(lk_c);
STORE_FIELD(lk_k);
STORE_FIELD(lk_v);

#define ID_A 0x200
#define ID_C 0x201
#define ID_K 0x202
#define ID_V 0x203

const struct core_lock_param lk_param_a = {STORE(lk_a), ID_A, 0, 127, "LKA", "Lock Check A", 0};
const struct core_lock_param lk_param_c = {STORE(lk_c), ID_C, 0, 1000, "LKC", "Lock Check C",
                                           CORE_LOCK_CATEGORY("LCHK")};
const struct core_lock_param lk_param_k = {STORE(lk_k), ID_K, 0, 127, "LKK", "Lock Check K", 0};
const struct core_lock_param lk_param_v = {STORE(lk_v), ID_V, 0, 127, "LKV", "Lock Check V", 0};

/* the current kit: the stored project's working kit index, the live kits */
static unsigned char *kit(void)
{
    const volatile unsigned char *ix = (const unsigned char *)0x4045c974 + 0x199330;
    unsigned k = (unsigned)ix[0] << 24 | (unsigned)ix[1] << 16 | (unsigned)ix[2] << 8 | ix[3];
    return k < 128 ? (unsigned char *)0x411e9936 + 0x960 * k : 0;
}

/* the current track (0..5), as stock's lock calls find it */
#define APP ((unsigned (*)(void))0x401494de)
#define APP_TRACKS ((unsigned (*)(unsigned))0x400a51be)
#define CURRENT_TRACK ((int (*)(unsigned))0x400adbd2)

static void *item(int32_t id)
{
    unsigned char *k = kit();
    int t;
    if (!k)
        return 0;
    if (id == ID_K || id == ID_V)
        return k;
    t = CURRENT_TRACK(APP_TRACKS(APP()));
    return (unsigned)t < 4 ? k + 0x1c + 0x152 * t : 0;
}

static unsigned field(int32_t id)
{
    return id == ID_A ? STORE(lk_a) : id == ID_C ? STORE(lk_c) : id == ID_K ? STORE(lk_k) : STORE(lk_v);
}

static int lk_get(int32_t id, int32_t *value)
{
    void *s = item(id);
    if (!s)
        return 0;
    *value = core_store_get(field(id), s, 0) << 8;
    return 1;
}

static int lk_delta(void *view, int32_t id, int32_t delta, int32_t func)
{
    void *s = item(id);
    int v, top = id == ID_C ? 1000 : 127;
    (void)view;
    (void)func;
    if (!s)
        return 0;
    v = (int)core_store_get(field(id), s, 0) + (delta > 0 ? (delta + 255) / 256 : -((255 - delta) / 256));
    core_store_set(field(id), s, 0, v < 0 ? 0 : v > top ? top : v);
    return 1;
}

/* the value as a number */
static int lk_text(struct core_text *t)
{
    int v = t->value >> 8, n = 0, d;
    char *b = t->buf;
    if (v < 0)
        v = 0;
    for (d = 1000; d > 1; d /= 10)
        if (v >= d || n)
            b[n++] = '0' + v / d % 10;
    b[n++] = '0' + v % 10;
    b[n] = 0;
    return CORE_TEXT_DONE;
}

const struct core_param lk_params = {ID_A, ID_V, lk_get, lk_delta, lk_text};

const struct core_page lk_page = {49, "LOCK CHECK", {ID_A, ID_C}};
const struct core_page lk_fx_page = {50, "LOCK CHECK FX", {ID_K}};
const struct core_page lk_cv_page = {51, "LOCK CHECK CV", {ID_V}};
const struct core_view_page lk_view = {CORE_TRACK_SYNTH, CORE_KEY_AMP, 49, 0};
const struct core_view_page lk_fx_view = {CORE_TRACK_FX, CORE_KEY_LFO, 50, 0};
const struct core_view_page lk_cv_view = {CORE_TRACK_CV, CORE_KEY_LFO, 51, 0};

/* the record the emulator reads */
struct seen {
    char magic[4];
    int32_t blocks;
    int16_t a[6], c[6], slide_c[6];
    int16_t track_a, track_c, track_k, track_v;
    int32_t hits[4];                    /* A, C, K, V: blocks with a lock */
    int16_t last[4];                    /* their last lock seen, -1 none */
};

volatile struct seen lk_seen = {{'L', 'K', 'C', 'K'}, .last = {-1, -1, -1, -1}};

static void note(int i, int v)
{
    if (v >= 0) {
        lk_seen.hits[i]++;
        lk_seen.last[i] = v;
    }
}

void lk_render_out(void)
{
    int v, ca = core_lock_code(STORE(lk_a)), cc = core_lock_code(STORE(lk_c));
    for (v = 0; v < 6; v++) {
        lk_seen.a[v] = core_lock_value_voice(v, ca);
        lk_seen.c[v] = core_lock_value_voice(v, cc);
        lk_seen.slide_c[v] = core_slide_delta_voice(v, cc);
    }
    lk_seen.track_a = core_lock_value(0, STORE(lk_a));
    lk_seen.track_c = core_lock_value(0, STORE(lk_c));
    lk_seen.track_k = core_lock_value(4, STORE(lk_k));
    lk_seen.track_v = core_lock_value(5, STORE(lk_v));
    note(0, lk_seen.track_a);
    note(1, lk_seen.track_c);
    note(2, lk_seen.track_k);
    note(3, lk_seen.track_v);
    lk_seen.blocks++;
}

void lk_draw(void *bmp, void *ctrl)
{
    int v;
    (void)ctrl;
    FILLRECT(bmp, 0, 0, 127, 1, 0);
    if ((v = lk_seen.track_a) >= 0)
        FILLRECT(bmp, 0, 0, v / 2, 1, 1);
    FILLRECT(bmp, 0, 3, 127, 4, 0);
    if ((v = lk_seen.track_c) >= 0)
        FILLRECT(bmp, 0, 3, v / 8, 4, 1);
}
