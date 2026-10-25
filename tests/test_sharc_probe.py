# SPDX-License-Identifier: GPL-2.0-or-later
"""Probe checker tests: no firmware, compiler or external runner required."""
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'examples/sharc-sine-probe'))
from execute import check_dag_store
from h1_spills import self_test


class ProbeChecks(unittest.TestCase):
    def test_h1_full_width_spills_and_mutations(self):
        result = self_test()
        self.assertEqual(result['roundtrips'], 1024)
        self.assertEqual(result['omitted_px1_failures'], 1024)
        self.assertEqual(result['normal32_low_byte_failures'], 1020)
        self.assertEqual(len(result['rejected_mutations']), 4)

    def test_same_dag_indexed_stores_rejected(self):
        for space, register in [('dm', 'i6'), ('dm', 'm7'), ('dm', 'b0'), ('dm', 'l4'),
                                ('pm', 'i15'), ('pm', 'm8'), ('pm', 'b12'), ('pm', 'l9')]:
            with self.subTest(space=space, register=register):
                index = 'i6' if space == 'dm' else 'i14'
                with self.assertRaisesRegex(ValueError, 'same-DAG'):
                    check_dag_store(f'{space} (-0x3,{index})={register}')

    def test_direct_and_opposite_dag_stores_are_exempt(self):
        for mnemonic in ('dm (0x2c0400)=i6', 'pm (0x300000)=i12',
                         'dm (i7,m7)=i12', 'pm (i14,m14)=i6', 'dm (i7,m7)=r2'):
            check_dag_store(mnemonic)


if __name__ == '__main__':
    unittest.main()
