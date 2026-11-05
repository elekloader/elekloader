/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private SDK example: a sine at the note played, FINE cents (-50..50) away. FINE's glyph shows which way
 * it is tuned: an arrow down when flat, a sine when in tune, an arrow up when sharp. */
#include "elekloader/sdk_alpha_machine.h"

enum { FINE };
enum { FLAT, IN_TUNE, SHARP };
static const struct elek_glyph tuning[] = {
    [FLAT] = { .rows = { [3] = 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0ff0, 0x07e0, 0x03c0,
                         0x0180 } },
    [IN_TUNE] = { .rows = { [5] = 0x1800, 0x2400, 0x4200, 0x4201, 0x8102, 0x0084, 0x0048, 0x0030 } },
    [SHARP] = { .rows = { [3] = 0x0180, 0x03c0, 0x07e0, 0x0ff0, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180,
                          0x0180 } },
};
static const struct elek_knob knobs[] = {
    [FINE] = { "FINE", .min = -50, .max = 50, .glyph = &tuning[IN_TUNE] },     /* starts at 0 */
};

static void present(const struct elek_ui *ui)
{
    int32_t cents = ui->knob[FINE];
    ui->view[FINE].glyph = &tuning[cents < 0 ? FLAT : cents > 0 ? SHARP : IN_TUNE];
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

struct state { uint32_t phase; };

static void render(const struct elek_voice *v, elek_sample *out)
{
    struct state *s = ELEK_STATE(struct state, v);
    uint32_t inc = elek_phase_inc(v->pitch + v->knob[FINE] * 65536 / 100);   /* cents: 1/100 semitone */
    uint32_t p = v->start ? 0 : s->phase;       /* each note starts its wave at zero */
    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i) {
        int32_t y = sine(p);
        /* half full scale: headroom for the filter's resonance */
        elek_sample half = elek_mul(elek_level(y < 0 ? -y : y, 32767), ELEK_SAMPLE(0.5));
        out[i] = y < 0 ? -half : half;
        p += inc;
    }
    s->phase = p;
}

static const struct elek_machine machine = {
    .id = 123, .name = "SDK Sine", .short_name = "SINE", ELEK_KNOBS(knobs),
    .pages = { { "SINE", { [ELEK_A] = &knobs[FINE] } } },
    .present = present, .render = render,
};
ELEK_REGISTER_MACHINE(machine);
