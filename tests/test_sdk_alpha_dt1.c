/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real adapter's render path. */
#undef NDEBUG                   /* the checks below are asserts */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "digitakt-mk1/core3.h"
#include "elekloader/sdk_alpha_machine.h"

#include "../mods/machine-pages/sdk_alpha.c"
#include "../mods/sdk-alpha/machine.c"

const char cm_ui_v33[] = "native UI";
volatile uint32_t fw_active_track, fw_voice_start;
uint8_t *volatile fw_kit;
volatile uint8_t fw_render_machine[8];
volatile int32_t fw_voice_note[8];
volatile int16_t fw_voice_vel[8];
volatile int32_t fw_amp_env[3 * 8];
static int32_t effective[8][15], blocks[8 * ELEK_VOICE_FRAMES];
static uint8_t kit[0x20 + 8 * 0xa2];
static uint32_t calls[8];
static struct elek_voice last[8];
static int32_t value[8], cc9[8], cc12[8], cc74[8];

int32_t cm_src_value(int32_t track, int32_t knob) { return effective[track][knob]; }
int32_t mp_shown_value(void *view, uint32_t id, uint8_t *locked)
{ (void)view; (void)id; *locked = 0; return 0; }
uint32_t mp_stock_layout(int32_t machine) { (void)machine; return 0; }

static void render(const struct elek_voice *v, elek_sample *out)
{
    assert(v->state && v->knob);
    last[v->track] = *v;
    value[v->track] = v->knob[0];
    assert(v->cc);
    cc9[v->track] = v->cc[9]; cc12[v->track] = v->cc[12]; cc74[v->track] = v->cc[74];
    uint32_t *state = ELEK_STATE(uint32_t, v);
    out[0] = ++*state;
    ++calls[v->track];
}
static const struct elek_knob knobs[] = { { "FINE", .min = -50, .max = 50 } };
static const struct elek_machine machine = {
    .id = 100, .name = "Test", .short_name = "TEST", ELEK_KNOBS(knobs),
    .pages = { { "TEST", { &knobs[0] } } }, .render = render,
};
/* Mono machines, the newest key sounding: 101 restarts the AMP at each change, 102 changes the pitch with
 * the AMP running on. */
static const struct elek_machine mono_machine = {
    .id = 101, .name = "Mono", .short_name = "MONO", ELEK_KNOBS(knobs),
    .pages = { { "MONO", { &knobs[0] } } }, .render = render, .play = ELEK_MONO,
};
static const struct elek_machine legato_machine = {
    .id = 102, .name = "Legato", .short_name = "LEG", ELEK_KNOBS(knobs),
    .pages = { { "LEG", { &knobs[0] } } }, .render = render, .play = ELEK_MONO_LEGATO,
};
/* Ten knobs: ids 8 and 9 are the first two extra values. */
#define K(n) { "K" #n, .max = 127 }
static const struct elek_knob wide_knobs[] = { K(0), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8), K(9) };
static const struct elek_machine wide_machine = {
    .id = 103, .name = "Wide", .short_name = "WIDE", ELEK_KNOBS(wide_knobs),
    .pages = { { "ONE", { &wide_knobs[0], &wide_knobs[1], &wide_knobs[2], &wide_knobs[3],
                          &wide_knobs[4], &wide_knobs[5], &wide_knobs[6], &wide_knobs[7] } },
               { "TWO", { &wide_knobs[8], &wide_knobs[9] } } },
};
const struct elek_machine *const sdk_alpha_machines[] = { &machine, &mono_machine, &legato_machine, &wide_machine, 0 };

/* The stock note routines, as calls: "+60/90" a note on (with its velocity), "-60" an off. */
static char played[64];
void fw_note_on(int32_t track, int32_t note, int32_t vel, int32_t source, int32_t skip, int32_t sound, int32_t flag)
{
    assert(track == 2 && source == 0x40 && !skip && sound == -1 && flag == -1);
    sprintf(played + strlen(played), "+%d/%d ", note, vel);
}
void fw_note_off(int32_t track, int32_t note, int32_t source)
{
    assert(track == 2 && source == 0x40);
    sprintf(played + strlen(played), "-%d ", note);
}
/* A key on track 2: what the handler returns (1: taken), then the stock calls it made, in played. */
static int32_t key(int32_t note, int32_t vel, int32_t on)
{
    played[0] = 0;
    return sdk_alpha_live_note(2, note, on ? vel : 0x40, on, 0x40);
}
#define KEY(note, vel, on, taken, calls) \
    do { assert(key(note, vel, on) == (taken)); assert(!strcmp(played, calls)); } while (0)
void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour)
{ (void)bmp; (void)x0; (void)y0; (void)x1; (void)y1; (void)colour; }

int main(void)
{
    fw_kit = kit;
    for (int t = 0; t < 2; ++t) {
        FW_SOUND(kit, t)[FW_SOUND_MACHINE] = 100;
        fw_render_machine[t] = 100;
        fw_voice_note[t] = (60 + t) << 16;
        fw_voice_vel[t] = 100 << 8;
        effective[t][0] = t ? SDK_ALPHA_RAW_MAX : 0;
        fw_amp_env[3 * t + 1] = 1;          /* sounding */
    }
    fw_voice_start = 3;
    sdk_alpha_render(blocks);
    assert(calls[0] == 1 && calls[1] == 1);
    assert(value[0] == -50 && value[1] == 50);   /* effective audio values, not UI kit zeros */
    assert(last[1].pitch == 61 << 16 && last[1].velocity == 100 && last[1].track == 1);
    assert(last[0].start && last[1].start);
    assert(last[0].state != last[1].state && blocks[0] == 1 && blocks[32] == 1);
    fw_voice_start = 2;
    sdk_alpha_render(blocks);
    sdk_alpha_render(blocks);
    assert(!last[0].start);                     /* the start went to one call */
    assert(blocks[0] == 3 && blocks[32] == 3);  /* retriggers preserve the voice's state */
    fw_render_machine[0] = 0;
    sdk_alpha_render(blocks);
    fw_render_machine[0] = 100;
    sdk_alpha_render(blocks);
    assert(blocks[0] == 1 && blocks[32] == 5);  /* changing one track resets only its state */
    fw_voice_start = 0;
    sdk_alpha_render(blocks);
    assert(!last[1].start);
    /* A track whose AMP envelope has ended and no trig starts is not rendered; idle counts its frames. */
    uint32_t before = calls[0], other = calls[1];
    fw_amp_env[1] = 0;
    sdk_alpha_render(blocks); sdk_alpha_render(blocks); sdk_alpha_render(blocks);
    assert(calls[0] == before && blocks[0] == 0);
    assert(calls[1] == other + 3 && last[1].idle == 0);
    fw_voice_start = 1;                         /* a trig renders it while the level is still 0 */
    sdk_alpha_render(blocks);
    assert(calls[0] == before + 1 && last[0].start && last[0].idle == 3 * ELEK_VOICE_FRAMES);
    fw_voice_start = 0; fw_amp_env[1] = 1;
    sdk_alpha_render(blocks);
    assert(last[0].idle == 0);
    fw_amp_env[1] = 0;                          /* silent, then another machine: its first render gets 0 */
    sdk_alpha_render(blocks);
    fw_render_machine[0] = 0; sdk_alpha_render(blocks);
    fw_render_machine[0] = 100; fw_amp_env[1] = 1; sdk_alpha_render(blocks);
    assert(calls[0] == before + 3 && last[0].idle == 0 && blocks[0] == 1);
    /* CCs: each reaches its track's voice once, at its next render, the stock still applying it. */
    fw_voice_start = 0; fw_amp_env[1] = fw_amp_env[4] = 1;
    sdk_alpha_render(blocks);
    assert(cc9[0] == -1 && cc9[1] == -1);
    assert(sdk_alpha_midi_cc(0, 9, 64, 0) == 0);
    sdk_alpha_midi_cc(0, 9, 100, 0);             /* the same CC again: its last value */
    sdk_alpha_midi_cc(1, 74, 5, 1);
    assert(sdk_alpha_midi_cc(8, 9, 1, 0) == 0 && sdk_alpha_midi_cc(16, 9, 1, 0) == 0);  /* MIDI and FX tracks */
    assert(sdk_alpha_midi_cc(0, 128, 1, 0) == 0 && sdk_alpha_midi_cc(-1, 9, 1, 0) == 0);
    sdk_alpha_render(blocks);
    assert(cc9[0] == 100 && cc74[0] == -1 && cc9[1] == -1 && cc74[1] == 5);
    sdk_alpha_render(blocks);
    assert(cc9[0] == -1 && cc74[1] == -1);       /* once */
    fw_amp_env[1] = 0;                           /* a silent track keeps it until it renders again */
    sdk_alpha_midi_cc(0, 12, 1, 0);
    sdk_alpha_render(blocks); sdk_alpha_render(blocks);
    assert(cc12[1] == -1);
    fw_amp_env[1] = 1;
    sdk_alpha_render(blocks);
    assert(cc12[0] == 1);
    /* The track's voice takes a machine at its next note, after the kit has: a CC in between is for the
     * new machine; one while the kit had another machine is not, and none comes twice. */
    FW_SOUND(kit, 0)[FW_SOUND_MACHINE] = 0;
    fw_render_machine[0] = 0; sdk_alpha_render(blocks);
    sdk_alpha_midi_cc(0, 9, 3, 0);               /* for the stock machine */
    FW_SOUND(kit, 0)[FW_SOUND_MACHINE] = 100;
    sdk_alpha_midi_cc(0, 12, 4, 0);              /* for machine 100, before its first note */
    sdk_alpha_render(blocks);
    fw_render_machine[0] = 100; sdk_alpha_render(blocks);
    assert(cc9[0] == -1 && cc12[0] == 4);
    fw_render_machine[0] = 0; sdk_alpha_render(blocks);
    fw_render_machine[0] = 100; sdk_alpha_render(blocks);
    assert(cc12[0] == -1);                       /* back on it: not again */
    cc_count = CC_COUNT - 1;                     /* the count wraps */
    sdk_alpha_render(blocks);
    sdk_alpha_midi_cc(0, 9, 11, 0); sdk_alpha_midi_cc(0, 12, 22, 0); sdk_alpha_midi_cc(0, 74, 33, 0);
    sdk_alpha_render(blocks);
    assert(cc_count == 2 && cc9[0] == 11 && cc12[0] == 22 && cc74[0] == 33);
    sdk_alpha_render(blocks);
    assert(cc9[0] == -1 && cc12[0] == -1 && cc74[0] == -1);
    assert(sdk_alpha_map(&knobs[0], sdk_alpha_default(&knobs[0])) == 0);   /* omitted: 0 */
    struct elek_knob level = { "LEVEL", .max = 127, .def = 100 };
    assert(sdk_alpha_map(&level, sdk_alpha_default(&level)) == 100);
    struct elek_knob above = { "ABOVE", .min = 10, .max = 20 };
    assert(sdk_alpha_default(&above) == 0);                                /* 0, clamped to min */

    /* Mono, retrigger: the stock holds one key at a time; the key pressed or released plays itself. */
    FW_SOUND(kit, 2)[FW_SOUND_MACHINE] = 101;
    fw_render_machine[2] = 101;
    KEY(60, 90, 1, 0, "");                      /* C: the stock plays it */
    KEY(64, 100, 1, 0, "-60 ");                 /* E: C off first, then the stock plays E */
    KEY(64, 0, 0, 1, "-64 +60/90 ");            /* E up: C again, with its own velocity */
    KEY(60, 0, 0, 0, "");                       /* C up: the stock releases it */
    KEY(60, 90, 1, 0, "");
    KEY(64, 100, 1, 0, "-60 ");
    KEY(60, 0, 0, 1, "");                       /* an older key up: E plays on */
    KEY(64, 0, 0, 0, "");
    KEY(67, 0, 0, 0, "");                       /* a key not held: the stock's */
    KEY(60, 90, 1, 0, "");
    KEY(60, 80, 1, 0, "-60 ");                  /* the same key again: off, then the stock plays it */
    KEY(60, 0, 0, 0, "");
    assert(sdk_alpha_live_note(0, 60, 90, 1, 0x40) == 0);   /* ELEK_POLY: every key is the stock's */
    assert(sdk_alpha_live_note(9, 60, 90, 1, 0x40) == 0);   /* a MIDI track */
    assert(!played[0]);

    /* Mono, legato: a change of key moves the pitch the render gets; start stays clear. */
    FW_SOUND(kit, 2)[FW_SOUND_MACHINE] = 102;
    fw_render_machine[2] = 102;
    fw_voice_note[2] = 60 << 16;
    fw_amp_env[3 * 2 + 1] = 1;
    KEY(60, 90, 1, 0, "");
    KEY(64, 100, 1, 1, "");                     /* E: taken, the stock holds C */
    fw_voice_start = 0;
    sdk_alpha_render(blocks);
    assert(last[2].pitch == 64 << 16 && !last[2].start);
    KEY(64, 0, 0, 1, "");                       /* E up: back to C, the AMP running on */
    sdk_alpha_render(blocks);
    assert(last[2].pitch == 60 << 16 && !last[2].start);
    KEY(64, 100, 1, 1, "");
    KEY(60, 0, 0, 1, "");                       /* C up: E sounds on */
    KEY(64, 0, 0, 1, "-60 ");                   /* the last key up: the stock's C off */
    sdk_alpha_render(blocks);
    assert(last[2].pitch == 60 << 16);          /* no bend once silent */

    /* MIDI CCs: knob ids 8-14 take CC 24-30, only for the knobs the machine declares; A-H none. */
    const struct cm_ui32 *wide = sdk_alpha_ui(103), *one = sdk_alpha_ui(100);
    assert(wide->base.abi == cm_ui_v33 && one->base.abi == cm_ui_v33);
    assert(wide->extra[0].cc == 24 && wide->extra[1].cc == 25 && !wide->extra[2].cc);
    for (int k = 0; k < 8; ++k)
        assert(!wide->base.knob[k].cc && !one->base.knob[k].cc);
    for (int k = 0; k < CM_EXTRAS; ++k)
        assert(!one->extra[k].cc);
    return 0;
}
