/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Author-only compiler probe; no firmware addresses or SDK interface. */
#include <stdint.h>

static uint32_t phase;
static uint32_t increment;

/* Positive half of 0.125 * sin(2*pi*i/64), rounded to binary32.
 * shortcut: this Selache pin miscompiles negative float initializers;
 * use runtime negation until that compiler path is qualified. */
static const float table[32] = {
    0.000000000f, 0.012252143f, 0.024386290f, 0.036285585f,
    0.047835429f, 0.058924592f, 0.069446279f, 0.079299160f,
    0.088388348f, 0.096626307f, 0.103933702f, 0.110240158f,
    0.115484942f, 0.119617542f, 0.122598160f, 0.124398091f,
    0.125000000f, 0.124398091f, 0.122598160f, 0.119617542f,
    0.115484942f, 0.110240158f, 0.103933702f, 0.096626307f,
    0.088388348f, 0.079299160f, 0.069446279f, 0.058924592f,
    0.047835429f, 0.036285585f, 0.024386290f, 0.012252143f
};

/* Inspect actual widths: the assembler's attributes alone do not prove them. */
const uint32_t sine_widths[4] = {
    sizeof(char), sizeof(uint32_t), sizeof(float), sizeof(void *)
};

void sine_init(uint32_t phase_increment)
{
    phase = 0;
    increment = phase_increment;
}

float sine_sample(uint32_t p)
{
    uint32_t index = p >> 26;
    float value = table[index & 31];
    if (index & 32)
        return -value;
    return value;
}

void sine_render(float *output)
{
    uint32_t p = phase;
    int i;
    for (i = 0; i < 32; ++i) {
        output[i] = sine_sample(p);
        p += increment;
    }
    phase = p;
}
