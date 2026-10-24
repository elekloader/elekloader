# SPDX-License-Identifier: GPL-2.0-or-later
"""Check indexed-store register restrictions for the assembly sine."""
import re

from qualify import require


def check_dag_store(mnemonic):
    # PRM rev 1.5 pp.13-13/16 and 15-7/10: indexed stores exclude
    # registers of their own DAG. Absolute Type 14 stores are exempt.
    store = re.fullmatch(r'(dm|pm)\s*\(([^)]+)\)\s*=\s*([imlb])(\d+)', mnemonic.lower())
    if store and re.search(r'\bi\d+\b', store[2]):
        source_dag = int(store[4]) // 8
        require(source_dag != (0 if store[1] == 'dm' else 1),
                f'illegal same-DAG indexed store: {mnemonic}')
