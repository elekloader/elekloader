/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The bridge owns slot selection; this compiled DSP entry owns sound and ramp. */
#include <stdint.h>

void sine_init(uint32_t phase_increment);
void sine_render(float *output);

/* Notes 36..47 at 48 kHz; the bridge doubles these steps by octave. */
const uint32_t steps12[12] = {
    5852465u, 6200470u, 6569170u, 6959793u,
    7373644u, 7812103u, 8276635u, 8768789u,
    9290209u, 9842633u, 10427907u, 11047982u
};

static float gain;

void sharc_init(uint32_t phase_increment) { sine_init(phase_increment); gain = 0.0f; }

void sharc_render(float *mono32)
{
    uint32_t gate = *(volatile uint32_t *)0x2dd9f0;
    float g = gain;
    sine_render(mono32);
    for (int i = 0; i < 32; ++i) {
        if (gate) {
            g += 0.03125f;
            if (g > 1.0f) g = 1.0f;
        } else {
            g -= 0.03125f;
            if (g < 0.0f) g = 0.0f;
        }
        mono32[i] *= g;
    }
    gain = g;
}
