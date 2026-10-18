"""DigiReel's constant tables (tables.h): the shell's (pedal_tables.py) and its own knob curves.
python3 gentables.py > tables.h"""
from pedal_tables import arr, knob, emit, FS

# TIME, free: 30 ms .. 4 s, exponential (frames)
arr('int32_t', 'dt_time', knob(lambda u: int(round(FS * 0.030 * (4.0 / 0.030) ** u))), 12)
# GRAIN: 20 .. 150 ms (frames)
arr('int32_t', 'dt_grain', knob(lambda u: int(round(FS * 0.020 * (0.150 / 0.020) ** u))), 12)
# XFADE: 2 .. 40 ms (frames)
arr('int32_t', 'dt_xfade', knob(lambda u: int(round(FS * 0.002 * (0.040 / 0.002) ** u))), 12)
emit('DigiReel')
