/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.11's events: counts each and logs its last arguments into `hdp` (read back by a
 * test harness from the emulator's memory). With hdp[TAKE] poked nonzero it takes the held editor's turns.
 */
#include <stdint.h>
#include "octatrack/core.h"

enum { MAGIC, HELD_N, HELD_ENC, HELD_DELTA, TAKE, TAKEN, COPY_N, COPY_DST, COPY_SRC, COPY_LEN,
       NOTE_N, NOTE_TRK, NOTE_NOTE, NOTE_VEL, NOTE_POOL, SCN_N, SCN_ENC, SCN_TAKE, SCN_TAKEN,
       TEMPO_N, TEMPO_TAKE, ARP_N, ARP_ENC, KNOB_N, KNOB_LAST, KNOB_FMT, KNOB_FORCE, WORDS };
volatile uint32_t hdp[WORDS] = { 0x48445031 };  /* 'HDP1' */

int hd_edit_held(unsigned enc, int delta)
{
    ++hdp[HELD_N];
    hdp[HELD_ENC] = enc;
    hdp[HELD_DELTA] = (uint32_t)delta;
    if (hdp[TAKE]) {
        ++hdp[TAKEN];
        return 1;
    }
    return 0;
}

void hd_copy(void *dst, const void *src, unsigned len)
{
    ++hdp[COPY_N];
    hdp[COPY_DST] = (uint32_t)dst;
    hdp[COPY_SRC] = (uint32_t)src;
    hdp[COPY_LEN] = len;
}

void hd_note_sent(unsigned trk, unsigned note, unsigned vel, unsigned pool)
{
    ++hdp[NOTE_N];
    hdp[NOTE_TRK] = trk;
    hdp[NOTE_NOTE] = note;
    hdp[NOTE_VEL] = vel;
    hdp[NOTE_POOL] = pool;
}

/* With hdp[SCN_TAKE] poked nonzero it takes the scene knob's turns. */
int hd_scene_knob(unsigned enc, int delta)
{
    (void)delta;
    ++hdp[SCN_N];
    hdp[SCN_ENC] = enc;
    if (hdp[SCN_TAKE]) {
        ++hdp[SCN_TAKEN];
        return 1;
    }
    return 0;
}

int hd_tempo_open(void) { ++hdp[TEMPO_N]; return hdp[TEMPO_TAKE] != 0; }
int hd_arp_knob(unsigned enc, int delta) { (void)delta; ++hdp[ARP_N]; hdp[ARP_ENC] = enc; return 0; }

/* With hdp[KNOB_FORCE] poked nonzero, every knob the widget draws shows that value. */
void hd_knob_draw(struct ot_knob *k)
{
    ++hdp[KNOB_N];
    hdp[KNOB_LAST] = (uint32_t)k->x << 16 | (uint32_t)k->y << 8 | (k->value & 0xff);
    hdp[KNOB_FMT] = (uint32_t)k->fmt;
    if (hdp[KNOB_FORCE])
        k->value = (int32_t)hdp[KNOB_FORCE];
}
