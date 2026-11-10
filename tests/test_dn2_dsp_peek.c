/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../mods/core-dn2/peek.c"

volatile unsigned short DSP_QUEUE[64];
void DSP_FRAME(unsigned data_bytes, const void *data, unsigned frame_bytes, void *reply) {}
unsigned core_keys_lock(void) { return 0; }
void core_keys_unlock(unsigned saved_sr) { (void)saved_sr; }

/* Tail word k as the DSP reads it: halfword 2k + 1 above halfword 2k, each a queue entry's data half. */
static unsigned tail_word(unsigned k)
{
    return DSP_QUEUE[2 * (2 * k) + 1] | (unsigned)DSP_QUEUE[2 * (2 * k + 1) + 1] << 16;
}

/* The newest CORE_DSP_POSTS posts ride in tail words 4..13, each its number and its word, the number's
 * low seven bits in bits 25-31; an older one drops out. */
static void newest_posts(void)
{
    unsigned short reply[6] = {0};
    unsigned i, found = 0;
    for (i = 1; i <= 7; ++i)
        core_dsp_post(0x1000000 | i);
    core_dsp_frame(0, 0, 0, reply);
    for (i = 0; i < CORE_DSP_POSTS; ++i) {
        unsigned number = tail_word(4 + 2 * i), word = tail_word(5 + 2 * i);
        assert(number >= 3 && number <= 7);
        assert(word == ((0x1000000 | number) | number << 25));
        found |= 1u << number;
    }
    assert(found == 0xf8);
}

static void read_guard(void)
{
    unsigned short reply[6] = {0};
    unsigned received, total;
    assert(core_dsp_peek(0x2df000, 1) == CORE_DSP_PEEK_OK);
    core_dsp_frame(0, 0, 0, reply);             /* The previous frame was idle. */
    assert(core_dsp_peek_status(&received, &total) == CORE_DSP_PEEK_BUSY);
    assert(received == 0 && total == 2);
    reply[4] = 0x6a2d;
    reply[5] = (seq & 15) << 12;
    core_dsp_frame(0, 0, 0, reply);
    reply[4] = 0x57ac;
    reply[5] |= 1;
    core_dsp_frame(0, 0, 0, reply);
    assert(core_dsp_peek_status(&received, &total) == CORE_DSP_PEEK_OK);
    assert(received == total);
    assert(core_dsp_peek_data()[0] == 0x6a2d && core_dsp_peek_data()[1] == 0x57ac);
    core_dsp_peek_stop();
}

int main(void)
{
    unsigned i;
    for (i = 0; i < 32; ++i)
        read_guard();
    seq = 0xffff;
    read_guard();
    newest_posts();
    return 0;
}
