# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes js/test/drivestore-vectors.json and js/test/drivestore-a4-vectors.json: what elekloader's
+Drive storage allocator (elekloader/drive_store.py) does through runs of sets, on made-up Analog Rytm
MKII builds and Analog Four MKII builds (lock codes per track kind), so test/drivestore.test.ts can check
drivestore.ts places the same fields, writes the same /mods/.store bytes and says the same notes. Each
device's Drive Loader host test reads its file. Run from the repository root:

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


A4 = 'analog-four-mk2'
A4_ROOM = {'sound': 32, 'kit': 128, 'global': 64, 'track': 16, 'pattern': 32, 'project': 18224}


def base_json(used=None, gen=None, codes=(), ccs=(), inits=0, spare=32, fid=T.FID, live=False, dev=T.DEV):
    used, gen = used or {}, gen or {}
    room = A4_ROOM if dev == A4 else T.ROOM
    out = {'device': dev, 'scopes': {s: [room[s], used.get(s, 0), gen.get(s, 1)] for s in store.SCOPES},
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


def a4(**kw):
    return base_json(dev=A4, **kw)


SYN, FXT, CVT = 1 << 28, 2 << 28, 2 << 28                    # a flashed entry's scope: synth, FX and CV
A4M = {'dist': T.mod('dist', T.f('dist_dig', 'sound', 1, lock=True), T.f('dist_chr', 'sound', 2, lock=True),
                     *[T.f('dist_' + x, 'sound', 7, lock=True, mod=True, cc=True) for x in ('drv', 'bit', 'srr', 'lev')],
                     T.f('dist_sld_f', 'sound', 1)),
       'wave': T.mod('wave', T.f('wave_mode', 'sound', 2, lock=True),
                     T.f('wave_chord', 'sound', 4, lock=True, mod=True, cc=True, smooth=False)),
       'reslim': T.mod('res-limit', T.f('res_lim', 'kit', 4)),
       'kfx': T.mod('kfx', T.f('kf_a', 'kit', 8, lock=True, mod=True, cc=True), T.f('kf_b', 'kit', 3, lock=True)),
       'kcv': T.mod('kcv', T.f('kc_a', 'kit', 8, lock=True, cc=True, track='cv', init=3)),
       'kcv2': T.mod('kcv', T.f('kc_a', 'kit', 8, lock=True, cc=True, init=3)),     # the same field, now the FX track's
       'syn1': T.mod('syn1', T.f('y1', 'sound', 1, lock=True)),
       'syn2': T.mod('syn2', T.f('y2', 'sound', 1, lock=True), T.f('y3', 'sound', 1, lock=True)),
       'hand': T.mod('hand', T.f('hd', 'project', 16, handle=True), T.f('hg', 'global', 3, init=5))}
A4_FLASHED = [(SYN, 0x70), (SYN | 1, 0x71), (FXT, 0x70), (CVT | 2, 0x70 | store.CV_FLAG)]
A4_SYN_FULL = [(SYN | i, c) for i, c in enumerate(range(0x70, 0x7f))]         # 15 synth codes: one left

A4_RUNS = [
    ('a4 fresh, nothing', [(a4(), ['place', []])]),
    ('a4 the AE mods', [(a4(used={'kit': 40}), ['place', ['wave', 'reslim']]),
                        (a4(used={'kit': 40}), ['fit', ['dist', 'wave', 'reslim']]),
                        (a4(used={'kit': 40}), ['place', ['dist', 'reslim']]),
                        (a4(used={'kit': 40}), ['place', ['wave', 'reslim']])]),
    ('a4 kinds', [(a4(codes=A4_FLASHED), ['place', ['wave', 'kfx', 'kcv']]),
                  (a4(codes=A4_FLASHED), ['place', ['wave', 'kfx', 'kcv2']]),
                  (a4(codes=A4_FLASHED), ['place', ['wave', 'kcv']]),
                  (a4(codes=A4_FLASHED), ['place', ['wave', 'kfx', 'kcv']])]),
    ('a4 flashed takes a code', [(a4(), ['place', ['wave', 'kfx']]),
                                 (a4(codes=[(SYN, 0x7f), (FXT, 0x7f)]), ['place', ['wave', 'kfx']])]),
    ('a4 codes run out', [(a4(codes=A4_SYN_FULL), ['place', ['syn2']]), (a4(codes=A4_SYN_FULL), ['place', ['syn1']]),
                          (a4(codes=A4_SYN_FULL), ['fit', ['syn1', 'syn2', 'kfx']]),
                          (a4(codes=A4_SYN_FULL), ['place', ['syn1', 'kfx']])]),
    ('a4 park, forget, return', [(a4(), ['place', ['dist', 'kcv']]), (a4(), ['place', ['kcv']]),
                                 (a4(), ['forget', 'dist']), (a4(), ['place', ['kcv', 'dist']])]),
    ('a4 ccs', [(a4(ccs=[(0, 101)]), ['place', ['wave', 'kfx', 'kcv']]),
                (a4(spare=0), ['fit', ['hand', 'wave']])]),
    ('a4 handles and inits', [(a4(inits=2), ['place', ['hand', 'kcv']])]),
]


def run(steps, mods=M):
    out, st = [], None
    for bj, (op, arg) in steps:
        b = make_base(bj)
        if op == 'fit':
            out.append({'fit': [list(r) for r in ds.fit(b, ds.Want([mods[k] for k in arg]))]})
        elif op == 'forget':
            st, changed = ds.forget(st, arg)
            out.append({'store': ds.encode(st).hex(), 'changed': changed})
        else:
            try:
                p = ds.allocate(b, st, ds.Want([mods[k] for k in arg]))
            except ModError as e:
                out.append({'error': str(e)})
                continue
            st = ds.decode(p.bytes)
            assert st is not None and ds.encode(st) == p.bytes
            out.append(p.summary())
    return out


def main():
    for name, mods, runs in (('drivestore-vectors.json', M, RUNS), ('drivestore-a4-vectors.json', A4M, A4_RUNS)):
        data = {'mods': mods, 'runs': [{'name': n, 'steps': [[bj, s] for bj, s in steps],
                                        'results': run(steps, mods)} for n, steps in runs]}
        path = os.path.join(ROOT, 'js', 'test', name)
        with open(path, 'w', newline='\n') as fh:
            json.dump(data, fh, indent=1, sort_keys=True)
            fh.write('\n')
        print('wrote %s: %d runs' % (path, len(runs)))


if __name__ == '__main__':
    main()
