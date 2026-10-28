/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The ColdFire side of the DSP peek (the DSP side is peek.s) and the block handler's span.
 * The frame sender's call of the SPI driver comes here. The driver's transmit queue has one
 * 32-bit entry per frame halfword, command half first, and it writes data halves only for the
 * data it is given: the frame's tail keeps the data halves written here, which carry the
 * request. The reply it copies carries the DSP's answer in word 2 and its span in word 0. */
#include "digitone-mk2/core.h"

extern void DSP_FRAME(unsigned data_bytes, const void *data, unsigned frame_bytes, void *reply);
extern volatile unsigned short DSP_QUEUE[];

enum { MAGIC = 0xd5ee, CHECK = 0x5a5a5a5a };

static volatile unsigned request[4];          /* as the DSP reads them; request[0] last */
static volatile unsigned short words[2 * CORE_DSP_PEEK_MAX];
static volatile unsigned char got[2 * CORE_DSP_PEEK_MAX / 8];
static volatile unsigned received, total, refused, seq;
static volatile unsigned span, span_peak;

void core_dsp_frame(unsigned data_bytes, const void *data, unsigned frame_bytes, void *reply)
{
    const volatile unsigned short *r = reply;
    unsigned i, at = data_bytes / 2, tag, index;
    /* The DSP reads word k of the tail as halfword 2k + 1 above halfword 2k. */
    for (i = 0; i < 4; ++i) {
        DSP_QUEUE[2 * (at + 2 * i) + 1] = request[i] & 0xffff;
        DSP_QUEUE[2 * (at + 2 * i + 1) + 1] = request[i] >> 16;
    }
    DSP_FRAME(data_bytes, data, frame_bytes, reply);
    /* The DSP stores its words with the halves swapped, so word 0 reads whole. */
    span = (unsigned)r[0] << 16 | r[1];
    if (span > span_peak)
        span_peak = span;
    tag = r[5];
    if (!request[0] || refused)
        return;
    index = tag & 0xfff;
    if ((tag >> 12) == (seq & 15) && index == 0xfff) {      /* this request's refusal */
        refused = 1;
        return;
    }
    if ((tag >> 12) == (seq & 15) && index < total && !(got[index / 8] & 1 << index % 8)) {
        words[index] = r[4];
        got[index / 8] |= 1 << index % 8;
        ++received;
    }
}

int core_dsp_peek(unsigned address, unsigned count)
{
    unsigned i, s;
    if (!count || count > CORE_DSP_PEEK_MAX || address % 4)
        return CORE_DSP_PEEK_INVALID;
    request[0] = 0;                            /* no frame carries a half-written request */
    for (i = 0; i < sizeof got; ++i)
        got[i] = 0;
    received = refused = 0;
    total = 2 * count;
    do {                                      /* Reply tag zero means idle. */
        s = seq = (seq + 1) & 0xffff;
    } while (!(s & 15));
    request[1] = address;
    request[2] = count;
    request[3] = (MAGIC | s << 16) ^ address ^ count ^ CHECK;
    request[0] = MAGIC | s << 16;
    return CORE_DSP_PEEK_OK;
}

int core_dsp_peek_status(unsigned *halfwords_received, unsigned *halfwords_total)
{
    *halfwords_received = received;
    *halfwords_total = total;
    if (refused)
        return CORE_DSP_PEEK_REFUSED;
    return request[0] && received < total ? CORE_DSP_PEEK_BUSY : CORE_DSP_PEEK_OK;
}

const volatile unsigned short *core_dsp_peek_data(void)
{
    return words;
}

void core_dsp_peek_stop(void)
{
    request[0] = 0;
}

void core_dsp_span(unsigned *latest, unsigned *peak)
{
    *latest = span;
    *peak = span_peak;
    span_peak = span;
}
