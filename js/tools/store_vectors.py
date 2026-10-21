# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes js/test/store-vectors.json: what elekloader's store.py (mod storage: the lockfile and the
allocator) gives for a run of builds, so test/store.test.ts can check store.ts says the same. Run from
the repository root:

    python js/tools/store_vectors.py
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from elekloader import store                                   # noqa: E402
from elekloader.elemod import ModError                         # noqa: E402

DEV = 'analog-rytm-mk2'


def F(mod, sym, scope, bits):
    return [mod, sym, scope, bits]


EVERY = [F('a', 'a_snd', 'sound', 8), F('a', 'a_kit', 'kit', 12), F('a', 'a_glo', 'global', 5),
         F('a', 'a_trk', 'track', 3), F('a', 'a_pat', 'pattern', 9), F('a', 'a_prj', 'project', 32),
         F('b', 'b_trk', 'track', 4), F('b', 'b_snd', 'sound', 16)]
RUNS = {
    'grow, drop, resize, keep': [EVERY, EVERY + [F('c', 'c_kit', 'kit', 32)],
                                 [d for d in EVERY if d[0] != 'b'] + [F('c', 'c_kit', 'kit', 32)],
                                 [d for d in EVERY if d[0] != 'b'] + [F('c', 'c_kit', 'kit', 30)],
                                 [d for d in EVERY if d[0] != 'b'] + [F('c', 'c_kit', 'kit', 30)]],
    'a scope runs out': [[F('a', 'k1', 'kit', 32), F('a', 'k2', 'kit', 30)], [F('a', 'k1', 'kit', 32), F('b', 'k3', 'kit', 32)]],
    'the split grows': [[F('a', 't1', 'track', 16), F('a', 'p1', 'pattern', 8), F('a', 'j1', 'project', 8)],
                        [F('a', 't1', 'track', 16), F('a', 't2', 'track', 9), F('a', 'p1', 'pattern', 8),
                         F('a', 'j1', 'project', 8)],
                        [F('a', 't1', 'track', 16), F('a', 't2', 'track', 9), F('a', 'p1', 'pattern', 8),
                         F('a', 'p2', 'pattern', 32), F('a', 'p3', 'pattern', 32), F('a', 'j1', 'project', 8)]],
    'too big': [[F('a', 't%d' % i, 'track', 32) for i in range(8)]],
    'tracks need too much': [[F('a', 't%d' % i, 'track', 32) for i in range(17)]],
    'one field, two mods': [[F('a', 'x', 'kit', 1), F('b', 'x', 'kit', 1)]],
    'sound full': [[F('a', 's', 'sound', 48), F('b', 's2', 'sound', 9)]],
    'sound past 24 bits': [[F('a', 's', 'sound', 24)], [F('a', 's', 'sound', 24), F('b', 's2', 'sound', 32)]],
    'ancestors': [[F('a', 'f%d' % j, 'global', 1) for j in range(i + 1)] for i in range(18)],
    'nothing': [[], []],
    'off and on': [[F('a', 'x', 'track', 4), F('b', 'y', 'track', 4)], [F('b', 'y', 'track', 4)],
                   [F('a', 'x', 'track', 4), F('b', 'y', 'track', 4)], [F('b', 'y', 'track', 4)],
                   [F('a', 'x', 'track', 4), F('b', 'y', 'track', 4)]],
    'resized and back': [[F('a', 'x', 'kit', 8)], [F('a', 'x', 'kit', 9)], [F('a', 'x', 'kit', 8)]],
    'off while its scope repacks': [[F('a', 'x', 'kit', 32), F('b', 'y', 'kit', 30)],
                                    [F('b', 'y', 'kit', 30), F('c', 'z', 'kit', 4)]],
    'patterns grow, tracks stay': [[F('a', 't', 'track', 4), F('a', 'p', 'pattern', 32)],
                                   [F('a', 't', 'track', 4), F('a', 'p', 'pattern', 32),
                                    F('a', 'q', 'pattern', 8)]],
}
# runs with the build's core's version given (store.allocate's core): each scope's room is that core's
CORE_RUNS = {
    'core 0.2: sound full': ('0.2', [[F('a', 's', 'sound', 24), F('b', 's2', 'sound', 1)]]),
    'core 0.3: sound past 24 bits': ('0.3', [[F('a', 's', 'sound', 24)],
                                             [F('a', 's', 'sound', 24), F('b', 's2', 'sound', 32)]]),
}
# a lockfile a core 0.3 build made, with a sound field past bit 24, then built with core 0.2
LATE = {'core 0.3 then 0.2': ([F('a', 's', 'sound', 24), F('b', 's2', 'sound', 8)],
                              '0.2', [F('b', 's2', 'sound', 8)]),
        'core 0.3 then 0.2, too big': ([F('a', 's', 'sound', 24), F('b', 's2', 'sound', 8)],
                                       '0.2', [F('a', 's', 'sound', 24), F('b', 's2', 'sound', 8)]),
        'core 0.3 then 0.2, fits': ([F('a', 's', 'sound', 8), F('b', 's2', 'sound', 8)],
                                    '0.2', [F('a', 's', 'sound', 8), F('b', 's2', 'sound', 8)])}
# the same, from a lockfile several core 0.3 builds made: a field freed, and a new one past bit 24, then the
# same mods built with core 0.2, so the repack alone changes the layout (and its ID)
LATE_STEPS = {'core 0.3 then 0.2, a repack alone': ([[F('a', 's', 'sound', 24)], [F('b', 's2', 'sound', 8)]],
                                                    '0.2', [F('b', 's2', 'sound', 8)])}
LOCKS = {
    'not a dict': [1], 'not ours': {'x': 1}, 'newer': {'elekloader_store': 2},
    'device': dict(store.fresh(DEV), device='digitakt-mk1'),
    'id 0': dict(store.fresh(DEV), id='0x0'), 'id text': dict(store.fresh(DEV), id='abc'),
    'split': dict(store.fresh(DEV), split={'track': 2}),
    'split too big': dict(store.fresh(DEV), split={'track': 3, 'pattern': 4}),
    'generation 0': dict(store.fresh(DEV), generation=dict(store.fresh(DEV)['generation'], kit=0)),
    'next past': dict(store.fresh(DEV), next=dict(store.fresh(DEV)['next'], sound=57)),
    'bad field': dict(store.fresh(DEV), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0}]),
    'field past': dict(store.fresh(DEV), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0,
                                                  'bits': 4}]),
    # an older lockfile, with an ID not from a hash: kept while nothing changes
    'random id': dict(store.fresh(DEV), id='0x5a17c0de', ancestors=['0x00000007']),
}
DECLS = {
    'ok': {'storage': [{'sym': 'a_b1', 'scope': 'kit', 'bits': 3}]},
    'not a list': {'storage': {}}, 'not a field': {'storage': [1]},
    'extra key': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 1, 'x': 2}]},
    'sym': {'storage': [{'sym': '1a', 'scope': 'kit', 'bits': 1}]},
    'sym type': {'storage': [{'sym': 5, 'scope': 'kit', 'bits': 1}]},
    'scope': {'storage': [{'sym': 'a', 'scope': 'song', 'bits': 1}]},
    'bits': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 33}]},
    'bits bool': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': True}]},
    'twice': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 1}, {'sym': 'a', 'scope': 'kit', 'bits': 2}]},
}


def main():
    out = {'runs': {}, 'locks': {}, 'decls': {}}
    def run(lock, steps, core):
        res = []
        for decls in steps:
            try:
                lock, notes = store.allocate(lock, [tuple(d) for d in decls], DEV, core)
                syms, tab = store.symbols(lock, DEV)
                res.append({'lock': lock, 'notes': notes, 'symbols': syms, 'ids': [t.hex() for t in tab],
                            'json': store.dumps(lock), 'layout_id': store.layout_id(lock)})
            except ModError as e:
                res.append({'error': str(e)})
        return res
    for name, steps in RUNS.items():
        out['runs'][name] = {'steps': steps, 'core': None, 'results': run(None, steps, None)}
    for name, (core, steps) in CORE_RUNS.items():
        out['runs'][name] = {'steps': steps, 'core': core, 'results': run(None, steps, core)}
    for name, (first, core, then) in LATE.items():
        lock, _ = store.allocate(None, [tuple(d) for d in first], DEV, '0.3')
        out['runs'][name] = {'start': lock, 'steps': [then], 'core': core, 'results': run(lock, [then], core)}
    for name, (firsts, core, then) in LATE_STEPS.items():
        lock = None
        for first in firsts:
            lock, _ = store.allocate(lock, [tuple(d) for d in first], DEV, '0.3')
        out['runs'][name] = {'start': lock, 'steps': [then], 'core': core, 'results': run(lock, [then], core)}
    for name, lock in LOCKS.items():
        try:
            same, _ = store.allocate(lock, [], DEV)
            out['locks'][name] = {'lock': lock, 'error': None, 'result': same}
        except ModError as e:
            out['locks'][name] = {'lock': lock, 'error': str(e)}
    for name, doc in DECLS.items():
        try:
            out['decls'][name] = {'doc': doc, 'ok': store.parse_decls(doc)}
        except ModError as e:
            out['decls'][name] = {'doc': doc, 'error': str(e)}
    path = os.path.join(ROOT, 'js', 'test', 'store-vectors.json')
    with open(path, 'w') as fh:
        json.dump(out, fh, indent=1)
        fh.write('\n')
    print('wrote', path)


if __name__ == '__main__':
    main()
