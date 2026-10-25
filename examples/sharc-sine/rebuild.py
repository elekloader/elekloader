# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild this prelinked sine from pinned Selache source and checked H1 inputs."""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
from keyed import build  # noqa: E402


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('selache', type=Path)
    parser.add_argument('--source-dir', type=Path, default=HERE)
    parser.add_argument('--out', type=Path, default=HERE / 'sine.sharc.json')
    parser.add_argument('--core-out', type=Path,
                        default=HERE.parent.parent / 'mods/core-dn2/bridge.sharc.json')
    args = parser.parse_args()
    try:
        build(args.selache, args.source_dir, args.out, args.core_out)
    except (ValueError, OSError) as error:
        sys.exit('FAIL: %s' % error)
