# SPDX-License-Identifier: GPL-2.0-or-later
"""DN2 input ownership/queue checks; no stock image needed."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class Input(unittest.TestCase):
    def test_ownership_and_encoder_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=str(Path(tmp)/'input')
            subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',
                            str(Path(__file__).with_suffix('.c')),'-o',exe],check=True)
            subprocess.run([exe],check=True)

if __name__=='__main__': unittest.main()
