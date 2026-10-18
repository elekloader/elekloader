/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GAIN, the smallest effect on fxkit (digitone-mk1/fxkit.h): a level and a tone on the inputs, a template for an
 * effect mod. GAIN in the Mod Menu (hold a track key) makes it the active effect and opens its page:
 *   SYN1   its knobs: A GAIN, B TONE (a one-pole low-pass), C PHAS (the right side inverted or not)
 *   AMP    fxkit's MIXER;  LFO  LFO 1, again LFO 2;  LEFT, RIGHT  the pages in turn
 *   RECORD LOCK mode: hold a trig and turn a knob to lock that step (FUNC + trig clears it, PAGE the next 16)
 *   MIDI   the stock UI, for the MIDI tracks: GAIN keeps playing, and its tile brings the page back
 *   NO     GAIN off: the synths again
 * Its knobs, the mixer and the LFOs are saved with the project; its locks with the pattern. It names no firmware
 * address, so its OS 1.44 port is empty. */
#include <stdint.h>
#include "digitone-mk1/core3.h"
#include "digitone-mk1/fxkit.h"

#define KEY_FUNC    1
#define KEY_MIDI    2
#define KEY_TEMPO   9
#define KEY_RECORD  10
#define KEY_PLAY    11
#define KEY_STOP    12
#define KEY_NO      14
#define KEY_LEFT    17
#define KEY_RIGHT   18
#define KEY_SYN1    21
#define KEY_AMP     24
#define KEY_LFO     25
#define KEY_PUSH_A  46
#define EV_KEY(ev)   (*(uint32_t *)((char *)(ev) + 12))
#define EV_FLAGS(ev) (*(uint32_t *)((char *)(ev) + 16))
#define EV_DELTA(ev) (*(int32_t *)((char *)(ev) + 16))

static const char *const gain_phase[] = { "NORM", "INV" };
static const struct fx_param gain_params[] = {
    { FX_S_P1, FX_KNOB, 0, 100, "GAIN", "Gain", 0 },
    { FX_S_P1 + 1, FX_KNOB, 0, 127, "TONE", "Tone", 0 },
    { FX_S_P1 + 2, FX_OPT, 2, 0, "PHAS", "Phase R", gain_phase },
    { 0 }
};
static const uint8_t gain_page[8] = { FX_S_P1, FX_S_P1 + 1, FX_S_P1 + 2 };

/* ---- the render: fxkit calls these while GAIN is the active effect ---------------------------------- */
static int32_t g_gain = 4096, g_a = 32767, g_inv, g_l, g_r;   /* Q12, Q15, the low-pass's states (Q15) */

static void gain_values(const int16_t *val, uint32_t flags)
{
    int32_t g = val[FX_S_P1] >> 4, t = val[FX_S_P1 + 1] >> 8;
    (void)flags;
    g_gain = g * g / 625;                       /* (v / 100)^2, Q12 */
    g_a = 512 + t * 254;                        /* 0: dark .. 127: open */
    g_inv = val[FX_S_P1 + 2] >= 128;
}

static void gain_render(int32_t *out, const int32_t *in)
{
    int i;
    for (i = 0; i < 64; i += 2) {
        int32_t L = ((in[i] >> 16) * g_gain) >> 12, R = ((in[i + 1] >> 16) * g_gain) >> 12;
        g_l += ((L - g_l) * g_a) >> 15;
        g_r += ((R - g_r) * g_a) >> 15;
        L = g_l > 32767 ? 32767 : g_l < -32768 ? -32768 : g_l;
        R = g_r > 32767 ? 32767 : g_r < -32768 ? -32768 : g_r;
        out[i] = L << 16;
        out[i + 1] = (g_inv ? -R : R) << 16;
    }
}

const struct fx_effect gain_effect = {
    FX_ABI, "GAIN", gain_params, 0, 0, gain_values, gain_render,
};
#define E (&gain_effect)

/* ---- the project: its knobs, the mixer and the LFOs ------------------------------------------------ */
static struct { uint8_t ver, knob[3], mix[FX_PACK]; } __attribute__((aligned(4))) save;
#define SAVE_VER 1

static void gain_loaded(int32_t found)
{
    int i;
    found = found && save.ver == SAVE_VER;
    for (i = 0; i < 3; i++)
        fx_set(E, gain_page[i], found ? save.knob[i] << 8 : gain_params[i].def << 8);
    fx_unpack(E, found ? save.mix : 0);
}

const struct core_projdata gain_projdata = { 0x47414e31u /* "GAN1" */, sizeof(save), &save, gain_loaded };

/* ---- the page --------------------------------------------------------------------------------------- */
static uint8_t open, pg, func;                  /* pg: 0 GAIN, 1 MIXER, 2 LFO 1, 3 LFO 2 */
static int32_t acc[8];

static void gain_open(void *brain, void *ev, int32_t track)
{
    (void)brain, (void)ev, (void)track;
    if (fx_active() != E)
        g_l = g_r = 0;
    fx_activate(E);
    open = 1;
    pg = 0;
    func = 0;
}

const struct core_menu_item gain_menu = { "GAIN", gain_open, 0, 0 };

static void hide(void)
{
    open = 0;
    fx_lock_mode_set(0);
}

int gain_key(void *brain, void *ev)
{
    uint32_t key = EV_KEY(ev), fl = EV_FLAGS(ev);
    (void)brain;
    if (!open)
        return 0;
    if (key == KEY_FUNC) {
        func = (uint8_t)(fl & 1);
        return 1;
    }
    if (fl & 2)
        func = 1;
    if (key == KEY_PLAY || key == KEY_STOP || key == KEY_TEMPO)
        return 0;                               /* the sequencer stays the Digitone's */
    if (pg && fx_page_key(E, pg - 1, key, fl))
        return 1;                               /* DEST's list on an LFO page */
    if (fx_lock_key(E, key, fl, func))
        return 1;
    if (!(fl & 1) || (fl & 8))
        return 1;                               /* releases, repeats */
    if (key == KEY_MIDI) {
        hide();
        return 0;                               /* the stock's MIDI mode: GAIN keeps playing */
    } else if (key == KEY_NO) {
        hide();
        fx_deactivate(E);
    } else if (key == KEY_RECORD) {
        fx_lock_mode_set(!fx_lock_mode());
    } else if (key == KEY_SYN1) {
        pg = 0;
    } else if (key == KEY_AMP) {
        pg = 1;
    } else if (key == KEY_LFO) {
        pg = pg == 2 ? 3 : 2;
    } else if (key == KEY_LEFT || key == KEY_RIGHT) {
        pg = (uint8_t)((pg + (key == KEY_LEFT ? 3 : 1)) % 4);
    } else if (key >= KEY_PUSH_A && key < KEY_PUSH_A + 8) {
        if (pg)
            fx_page_push(E, pg - 1, key - KEY_PUSH_A);
        else
            fx_knob_push(E, gain_page[key - KEY_PUSH_A]);
    }
    return 1;
}

int gain_enc(void *brain, void *ev)
{
    uint32_t e = EV_KEY(ev);
    int32_t d, i;
    (void)brain;
    if (!open || e < 1 || e > 8)
        return 0;                               /* LEVEL/DATA stays the Digitone's */
    i = (int32_t)e - 1;
    acc[i] += EV_DELTA(ev);
    d = acc[i] / 4;                             /* 4 counts a detent */
    acc[i] -= d * 4;
    if (pg)
        fx_page_turn(E, pg - 1, i, d);
    else
        fx_knob_turn(E, gain_page[i], d);
    return 1;
}

typedef int32_t (*text_fn)(void *bmp, const void *font, int32_t x, int32_t y, int32_t maxlen, const char *fmt, ...);
#define TEXTF ((text_fn)fw_textf)

void gain_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (!open)
        return;
    fw_fillrect(bmp, 0, 0, 127, 63, 0);
    TEXTF(bmp, fw_font_title, 19, 56, -1, "%s", fx_lock_mode() ? "LOCK" : pg ? fx_page_title(pg - 1) : "Gain");
    if (fx_lock_mode())
        fx_lock_strip(E, bmp, 62, 55);
    else if (fx_overload())
        TEXTF(bmp, fw_font_title, 60, 56, -1, "CPU full");
    fw_fillrect(bmp, 17, 54, 127, 63, -1);       /* the title bar, inverted */
    if (pg)
        fx_page_draw(E, bmp, pg - 1);
    else
        fx_draw_controls(E, bmp, gain_page);
}

void gain_tick(void *ctrl)
{
    static uint32_t sig, saved = ~0u;
    uint32_t h = (uint32_t)(open | pg << 1 | fx_lock_mode() << 3 | (fx_held_step() + 1) << 4 | fx_lock_page() << 11);
    int i;
    if (fx_serial(E) != saved) {                /* the project's copy, when GAIN's values moved */
        saved = fx_serial(E);
        save.ver = SAVE_VER;
        for (i = 0; i < 3; i++)
            save.knob[i] = (uint8_t)(fx_get(E, gain_page[i]) >> 8);
        fx_pack(E, save.mix);
    }
    if (!open)
        return;
    for (i = 0; i < 8; i++) {
        int32_t k = pg ? fx_page_slot(pg - 1, i) : gain_page[i];
        h = h * 31 + (uint32_t)fx_view(k, fx_get(E, k));
    }
    h = h * 31 + fx_lock_mask(E, fx_lock_page()) + (pg ? fx_page_state() : 0);
    if (h != sig) {
        sig = h;
        *((unsigned char *)ctrl + 0x20) = 1;    /* a redraw */
    }
}
