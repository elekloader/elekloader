# SPDX-License-Identifier: GPL-2.0-or-later
"""The Analog Four MKII Drive Loader's +Drive storage allocator (mods/drive-loader-a4mk2/dstore.c, lock
codes per track kind), built for the computer and run through every step of
js/test/drivestore-a4-vectors.json and random runs against elekloader/drive_store.py; and a
format-4 /mods/.store (the Rytm's: codes not per kind, with knob ids), which the A4 never writes,
read as none: a fresh placement, no crash.

    python tests/test_drive_a4_dstore_host.py

Needs a C compiler (cc)."""
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import test_drive_store_host as t  # noqa: E402
from elekloader import drive_store  # noqa: E402


def format_four(kinded_store):
    """an A4 .store's state made the Rytm's: one code counter, no kinds, and a knob run -> format 4"""
    st = drive_store.decode(kinded_store)
    st['next_code'] = st['next_code']['synth']
    st['knobs'] = [{'mod': 'm', 'first': 1, 'count': 2}]
    st['freed_knobs'], st['next_knob'] = [], 3
    raw = drive_store.encode(st)
    assert struct.unpack_from('>H', raw, 4)[0] == drive_store.FORMAT_ONE_KNOBS
    return raw


def format_four_reads_as_none():
    data = json.load(open(t.A4_VECTORS, encoding='utf-8'))
    exe = t.build(tempfile.mkdtemp(), t.A4_SRC)
    fails = 0
    for run in data['runs'][:4]:
        (bj, (op, arg)), want = run['steps'][0], run['results'][0]
        if op != 'place' or 'error' in want:
            continue
        mods = [data['mods'][k] for k in arg]
        fresh = subprocess.run([exe], input=t.step_input(bj, mods, None, op, arg), capture_output=True, text=True)
        raw = format_four(bytes.fromhex(want['store']))
        four = subprocess.run([exe], input=t.step_input(bj, mods, raw, op, arg), capture_output=True, text=True)
        ok = not fresh.returncode and not four.returncode and json.loads(four.stdout) == json.loads(fresh.stdout)
        # the loader's decoder takes the A4's own file and refuses the format-4 one (forget exits 3
        # when ds_decode gives none), so the placement above was a fresh one
        dec = [subprocess.run([exe], input=t.step_input(bj, [], prev, 'forget', 'x'), capture_output=True,
                              text=True).returncode for prev in (bytes.fromhex(want['store']), raw)]
        ok = ok and dec == [0, 3]
        # the host's allocator takes it the same way: not the A4's kind, so a fresh placement
        ok = ok and drive_store._kinded(drive_store.decode(raw)) is False
        print('%s a format-4 .store reads as none on the A4 (%s)' % ('ok  ' if ok else 'FAIL', run['name']))
        fails += not ok
    return fails


if __name__ == '__main__':
    rc = t.main(a4=True)
    sys.exit(rc or (1 if format_four_reads_as_none() else 0))
