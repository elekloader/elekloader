/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include "../mods/core-dn2/peek.c"

volatile unsigned short DSP_QUEUE[16];
void DSP_FRAME(unsigned data_bytes, const void *data, unsigned frame_bytes, void *reply) {}

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
    return 0;
}
