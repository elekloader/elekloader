/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private SDK example: a sine at the note played, FINE cents (-50..50) away. FINE's glyph shows which way
 * it is tuned: an arrow down when flat, a sine when in tune, an arrow up when sharp. */
#include "elekloader/sdk_alpha_machine.h"

enum { FINE };
enum { FLAT, IN_TUNE, SHARP };
static const struct elek_ui_glyph tuning[] = {
    [FLAT] = { .rows = { [3] = 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0ff0, 0x07e0, 0x03c0,
                         0x0180 } },
    [IN_TUNE] = { .rows = { [5] = 0x1800, 0x2400, 0x4200, 0x4201, 0x8102, 0x0084, 0x0048, 0x0030 } },
    [SHARP] = { .rows = { [3] = 0x0180, 0x03c0, 0x07e0, 0x0ff0, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180,
                          0x0180 } },
};
static const struct elek_ui_knob fine = {
    .id = FINE, .label = "FINE",
    .min = ELEK_UI_BOUND(-50), .max = ELEK_UI_BOUND(50), .def = ELEK_UI_BOUND(0),
    .glyph = &tuning[IN_TUNE],
};
static const struct elek_ui_page pages[] = {
    { .label = "SINE", .knob = { [ELEK_ENCODER_A] = &fine } },
};

static void present(const struct elek_ui_context *ctx, struct elek_ui_presentation *out)
{
    int32_t cents = ctx->knob[FINE]->value;
    out->knob[FINE]->glyph = &tuning[cents < 0 ? FLAT : cents > 0 ? SHARP : IN_TUNE];
}

/* sin(2 pi ph / 2^32), Q15: a parabola, then its square nudged in (within 0.1 %). */
static int32_t sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, a = x < 0 ? -x : x, y, z;
    y = (x * (32768 - a)) >> 13;
    z = y < 0 ? -y : y;
    y += (7373 * (((y * z) >> 15) - y)) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static uint32_t phase[ELEK_VOICES_MAX];
static uint32_t gated[ELEK_VOICES_MAX];

static void render(const struct elek_ui_context *ctx, const struct elek_voice *v, int32_t *out)
{
    uint32_t inc = elek_phase_inc(v->pitch + ctx->knob[FINE]->value * 65536 / 100);   /* cents: 1/100 semitone */
    /* Retrigger a held voice without cutting its waveform; start released voices at zero. */
    uint32_t p = v->fresh && !gated[v->voice] ? 0 : phase[v->voice];
    gated[v->voice] = v->gate;
    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i) {
        out[i] = sine(p) << 15;         /* half full scale: headroom for the filter's resonance */
        p += inc;
    }
    phase[v->voice] = p;
}

static const struct elek_machine machine = {
    .id = 123, .name = "SDK Sine", .short_name = "SINE",
    .pages = pages, .page_count = 1, .present = present, .render = render,
};
ELEK_REGISTER_MACHINE(machine);
