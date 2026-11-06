/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII), part 3: the moments (core 0.11), checked
 * in RAM (README). A command word, written by the emulator or a readback
 * tool, makes the mod act on the next ev_tick. */
#include <stdint.h>
#include "analog-rytm-mk2/moments.h"
#include "analog-rytm-mk2/store.h"

STORE_FIELD(sc_h_project);
STORE_FIELD(sc_h_pattern);

#define RING 32

enum {
    CMD3_NONE,
    CMD3_BURST,         /* arg note-ons (ch 1, note 60 + i % 12) from the UI task; a refused one is tried again next tick */
    CMD3_IRQ_BURST,     /* arg note-ons from the audio interrupt (ev_render_in), into core's ring; the same */
    CMD3_HSET,          /* the project's handle = arg, the current pattern's = arg + 1 */
    CMD3_HWALK,         /* walk the handles (core_handle_fields) */
    CMD3_SYSEX,         /* one whole SysEx (F0 7D 01 .. F7, arg bytes in all) from the UI task */
};

struct sc3_ev { int16_t kind, a; uint32_t b; };    /* kind 1 ev_kit, 2 ev_project_slot */

struct {
    char magic[4];                              /* SCK3 */
    volatile int32_t cmd, arg;                  /* written from outside; cmd back to 0 when done;
                                                   arg's high half delays it by that many ticks */
    int32_t done;
    /* ev_sysex_in: messages taken (F0 7D ...) and passed on */
    uint32_t sx_taken, sx_passed, sx_passed_elektron;
    int32_t sx_len, sx_iface;
    uint8_t sx_head[8];
    /* core_midi_try_send */
    uint32_t sent, refused, irq_sent, irq_refused;
    volatile int32_t irq_left;
    /* getters, each tick */
    int32_t kit, track, pattern;
    uint32_t kit_item, sound_item, pattern_item;
    char name[16];
    /* ev_project_save: the name at pre 1 and pre 0 of the last save */
    char name_pre1[16], name_pre0[16];
    uint32_t save_how;
    /* handles */
    int32_t walk_n;
    uint32_t walk_sum, walk_fields;
    /* ev_kit and ev_project_slot */
    uint32_t n;
    struct sc3_ev e[RING];
    uint32_t ticks;
    int32_t burst_at;                           /* CMD3_BURST: the next note to send */
} sc3 = {{'S', 'C', 'K', '3'}};
const unsigned sc3_size = sizeof sc3;          /* remote.c */

int sc_remote_in(const unsigned char *data, int len);   /* remote.c */
void sc_remote_tick(void);

int32_t sc_sysex_in(const unsigned char *data, int32_t len, int32_t iface)
{
    int i;
    sc3.sx_len = len;
    sc3.sx_iface = iface;
    for (i = 0; i < 8 && i < len; i++)
        sc3.sx_head[i] = data[i];
    if (sc_remote_in(data, len))                /* 0.8: F0 7D 53 43, the readback's commands */
        return 1;
    if (len >= 2 && data[0] == 0xf0 && data[1] == 0x7d) {
        sc3.sx_taken++;
        return 1;
    }
    sc3.sx_passed++;
    if (len >= 5 && data[1] == 0x00 && data[2] == 0x20 && data[3] == 0x3c)
        sc3.sx_passed_elektron++;
    return 0;
}

static void log_ev(int kind, int a, uint32_t b)
{
    struct sc3_ev e = {(int16_t)kind, (int16_t)a, b};
    sc3.e[sc3.n++ % RING] = e;
}

void sc_kit(int32_t kit, int32_t how)
{
    log_ev(1, kit, (uint32_t)how);
}

void sc_slot(int32_t slot, uint32_t how)
{
    log_ev(2, slot, how);
}

static void copy16(char *d, const char *s)
{
    int i;
    for (i = 0; i < 16; i++)
        d[i] = s[i];
}

void sc_save(unsigned pre, unsigned how)
{
    copy16(pre ? sc3.name_pre1 : sc3.name_pre0, core_project_name());
    sc3.save_how = how;
}

static void walk_fn(unsigned field, void *item, int index, unsigned value)
{
    (void)item; (void)index;
    sc3.walk_n++;
    sc3.walk_sum += value;
    sc3.walk_fields |= field == STORE(sc_h_project) ? 1 : field == STORE(sc_h_pattern) ? 2 : 4;
}

static int note(int i)
{
    unsigned char m[3] = {0x90, (unsigned char)(60 + i % 12), 100};
    return core_midi_try_send(3, m);
}

/* the audio interrupt: the sends asked for from there */
void sc_render_in(void)
{

    while (sc3.irq_left > 0) {
        if (!note(sc3.irq_left)) {
            sc3.irq_refused++;
            return;
        }
        sc3.irq_sent++;
        sc3.irq_left--;
    }
}

void sc_moments_tick(void *ctrl)
{
    unsigned char sx[64];
    void *p;
    int i;
    (void)ctrl;
    sc3.ticks++;
    sc_remote_tick();
    sc3.kit = core_current_kit();
    sc3.track = core_current_track();
    sc3.pattern = core_current_pattern();
    sc3.kit_item = (uint32_t)core_current_kit_item();
    sc3.sound_item = (uint32_t)core_current_sound_item();
    sc3.pattern_item = (uint32_t)core_current_pattern_item();
    copy16(sc3.name, core_project_name());
    if (sc3.cmd && sc3.arg >= 0x10000) {        /* arg's high half: ticks to wait first */
        sc3.arg -= 0x10000;
        return;
    }
    switch (sc3.cmd) {
    case CMD3_BURST:
        for (; sc3.burst_at < sc3.arg; sc3.burst_at++)
            if (note(sc3.burst_at))
                sc3.sent++;
            else {
                sc3.refused++;
                return;
            }
        sc3.burst_at = 0;
        break;
    case CMD3_IRQ_BURST:
        sc3.irq_left = sc3.arg;
        break;
    case CMD3_HSET:
        core_store_set(STORE(sc_h_project), 0, 0, (unsigned)sc3.arg);
        if ((p = core_current_pattern_item()) != 0)
            core_store_set(STORE(sc_h_pattern), p, 0, (unsigned)sc3.arg + 1);
        break;
    case CMD3_HWALK:
        sc3.walk_n = 0;
        sc3.walk_sum = sc3.walk_fields = 0;
        core_handle_fields(walk_fn);
        break;
    case CMD3_SYSEX:
        if (sc3.arg < 4 || sc3.arg > 64)
            break;
        sx[0] = 0xf0;
        sx[1] = 0x7d;
        for (i = 2; i < sc3.arg - 1; i++)
            sx[i] = (unsigned char)(i & 0x7f);
        sx[sc3.arg - 1] = 0xf7;
        if (core_midi_try_send(sc3.arg, sx))
            sc3.sent++;
        else
            sc3.refused++;
        break;
    default:
        return;
    }
    sc3.done = sc3.cmd;
    sc3.cmd = CMD3_NONE;
}
