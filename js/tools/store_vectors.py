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
# runs on another device (each scope's room is that device's): the Analog Four MKII (core-a4)
DEV_RUNS = {
    'a4: every scope': ('analog-four-mk2', [EVERY + [F('c', 'c_kit', 'kit', 32), F('c', 'c_glo', 'global', 32)]]),
    'a4: room': ('analog-four-mk2', [[F('a', 's', 'sound', 32), F('a', 'k%d' % i, 'kit', 32)] for i in range(1)]
                 + [[F('a', 's', 'sound', 32)] + [F('a', 'k%d' % i, 'kit', 32) for i in range(4)]]
                 + [[F('a', 's', 'sound', 32)] + [F('a', 'k%d' % i, 'kit', 32) for i in range(5)]]),
    'a4: the split grows': ('analog-four-mk2', [[F('a', 't1', 'track', 16)],
                                                [F('a', 't1', 'track', 16), F('a', 't2', 'track', 9)],
                                                [F('a', 't1', 'track', 16), F('a', 't2', 'track', 9),
                                                 F('a', 't3', 'track', 32), F('a', 't4', 'track', 32)]]),
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
# runs with lockable fields (core-ar2 0.8): each step is (decls, [[mod, sym], ...] lockable)
SL = [F('a', 'x', 'sound', 7), F('a', 'y', 'sound', 8), F('b', 'z', 'sound', 3)]
LOCK_RUNS = {
    'codes: give, keep, free, back': [(SL, [['a', 'x'], ['b', 'z']]), (SL, [['a', 'x'], ['b', 'z']]),
                                      (SL[:1], [['a', 'x']]), (SL, [['a', 'x'], ['b', 'z']]),
                                      (SL, [['a', 'x'], ['a', 'y'], ['b', 'z']]), (SL, [['b', 'z']])],
    'codes: none keeps the ID': [([F('a', 'x', 'kit', 4)], []), ([F('a', 'x', 'kit', 4)], [])],
    'codes: resized': [(SL, [['a', 'x']]), ([F('a', 'x', 'sound', 9)], [['a', 'x']])],
    'codes: run out': [([F('m', 'f%02d' % i, 'sound', 1) for i in range(50)],
                        [['m', 'f%02d' % i] for i in range(50)])],
}
# runs with CCs (core-ar2 0.8): each step is (decls, lockable, [[mod, sym], ...] with a CC)
SC = [F('a', 'x', 'sound', 7), F('b', 'z', 'sound', 3)]
XZ = [['a', 'x'], ['b', 'z']]
MANY = [F('m', 'f%02d' % i, 'sound', 1) for i in range(35)]
CC_RUNS = {
    'ccs: give, keep, free, back': [(SC, XZ, XZ), (SC, XZ, XZ), (SC[:1], XZ[:1], XZ[:1]), (SC, XZ, XZ),
                                    (SC, XZ, [['b', 'z']])],
    'ccs: a resized field keeps its CC': [(SC, XZ, XZ), ([F('a', 'x', 'sound', 9), SC[1]], XZ, XZ)],
    'ccs: run out, then reuse a freed one': [(MANY[:34], [[f[0], f[1]] for f in MANY[:34]],
                                              [[f[0], f[1]] for f in MANY[:34]]),
                                             (MANY[1:35], [[f[0], f[1]] for f in MANY[1:35]],
                                              [[f[0], f[1]] for f in MANY[1:35]])],
}
# runs with inits (core-ar2 0.10): each step is (decls, lockable, [[mod, sym, init], ...])
SI = [F('a', 'frq', 'sound', 7), F('a', 'dep', 'sound', 7), F('a', 'p', 'sound', 3), F('b', 'k', 'kit', 7)]
FD = [['a', 'frq'], ['a', 'dep']]
INIT_RUNS = {
    'inits: give, keep, change, drop, back': [
        (SI, FD, [['a', 'frq', 127], ['a', 'dep', 127], ['b', 'k', 100]]),
        (SI, FD, [['a', 'frq', 127], ['a', 'dep', 127], ['b', 'k', 100]]),
        (SI, FD, [['a', 'frq', 127], ['a', 'dep', 64], ['b', 'k', 100]]),
        (SI, FD, [['a', 'frq', 127], ['a', 'dep', 64]]),
        (SI, FD, [['a', 'frq', 127], ['a', 'dep', 64], ['b', 'k', 100]]),
        (SI[:2], FD, [['a', 'frq', 127], ['a', 'dep', 64]]),
        (SI, FD, [['a', 'frq', 127], ['a', 'dep', 0], ['b', 'k', 100]])],
    'inits: none keeps format 1': [(SI, FD, []), (SI, FD, [['a', 'p', 0]])],
    'inits: too many': [([F('m', 'g%02d' % i, 'project', 1) for i in range(64)], [],
                         [['m', 'g%02d' % i, 1] for i in range(64)])],
}
LOCKS = {
    'not a dict': [1], 'not ours': {'x': 1}, 'newer': {'elekloader_store': 3},
    'init in format 1': dict(store.fresh(DEV), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0,
                                                        'bits': 4, 'init': 3}], next=dict(store.fresh(DEV)['next'], kit=4)),
    'init past its bits': dict(store.fresh(DEV), elekloader_store=2,
                               fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0, 'bits': 4, 'init': 16}],
                               next=dict(store.fresh(DEV)['next'], kit=4)),
    'init 0 written': dict(store.fresh(DEV), elekloader_store=2,
                           fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0, 'bits': 4, 'init': 0}],
                           next=dict(store.fresh(DEV)['next'], kit=4)),
    'format 2 with an init': dict(store.fresh(DEV), elekloader_store=2,
                                  fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0, 'bits': 4, 'init': 15}],
                                  next=dict(store.fresh(DEV)['next'], kit=4)),
    'device': dict(store.fresh(DEV), device='digitakt-mk1'),
    'id 0': dict(store.fresh(DEV), id='0x0'), 'id text': dict(store.fresh(DEV), id='abc'),
    'split': dict(store.fresh(DEV), split={'track': 2}),
    'split too big': dict(store.fresh(DEV), split={'track': 3, 'pattern': 4}),
    'generation 0': dict(store.fresh(DEV), generation=dict(store.fresh(DEV)['generation'], kit=0)),
    'next past': dict(store.fresh(DEV), next=dict(store.fresh(DEV)['next'], sound=57)),
    'bad field': dict(store.fresh(DEV), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0}]),
    'field past': dict(store.fresh(DEV), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0,
                                                  'bits': 4}]),
    'next_code': dict(store.fresh(DEV), codes=[], freed_codes=[], next_code=41),
    'code past next': dict(store.fresh(DEV), codes=[], freed_codes=[{'mod': 'a', 'sym': 'x', 'code': 50}],
                           next_code=43),
    'code for no field': dict(store.fresh(DEV), codes=[{'mod': 'a', 'sym': 'x', 'code': 42}], freed_codes=[],
                              next_code=43),
    'cc not in the pool': dict(store.fresh(DEV), ccs=[], freed_ccs=[{'mod': 'a', 'sym': 'x', 'cc': 64}]),
    'cc twice': dict(store.fresh(DEV), ccs=[], freed_ccs=[{'mod': 'a', 'sym': 'x', 'cc': 8},
                                                           {'mod': 'a', 'sym': 'y', 'cc': 8}]),
    'cc for no code': dict(store.fresh(DEV), ccs=[{'mod': 'a', 'sym': 'x', 'cc': 8}], freed_ccs=[]),
    'freed cc kept': dict(store.fresh(DEV), ccs=[], freed_ccs=[{'mod': 'a', 'sym': 'x', 'cc': 9}]),
    # a kit field (the FX track's) with a CC stock uses on the FX channel
    'kit cc taken': dict(store.fresh(DEV), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0, 'bits': 4}],
                         codes=[{'mod': 'a', 'sym': 'x', 'code': 42}], freed_codes=[], next_code=43,
                         ccs=[{'mod': 'a', 'sym': 'x', 'cc': 84}], freed_ccs=[],
                         next=dict(store.fresh(DEV)['next'], kit=4)),
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
    'lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True}]},
    'lock kit': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'lock': True, 'cc': True, 'lfo': True}]},
    'lock global': {'storage': [{'sym': 'a', 'scope': 'global', 'bits': 3, 'lock': True}]},
    'mod kit': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'lock': True, 'mod': True}]},
    'lock type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': 1}]},
    'cc': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'cc': True},
                       {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'cc': False}]},
    'cc without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'cc': True}]},
    'cc type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'cc': 'yes'}]},
    'lfo': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': True},
                        {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': False}]},
    'lfo without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lfo': True}]},
    'lfo type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': 1}]},
    'smooth': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'smooth': False},
                           {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'smooth': True},
                           {'sym': 'c', 'scope': 'sound', 'bits': 3, 'lock': True}]},
    'mod': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'mod': True},
                        {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'mod': False},
                        {'sym': 'c', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': True, 'mod': True}]},
    'mod without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'mod': True}]},
    'mod type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'mod': 1}]},
    'smooth without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'smooth': False}]},
    'smooth type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'smooth': 0}]},
    'init': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 7, 'lock': True, 'init': 127},
                         {'sym': 'b', 'scope': 'kit', 'bits': 3, 'init': 0}, {'sym': 'c', 'scope': 'kit', 'bits': 3}]},
    'init past its bits': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'init': 8}]},
    'init negative': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'init': -1}]},
    'init type': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'init': True}]},
    'init text': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'init': '1'}]},
    'twice': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 1}, {'sym': 'a', 'scope': 'kit', 'bits': 2}]},
    'handle': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 16, 'handle': True},
                           {'sym': 'b', 'scope': 'project', 'bits': 32, 'handle': False},
                           {'sym': 'c', 'scope': 'track', 'bits': 12, 'handle': True, 'init': 0}]},
    'handle type': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 16, 'handle': 1}]},
    'handle lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 16, 'lock': True, 'handle': True}]},
    'handle init': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 16, 'handle': True, 'init': 3}]},
}
# core-ar2 0.11: the same declarations with and without handles, and which fields core_handles lists
HANDLE_RUNS = {
    'handles': [[['a', 'a_k', 'kit', 16], ['a', 'a_p', 'project', 32], ['b', 'b_t', 'track', 12]],
                [['a', 'a_p'], ['b', 'b_t']]],
    'none': [[['a', 'a_k', 'kit', 16]], []],
}
# runs with codes per track kind (core-a4 0.5, the Analog Four MKII): each step is (decls, lockable, CV fields)
A4 = 'analog-four-mk2'
AK = [F('a', 's1', 'sound', 7), F('a', 's2', 'sound', 8), F('a', 'k1', 'kit', 7), F('b', 'k2', 'kit', 4),
      F('b', 'c1', 'kit', 6)]
AL = [['a', 's1'], ['a', 's2'], ['a', 'k1'], ['b', 'k2'], ['b', 'c1']]
KIND_RUNS = {
    'kinds: each from 0x70, keep, free, back': [(AK, AL, [['b', 'c1']]), (AK, AL, [['b', 'c1']]),
                                                (AK[:3] + AK[4:], AL[:3] + AL[4:], [['b', 'c1']]),
                                                (AK, AL, [['b', 'c1']])],
    'kinds: FX to CV is a new code': [(AK, AL, []), (AK, AL, [['b', 'k2'], ['b', 'c1']])],
    'kinds: 16 a kind, then a freed one, then refused': [
        ([F('m', 'f%02d' % i, 'kit', 1) for i in range(17)], [['m', 'f%02d' % i] for i in range(16)], []),
        ([F('m', 'f%02d' % i, 'kit', 1) for i in range(17)], [['m', 'f%02d' % i] for i in range(1, 17)], []),
        ([F('m', 'f%02d' % i, 'kit', 1) for i in range(17)], [['m', 'f%02d' % i] for i in range(17)], []),
        ([F('m', 'f%02d' % i, 'kit', 1) for i in range(17)] + [F('m', 's', 'sound', 1)],
         [['m', 'f%02d' % i] for i in range(1, 17)] + [['m', 's']], [['m', 'f16']])],
    # core-a4 0.5: a fourth list, the fields with a CC: the synth fields take the CCs the FX and CV tables map, a kit
    # field (FX or CV) only those every kind leaves free
    'kinds: CCs, synth first, kits past the FX and CV tables': [
        (AK, AL, [['b', 'c1']], [['a', 's1'], ['a', 'k1'], ['b', 'c1']]),
        (AK, AL, [['b', 'c1']], [['a', 's1'], ['a', 's2'], ['a', 'k1'], ['b', 'c1']]),
        (AK[1:], AL[1:], [['b', 'c1']], [['a', 's2'], ['a', 'k1'], ['b', 'c1']])],
}
KL = dict(store.fresh(A4), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0, 'bits': 4}],
          next=dict(store.fresh(A4)['next'], kit=4))
KIND_LOCKS = {
    'a4 next_code a number': dict(KL, codes=[], freed_codes=[], next_code=112),
    'a4 next_code a kind short': dict(KL, codes=[], freed_codes=[], next_code={'synth': 112, 'fx': 112}),
    'a4 next_code below': dict(KL, codes=[], freed_codes=[], next_code={'synth': 111, 'fx': 112, 'cv': 112}),
    'a4 code without kind': dict(KL, codes=[{'mod': 'a', 'sym': 'x', 'code': 112}], freed_codes=[],
                                 next_code={'synth': 112, 'fx': 113, 'cv': 112}),
    'a4 code past its kind': dict(KL, codes=[{'mod': 'a', 'sym': 'x', 'code': 112, 'kind': 'cv'}], freed_codes=[],
                                  next_code={'synth': 112, 'fx': 113, 'cv': 112}),
    'a4 kit field synth': dict(KL, codes=[{'mod': 'a', 'sym': 'x', 'code': 112, 'kind': 'synth'}], freed_codes=[],
                               next_code={'synth': 113, 'fx': 112, 'cv': 112}),
    'a4 same code two kinds': dict(KL, codes=[{'mod': 'a', 'sym': 'x', 'code': 112, 'kind': 'fx'}],
                                   freed_codes=[{'mod': 'a', 'sym': 'y', 'code': 112, 'kind': 'cv'}],
                                   next_code={'synth': 112, 'fx': 113, 'cv': 113}),
    'a4 same code one kind': dict(KL, codes=[{'mod': 'a', 'sym': 'x', 'code': 112, 'kind': 'fx'}],
                                  freed_codes=[{'mod': 'a', 'sym': 'y', 'code': 112, 'kind': 'fx'}],
                                  next_code={'synth': 112, 'fx': 113, 'cv': 112}),
    'a4 rytm code': dict(KL, codes=[{'mod': 'a', 'sym': 'x', 'code': 42, 'kind': 'fx'}], freed_codes=[],
                         next_code={'synth': 112, 'fx': 113, 'cv': 112}),
}
# the device checks of a build's lockable fields' flags: (device, {mod\0sym: scope}, lfos, routes, cvs, ccs)
FLAGS = {
    'rytm: ok': ('analog-rytm-mk2', {'m\0a': 'sound', 'm\0k': 'kit'}, [['m', 'a'], ['m', 'k']], [['m', 'a']], [],
                 [['m', 'a']]),
    'rytm: mod on a kit field': ('analog-rytm-mk2', {'m\0k': 'kit'}, [], [['m', 'k']], [], []),
    'rytm: a CV field': ('analog-rytm-mk2', {'m\0k': 'kit'}, [], [], [['m', 'k']], []),
    'a4: ok': (A4, {'m\0a': 'sound', 'm\0k': 'kit'}, [], [['m', 'a'], ['m', 'k']], [['m', 'k']], []),
    'a4: lfo': (A4, {'m\0a': 'sound'}, [['m', 'a']], [], [], []),
    'a4: cc': (A4, {'m\0a': 'sound'}, [], [], [], [['m', 'a']]),
}
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
    'next_code': dict(store.fresh(DEV), codes=[], freed_codes=[], next_code=41),
    'code past next': dict(store.fresh(DEV), codes=[], freed_codes=[{'mod': 'a', 'sym': 'x', 'code': 50}],
                           next_code=43),
    'code for no field': dict(store.fresh(DEV), codes=[{'mod': 'a', 'sym': 'x', 'code': 42}], freed_codes=[],
                              next_code=43),
    'cc not in the pool': dict(store.fresh(DEV), ccs=[], freed_ccs=[{'mod': 'a', 'sym': 'x', 'cc': 64}]),
    'cc twice': dict(store.fresh(DEV), ccs=[], freed_ccs=[{'mod': 'a', 'sym': 'x', 'cc': 8},
                                                           {'mod': 'a', 'sym': 'y', 'cc': 8}]),
    'cc for no code': dict(store.fresh(DEV), ccs=[{'mod': 'a', 'sym': 'x', 'cc': 8}], freed_ccs=[]),
    'freed cc kept': dict(store.fresh(DEV), ccs=[], freed_ccs=[{'mod': 'a', 'sym': 'x', 'cc': 9}]),
    # a kit field (the FX track's) with a CC stock uses on the FX channel
    'kit cc taken': dict(store.fresh(DEV), fields=[{'mod': 'a', 'sym': 'x', 'scope': 'kit', 'offset': 0, 'bits': 4}],
                         codes=[{'mod': 'a', 'sym': 'x', 'code': 42}], freed_codes=[], next_code=43,
                         ccs=[{'mod': 'a', 'sym': 'x', 'cc': 84}], freed_ccs=[],
                         next=dict(store.fresh(DEV)['next'], kit=4)),
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
    'lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True}]},
    'lock kit': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'lock': True, 'cc': True, 'lfo': True}]},
    'lock global': {'storage': [{'sym': 'a', 'scope': 'global', 'bits': 3, 'lock': True}]},
    'mod kit': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'lock': True, 'mod': True}]},
    'track cv': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'lock': True, 'mod': True, 'track': 'cv'},
                             {'sym': 'b', 'scope': 'kit', 'bits': 3, 'lock': True}]},
    'track fx': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'lock': True, 'track': 'fx'}]},
    'track sound': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'track': 'cv'}]},
    'track unlocked': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 3, 'track': 'cv'}]},
    'lock type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': 1}]},
    'cc': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'cc': True},
                       {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'cc': False}]},
    'cc without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'cc': True}]},
    'cc type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'cc': 'yes'}]},
    'lfo': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': True},
                        {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': False}]},
    'lfo without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lfo': True}]},
    'lfo type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': 1}]},
    'smooth': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'smooth': False},
                           {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'smooth': True},
                           {'sym': 'c', 'scope': 'sound', 'bits': 3, 'lock': True}]},
    'mod': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'mod': True},
                        {'sym': 'b', 'scope': 'sound', 'bits': 3, 'lock': True, 'mod': False},
                        {'sym': 'c', 'scope': 'sound', 'bits': 3, 'lock': True, 'lfo': True, 'mod': True}]},
    'mod without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'mod': True}]},
    'mod type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'mod': 1}]},
    'smooth without lock': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'smooth': False}]},
    'smooth type': {'storage': [{'sym': 'a', 'scope': 'sound', 'bits': 3, 'lock': True, 'smooth': 0}]},
    'twice': {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 1}, {'sym': 'a', 'scope': 'kit', 'bits': 2}]},
}


# knob ids (store.knobs): each run is steps of [want ({mod: count}), first, fixed, top], from {} or a fresh lockfile
KNOB_RUNS = {
    'from 555 past fixed ids, kept, freed and back': [
        [{'b': 4, 'a': 2}, 555, [[560, 573]], 0xFFFF], [{'b': 4, 'a': 2}, 555, [[560, 573]], 0xFFFF],
        [{'b': 4}, 555, [[560, 573]], 0xFFFF], [{'b': 4, 'c': 1}, 555, [[560, 573]], 0xFFFF],
        [{'a': 2, 'b': 6, 'c': 1}, 555, [[560, 573]], 0xFFFF], [{'a': 2, 'b': 5, 'c': 1}, 555, [[560, 573]], 0xFFFF],
        [{'a': 2, 'b': 5, 'c': 1}, 555, [[540, 556]], 0xFFFF]],
    'run out, then the freed runs again': [
        [{'a': 4, 'b': 4}, 10, [], 20], [{'b': 4}, 10, [], 20], [{'b': 4, 'c': 4}, 10, [], 20],
        [{'b': 4, 'c': 4, 'd': 30}, 10, [], 20]],
}
KNOB_LOCKS = {
    'next_knob below a run': {'next_knob': 556, 'knobs': [{'mod': 'a', 'first': 555, 'count': 2}], 'freed_knobs': []},
    'overlapping runs': {'next_knob': 560, 'knobs': [{'mod': 'a', 'first': 555, 'count': 2},
                                                    {'mod': 'b', 'first': 556, 'count': 2}], 'freed_knobs': []},
    'one mod twice': {'next_knob': 560, 'knobs': [{'mod': 'a', 'first': 555, 'count': 1},
                                                 {'mod': 'a', 'first': 557, 'count': 1}], 'freed_knobs': []},
    'freed not a list': {'next_knob': 560, 'knobs': [], 'freed_knobs': {}},
    'a count of 0': {'next_knob': 560, 'knobs': [{'mod': 'a', 'first': 555, 'count': 0}], 'freed_knobs': []},
    'good': {'next_knob': 560, 'knobs': [{'mod': 'a', 'first': 555, 'count': 2}],
             'freed_knobs': [{'mod': 'b', 'first': 557, 'count': 3}]},
}


def main():
    out = {'runs': {}, 'lock_runs': {}, 'cc_runs': {}, 'init_runs': {}, 'handle_runs': {}, 'kind_runs': {},
           'locks': {}, 'kind_locks': {}, 'flags': {}, 'decls': {}, 'knob_runs': {}, 'knob_locks': {}}
    def run(lock, steps, core, dev=DEV):
        res = []
        for decls in steps:
            try:
                lock, notes = store.allocate(lock, [tuple(d) for d in decls], dev, core)
                syms, tab = store.symbols(lock, dev)
                res.append({'lock': lock, 'notes': notes, 'symbols': syms, 'ids': [t.hex() for t in tab],
                            'json': store.dumps(lock), 'layout_id': store.layout_id(lock)})
            except ModError as e:
                res.append({'error': str(e)})
        return res
    for name, steps in RUNS.items():
        out['runs'][name] = {'steps': steps, 'core': None, 'results': run(None, steps, None)}
    for name, (core, steps) in CORE_RUNS.items():
        out['runs'][name] = {'steps': steps, 'core': core, 'results': run(None, steps, core)}
    for name, (dev, steps) in DEV_RUNS.items():
        out['runs'][name] = {'steps': steps, 'core': None, 'device': dev, 'results': run(None, steps, None, dev)}
    for name, (first, core, then) in LATE.items():
        lock, _ = store.allocate(None, [tuple(d) for d in first], DEV, '0.3')
        out['runs'][name] = {'start': lock, 'steps': [then], 'core': core, 'results': run(lock, [then], core)}
    for name, (firsts, core, then) in LATE_STEPS.items():
        lock = None
        for first in firsts:
            lock, _ = store.allocate(lock, [tuple(d) for d in first], DEV, '0.3')
        out['runs'][name] = {'start': lock, 'steps': [then], 'core': core, 'results': run(lock, [then], core)}
    for name, steps in LOCK_RUNS.items():
        lock, res = None, []
        for decls, locks in steps:
            try:
                lock, notes = store.allocate(lock, [tuple(d) for d in decls], DEV, None, [tuple(k) for k in locks])
                res.append({'lock': lock, 'notes': notes, 'codes': [t.hex() for t in store.code_entries(lock)],
                            'json': store.dumps(lock), 'layout_id': store.layout_id(lock)})
            except ModError as e:
                res.append({'error': str(e)})
        out['lock_runs'][name] = {'steps': [[d, k] for d, k in steps], 'results': res}
    for name, steps in CC_RUNS.items():
        lock, res = None, []
        for decls, locks, ccs in steps:
            try:
                lock, notes = store.allocate(lock, [tuple(d) for d in decls], DEV, None, [tuple(k) for k in locks],
                                             [tuple(k) for k in ccs])
                res.append({'lock': lock, 'notes': notes, 'codes': [t.hex() for t in store.code_entries(lock)],
                            'lfo_codes': [t.hex() for t in store.code_entries(lock, [tuple(k) for k in ccs])],
                            'rough_codes': [t.hex() for t in store.code_entries(lock, [tuple(k) for k in ccs][:1],
                                                                                [tuple(k) for k in ccs][1:])],
                            'route_codes': [t.hex() for t in store.code_entries(lock, (), [tuple(k) for k in ccs][1:],
                                                                                [tuple(k) for k in ccs][:2])],
                            'ccs': [t.hex() for t in store.cc_entries(lock)],
                            'json': store.dumps(lock), 'layout_id': store.layout_id(lock)})
            except ModError as e:
                res.append({'error': str(e)})
        out['cc_runs'][name] = {'steps': [list(st) for st in steps], 'results': res}
    for name, steps in INIT_RUNS.items():
        lock, res = None, []
        for decls, locks, inits in steps:
            try:
                lock, notes = store.allocate(lock, [tuple(d) for d in decls], DEV, None, [tuple(k) for k in locks], (),
                                             inits={(m, s): v for m, s, v in inits})
                syms, tab = store.symbols(lock, DEV)
                res.append({'lock': lock, 'notes': notes, 'symbols': syms, 'codes': [t.hex() for t in store.code_entries(lock)],
                            'inits': [t.hex() for t in store.init_entries(lock)],
                            'json': store.dumps(lock), 'layout_id': store.layout_id(lock)})
            except ModError as e:
                res.append({'error': str(e)})
        out['init_runs'][name] = {'steps': [list(st) for st in steps], 'results': res}
    for name, (decls, handles) in HANDLE_RUNS.items():
        lock, notes = store.allocate(None, [tuple(d) for d in decls], DEV)
        out['handle_runs'][name] = {'decls': decls, 'handles': handles, 'lock': lock,
                                    'handles_table': [t.hex() for t in store.handle_entries(lock, [tuple(h) for h in handles])],
                                    'layout_id': store.layout_id(lock)}
    for name, steps in KIND_RUNS.items():
        lock, res = None, []
        for st in steps:
            decls, locks, cvs = st[:3]
            ccs = st[3] if len(st) > 3 else []
            try:
                lock, notes = store.allocate(lock, [tuple(d) for d in decls], A4, None, [tuple(k) for k in locks],
                                             [tuple(k) for k in ccs], cvs=[tuple(k) for k in cvs])
                res.append({'lock': lock, 'notes': notes, 'codes': [t.hex() for t in store.code_entries(lock)],
                            'ccs': [t.hex() for t in store.cc_entries(lock)],
                            'route_codes': [t.hex() for t in store.code_entries(lock, (), (), [tuple(k) for k in locks])],
                            'json': store.dumps(lock), 'layout_id': store.layout_id(lock)})
            except ModError as e:
                res.append({'error': str(e)})
        out['kind_runs'][name] = {'steps': [list(st) for st in steps], 'results': res}
    for name, lock in KIND_LOCKS.items():
        try:
            same, _ = store.allocate(lock, [], A4)
            out['kind_locks'][name] = {'lock': lock, 'error': None, 'result': same}
        except ModError as e:
            out['kind_locks'][name] = {'lock': lock, 'error': str(e)}
    for name, (dev, scopes, lfos, routes, cvs, ccs) in FLAGS.items():
        try:
            store.check_flags(dev, {tuple(k.split('\0')): v for k, v in scopes.items()}, [tuple(k) for k in lfos],
                              [tuple(k) for k in routes], [tuple(k) for k in cvs], [tuple(k) for k in ccs])
            err = None
        except ModError as e:
            err = str(e)
        out['flags'][name] = {'device': dev, 'scopes': scopes, 'lfos': lfos, 'routes': routes, 'cvs': cvs,
                              'ccs': ccs, 'error': err}
    for name, lock in LOCKS.items():
        try:
            same, _ = store.allocate(lock, [], DEV)
            out['locks'][name] = {'lock': lock, 'error': None, 'result': same}
        except ModError as e:
            out['locks'][name] = {'lock': lock, 'error': str(e)}
    for name, doc in DECLS.items():
        try:
            out['decls'][name] = {'doc': doc, 'ok': store.parse_decls(doc), 'locks': store.parse_locks(doc),
                                  'ccs': store.parse_ccs(doc), 'lfos': store.parse_lfos(doc),
                                  'rough': store.parse_rough(doc), 'routes': store.parse_routes(doc),
                                  'cvs': store.parse_cvs(doc), 'inits': store.parse_inits(doc), 'handles': store.parse_handles(doc)}
        except ModError as e:
            out['decls'][name] = {'doc': doc, 'error': str(e)}
    for name, steps in KNOB_RUNS.items():
        for start in ('none', 'fresh'):
            lock, res = ({} if start == 'none' else store.fresh(DEV)), []
            for want, first, fixed, top in steps:
                try:
                    base, notes = store.knobs(lock, want, first, [tuple(f) for f in fixed], top)
                    res.append({'lock': json.loads(json.dumps(lock)), 'base': base, 'notes': notes,
                                'layout_id': store.layout_id(lock) if start == 'fresh' else None})
                except ModError as e:
                    res.append({'error': str(e)})
            out['knob_runs']['%s (%s)' % (name, start)] = {'start': start, 'steps': steps, 'results': res}
    for name, extra in KNOB_LOCKS.items():
        lock = dict(store.fresh(DEV), **extra)
        try:
            store.check_lock(lock, DEV)
            out['knob_locks'][name] = {'lock': lock, 'error': None}
        except ModError as e:
            out['knob_locks'][name] = {'lock': lock, 'error': str(e)}
    path = os.path.join(ROOT, 'js', 'test', 'store-vectors.json')
    with open(path, 'w') as fh:
        json.dump(out, fh, indent=1)
        fh.write('\n')
    print('wrote', path)


if __name__ == '__main__':
    main()
