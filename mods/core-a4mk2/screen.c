/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.6: the screen calls (README, "Screen calls (0.6)";
 * elekloader/sdk/include/analog-four-mk2/screen.h), core-ar2 0.13's
 * screen.c with the A4's addresses (OS 1.56). Nothing of stock's code or
 * glyphs is copied, core calls it.
 *
 *   - text in stock's 17 fonts, drawn by stock's own text routine, and a
 *     bitmap record over a mod's own buffers to draw into;
 *   - a key event handed to stock alone (Brain::key), later or from inside
 *     ev_key;
 *   - whether a stock overlay or window is on the view stack;
 *   - the track level readout's window after a LEVEL turn reached stock,
 *     and a copy of the header field it shows in;
 *   - stock's progress box (the one SAVE PROJECT opens): open, fill, close.
 *
 * All but core_progress_set are for the UI task (ev_tick, ev_draw, ev_key,
 * ev_enc and what they call).
 */

#include <stdint.h>

/* ---- stock's routines ---- */
#define BITMAP_INIT ((void (*)(void *, int, int, void *, void *))0x40059222) /* owned = 0 */
/* stock returns pointers in d0, where this compiler looks for an int: every
 * routine below that returns a pointer is declared to return uintptr_t */
#define FONT_DATA ((uintptr_t (*)(const void *))0x4005b3dc)                  /* font -> *(font + 4) */
#define TEXT_DRAW ((void (*)(void *, const void *, int, int, int, int, const char *, const char *, ...))0x4005ad40)
#define TEXT_WIDTH ((int (*)(const void *, int, const void *))0x4005b3e6)  /* (font, -1, std::string *) */
#define STRING_NEW ((void (*)(void *, const char *, void *))0x4018471e)     /* std::string(cstr, alloc) */
#define STRING_DEL ((void (*)(void *))0x40182c6e)
#define FONTS 0x41735764u                                                    /* 17 x {vtable, data} (4Font) */
#define NFONTS 17
/* Brain::key: unlike the Rytm's, it drops the view controller's answer
 * (0x4006304c) and returns its listeners' call's, 0x40096aee(brain,
 * 0x7ffffff) */
#define KEYDISP ((int (*)(void *, void *))0x40098b36)

/* the progress box: ProgressWindow (0x88 bytes), its shared_ptr's count,
 * and the view controller's push, as SAVE PROJECT's SAVING box (0x4003d448) */
#define OP_NEW ((uintptr_t (*)(unsigned))0x400653b8)
#define PROGRESS_CTOR ((void (*)(void *, const char *))0x40060462)
#define PROGRESS_SET ((void (*)(void *, int, int))0x400603c8)
#define COUNT_NEW ((void (*)(void **, void *))0x4014f2e8)      /* *count = a new count of 1 for win */
#define COUNT_COPY ((void (*)(void **, void *const *))0x401465dc)  /* *dst = *src, one more use */
#define COUNT_RELEASE ((void (*)(void **))0x40144764)          /* one use less; the last frees win */
#define VIEW_PUSH ((int (*)(void *, void *, int))0x4006293a)  /* (controller, &shared_ptr, 0) */
#define WIN_CLOSE 0x2c                                         /* the window's vtable: close (0x40061afc) */

/* ---- the view controller (ev_tick's and ev_draw's ctrl) ----
 * Its push (0x4006293a) is the Rytm's byte for byte: +20 the list's end
 * node, +28 the first node; a node's +0 is the next node and +8 its view.
 * Windows are views on the same list. */
#define CTRL_END 20
#define CTRL_FIRST 28
#define VT_MAINLAST 0x4018d228u     /* HelpBubbleView: the main screen's last view */
#define VT_SCALEMENU 0x40192e70u    /* ScaleMenuView */
#define CODE_LO 0x40000400u
#define CODE_HI 0x40262b38u         /* the image's end */

/* ---- events ---- */
#define KEY_EVENT_SIZE 24           /* KeyEvent: vtable, +4 taken, +8 time, +12 code, +16 flags, +20 */
#define ENC_INDEX 12                /* EncoderEvent +12: the encoder, 0..9 A..J, then LEVEL / DATA */
/* 1.56 runs the Analog Keys too: bit 20 of the boot word is its flag, and
 * there LEVEL / DATA is encoder 11 */
#define BOOT_WORD (*(const volatile uint32_t *)0x401ff54c)
#define ENC_LEVEL ((BOOT_WORD & 0x100000) ? 11 : 10)
#define LEVEL_TICKS 47              /* 2.35 s at 20 ticks a second; stock's countdown is the Rytm's, 70 */

/* ---- the LEVEL readout's field: rows 0..8 (from the top), columns 0..69,
 * in a 128 x 64 bitmap: pixels at +16, 8 bytes a column, byte 7 - (row / 8)
 * holding row & 7's bit (rows count from the bottom, so the top row is byte
 * 7's bit 7 ... ); the Rytm's field, which the A4's "Trk Level %d" is drawn
 * in by the same routine family (0x40021a54) ---- */
#define FIELD_COLS 70

static void *brain;
static void *ctrl_seen;
static volatile int level_left;

/* ================================ text ==================================== */

void core_bitmap_init(void *bmp, int w, int h, void *bits, void *mask)
{
    BITMAP_INIT(bmp, w, h, bits, mask);
}

static const void *font_data(int font)
{
    if ((unsigned)font >= NFONTS)
        return 0;
    return (const void *)FONT_DATA((const void *)(FONTS + 8u * (unsigned)font));
}

void core_text_draw(void *bmp, int font, int x, int y, int align, const char *str)
{
    const void *f = font_data(font);
    if (!f || !bmp || !str)
        return;
    TEXT_DRAW(bmp, f, x, y, align & 3, (align >> 4) & 1, str, "%s", str);
}

int core_text_width(int font, const char *str)
{
    const void *f = font_data(font);
    uintptr_t s;
    char alloc;
    int w;
    if (!f || !str)
        return 0;
    STRING_NEW(&s, str, &alloc);
    w = TEXT_WIDTH(f, -1, &s);
    STRING_DEL(&s);
    return w;
}

/* the font's height and descent in pixels (its data's +6 and +4) */
int core_font_height(int font)
{
    const unsigned char *f = font_data(font);
    return f ? *(const uint16_t *)(f + 6) : 0;
}

int core_font_descent(int font)
{
    const unsigned char *f = font_data(font);
    return f ? *(const uint16_t *)(f + 4) : 0;
}

/* ================================ keys ==================================== */

/* ev_key, before every other handler: keeps the brain for core_stock_key */
int core_screen_key(void *b, void *ev)
{
    (void)ev;
    brain = b;
    return 0;
}

/* the event to stock alone, as if no mod had taken it; -1 before the first
 * key event of the boot (no brain yet), else 0: the A4's Brain::key doesn't
 * pass the view controller's answer on */
int core_stock_key(void *ev)
{
    if (!brain || !ev)
        return -1;
    KEYDISP(brain, ev);
    return 0;
}

/* ============================== overlays ================================== */

/* a view's class name from its RTTI (vtable - 4 -> typeinfo, +4 -> name),
 * without the length prefix; 0 when it doesn't look like one */
static const char *class_name(uint32_t vt)
{
    uint32_t ti, nm;
    const char *s;
    if (vt < CODE_LO + 4 || vt >= CODE_HI || (vt & 1))
        return 0;
    ti = *(const uint32_t *)(vt - 4);
    if (ti < CODE_LO || ti >= CODE_HI || (ti & 1))
        return 0;
    nm = *(const uint32_t *)(ti + 4);
    if (nm < CODE_LO || nm >= CODE_HI)
        return 0;
    s = (const char *)nm;
    while (*s >= '0' && *s <= '9')
        s++;
    return s;
}

static int ends(const char *s, int n, const char *x)
{
    int m = 0, i;
    while (x[m])
        m++;
    if (m > n)
        return 0;
    for (i = 0; i < m; i++)
        if (s[n - m + i] != x[i])
            return 0;
    return 1;
}

static int same(const char *s, int n, const char *x)
{
    int i;
    for (i = 0; i < n; i++)
        if (s[i] != x[i])
            return 0;
    return x[n] == 0;
}

#define K_NONE 0     /* a view that covers nothing: a pad mode's, an unknown */
#define K_VIEW 1     /* CORE_OVERLAY_VIEW */
#define K_WINDOW 2   /* CORE_OVERLAY_WINDOW */

/* the views stacked over the main screen that are not overlays: those whose
 * draw (vtable +0x10) is View's own, a bare rts (QuickMuteMenuView draws,
 * but sits below the main screen's last view) */
static const char *const pad_views[] = {
    "KeyboardView", "QuickPerformanceView", "QuickPerformanceSelectionView", "TrackSelectionView",
    "PatternGridView", "StepAttrView",
};
/* overlays whose names don't end in MenuView or Window: the A4's view
 * classes that draw */
static const char *const overlays[] = {
    "ControlInputConfig", "ExtMidiControlSetupView", "HighScoreView", "KitBrowserView",
    "ModDestListView", "ModSetupView", "MultiMapEditView", "NameView", "PatternSelectionView",
    "ProjectManager", "SongBrowserView", "SongEditView", "SoundBrowser", "SoundManager",
    "SoundSlotListView", "SysexObjectCounterView", "TagSelector", "TapTempoView",
    "TestCompletedView", "TimeoutView", "UITestView",
};

static int kind_of(uint32_t vt)
{
    const char *s = class_name(vt);
    int n = 0, j;
    if (vt == VT_SCALEMENU)
        return K_VIEW;
    if (!s)
        return K_NONE;
    while (n < 48 && s[n])
        n++;
    if (n >= 48)
        return K_NONE;
    if (ends(s, n, "Window"))
        return K_WINDOW;
    for (j = 0; j < (int)(sizeof pad_views / sizeof pad_views[0]); j++)
        if (same(s, n, pad_views[j]))
            return K_NONE;
    if (ends(s, n, "MenuView"))
        return K_VIEW;
    for (j = 0; j < (int)(sizeof overlays / sizeof overlays[0]); j++)
        if (same(s, n, overlays[j]))
            return K_VIEW;
    return K_NONE;
}

/* a few classes' answers, so a tick costs a walk of the list and no names */
#define NCACHE 8
static uint32_t cache_vt[NCACHE];
static uint8_t cache_kind[NCACHE];
static unsigned cache_next;

static int kind_cached(uint32_t vt)
{
    unsigned i;
    for (i = 0; i < NCACHE; i++)
        if (cache_vt[i] == vt)
            return cache_kind[i];
    i = cache_next++ % NCACHE;
    cache_kind[i] = (uint8_t)kind_of(vt);
    cache_vt[i] = vt;
    return cache_kind[i];
}

int core_stock_overlay(void)
{
    const uint8_t *c = ctrl_seen;
    const uint32_t *n;
    int k, after = 0, bits = 0;
    if (!c)
        return 0;
    n = *(const uint32_t *const *)(c + CTRL_FIRST);
    for (k = 0; k < 32 && n && (const uint8_t *)n != c + CTRL_END; k++) {
        const uint32_t *v = (const uint32_t *)n[2];
        if (v) {
            uint32_t vt = v[0];
            int kind = vt == VT_MAINLAST ? K_NONE : kind_cached(vt);
            if (kind == K_WINDOW)
                bits |= 2;
            else if (kind == K_VIEW && after)
                bits |= 1;
            if (vt == VT_MAINLAST)
                after = 1;
        }
        n = (const uint32_t *)n[0];
    }
    if (!after)
        bits |= 4;                 /* the main screen isn't up: a whole-screen menu, or boot */
    return bits;
}

/* ============================ LEVEL readout =============================== */

/* ev_enc, after every other handler: only a turn no mod took reaches here,
 * and it goes on to stock */
int core_screen_enc(void *b, void *ev)
{
    (void)b;
    if (ev && *(const int32_t *)((const uint8_t *)ev + ENC_INDEX) == ENC_LEVEL)
        level_left = LEVEL_TICKS;
    return 0;
}

int core_level_readout(void)
{
    return level_left;
}

/* the field in a 128 x 64 bitmap: 2 bytes a column, 140 in all */
static uint8_t *pixels(void *bmp)
{
    const uint8_t *b = bmp;
    if (!b || *(const int32_t *)(b + 4) != 128 || *(const int32_t *)(b + 8) != 64)
        return 0;
    return *(uint8_t *const *)(b + 16);
}

void core_level_field_save(void *bmp, void *keep)
{
    const uint8_t *px = pixels(bmp);
    uint8_t *k = keep;
    int x;
    if (!px || !k)
        return;
    for (x = 0; x < FIELD_COLS; x++) {
        k[2 * x] = px[8 * x + 7];          /* rows 0..7 */
        k[2 * x + 1] = px[8 * x + 6] & 1;  /* row 8 */
    }
}

void core_level_field_put(void *bmp, const void *keep)
{
    uint8_t *px = pixels(bmp);
    const uint8_t *k = keep;
    int x;
    if (!px || !k)
        return;
    for (x = 0; x < FIELD_COLS; x++) {
        px[8 * x + 7] = k[2 * x];
        px[8 * x + 6] = (uint8_t)((px[8 * x + 6] & 0xfe) | (k[2 * x + 1] & 1));
    }
}

/* ============================= progress box =============================== */

#define NBOX 4
static struct {
    void *win;      /* the ProgressWindow */
    void *count;    /* core's own use of it (its shared_ptr's count) */
} box[NBOX];

int core_progress_open(const char *title)
{
    struct { void *win, *count; } sp;
    void *win, *count = 0;
    int h;
    if (!ctrl_seen)
        return -1;
    for (h = 0; h < NBOX && box[h].win; h++)
        ;
    if (h == NBOX)
        return -1;
    win = (void *)OP_NEW(0x88);
    if (!win)
        return -1;
    PROGRESS_CTOR(win, title ? title : "");
    COUNT_NEW(&count, win);
    box[h].win = win;
    box[h].count = count;
    sp.win = win;
    COUNT_COPY(&sp.count, &box[h].count);   /* the controller's own use */
    VIEW_PUSH(ctrl_seen, &sp, 0);
    COUNT_RELEASE(&sp.count);
    return h;
}

void core_progress_set(int h, int done, int total)
{
    void *win;
    if ((unsigned)h >= NBOX || !(win = box[h].win))
        return;
    PROGRESS_SET(win, done, total);
}

void core_progress_close(int h)
{
    void *win;
    if ((unsigned)h >= NBOX || !(win = box[h].win))
        return;
    (*(void (**)(void *))(*(uint8_t *const *)win + WIN_CLOSE))(win);
    box[h].win = 0;
    COUNT_RELEASE(&box[h].count);
    box[h].count = 0;
}

/* ================================= tick =================================== */

void core_screen_tick(void *ctrl)
{
    ctrl_seen = ctrl;
    if (level_left > 0)
        level_left--;
}
