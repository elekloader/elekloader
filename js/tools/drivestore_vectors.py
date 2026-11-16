# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes js/test/drivestore-vectors.json: what elekloader's +Drive storage allocator
(elekloader/drive_store.py) does through runs of sets, on made-up Analog Rytm MKII builds, so
test/drivestore.test.ts can check drivestore.ts places the same fields, writes the same /mods/.store
bytes and says the same notes. The Drive Loader's own host test can read the same file. Run from the
repository root:

    python js/tools/drivestore_vectors.py
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))

from elekloader import drive_store as ds, store            # noqa: E402
from elekloader.elemod import ModError                     # noqa: E402
import test_drive_store as T                               # noqa: E402


def base_json(used=None, gen=None, codes=(), ccs=(), inits=0, spare=32, fid=T.FID, live=False):
    used, gen = used or {}, gen or {}
    out = {'device': T.DEV, 'scopes': {s: [T.ROOM[s], used.get(s, 0), gen.get(s, 1)] for s in store.SCOPES},
           'flashed_id': fid, 'ancestors': [0x1234], 'codes': [list(c) for c in codes],
           'ccs': [list(c) for c in ccs], 'inits': inits, 'room': {t: spare for t in ds.TABLES}}
    if live:                                    # a core with its LFO destinations' fields live (core_lfo_dsts)
        out['live_dst'] = True
    return out


def make_base(j):
    return ds.Base(j['device'], j['scopes'], j['flashed_id'], j['ancestors'], j['codes'], j['ccs'], j['inits'],
                   j['room'], live_dst=j.get('live_dst', False))


M = {'duck': T.DUCK, 'lfo': T.LFO, 'fx': T.FX, 'vdac': T.VDAC, 'a': T.A4, 'b': T.B4,
     'small': T.mod('small', T.f('sm', 'kit', 8)),
     'one': T.mod('one', T.f('o1', 'sound', 1, lock=True)),
     'two': T.mod('two', T.f('t1', 'sound', 1, lock=True), T.f('t2', 'sound', 1, lock=True)),
     's': T.mod('s', T.f('s1', 'sound', 1, lock=True, cc=True)),
     'kitcc': T.mod('kitcc', T.f('kc', 'kit', 1, lock=True, cc=True)),
     'sidecar': T.mod('sidecar', T.f('sc_h', 'project', 16, handle=True)),
     'rough': T.mod('rough', T.f('rg', 'sound', 3, lock=True, smooth=False, mod=True, init=2)),
     'duck2': T.mod('duck', T.f('duck_src', 'kit', 5, lock=True, cc=True), T.f('duck_amt', 'kit', 28, lock=True, init=5)),
     'utf': T.mod('écho', T.f('ec', 'global', 3)),
     'klfo': T.KLFO,
     'zed': T.mod('zed', T.f('zz', 'global', 2))}
LFO_CODES = [(1 << 28 | 3 << 22, 42 | store.LFO_FLAG)]
MANY = [(1 << 28, c) for c in range(42, 126)]
TAKEN = [(0, cc) for cc in store.DEVICES[T.DEV]['ccs'] if cc not in (84, 85)]

# (name, [(base, step)]): a step is ['place', [mod keys]], ['forget', mod id] or ['fit', [mod keys]];
# each place goes on from the state before it
RUNS = [
    ('fresh, nothing', [(base_json(), ['place', []])]),
    ('fresh, two mods', [(base_json(used={'sound': 20, 'kit': 8}), ['place', ['duck', 'lfo']])]),
    ('park and return', [(base_json(), ['place', ['duck', 'lfo']]), (base_json(), ['place', ['lfo']]),
                         (base_json(), ['place', ['lfo', 'small']]), (base_json(), ['place', ['duck', 'lfo', 'small']])]),
    ('run out, repack', [(base_json(), ['place', ['duck']]), (base_json(), ['place', []]),
                         (base_json(), ['place', ['fx']]), (base_json(), ['place', ['fx', 'lfo', 'duck2']])]),
    ('forget', [(base_json(), ['place', ['duck', 'lfo']]), (base_json(), ['place', ['lfo']]),
                (base_json(), ['forget', 'duck']), (base_json(), ['place', ['lfo', 'duck']])]),
    ('flashed growth', [(base_json(), ['place', ['a', 'b']]), (base_json(), ['place', ['a']]),
                        (base_json(used={'sound': 50}), ['place', ['a']]),
                        (base_json(used={'sound': 50}, gen={'sound': 2}), ['place', ['a']])]),
    ('flashed repack', [(base_json(), ['place', ['duck', 'lfo']]), (base_json(gen={'kit': 2}), ['place', ['duck', 'lfo']]),
                        (base_json(gen={'kit': 2, 'global': 3}, fid=0x0badcafe), ['place', ['duck']])]),
    ('codes', [(base_json(codes=[(1 << 28, 42), (1 << 28, 120)]), ['place', ['duck', 'lfo', 'rough']]),
               (base_json(codes=[(1 << 28, 42), (1 << 28, 120), (1 << 28, 126)]), ['place', ['duck', 'lfo', 'rough']]),
               (base_json(codes=LFO_CODES), ['place', ['lfo']])]),
    ('codes run out', [(base_json(codes=MANY), ['place', ['two']]), (base_json(codes=MANY), ['place', ['one']]),
                       (base_json(codes=MANY), ['fit', ['one', 'two']])]),
    ('ccs', [(base_json(ccs=[(0, 119)]), ['place', ['duck', 's']]), (base_json(ccs=TAKEN), ['place', ['kitcc']]),
             (base_json(spare=0, ccs=[]), ['fit', ['vdac']])]),
    ('fit', [(base_json(), ['fit', ['duck', 'fx']]), (base_json(), ['fit', ['fx', 'lfo', 'vdac']]),
             (base_json(spare=0), ['fit', ['lfo']]), (base_json(), ['place', ['duck', 'fx']])]),
    ('LFO fields on demand', [(base_json(live=True), ['place', ['duck', 'lfo']]),
                              (base_json(live=True), ['place', ['duck']]),
                              (base_json(live=True), ['place', ['duck', 'lfo']]),
                              (base_json(live=True, codes=LFO_CODES), ['place', ['lfo']]),
                              (base_json(live=True), ['place', ['klfo', 'lfo']]),
                              (base_json(live=True), ['fit', ['lfo', 'vdac']]),
                              (base_json(), ['fit', ['lfo', 'vdac']])]),
    ('names and handles', [(base_json(), ['place', ['utf', 'zed', 'sidecar', 'vdac']]),
                           (base_json(), ['place', ['zed', 'sidecar']])]),
]


def run(steps):
    out, st = [], None
    for bj, (op, arg) in steps:
        b = make_base(bj)
        if op == 'fit':
            out.append({'fit': [list(r) for r in ds.fit(b, ds.Want([M[k] for k in arg]))]})
        elif op == 'forget':
            st, changed = ds.forget(st, arg)
            out.append({'store': ds.encode(st).hex(), 'changed': changed})
        else:
            try:
                p = ds.allocate(b, st, ds.Want([M[k] for k in arg]))
            except ModError as e:
                out.append({'error': str(e)})
                continue
            st = ds.decode(p.bytes)
            assert st is not None and ds.encode(st) == p.bytes
            out.append(p.summary())
    return out


def main():
    data = {'mods': M, 'runs': [{'name': n, 'steps': [[bj, s] for bj, s in steps], 'results': run(steps)}
                                for n, steps in RUNS]}
    path = os.path.join(ROOT, 'js', 'test', 'drivestore-vectors.json')
    with open(path, 'w') as fh:
        json.dump(data, fh, indent=1, sort_keys=True)
        fh.write('\n')
    print('wrote %s: %d runs' % (path, len(RUNS)))


if __name__ == '__main__':
    main()
