# SPDX-License-Identifier: GPL-2.0-or-later
"""Core Sound logic with native-object fixtures; no firmware or emulator required.

Run with python tests/test_core_dn1_sound.py. Firmware build/link checks remain
in test_sdk.py; these host callbacks do not prove device ABI or performance.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
from unittest import SkipTest
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_native_sound_logic():
    if sys.platform != 'linux':
        raise SkipTest('native-object host fixtures require Linux low-address non-PIE linking')
    cc = os.environ.get('CC', 'cc')
    if not shutil.which(cc):
        raise RuntimeError('a host C compiler is required')
    for source in ('core_dn1_sound.c', 'core_dn1_selection.c', 'core_dn1_presentation.c'):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp) / 'sound')
            subprocess.run([cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
                            '-Wl,--gc-sections', '-DCORE_NATIVE_UI=1',
                            '-I' + str(ROOT / 'elekloader/sdk/include'),
                            str(ROOT / 'tests' / source), '-o', exe], check=True)
            subprocess.run([exe], check=True)


if __name__ == '__main__':
    try:
        test_native_sound_logic()
    except SkipTest as e:
        print('SKIP', e)
