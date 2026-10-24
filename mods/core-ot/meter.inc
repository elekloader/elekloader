| SPDX-License-Identifier: GPL-2.0-or-later
| ot_meter's fields (core-ot.s, .bss; the first seven are public: core.h)
        .equ    M_FRAMES, 0             | frame interrupts measured
        .equ    M_LAST,   4             | the last one's length, DMA timer 3 counts
        .equ    M_WORST,  8             | the longest
        .equ    M_OVER,   12            | those longer than a frame
        .equ    M_LATE,   16            | frames that began 1.5 frames late
        .equ    M_TPF,    20            | counts a frame takes
        .equ    M_TOTAL,  24            | the sum of the lengths (wraps)
        .equ    M_T0,     28            | private: this frame began
        .equ    M_PREV,   32            | the last frame began
        .equ    M_WIN,    36            | the window of 1,024 frames began
        .equ    M_WINN,   40            | frames into the window
        .equ    OT_METER_SIZE, 44
