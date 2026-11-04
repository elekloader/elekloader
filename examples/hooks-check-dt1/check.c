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
/*  9 */ int32_t hc_transport(int32_t a0, int32_t a1) { note(9, a0, a1, 0, 0); return 0; }
/* 10 */ void hc_seq_tick(void) { note(10, 0, 0, 0, 0); }
/* 11 */ void hc_pattern_request(int32_t a0, int32_t a1) { note(11, a0, a1, 0, 0); }
/* 12 */ void hc_pattern_switch(int32_t a0) { note(12, a0, 0, 0, 0); }
/* 13 */ void hc_tempo(int32_t a0, int32_t a1) { note(13, a0, a1, 0, 0); }
/* 14 */ int32_t hc_midi_in(int32_t a0, int32_t a1, int32_t a2) { note(14, a0, a1, a2, 0); return 0; }
/* 15 */ int32_t hc_sysex(int32_t a0, int32_t a1, int32_t a2) { note(15, a0, a1, a2, 0); return 0; }
/* 16 */ int32_t hc_midi_cc(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(16, a0, a1, a2, a3); return 0; }
/* 17 */ int32_t hc_midi_pc(int32_t a0, int32_t a1, int32_t a2) { note(17, a0, a1, a2, 0); return 0; }
/* 18 */ int32_t hc_midi_out(int32_t a0, int32_t a1, int32_t a2) { note(18, a0, a1, a2, 0); return 0; }
/* 19 */ int32_t hc_live_note(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(19, a0, a1, a2, a3); return 0; }
/* 20 */ int32_t hc_ui_msg(int32_t a0) { note(20, a0, 0, 0, 0); return 0; }
/* 21 */ void hc_mute(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(21, a0, a1, a2, a3); }
/* 22 */ void hc_key_leds(int32_t a0) { note(22, a0, 0, 0, 0); }
/* 23: the last value is fw_view(), the top view before the change */
extern void *fw_view(void);
void hc_view(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { (void)a3; note(23, a0, a1, a2, (int32_t)fw_view()); }
/* 24 */ void hc_voice_on(int32_t a0, int32_t a1, int32_t a2) { note(24, a0, a1, a2, 0); }
/* 25 */ void hc_sound_live(int32_t a0, int32_t a1, int32_t a2) { note(25, a0, a1, a2, 0); }
/* 26 */ void hc_render_params(int32_t a0) { note(26, a0, 0, 0, 0); }
/* 27 */ void hc_usb(int32_t a0) { note(27, a0, 0, 0, 0); }
/* 28 */ void hc_trig_set(int32_t a0, int32_t a1, int32_t a2) { note(28, a0, a1, a2, 0); }
/* 29 */ void hc_plock_store(int32_t a0, int32_t a1, int32_t a2, int32_t a3) { note(29, a0, a1, a2, a3); }
/* 30 */ void hc_track_length(int32_t a0, int32_t a1) { note(30, a0, a1, 0, 0); }
/* 31 */ void hc_param_set(int32_t a0, int32_t a1, int32_t a2) { note(31, a0, a1, a2, 0); }
/* 32 */ void hc_sound_read(int32_t a0, int32_t a1) { note(32, a0, a1, 0, 0); }
/* 33 */ void hc_sound_write(int32_t a0, int32_t a1, int32_t a2) { note(33, a0, a1, a2, 0); }
/* 34 */ void hc_kit_write(int32_t a0, int32_t a1, int32_t a2) { note(34, a0, a1, a2, 0); }
/* 35 */ void hc_kit_read(int32_t a0, int32_t a1) { note(35, a0, a1, 0, 0); }
/* 36 */ void hc_sampler(int32_t a0) { note(36, a0, 0, 0, 0); }
/* 37 */ void hc_clip_op(int32_t a0, int32_t a1, int32_t a2) { note(37, a0, a1, a2, 0); }
/* 38 */ void hc_track_reload(int32_t a0) { note(38, a0, 0, 0, 0); }
/* 39 */ void hc_project_erase(int32_t a0, int32_t a1) { note(39, a0, a1, 0, 0); }
/* 40 */ void hc_song_mode(int32_t a0) { note(40, a0, 0, 0, 0); }
/* 41 */ void hc_beat(int32_t a0) { note(41, a0, 0, 0, 0); }
