"""DigiSky's constant tables (tables.h): the shell's (pedal_tables.py) and its own.
python3 gentables.py > tables.h"""
import math
from pedal_tables import arr, knob, emit, FS


# SWELL's RISE TIME, the BigSky's 23 steps (frames)
RISE = [0.08, 0.10, 0.12, 0.14, 0.17, 0.20, 0.25, 0.30, 0.35, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00, 1.20,
        1.40, 1.70, 2.00, 2.50, 3.00, 4.00]
arr('int32_t', 'sky_rise', [int(round(FS * r)) for r in RISE], 12)
# SHIMMER's intervals, the BigSky's 28 (SHIFT 2 has OFF before them): 1/256 octaves
SEMI = [-12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, -0.1, 0.1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 19, 24]
arr('int16_t', 'sky_shift', [int(round(s * 256 / 12.0)) for s in SEMI], 14)


# NONLINEAR's six shapes over its 16 taps (Q15), each scaled to the same energy
def shape(k, x):
    if k == 0:                  # SWOOSH: slow, then fast
        return 0.05 + x ** 3
    if k == 1:                  # REVERSE: a straight rise, cut off
        return 0.05 + x
    if k == 2:                  # RAMP: a quick rise, then level
        return 0.1 + math.sqrt(x)
    if k == 3:                  # GATE: even, cut off
        return 1.0
    if k == 4:                  # GAUSS: a bell
        return math.exp(-((x - 0.5) / 0.22) ** 2)
    return 0.15 + 1 - math.exp(-((x - 0.5) / 0.25) ** 2)        # BOUNCE: the bell upside down


nl = []
for k in range(6):
    g = [shape(k, i / 15.0) for i in range(16)]
    e = math.sqrt(sum(v * v for v in g) / 2)                    # each side has eight taps
    nl += [int(round(32767 * 0.9 * v / e)) for v in g]
arr('int16_t', 'sky_nlg', nl, 16)
emit('DigiSky')
