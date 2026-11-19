/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 (0.15): track mutes and DIN MIDI out for mods
 * (OS 1.74). The voice note-off getter, core_voice_released, is beside
 * the trig latch in machines.c.
 *
 * Track mutes. The project settings model is the app's +0x30 (the object
 * core_current_track reads, 0x400ae492). Its data's +0x20 holds the track
 * mutes, bit t for track t (0..12); 0x400b6d94(model, t) reads a bit and
 * 0x400b6dd8(model, t, on) sets one and tells the model's observers
 * (ProjectSettingsTrackMuteChangedInfo), as an Overbridge or SysEx settings
 * write does: the engine's copy (0x4011f99e writes 0x8000ed98 from the
 * data's +0x20) and the screens follow. It sends no MIDI, where the mute
 * key's setter (0x400b78d8) may also send CC 94 or an NRPN.
 *
 * DIN MIDI out. Stock's DIN sender 0x40001dea(len, msg) waits while its
 * 4 KB DMA ring lacks room, then copies the whole message into the ring
 * with every interrupt masked and starts the DMA or leaves the bytes for
 * the running one. core_midi_din_try_send checks the same room with
 * interrupts masked and calls it only when the message fits, so it never
 * waits; a message goes out whole, between two of stock's (clock, notes,
 * the sequencer's MIDI tracks). The port must be a MIDI port: OUT PORT
 * FUNC (the settings data's +0x60, 0x400b5424) is cached at ev_tick, and
 * DIN24 or DIN48 refuses.
 */
#include <stdint.h>

#define APP() (*(volatile unsigned *)0x419f28a8)       /* 0 until the app is made */
#define APP_TRACKS ((unsigned (*)(unsigned))0x400ae492) /* the settings model: app + 0x30 */
#define APP_SETTINGS 0xec                               /* the global settings: app + 0xec */
#define MUTED ((unsigned (*)(unsigned, unsigned))0x400b6d94)
#define SET_MUTE ((void (*)(unsigned, unsigned, int))0x400b6dd8)
#define OUT_PORT_FUNC ((int (*)(unsigned))0x400b5424)   /* 0 MIDI, 1 DIN24, 2 DIN48 */
#define TRACKS 13

/* stock's DIN sender and its ring (0x40001dea's room test) */
#define DIN_SEND ((void (*)(int, const unsigned char *))0x40001dea)
#define DIN_HEAD (*(volatile uint32_t *)0x413da670)     /* the DMA's start in the ring */
#define DIN_DONE (*(volatile uint32_t *)0x413da67c)
#define DIN_WAIT (*(volatile uint32_t *)0x413da678)     /* bytes queued behind the DMA */
#define DIN_LEFT (*(volatile uint16_t *)0xfc0454b4)     /* the DMA's count */
#define DIN_LAST (*(volatile uint16_t *)0xfc0454bc)
#define DIN_RING 0x1000
#define DIN_MAX 256        /* bounds the time the copy runs with interrupts masked */

/* ---- track mutes (UI task) ---- */

unsigned core_track_mutes(void)
{
    unsigned app = APP(), mask = 0;
    if (!app)
        return 0;
    unsigned model = APP_TRACKS(app);
    for (unsigned t = 0; t < TRACKS; t++)
        mask |= (MUTED(model, t) & 1) << t;
    return mask;
}

void core_set_track_mutes(unsigned mask)
{
    unsigned app = APP();
    if (!app)
        return;
    unsigned model = APP_TRACKS(app);
    for (unsigned t = 0; t < TRACKS; t++) {
        unsigned on = mask >> t & 1;
        if ((MUTED(model, t) & 1) != on)
            SET_MUTE(model, t, on);
    }
}

/* ---- DIN MIDI out (any task or interrupt) ---- */

static volatile int out_port = -1;      /* OUT PORT FUNC at the last ev_tick; -1 none yet */

void core_voiceio_tick(void *ctrl)
{
    (void)ctrl;
    unsigned app = APP();
    out_port = app ? OUT_PORT_FUNC(app + APP_SETTINGS) : -1;
}

int core_midi_din_up(void)
{
    return out_port == 0;
}

int core_midi_din_try_send(int len, const unsigned char *msg)
{
    unsigned short sr;
    int sent = 0;
    if (len <= 0 || len > DIN_MAX || !msg || out_port != 0)
        return 0;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    uint32_t busy = ((DIN_HEAD - DIN_DONE) + ((uint32_t)DIN_LEFT - (uint32_t)DIN_LAST)) & (DIN_RING - 1);
    if (DIN_RING - busy >= (uint32_t)len + DIN_WAIT) {
        DIN_SEND(len, msg);             /* its room test passes at once; it keeps the mask */
        sent = 1;
    }
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
    return sent;
}
