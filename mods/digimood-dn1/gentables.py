"""DigiMood's constant tables (tables.h): the shell's (pedal_tables.py) and its own knob curves. The exponential
curves (LENGTH, TIME, the grains, XFAD, ENV's threshold, SMOOTH CLOCK) are worked out in dsp.c instead, to keep
the RAM the mods share. python3 gentables.py > tables.h"""
import math
from pedal_tables import arr, knob, emit, FS

# CLOCK's steps, as the MOOD MKII's (its CC 18 chart): 2K .. 48K, the clock's step through the frames a tick in
# Q16 (65536 is every frame at 48 kHz). The pedal's 64K is its 48K here.
arr('int32_t', 'md_clk_step', [int(round(65536 * k / 48.0)) for k in (2, 3, 4, 6, 8, 12, 16, 24, 32, 48)], 10)
# REVERB's TIME: its lines' size (Q8 of the base lengths) and their feedback (Q15), together
arr('int16_t', 'md_rmul', knob(lambda u: int(round(70 + 530 * u))), 16)
arr('int16_t', 'md_rfb', knob(lambda u: int(round(32768 * (0.55 + 0.40 * u ** 0.6)))), 16)
# DAMP: the one-pole inside the wet's feedback, Q15: 20 kHz (0) .. 1.2 kHz (127)
arr('int16_t', 'md_damp', knob(lambda u: int(round(32767 * (1 - math.exp(-2 * math.pi * 20000.0 * (0.06 ** u) / FS))))),
    16)
emit('DigiMood')
