/* SPDX-License-Identifier: GPL-2.0-or-later */
/* voiceio-check: core-ar2's note ends, track mutes and DIN MIDI out on the
 * Analog Rytm MKII (OS 1.74), on screen. Without its bar shown it changes
 * nothing but adding BD VIO to the BD track's machine list.
 *
 *   BD VIO   slot 44, BD voice, plays as BD CLASSIC. Each block its engine
 *            counts core_voice_trigged (T), core_voice_released 1 (R) and
 *            2 (C), and on each release sends a DIN note-off (80 24 00)
 *            from the audio interrupt (I sent/refused).
 *   PLAYMODE shows or hides the bar (taken).
 *   QPER     the BD track's mute toggled by core_set_track_mutes (taken
 *            while the bar shows); M shows core_track_mutes in hex.
 *   FLTR     a DIN note-on (90 24 64) on press, its note-off on release, from
 *            the UI task (D sent/refused; taken while the bar shows).
 *   U        core_midi_din_up.
 */
#include <stdint.h>
#include "analog-rytm-mk2/machines.h"
#include "analog-rytm-mk2/moments.h"
#include "analog-rytm-mk2/screen.h"

#define K_PLAYMODE 0xb5
#define K_QPER 0xb1
#define K_FLTR 0x33
#define KEY(ev) (*(const int32_t *)((const uint8_t *)(ev) + 12))
#define FLAGS(ev) (*(const int32_t *)((const uint8_t *)(ev) + 16))
#define DOWN 1

static volatile unsigned n_trig, n_rel, n_cut, irq_sent, irq_refused;
static unsigned ui_sent, ui_refused;
static volatile int shown;

static void vio_engine(int32_t voice, void *frame, uint16_t *blk, int32_t pitch)
{
    static const unsigned char off[3] = {0x80, 0x24, 0x00};
    int r = core_voice_released(voice);
    if (core_voice_trigged(voice))
        n_trig++;
    if (r == 1) {
        n_rel++;
        if (core_midi_din_try_send(3, off))
            irq_sent++;
        else
            irq_refused++;
    } else if (r == 2)
        n_cut++;
    CORE_ENGINE_BD_CLASSIC(voice, frame, blk, pitch);
}

const struct core_machine vio_machine = {
    44, vio_engine, 0, CORE_VOICE_BD, 0, "BD VIO", "BDVI",
    {
        {"Level", "LEV", 0, 127, 100, 0},
        {"Tune", "TUN", 32, 96, 58, 1},
        {"Decay Time", "DEC", 0, 127, 32, 0},
        {"Hold Time", "HLD", 0, 127, 32, 0},
        {"Sweep Time", "SWT", 0, 127, 64, 0},
        {"Sweep Depth", "SWD", 0, 127, 42, 0},
        {"Waveform", "WAV", 0, 2, 1, 0},
        {"Transient", "TRA", 0, 55, 17, 0},
    },
};

static void din(const unsigned char *m)
{
    if (core_midi_din_try_send(3, m))
        ui_sent++;
    else
        ui_refused++;
}

int vio_key(void *brain, void *ev)
{
    static const unsigned char on[3] = {0x90, 0x24, 0x64}, off[3] = {0x80, 0x24, 0x00};
    int k = KEY(ev), down = FLAGS(ev) & DOWN;
    (void)brain;
    if (k == K_PLAYMODE) {
        if (down)
            shown = !shown;
        return 1;
    }
    if (!shown)
        return 0;
    if (k == K_QPER) {
        if (down)
            core_set_track_mutes(core_track_mutes() ^ 1);
        return 1;
    }
    if (k == K_FLTR) {
        din(down ? on : off);
        return 1;
    }
    return 0;
}

void vio_tick(void *ctrl)
{
    if (shown)
        *((volatile uint8_t *)ctrl + 0x20) = 1;   /* redraw: the counts move */
}

/* ---- the bar, rows 55..63, 8 bytes a column, row r in byte 7 - r / 8 ---- */

static char *num(char *o, unsigned v, int base)
{
    char t[10];
    int n = 0;
    do t[n++] = "0123456789abcdef"[v % base]; while ((v /= base) && n < 10);
    while (n)
        *o++ = t[--n];
    return o;
}

static char *put(char *o, char c, unsigned v, int base)
{
    *o++ = c;
    return num(o, v, base);
}

void vio_draw(void *bmp, void *ctrl)
{
    char t[64], *o = t;
    uint8_t *p = *(uint8_t **)((uint8_t *)bmp + 16);
    int x, r;
    (void)ctrl;
    if (!shown)
        return;
    for (x = 0; x < 128; x++)
        for (r = 55; r < 64; r++)
            p[8 * x + 7 - r / 8] |= (uint8_t)(1 << (r % 8));
    o = put(o, 'T', n_trig, 10);
    o = put(o, 'R', n_rel, 10);
    o = put(o, 'C', n_cut, 10);
    o = put(o, 'M', core_track_mutes(), 16);
    o = put(o, 'D', ui_sent, 10);
    o = put(o, '/', ui_refused, 10);
    o = put(o, 'I', irq_sent, 10);
    o = put(o, '/', irq_refused, 10);
    o = put(o, 'U', (unsigned)core_midi_din_up(), 10);
    *o = 0;
    core_text_draw(bmp, CORE_FONT_MAIN, 2, 1, CORE_ALIGN_LEFT | CORE_TEXT_INVERT, t);
}
