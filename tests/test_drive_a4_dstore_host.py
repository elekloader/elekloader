# SPDX-License-Identifier: GPL-2.0-or-later
"""The Analog Four MKII Drive Loader's +Drive storage allocator (mods/drive-loader-a4mk2/dstore.c, lock
codes per track kind), built for the computer and run through every step of
js/test/drivestore-a4-vectors.json and random runs against elekloader/drive_store.py.

    python tests/test_drive_a4_dstore_host.py

Needs a C compiler (cc)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_drive_store_host  # noqa: E402

if __name__ == '__main__':
    sys.exit(test_drive_store_host.main(a4=True))
