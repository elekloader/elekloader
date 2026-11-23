/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: core_midi_try_send and core_usb_midi_up, core-ar2 0.11's
 * (mods/core-ar2/moments.c) at OS 1.56's addresses (README, "Moments
 * ").
 *
 * A whole MIDI message out of USB, now or never: 1 sent, 0 not now. From a
 * task it goes through stock's sender (0x40003578, the Rytm's 0x400029e0
 * with the A4's addresses) under its mutex's try-lock; from an interrupt it
 * calls the sender at once, with every interrupt masked, when no task holds
 * the mutex and the same checks pass, else it goes into core's ring, which
 * the next sequencer tick (stdmidi.c) or ev_tick sends.
 */

#include <stdint.h>

/* stock's USB MIDI out */
#define TX_MUTEX ((void *)0x4287c6a8)            /* 0x400034ac locks it, 0x400034bc gives it */
#define TX_OWNER (*(volatile unsigned *)0x4287c6a8) /* the mutex's owner task, 0: free */
#define TRY_LOCK ((int (*)(void *))0x40001d34)  /* 1 taken (the owner the current task), 0 held */
#define GIVE ((void (*)(void *))0x40001d86)
#define SEND ((void (*)(int, const unsigned char *))0x40003578)
#define PORT_STATE (*(volatile int *)0x4133db58)        /* 1: configured */
#define USB_ON (*(volatile unsigned *)0xfc0b01c4)       /* ENDPTCTRL1 */
#define TX_SYSEX (*(volatile int *)0x4133db48)  /* 0xF0 while a SysEx is under way */
#define TX_LEVEL (*(volatile int *)0x4133db5c)  /* bytes submitted, not yet done */
#define TX_AT (*(volatile int *)0x4133db60)     /* the next byte of the 4 KB buffer */
#define TX_ROOM 0xffb                           /* the sender waits above this */
/* the sender's transfer requests: a free list whose head is here, linked at
 * +0x34; its pop (0x400045a8) spins while the head's byte +7 is negative
 * (still busy), and with none free the sender waits (0x400045fc) */
#define REQ_FREE (*(unsigned char *volatile *)0x4133dc94)
#define REQ_NEXT 0x34
#define REQ_BUSY 7

static unsigned short irq_mask(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    return sr;
}

static void irq_restore(unsigned short sr)
{
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

static int in_interrupt(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0" : "=d"(sr));
    return (sr & 0x0700) != 0;
}

/* 1 while USB is on and the host has configured the MIDI port, else 0: the
 * port state core_midi_try_send checks first. Any task or interrupt. */
int core_usb_midi_up(void)
{
    return PORT_STATE == 1 && USB_ON ? 1 : 0;
}

/* whether msg is one whole message the sender takes as it is: a status byte
 * first, and a SysEx (or one inside) ended in the same call, so stock's
 * framing state never stays open on a mod's bytes */
static int whole(int len, const unsigned char *msg)
{
    int i, ex = 0;
    if (len < 1 || len > 512 || !(msg[0] & 0x80))
        return 0;
    for (i = 0; i < len; i++)
        if (msg[i] == 0xf0)
            ex = 1;
        else if (msg[i] == 0xf7)
            ex = 0;
    return !ex;
}

/* the requests the sender needs, free and not busy */
static int requests_free(int need)
{
    unsigned char *r = REQ_FREE;
    if (!r || (signed char)r[REQ_BUSY] < 0)
        return 0;
    for (; r; r = *(unsigned char **)(r + REQ_NEXT))
        if (--need == 0)
            return 1;
    return 0;
}

/* whether the sender takes len bytes now without waiting: the port up, no
 * stock SysEx open, 4 bytes a message byte below its level's ceiling, not
 * a long message near the buffer's end, and the requests it needs free.
 * With the mutex held (a task) or no task holding it (an interrupt). */
static int room(int len)
{
    int at;
    if (PORT_STATE != 1 || !USB_ON || TX_SYSEX == 0xf0)
        return 0;
    at = TX_AT;
    if (TX_LEVEL + 4 * len > TX_ROOM ||
        (len > 0x2f && at > 0xfbf && TX_LEVEL != 0))   /* 0.11: core_usb_send_site waits for 0 */
        return 0;
    return requests_free(at + 4 * len > 0xfff ? 2 : 1);
}

/* core's ring: messages that waited, sent in order; each is its length
 * byte then its bytes. ring_in moves with interrupts masked; ring_out only
 * where the sender is core's: a task holding the mutex, or an interrupt
 * while no task holds it and no other interrupt is sending (irq_busy). */
#define RING 1024
#define RING_MSG 128
static unsigned char ring[RING];
static volatile unsigned ring_in, ring_out;     /* free-running */
static volatile int irq_busy;

/* the interrupt path's counts and its longest masked send, in DMA timer 0
 * ticks (core's frame meter runs the timer) */
#define DTCN0 (*(volatile unsigned *)0xfc07000c)
struct {
    char magic[4];
    uint32_t now, queued, drained, refused, max_ticks;
} core_midi_irq = {{'M', 'I', 'R', 'Q'}};

static int queue(int len, const unsigned char *msg)
{
    unsigned short sr;
    int i;
    if (len > RING_MSG)
        return 0;
    sr = irq_mask();
    if (RING - (ring_in - ring_out) < (unsigned)len + 1) {
        irq_restore(sr);
        return 0;
    }
    ring[ring_in % RING] = (unsigned char)len;
    for (i = 0; i < len; i++)
        ring[(ring_in + 1 + i) % RING] = msg[i];
    ring_in += len + 1;
    irq_restore(sr);
    return 1;
}

/* from a task holding the mutex: the ring's messages while the sender has
 * room; 1 when it is empty */
static int drain_ring(void)
{
    unsigned char m[RING_MSG];
    int len, i;
    while (ring_out != ring_in) {
        len = ring[ring_out % RING];
        if (!room(len))
            return 0;
        for (i = 0; i < len; i++)
            m[i] = ring[(ring_out + 1 + i) % RING];
        SEND(len, m);
        ring_out += len + 1;
    }
    return 1;
}

/* one message to the sender from an interrupt, every interrupt masked for
 * the call (as sidecar-a4 sends on the A4: the sender's completion cannot
 * cut in between the checks and the send); 0 when it has no room */
static int irq_one(int len, const unsigned char *msg)
{
    unsigned short sr = irq_mask();
    unsigned t0 = DTCN0;
    int ok = room(len);
    if (ok)
        SEND(len, msg);
    t0 = DTCN0 - t0;
    if (t0 > core_midi_irq.max_ticks)
        core_midi_irq.max_ticks = t0;
    irq_restore(sr);
    return ok;
}

/* from an interrupt: the ring's messages queued before this call, then
 * msg, while no task holds the mutex (a task inside the sender) and no
 * other interrupt is sending (irq_busy); never the try-lock or the give,
 * which are a task's. msg 0: the ring only */
static int irq_send(int len, const unsigned char *msg)
{
    unsigned char m[RING_MSG];
    unsigned short sr;
    unsigned end;
    int sent = 0, n, i;
    sr = irq_mask();
    if (irq_busy || TX_OWNER != 0) {
        irq_restore(sr);
        goto later;
    }
    irq_busy = 1;
    end = ring_in;
    irq_restore(sr);
    while (ring_out != end) {
        n = ring[ring_out % RING];
        for (i = 0; i < n; i++)
            m[i] = ring[(ring_out + 1 + i) % RING];
        if (!irq_one(n, m))
            break;
        ring_out += n + 1;
        core_midi_irq.drained++;
    }
    if (msg && ring_out == end)
        sent = irq_one(len, msg);
    irq_busy = 0;
later:
    if (!msg)
        return 0;
    if (sent) {
        core_midi_irq.now++;
        return 1;
    }
    if (queue(len, msg)) {
        core_midi_irq.queued++;
        return 1;
    }
    core_midi_irq.refused++;
    return 0;
}

/* the sequencer tick's exit (stdmidi.c), before ev_seq_tick: what waited */
void core_midi_irq_drain(void)
{
    if (ring_out != ring_in)
        irq_send(0, 0);
}

/* from a task: under the try-lock, the ring first, then msg if the ring
 * emptied and there is room; a message that would overtake a queued one
 * is queued instead */
static int task_send(int len, const unsigned char *msg)
{
    int r = 0;
    if (!TRY_LOCK(TX_MUTEX))
        return ring_out != ring_in ? queue(len, msg) : 0;
    if (!drain_ring())
        r = queue(len, msg);
    else if (room(len)) {
        SEND(len, msg);
        r = 1;
    }
    GIVE(TX_MUTEX);
    return r;
}

int core_midi_try_send(int len, const unsigned char *msg)
{
    if (!msg || !whole(len, msg))
        return 0;
    if (in_interrupt())
        return irq_send(len, msg);
    return task_send(len, msg);
}

/* ev_tick (order 3): the queued sends, in the UI task */
void core_midi_tick(void *ctrl)
{
    (void)ctrl;
    if (ring_out != ring_in && TRY_LOCK(TX_MUTEX)) {
        drain_ring();
        GIVE(TX_MUTEX);
    }
}
