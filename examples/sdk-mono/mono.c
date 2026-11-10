/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private SDK example: a mono saw. One note at a time on its track: the last key pressed sounds, and the
 * key still held before it returns when it is released, the AMP restarted at each change. .play =
 * ELEK_MONO_LEGATO moves the pitch with the AMP running on instead (single trigger); .prio =
 * ELEK_PRIO_LOW or ELEK_PRIO_HIGH sounds the lowest or highest key held. On the Digitone II, whose render
 * is SHARC code, FM TONE plays. */
#include "elekloader/sdk_alpha_machine.h"

enum { LEV };
static const struct elek_knob knobs[] = {
    [LEV] = { "LEV", .max = 127, .def = 100 },
};

#ifdef ELEK_TARGET_SHARC
#define RENDER 0
#else
struct state { uint32_t phase; };

static void render(const struct elek_voice *v, elek_sample *out)
{
    struct state *s = ELEK_STATE(struct state, v);
    uint32_t inc = elek_phase_inc(v->pitch), p = v->start ? 0 : s->phase;   /* legato keeps the phase */
    elek_sample level = elek_from_q15(v->knob[LEV] * 258);
    for (int i = 0; i < ELEK_VOICE_FRAMES; ++i, p += inc)
        out[i] = elek_mul(elek_from_q15((int32_t)(p >> 16) - 32768), level);
    s->phase = p;
}
#define RENDER render
#endif

static const struct elek_machine machine = {
    .id = 124, .name = "SDK Mono", .short_name = "MONO", ELEK_KNOBS(knobs),
    .pages = { { "MONO", { [ELEK_A] = &knobs[LEV] } } },
    .render = RENDER, .play = ELEK_MONO, .prio = ELEK_PRIO_LAST,
};
ELEK_REGISTER_MACHINE(machine);
