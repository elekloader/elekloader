/* SPDX-License-Identifier: GPL-2.0-or-later */
/* hooks check for core-dn1 5.0: each handler counts its calls in hc_counts[] and keeps its first four
 * arguments in hc_args[][]. It takes nothing. The emulator probe reads both through the link map. */
#include <stdint.h>

#define HC_N 48
volatile int32_t hc_counts[HC_N];
volatile int32_t hc_args[HC_N][4];

static void note(int i, int32_t a, int32_t b, int32_t c, int32_t d)
{
    hc_counts[i]++;
    hc_args[i][0] = a; hc_args[i][1] = b; hc_args[i][2] = c; hc_args[i][3] = d;
}

/*  0 */ void hc_card_ready(int32_t a0) { note(0, a0, 0, 0, 0); }
/*  1 */ int32_t hc_sysex(int32_t a0, int32_t a1, int32_t a2) { note(1, a0, a1, a2, 0); return 0; }
/*  2 */ void hc_crash(int32_t a0) { note(2, a0, 0, 0, 0); }
/*  3 */ void hc_boot_ui(void) { note(3, 0, 0, 0, 0); }
/*  4 */ void hc_usb(int32_t a0) { note(4, a0, 0, 0, 0); }
/*  5 */ void hc_project_saving(int32_t a0, int32_t a1, int32_t a2) { note(5, a0, a1, a2, 0); }
/*  6 */ void hc_project_saved(int32_t a0, int32_t a1, int32_t a2) { note(6, a0, a1, a2, 0); }
/*  7 */ int32_t hc_transport(int32_t a0) { note(7, a0, 0, 0, 0); return 0; }
/*  8 */ void hc_seq_tick(void) { note(8, 0, 0, 0, 0); }
/*  9 */ void hc_pattern_request(int32_t a0, int32_t a1, int32_t a2) { note(9, a0, a1, a2, 0); }
/* 10 */ int32_t hc_midi_in(int32_t a0, int32_t a1, int32_t a2) { note(10, a0, a1, a2, 0); return 0; }
/* 11 */ int32_t hc_midi_out(int32_t a0, int32_t a1, int32_t a2) { note(11, a0, a1, a2, 0); return 0; }
/* 12 */ void hc_voice_free(int32_t a0) { note(12, a0, 0, 0, 0); }
/* 13 */ void hc_ui_msg(int32_t a0) { note(13, a0, 0, 0, 0); }
/* 14 */ void hc_trig_set(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(14, a0, a1, a2, a3); }
/* 15 */ void hc_step_note(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(15, a0, a1, a2, a3); }
/* 16 */ void hc_plock_store(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(16, a0, a1, a2, a3); }
/* 17 */ void hc_plock_clear(int32_t a0, int32_t a1, int32_t a2) { note(17, a0, a1, a2, 0); }
/* 18 */ void hc_step_clear(int32_t a0, int32_t a1) { note(18, a0, a1, 0, 0); }
/* 19 */ void hc_track_length(int32_t a0, int32_t a1) { note(19, a0, a1, 0, 0); }
/* 20 */ void hc_param_set(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(20, a0, a1, a2, a3); }
/* 21 */ void hc_sound_live(int32_t a0, int32_t a1, int32_t a2) { note(21, a0, a1, a2, 0); }
/* 22 */ int32_t hc_render_fx(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(22, a0, a1, a2, a3); return 0; }
/* 23 */ int32_t hc_tempo(int32_t a0) { note(23, a0, 0, 0, 0); return 0; }
/* 24 */ void hc_pattern_switch(int32_t a0) { note(24, a0, 0, 0, 0); }
