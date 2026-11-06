# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes js/test/desc-vectors.json: what elekloader's linker gives for builds with and without the
build descriptor (core_desc, elekloader/desc.py), on a made-up release of the Analog Rytm MKII (an
image of nops, as tests/test_link_refusals.py's Bench makes it), so test/desc.test.ts can check
link.ts writes the same bytes and refuses the same things. Run from the repository root:

    python js/tools/desc_vectors.py
"""
import copy
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))

from elekloader import desc, link                               # noqa: E402
from elekloader.elemod import ModError                          # noqa: E402
import test_link_refusals as T                                  # noqa: E402


def cases(B):
    out = {'with the descriptor': T.descriptor_mods(B),
           'without it': T.descriptor_mods(B, False)}
    d = T.descriptor_mods(B)
    d[0]['collections']['ev_x']['spare'] = 0
    d[2]['contribute'][1]['order'] = 0
    out['no spare in ev_x, an order of 0'] = d
    d = T.descriptor_mods(B)
    d[2]['contribute'][0]['order'] = 65535
    out['an order past the keys\''] = d
    d = T.descriptor_mods(B)
    d[0]['version'] = '0.11'
    d[1]['sections']['.bss']['size'] = 0x200000
    out['past the +Drive area'] = d
    d = T.descriptor_mods(B)
    d[0]['collections']['ev_x']['spare'] = -1
    out['a negative spare'] = d
    d = T.descriptor_mods(B)
    d[2]['symbols']['core_desc'] = ['.run', 0]
    d[2]['exports'] = ['core_desc']
    out['a mod exports core_desc'] = d
    return out


def main():
    rows = []
    with T.Bench('analog-rytm-mk2') as B:
        for name, docs in cases(B).items():
            row = {'name': name, 'docs': copy.deepcopy(docs)}
            try:
                out = link.link([link.Mod2(d, d['id']) for d in docs], B.img)
            except ModError as e:
                row['error'] = str(e)
            else:
                at = desc.find(out.image)
                row.update(image_sha256=hashlib.sha256(out.image).hexdigest(), layout=out.layout,
                           map={k: v for k, v in out.map.items()},
                           desc=out.image[at[0]:at[0] + desc.decode(out.image, at[0])['size']].hex() if at else None)
            rows.append(row)
        release = {'version': B.rel.version, 'syxSha256': B.rel.syx_sha256, 'mainSha256': B.rel.main_sha256,
                   'mainLen': B.rel.main_len}
    path = os.path.join(ROOT, 'js', 'test', 'desc-vectors.json')
    with open(path, 'w') as fh:
        json.dump({'release': release, 'cases': rows}, fh, indent=1, sort_keys=False)
        fh.write('\n')
    print('wrote %s: %d cases' % (path, len(rows)))


if __name__ == '__main__':
    main()
