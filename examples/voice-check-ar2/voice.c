/* SPDX-License-Identifier: GPL-2.0-or-later */
/* voice-check: core-ar2 0.17's std1 voice events, one mod built with
 * "events": "std1" against elekloader/sdk/include/std1.h; voice-check-a4mk2
 * for the Rytm's eight voices and twelve tracks. Both handlers run in the
 * audio interrupt, so they only count. Everything is in voice_check (an
 * exported symbol), for an emulator's or a unit's memory read:
 *   magic        0x564f4943 ("VOIC") once ev_boot ran
 *   start[v]     ev_voice_start's calls for voice v (0..7)
 *   end[v]       ev_voice_end's calls for voice v
 *   track[v]     the track of voice v's last start, 0xff before any
 *   playing      a bit per voice: started and not yet ended
 *   restart      starts on a voice already playing (0: core ends a voice
 *                just before its new start)
 *   steal        starts for a track other than the voice's last one
 *   orphan       ends on a voice not playing
 *   bad          calls with a voice or track out of range
 */
#include <stdint.h>
#include <std1.h>

#define VOICES 8
#define TRACKS 12

struct voice_check {
    uint32_t magic;
    uint32_t start[VOICES], end[VOICES];
    uint8_t track[VOICES];
    uint32_t playing, restart, steal, orphan, bad;
};
volatile struct voice_check voice_check;

void vc_boot(void)
{
    int v;
    voice_check.magic = 0x564f4943u;
    for (v = 0; v < VOICES; v++)
        voice_check.track[v] = 0xff;
}

void vc_start(unsigned voice, unsigned track)
{
    if (voice >= VOICES || track >= TRACKS) {
        voice_check.bad++;
        return;
    }
    voice_check.start[voice]++;
    if (voice_check.track[voice] != 0xff && voice_check.track[voice] != track)
        voice_check.steal++;
    voice_check.track[voice] = (uint8_t)track;
    if (voice_check.playing & (1u << voice))
        voice_check.restart++;
    voice_check.playing |= 1u << voice;
}

void vc_end(unsigned voice)
{
    if (voice >= VOICES) {
        voice_check.bad++;
        return;
    }
    voice_check.end[voice]++;
    if (!(voice_check.playing & (1u << voice)))
        voice_check.orphan++;
    voice_check.playing &= ~(1u << voice);
}
