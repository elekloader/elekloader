# SPDX-License-Identifier: GPL-2.0-or-later
"""drive-loader-a4mk2's /mods route (mods/drive-loader-a4mk2/route.c) over its store (store.c),
built for the computer and run over a card in RAM (tests/drive_a4_route_host.c): an upload in
stock's order, with the entry copied as the router copies it (every std::function slot its own
closure), reaches its commit and reads back; a Clear resolved after a read reserves nothing.

    python tests/test_drive_a4_route.py

Needs a C compiler (cc) that builds 64-bit x86 or another little-endian host with -no-pie, so the
route's 32-bit slots hold the host's pointers."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), 'mods', 'drive-loader-a4mk2')


def run(*defs):
    with tempfile.TemporaryDirectory() as work:
        exe = os.path.join(work, 'a4route')
        subprocess.run(['cc', '-O1', '-Wall', '-Wno-format-truncation', '-Wno-pointer-to-int-cast',
                        '-Wno-int-to-pointer-cast', '-no-pie', '-DDL_HOST', *defs, '-I', SRC, '-o', exe,
                        os.path.join(HERE, 'drive_a4_route_host.c'), os.path.join(SRC, 'store.c')], check=True)
        return subprocess.run([exe], capture_output=True, text=True)


def test_a4_route_upload():
    r = run()
    assert r.returncode == 0 and r.stdout.rstrip().endswith('PASSED'), r.stdout


def test_a4_route_upload_test_folder():
    """The same with the unit-run test folder (-DDL_TEST_DIR, `/~T MODS`)."""
    r = run('-DDL_TEST_DIR')
    assert r.returncode == 0 and r.stdout.rstrip().endswith('PASSED'), r.stdout


if __name__ == '__main__':
    for t in (test_a4_route_upload, test_a4_route_upload_test_folder):
        t()
        print('ok', t.__name__)
