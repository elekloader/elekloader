"""The pedal shell's constant tables (pedal.h): a pedal's gentables.py imports this, adds its own with arr(), and
prints the lot with emit(). python3 gentables.py > tables.h"""
import math

FS = 48000.0
out = []


def arr(ctype, name, vals, per=12):
    out.append('static const %s %s[%d] = {' % (ctype, name, len(vals)))
    for i in range(0, len(vals), per):
        out.append('    ' + ', '.join(str(v) for v in vals[i:i + per]) + ',')
    out.append('};')


def clip16(v):
    return max(-32768, min(32767, int(round(v))))


def knob(fn, n=129):
    """A knob's curve: 129 values, so 0-127 interpolates with its fraction (the last repeats 127's)."""
    return [fn(min(k, 127) / 127.0) for k in range(n)]


def shaper(name, f):
    """A waveshaper: 1025 points over -4.0 .. +4.0 of full scale (32768), Q15 out."""
    arr('int16_t', name, [clip16(32767 * f(-4.0 + 8.0 * i / 1024)) for i in range(1025)], 16)


# the output's soft clip: straight to 0.8, then a tanh into 1.0
shaper('sh_out', lambda x: x if abs(x) < 0.8 else math.copysign(0.8 + 0.2 * math.tanh((abs(x) - 0.8) / 0.2), x))
shaper('sh_soft', lambda x: math.tanh(x))
arr('int16_t', 'pd_sin', [clip16(32767 * math.sin(2 * math.pi * i / 256)) for i in range(257)], 16)
# 2^(i/256), Q12 (4096..8191): a ratio from an offset in 1/256 octaves.
arr('uint16_t', 'pd_exp2', [int(round(4096 * 2 ** (i / 256.0))) for i in range(256)], 16)


# one-pole coefficients: a = 1 - exp(-2 pi f / fs), Q14, f 20 Hz .. 20 kHz over 256 log steps
def op_f(i):
    return 20.0 * 1000.0 ** (i / 255.0)


arr('int16_t', 'pd_op', [int(round(16384 * (1 - math.exp(-2 * math.pi * op_f(i) / FS)))) for i in range(256)], 16)
# the Chamberlin SVF's f = 2 sin(pi fc / fs), Q14, 40 Hz .. 12 kHz over 256 log steps
arr('int16_t', 'pd_svf', [int(round(16384 * 2 * math.sin(math.pi * 40.0 * 300.0 ** (i / 255.0) / FS)))
                          for i in range(256)], 16)


# Volumes: 64 is 0 dB; -36 dB at 1, 0 mutes; +12 dB at 127. Q12.
def vol(u):
    k = u * 127
    if k < 0.5:
        return 0
    db = (k - 64) * 12 / 63.0 if k >= 64 else (k - 64) * 36 / 63.0
    return int(round(4096 * 10 ** (db / 20)))


arr('int16_t', 'pd_vol', knob(vol), 16)
# Mix: equal power, Q12.
arr('int16_t', 'pd_mixd', knob(lambda u: int(round(4096 * math.cos(math.pi / 2 * u)))), 16)
arr('int16_t', 'pd_mixw', knob(lambda u: int(round(4096 * math.sin(math.pi / 2 * u)))), 16)
# Rates in milli-hertz, exponential: 0.05 .. 16 Hz.
arr('int32_t', 'pd_rate', knob(lambda u: int(round(1000 * 0.05 * 320.0 ** u))), 12)
# Feedback, Q12: 0 .. 1.0 with a gentle top (0.98 at 120, 1.0 from 127: a hold).
arr('int16_t', 'pd_fb', knob(lambda u: int(round(4096 * min(1.0, (u * 127 / 120.0) ** 0.7 * 0.98)))
                             if u < 1 else 4096), 16)


def emit(title):
    print('/* SPDX-License-Identifier: GPL-2.0-or-later */')
    print('/* %s\'s constant tables, from gentables.py and pedal_tables.py: do not edit. */' % title)
    print('#include <stdint.h>')
    print('\n'.join(out))
