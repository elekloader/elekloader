"""DigiHabit's constant tables (tables.h): the shell's (pedal_tables.py) and its own knob curves.
python3 gentables.py > tables.h"""
import math
from pedal_tables import arr, knob, emit, FS

# FILTER's Chamberlin SVF run twice a frame: f = 2 sin(pi fc / 96 kHz), Q12, 40 Hz .. 8 kHz over 256 log steps
arr('int16_t', 'hb_svf2', [int(round(4096 * 2 * math.sin(math.pi * 40.0 * 200.0 ** (i / 255.0) / (2 * FS))))
                           for i in range(256)], 16)
emit('DigiHabit')
