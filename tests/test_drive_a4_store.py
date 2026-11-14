# SPDX-License-Identifier: GPL-2.0-or-later
"""drive-loader-a4mk2's own store (mods/drive-loader-a4mk2/store.c), built for the computer and run
over a card in RAM (tests/drive_a4_store_host.c): files of every size written and read back in odd
chunks, a failed write and a cut directory write leaving the store as it was, a torn newer copy
falling back to the older, folders three deep, moves of files and folders, the route's reservations
and commits, runs that never share a sector, FORMAT +DRIVE, and no write outside 0x3000..0x7a000.

    python tests/test_drive_a4_store.py

Needs a C compiler (cc)."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), 'mods', 'drive-loader-a4mk2')


def run(*defs):
    with tempfile.TemporaryDirectory() as work:
        exe = os.path.join(work, 'a4store')
        subprocess.run(['cc', '-O1', '-Wall', '-Wno-format-truncation', '-DDL_HOST', *defs, '-I', SRC, '-o', exe,
                        os.path.join(HERE, 'drive_a4_store_host.c'), os.path.join(SRC, 'store.c')], check=True)
        return subprocess.run([exe], capture_output=True, text=True)


def test_a4_store():
    r = run()
    assert r.returncode == 0 and r.stdout.rstrip().endswith('PASSED'), r.stdout


def test_a4_store_test_folder():
    """The same with the unit-run test folder (-DDL_TEST_DIR, `/~T MODS`)."""
    r = run('-DDL_TEST_DIR')
    assert r.returncode == 0 and r.stdout.rstrip().endswith('PASSED'), r.stdout


# drive-loader-ar2's files the A4 loader carries unchanged (decided 2026-11-14: a separate mod, no
# shared source; later Rytm fixes are ported by hand). A difference here is a fix to port, or a
# change to make on purpose: then take the file off this list.
SAME_AS_RYTM = ('dlink.c', 'json.c', 'json.h', 'sha256.c', 'dstore.c', 'dstore.h', 'park.c', 'park.h',
                'names.inc')


def test_portable_files_are_the_rytms():
    rytm = os.path.join(os.path.dirname(SRC), 'drive-loader-ar2')
    for name in SAME_AS_RYTM:
        with open(os.path.join(SRC, name), 'rb') as a, open(os.path.join(rytm, name), 'rb') as b:
            assert a.read() == b.read(), name


if __name__ == '__main__':
    for t in (test_a4_store, test_a4_store_test_folder, test_portable_files_are_the_rytms):
        t()
        print('ok', t.__name__)
