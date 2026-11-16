# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes js/test/drivereserve-vectors.json: every store.allocate call tests/test_drive_reserve.py makes
(a lockfile and what the unit's +Drive keeps in, the new lockfile and its notes or the refusal out) and
every drive_store.reserve it reads from a .store, so test/drivereserve.test.ts can check store.ts and
drivestore.ts give the same. Run from the repository root:

    python js/tools/drivereserve_vectors.py
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))

from elekloader import drive_store as ds, store            # noqa: E402
from elekloader.elemod import ModError                     # noqa: E402
import test_drive_reserve as T                             # noqa: E402

calls, reserves = [], []
allocate, reserve = store.allocate, ds.reserve


def rec_allocate(lock, decls, dev, core=None, locks=(), ccs=(), inits=None, cvs=(), drive=None):
    case = {'lock': lock, 'decls': [list(d) for d in decls], 'device': dev, 'locks': [list(k) for k in locks],
            'ccs': [list(k) for k in ccs], 'drive': drive}
    try:
        new, notes = allocate(lock, decls, dev, core, locks, ccs, inits, cvs, drive)
    except ModError as e:
        case['error'] = str(e)
        calls.append(json.loads(json.dumps(case)))
        raise
    case['out'] = store.dumps(new)
    case['notes'] = notes
    calls.append(json.loads(json.dumps(case)))
    return new, notes


def rec_reserve(st):
    out = reserve(st)
    reserves.append({'store': ds.encode(st).hex(), 'reserve': out})
    return out


store.allocate, ds.reserve = rec_allocate, rec_reserve
for name, fn in sorted(vars(T).items()):
    if name.startswith('test_') and callable(fn):
        fn()
path = os.path.join(ROOT, 'js', 'test', 'drivereserve-vectors.json')
with open(path, 'w') as fh:
    json.dump({'allocate': calls, 'reserve': reserves}, fh, separators=(',', ':'))
    fh.write('\n')
print('wrote %s: %d allocations, %d reserves' % (path, len(calls), len(reserves)))
