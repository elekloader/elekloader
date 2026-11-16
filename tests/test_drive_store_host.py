# SPDX-License-Identifier: GPL-2.0-or-later
"""The Drive Loader's +Drive storage allocator (mods/drive-loader-ar2/dstore.c), built for the computer
and run through every step of js/test/drivestore-vectors.json: the same placements, table entries,
layout IDs, room refusals and /mods/.store bytes as elekloader/drive_store.py.

    python tests/test_drive_store_host.py

Needs a C compiler (cc). The notes drive_store.py writes for a person are not compared: the loader
does not make them."""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'mods', 'drive-loader-ar2')
VECTORS = os.path.join(ROOT, 'js', 'test', 'drivestore-vectors.json')
SCOPES = ('sound', 'kit', 'global', 'track', 'pattern', 'project')
TABLES = ('core_lock_codes', 'core_lock_ccs', 'core_field_inits', 'core_handles')
FLAGS = (('lock', 1), ('cc', 2), ('lfo', 4), ('mod', 16), ('handle', 32))


def build(work):
    exe = os.path.join(work, 'drive_store_host')
    subprocess.run(['cc', '-O1', '-Wall', '-DDL_HOST', '-o', exe, os.path.join(HERE, 'drive_store_host.c'),
                    os.path.join(SRC, 'dstore.c'), os.path.join(SRC, 'sha256.c'), os.path.join(SRC, 'dlink.c'),
                    os.path.join(SRC, 'json.c')], check=True)
    return exe


def want_words(mods):
    """The chosen mods' fields as the loader gives them: by mod id (bytes), each mod's in its order."""
    out = []
    for m in sorted(mods, key=lambda m: m['id'].encode('utf-8')):
        for f in m['storage']:
            flags = sum(v for k, v in FLAGS if f.get(k) is True) | (8 if f.get('smooth') is False else 0)
            out.append('%s %s %d %d %d %d' % (m['id'].encode('utf-8').hex(), f['sym'].encode('utf-8').hex(),
                                             SCOPES.index(f['scope']) + 1, f['bits'], f.get('init', 0), flags))
    return out


def step_input(bj, mods, prev, op, arg):
    words = []
    for s in SCOPES:
        words += [str(x) for x in bj['scopes'][s]]
    words += [str(bj['flashed_id']), str(len(bj['ancestors']))] + [str(a) for a in bj['ancestors']]
    words += [str(len(bj['codes']))] + [str(x) for c in bj['codes'] for x in c]
    words += [str(len(bj['ccs']))] + [str(x) for c in bj['ccs'] for x in c]
    words += [str(bj['inits'])] + [str(-1 if bj['room'].get(t) is None else bj['room'][t]) for t in TABLES]
    words += [str(int(bj.get('live_dst', False)))]
    w = want_words(mods)
    words += [str(len(w))] + w
    words += [prev.hex() if prev else '-', op]
    if op == 'forget':
        words.append(arg.encode('utf-8').hex())
    return ' '.join(words) + '\n'


def random_runs(exe, runs, steps, seed=5):
    import random
    sys.path.insert(0, ROOT)
    from elekloader import drive_store as ds
    from elekloader.elemod import ModError
    rnd, fails = random.Random(seed), []
    room = {'sound': 56, 'kit': 64, 'global': 40, 'track': 16, 'pattern': 32, 'project': 3776}
    names = ['a', 'b', 'duck', 'fx-sends', 'lfo-rnd', 'voice-dac', '\u00e9cho', 'zed', 'z2']

    def mod(i, ver):
        r = random.Random(i * 1000 + ver)
        fields = []
        for k in range(r.randint(1, 3)):
            scope = r.choice(['sound', 'sound', 'kit', 'global', 'project', 'track'])
            bits = r.choice([1, 2, 3, 4, 7, 8, 16, 28])
            f = {'sym': 's%s_%d' % (names[i].encode('utf-8').hex(), k), 'scope': scope, 'bits': bits}
            if scope in ('sound', 'kit') and r.random() < 0.6:
                f['lock'] = True
                if r.random() < 0.5:
                    f['cc'] = True
                if r.random() < 0.3:
                    f['lfo'] = True
                if r.random() < 0.2:
                    f['smooth'] = False
                if scope == 'sound' and r.random() < 0.2:
                    f['mod'] = True
            elif r.random() < 0.2:
                f['handle'] = True
            if not f.get('handle') and r.random() < 0.25:
                f['init'] = r.randrange(1, 1 << bits)
            fields.append(f)
        return {'id': names[i], 'storage': fields}
    for run in range(runs):
        st, prev = None, None
        used = {s: 0 for s in room}
        gen = {s: 1 for s in room}
        codes = [(1 << 28, c) for c in sorted(rnd.sample(range(42, 128), rnd.choice([0, 2, 20, 80])))]
        if codes and rnd.random() < 0.5:
            codes[0] = (1 << 28 | 3 << 22, codes[0][1] | 0x100)
        ccs = [(0, c) for c in rnd.sample([8, 9, 48, 84, 85, 119, 116], rnd.choice([0, 1, 3]))]
        vers = [0] * len(names)
        live = run % 2 == 1                     # odd runs: a core with its LFO fields live (core_lfo_dsts)
        for k in range(steps):
            if rnd.random() < 0.15:
                s = rnd.choice(list(room))
                used[s] = min(room[s], used[s] + rnd.choice([4, 16]))
            if rnd.random() < 0.08:
                gen[rnd.choice(list(room))] += 1
            if rnd.random() < 0.1:
                vers[rnd.randrange(len(names))] += 1
            spare = rnd.choice([32, 32, 32, 2])
            bj = {'device': 'analog-rytm-mk2', 'scopes': {s: [room[s], used[s], gen[s]] for s in room},
                  'flashed_id': 0x5a17c0de + run, 'ancestors': [0x1234], 'codes': [list(c) for c in codes],
                  'ccs': [list(c) for c in ccs], 'inits': rnd.choice([0, 3]), 'room': {t: spare for t in TABLES},
                  'live_dst': live}
            mods = [mod(i, vers[i]) for i in sorted(rnd.sample(range(len(names)), rnd.randint(0, 5)))]
            b = ds.Base(bj['device'], bj['scopes'], bj['flashed_id'], bj['ancestors'], bj['codes'], bj['ccs'],
                        bj['inits'], bj['room'], live_dst=live)
            name = 'random run %d step %d' % (run + 1, k + 1)
            if rnd.random() < 0.1 and st is not None and st['parked']:
                who = rnd.choice(st['parked'])['mod']
                st, changed = ds.forget(st, who)
                exp = {'changed': changed, 'store': ds.encode(st).hex()}
                r = subprocess.run([exe], input=step_input(bj, [], prev, 'forget', who), capture_output=True, text=True)
                prev = ds.encode(st)
                if r.returncode or json.loads(r.stdout) != exp:
                    fails.append(name + ' (forget)')
                continue
            w = ds.Want(mods)
            short = ds.fit(b, w)
            r = subprocess.run([exe], input=step_input(bj, mods, prev, 'fit', []), capture_output=True, text=True)
            if r.returncode or json.loads(r.stdout)['fit'] != [list(x) for x in short]:
                fails.append(name + ' (fit)')
                continue
            refused = {x[0] for x in short}
            mods = [m for m in mods if m['id'] not in refused]
            try:
                p = ds.allocate(b, st, ds.Want(mods))
            except ModError:
                continue
            r = subprocess.run([exe], input=step_input(bj, mods, prev, 'place', []), capture_output=True, text=True)
            got = json.loads(r.stdout) if not r.returncode else {}
            sm = p.summary()
            exp = {'id': sm['id'], 'ancestors': sm['ancestors'], 'gens': [sm['generations'][s] for s in SCOPES],
                   'fields': sm['fields'], 'parked': sm['parked'], 'codes': sm['codes'], 'ccs': sm['ccs'],
                   'inits': sm['inits'], 'handles': sm['handles'], 'lfo_dsts': sm['lfo_dsts'], 'store': sm['store']}
            got.pop('nparked', None)
            if got != exp:
                fails.append(name)
                for key in exp:
                    if got.get(key) != exp[key]:
                        print('  %s %s: got %s want %s' % (name, key, got.get(key), exp[key]))
            st, prev = ds.decode(p.bytes), p.bytes
    print('%s %d random runs of %d steps' % ('ok  ' if not fails else 'FAIL', runs, steps))
    return fails


def main():
    work = tempfile.mkdtemp()
    exe = build(work)
    data = json.load(open(VECTORS))
    M = data['mods']
    fails, n = [], 0
    for run in data['runs']:
        prev = None
        for i, ((bj, (op, arg)), want) in enumerate(zip(run['steps'], run['results'])):
            mods = [M[k] for k in arg] if op in ('place', 'fit') else []
            r = subprocess.run([exe], input=step_input(bj, mods, prev, op, arg), capture_output=True, text=True)
            name = '%s, step %d (%s)' % (run['name'], i + 1, op)
            n += 1
            if r.returncode:
                fails.append('%s: exit %d %s' % (name, r.returncode, r.stderr))
                continue
            got = json.loads(r.stdout)
            if op == 'fit':
                ok = got['fit'] == want['fit']
            elif op == 'forget':
                ok = got == {'changed': want['changed'], 'store': want['store']}
                prev = bytes.fromhex(want['store'])
            elif 'error' in want:
                ok = 'error' in got
            else:
                exp = {'id': want['id'], 'ancestors': want['ancestors'],
                       'gens': [want['generations'][s] for s in SCOPES], 'fields': want['fields'],
                       'parked': want['parked'], 'codes': want['codes'], 'ccs': want['ccs'],
                       'inits': want['inits'], 'handles': want['handles'], 'lfo_dsts': want.get('lfo_dsts', {}),
                       'store': want['store']}
                got.pop('nparked', None)
                ok = got == exp
                if not ok:
                    for k in exp:
                        if got.get(k) != exp[k]:
                            print('  %s: got %s\n  %s  want %s' % (k, got.get(k), ' ' * len(k), exp[k]))
                prev = bytes.fromhex(want['store'])
            print('%s %s' % ('ok  ' if ok else 'FAIL', name))
            if not ok:
                fails.append(name)
    # random runs against drive_store.py itself: sets coming and going on builds that change under them
    fails += random_runs(exe, 40, 8)
    n += 40 * 8
    # a .store the loader does not take reads as a fresh placement
    bad = step_input(data['runs'][0]['steps'][0][0], [], b'DLST' + bytes(60), 'place', [])
    r = subprocess.run([exe], input=bad, capture_output=True, text=True)
    ok = not r.returncode and json.loads(r.stdout)['store'] == data['runs'][0]['results'][0]['store']
    print('%s a bad .store reads as none' % ('ok  ' if ok else 'FAIL'))
    if not ok:
        fails.append('bad store')
    if fails:
        print('%d of %d failed' % (len(fails), n + 1))
        return 1
    print('all %d passed' % (n + 1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
