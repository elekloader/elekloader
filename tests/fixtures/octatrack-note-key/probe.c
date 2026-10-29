/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe for intercepts-ot's ev_midi_note_key (tests/probe_octatrack_note_key.py): one record
 * whose claims counts the track it is asked about and claims the tracks in nk_cfg.claim, and
 * whose entry (entry.s) logs the track and the note, then acts by nk_cfg.mode. The probe writes
 * the configuration into the image's RAM before the sequencer starts. Not a mod to ship. */
#include <stdint.h>

struct nk_config {
    uint32_t claim;     /* the tracks claims says yes to: bit t */
    uint32_t mode;      /* 0 observe, 1 note+2 then stock, 2 drop, 3 note+2 then send, 4 set KEY offset then stock */
    uint32_t offset;    /* mode 4: stock's KEY root offset, -64(%fp) */
};
struct nk_config nk_cfg;

uint32_t nk_asked[16];      /* claims calls, by track */
uint8_t nk_log[64 * 4];     /* the entry's log: track, the note it was given */
uint32_t nk_n;

int nk_claims(unsigned track)
{
    nk_asked[track & 15]++;
    return (nk_cfg.claim >> (track & 15)) & 1;
}