# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild dispatch.sharc.json, the dispatcher's kernel artifact that machines-dn2's mod.json names, from
dispatch.s with the pinned Selache ($SELACHE), so that building the mod needs no Selache. With --check,
compare instead of writing: exit 1 when the committed file is stale."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]))
from elekloader.sdk.sharc import kernel_artifact    # noqa: E402

ARTIFACT = HERE / 'dispatch.sharc.json'


def built():
    with tempfile.TemporaryDirectory() as tmp:
        return json.dumps(kernel_artifact([HERE / 'dispatch.s'], tmp), indent=1) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    text = built()
    if parser.parse_args().check:
        if ARTIFACT.read_text() != text:
            print('%s is stale: run %s' % (ARTIFACT.name, Path(__file__).name))
            return 1
        print('%s matches dispatch.s' % ARTIFACT.name)
        return 0
    ARTIFACT.write_text(text)
    print('wrote %s' % ARTIFACT.name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
