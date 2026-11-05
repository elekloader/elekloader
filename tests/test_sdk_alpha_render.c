/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK's sound contract on the host: built twice by test_sdk_alpha_render.py, as the ColdFire's Q31
 * (plain) and as the SHARC's float (-DELEK_TARGET_SHARC). It checks the helpers against 64-bit and double
 * references, then renders examples/sdk-sine through its .render (argv: FINE, then the note) and prints the
 * samples, -1.0..1.0, one a line, for the Python side to compare the two builds. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elekloader/sdk_alpha_machine.h"

#ifndef ELEK_TARGET_SHARC
/* Q31 x Q31 rounded to nearest, halves away from zero, saturating: what elek_mul promises. */
static int32_t reference_mul(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b, m = p < 0 ? -p : p, r = (m + (1LL << 30)) >> 31;
    r = p < 0 ? -r : r;
    return r > INT32_MAX ? INT32_MAX : (int32_t)r;
}

static void check_q31(void)
{
    const int32_t edge[] = {0, 1, -1, 2, -2, 0x40000000, -0x40000000, INT32_MAX, INT32_MIN, 0x7fff, 0x8000, -0x8000,
                            0xffff, 0x10000, -0x10000, 0x12345678, -0x12345678};
    for (unsigned i = 0; i < sizeof(edge) / 4; ++i)
        for (unsigned j = 0; j < sizeof(edge) / 4; ++j)
            assert(elek_mul(edge[i], edge[j]) == reference_mul(edge[i], edge[j]));
    srand(1);
    for (int i = 0; i < 1000000; ++i) {
        int32_t a = (int32_t)((uint32_t)rand() << 16 ^ (uint32_t)rand()), b = (int32_t)((uint32_t)rand() << 17 ^ (uint32_t)rand());
        assert(elek_mul(a, b) == reference_mul(a, b));
        int64_t s = (int64_t)a + b;
        assert(elek_add(a, b) == (s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s));
    }
    assert(ELEK_SAMPLE(1.0) == INT32_MAX && ELEK_SAMPLE(-1.0) == INT32_MIN && ELEK_SAMPLE(0.5) == 0x40000000);
    assert(elek_clip(INT32_MIN) == INT32_MIN);
    assert(elek_level(127, 127) >= INT32_MAX - 127 && elek_level(0, 127) == 0 && elek_level(-5, 127) == 0);
    assert(elek_level(200, 127) == elek_level(127, 127));
    assert(elek_from_q15(-32768) == INT32_MIN && elek_from_q15(16384) == 0x40000000);
    assert(elek_from_q15(32767) == 0x7fff0000 && elek_from_q15(-1) == -0x10000);
    assert(elek_from_q15(40000) == elek_from_q15(32767) && elek_from_q15(-40000) == INT32_MIN);
}
#else
static void check_float(void)
{
    assert(elek_mul(0.5f, -0.5f) == -0.25f && elek_add(0.75f, 0.75f) == 1.5f && elek_clip(1.5f) == 1.0f);
    assert(elek_clip(-2.0f) == -1.0f && ELEK_SAMPLE(0.5) == 0.5f);
    assert(elek_level(127, 127) == 1.0f && elek_level(0, 127) == 0.0f && elek_level(200, 127) == 1.0f);
    assert(elek_from_q15(-32768) == -1.0f && elek_from_q15(16384) == 0.5f && elek_from_q15(-40000) == -1.0f);
}
#endif

/* The same on both: a phase step within 0.05 cent of the exact one, clamped to notes 0..127. */
static void check_phase(void)
{
    for (int32_t pitch = 0; pitch <= 127 << 16; pitch += 997) {
        double hz = 440.0 * pow(2.0, (pitch / 65536.0 - 69.0) / 12.0);
        double exact = hz / ELEK_SAMPLE_RATE * 4294967296.0;
        assert(fabs(1200.0 * log2(elek_phase_inc(pitch) / exact)) < 0.05);
    }
    assert(elek_phase_inc(-1) == elek_phase_inc(0) && elek_phase_inc(200 << 16) == elek_phase_inc(127 << 16));
}

/* The label helpers: no libc on the device. */
static void check_labels(void)
{
    static const struct { const char *prefix; int32_t v; const char *want; } nums[] = {
        {"F", 7, "F7"}, {"A ", 37, "A 37"}, {"F", -50, "F-50"}, {"LONG", 127, "LO127"}, {"", 0, "0"},
        {"X", 99999, "99999"}, {"X", -9999, "-9999"}, {"X", 100000, "#"}, {"X", INT32_MIN, "#"},
    };
    char label[ELEK_UI_LABEL_MAX + 1];
    for (unsigned i = 0; i < sizeof nums / sizeof *nums; ++i) {
        elek_label_num(label, nums[i].prefix, nums[i].v);
        assert(!strcmp(label, nums[i].want));
    }
    elek_label_set(label, "SAW");
    assert(!strcmp(label, "SAW"));
    elek_label_set(label, "TOOLONG");
    assert(!strcmp(label, "TOOLO"));
}

/* The example authors start from, examples/sdk-sine, as it is: the registration becomes a pointer here. */
#undef ELEK_REGISTER_MACHINE
#define ELEK_REGISTER_MACHINE(m) static const struct elek_machine *const example = &(m)
#include "../examples/sdk-sine/sine.c"

int main(int argc, char **argv)
{
#ifndef ELEK_TARGET_SHARC
    check_q31();
#else
    check_float();
#endif
    check_phase();
    check_labels();
    int32_t knob[1] = {argc > 1 ? atoi(argv[1]) : 0};
    struct state state = {12345};               /* a previous note's phase: the start must reset it */
    struct elek_voice voice = {.knob = knob, .start = 1, .pitch = (argc > 2 ? atoi(argv[2]) : 69) << 16,
                               .velocity = 100, .state = &state};
    elek_sample block[ELEK_VOICE_FRAMES];
    for (int b = 0; b < 64; ++b) {
        example->render(&voice, block);
        voice.start = 0;
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
#ifdef ELEK_TARGET_SHARC
            printf("%.9f\n", block[i]);
#else
            printf("%.9f\n", block[i] / 2147483648.0);
#endif
    }
    printf("phase %u\n", state.phase);
    return 0;
}
