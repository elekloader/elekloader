/* SPDX-License-Identifier: GPL-2.0-or-later */
/* hooks check for core 5.0 (Digitakt mk1): each handler counts its calls in hc_counts[] and keeps its
 * first four arguments in hc_args[][]. It takes nothing. The emulator probe reads both through the link
 * map. ev_idle has no handler here: digiemu's cold boot waits for the stock idle spin. */
#include <stdint.h>

#define HC_N 64
volatile int32_t hc_counts[HC_N];
volatile int32_t hc_args[HC_N][4];

static void note(int i, int32_t a, int32_t b, int32_t c, int32_t d)
{
    hc_counts[i]++;
    hc_args[i][0] = a; hc_args[i][1] = b; hc_args[i][2] = c; hc_args[i][3] = d;
}

/*  0 */ void hc_card_ready(int32_t a0) { note(0, a0, 0, 0, 0); }
/*  1 */ void hc_boot_ui(void) { note(1, 0, 0, 0, 0); }
/*  2 */ void hc_crash(int32_t a0) { note(2, a0, 0, 0, 0); }
/*  3 */ void hc_project_saving(int32_t a0, int32_t a1, int32_t a2) { note(3, a0, a1, a2, 0); }
/*  4 */ void hc_project_saved(int32_t a0, int32_t a1, int32_t a2) { note(4, a0, a1, a2, 0); }
/*  5 */ void hc_project_loaded(int32_t a0, int32_t a1) { note(5, a0, a1, 0, 0); }
/*  6 */ void hc_project_new(int32_t a0) { note(6, a0, 0, 0, 0); }
/*  7 */ void hc_pattern_op(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(7, a0, a1, a2, a3); }
/*  8 */ void hc_sample_assign(int32_t a0, int32_t a1, int32_t a2) { note(8, a0, a1, a2, 0); }
