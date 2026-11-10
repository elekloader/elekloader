#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""look-check: prints the README's table of pages and knobs from looks.h.
Run in this folder: python3 page_table.py"""
import re
h = open('looks.h').read()
looks = [int(x) for x in re.search(r'#define LOOK_IDS (.*)', h).group(1).split(',')]
pairs = [(L, '') for L in looks]
pairs.append((457, "ui callback: a copy of 457's record"))
pairs.append((94, "COMP MIX's look with min 0, max 12 (stock partner shows the value on 0..127)"))
FIRST = 555   # the first own id the linker gives (the lockfile's look-check entry)
print('| page | key | pair | stock id | own id | note |')
print('|---|---|---|---|---|---|')
for k, (L, what) in enumerate(pairs):
    p = k // 4
    print('| %d | %s | %s | %d | %d | %s |' % (p + 1, 'LFO' if p < 12 else 'AMP',
          'ACEG'[k % 4] + '/' + 'BDFH'[k % 4], L, FIRST + k, what))
