"""DigiTriad's constant tables (tables.h): the shell's (pedal_tables.py) and its own: the PITCH knobs' intervals
and KEY's bands, the time divisions, log2, the knob curves.
python3 gentables.py > tables.h"""
import math
from pedal_tables import arr, emit

OFF = -128


def bands(spec):
    """A 0-127 map from (first value, entry) pairs, each entry up to the next's first value."""
    t = []
    for i, (lo, v) in enumerate(spec):
        hi = spec[i + 1][0] if i + 1 < len(spec) else 128
        t += [v] * (hi - lo)
    assert len(t) == 128
    return t


# PITCH 1-3 with KEY on chromatic: semitones (the chromatic chart; 0 is OFF)
arr('int8_t', 'dh_chrom', bands([(0, OFF), (1, -24), (3, -12), (12, -11), (16, -10), (20, -9), (24, -8), (28, -7),
                                 (32, -6), (36, -5), (40, -4), (44, -3), (48, -2), (52, -1), (56, 0), (72, 1), (76, 2),
                                 (80, 3), (84, 4), (88, 5), (92, 6), (96, 7), (100, 8), (104, 9), (108, 10),
                                 (112, 11), (116, 12), (124, 19), (126, 24)]), 16)
# with a seven-note scale: scale steps (a third is two steps up; the octave seven)
arr('int8_t', 'dh_dia', bands([(0, OFF), (1, -14), (8, -7), (12, -6), (20, -5), (28, -4), (36, -3), (44, -2),
                               (52, -1), (60, 0), (68, 1), (76, 2), (84, 3), (92, 4), (100, 5), (108, 6), (116, 7),
                               (120, 14)]), 16)
# with a pentatonic scale: its steps (the octave five)
arr('int8_t', 'dh_pent', bands([(0, OFF), (1, -10), (20, -5), (28, -4), (36, -3), (44, -2), (52, -1), (60, 0),
                                (68, 1), (76, 2), (84, 3), (92, 4), (100, 5), (108, 10)]), 16)
# KEY: C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B, then chromatic (12)
arr('uint8_t', 'dh_key', bands([(0, 0), (3, 1), (12, 2), (26, 3), (38, 4), (48, 5), (59, 6), (67, 7), (78, 8),
                                (88, 9), (98, 10), (109, 11), (120, 12)]), 16)
# the time divisions (TDV1-3): 0 off; the micro range (1-14) a short delay, -steps of 3 ms;
# then 1/6, 1/4, 1/3, 3/8, 1/2, 5/8, 2/3, 3/4, 5/6, 7/8 and 1 of the main time, in 24ths
arr('int8_t', 'dh_tdiv', bands([(0, 0)] + [(v, -v) for v in range(1, 15)]
                                + [(15 + 11 * i, d) for i, d in enumerate([4, 6, 8, 9, 12, 15, 16, 18, 20, 21])]
                                + [(125, 24)]), 16)
# log2(1 + i/64), Q12
arr('uint16_t', 'dh_log2', [int(round(4096 * math.log2(1 + i / 64.0))) for i in range(65)], 16)

emit('DigiTriad')
