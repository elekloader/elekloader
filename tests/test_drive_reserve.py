# SPDX-License-Identifier: GPL-2.0-or-later
"""A build that keeps the +Drive's storage free (store.py, "drive"; docs/DRIVE.md, "+Drive storage"): the
lockfile reads the unit's /mods/.store, and a later build's lock codes and CCs leave the +Drive's alone, so
the boot allocator (drive_store.py) keeps them. Needs no firmware: the flashed builds are lockfiles, and
their Bases are what the descriptor and tables of such a build would say (pytest, or run with python).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from elekloader import container, patch, store              # noqa: E402
from elekloader import drive_store as ds                      # noqa: E402
from elekloader.elemod import ModError                        # noqa: E402

DEV = 'analog-rytm-mk2'


def flashed(lock, decls_of):
    """A lockfile -> the Base its build gives the boot allocator."""
    cap = store.capacity(DEV, lock['split'])
    desc = store.descriptors(lock)
    codes = [(desc[(c['mod'], c['sym'])], c['code']) for c in lock.get('codes', [])]
    ccs = [(desc[(c['mod'], c['sym'])], c['cc']) for c in lock.get('ccs', [])]
    return ds.Base(DEV, {s: (cap[s], lock['next'][s], lock['generation'][s]) for s in store.SCOPES},
                   int(lock['id'], 16), [int(a, 16) for a in lock['ancestors']], codes, ccs, 0,
                   {t: 64 for t in ds.TABLES})


def build(lock, fields, drive=None):
    """fields: [(mod, sym, scope, lock?, cc?)], 1 bit each -> (lockfile, notes)."""
    decls = [(m, s, sc, 1) for m, s, sc, _l, _c in fields]
    locks = [(m, s) for m, s, _sc, l, _c in fields if l]
    ccs = [(m, s) for m, s, _sc, _l, c in fields if c]
    return store.allocate(lock, decls, DEV, None, locks, ccs, {}, (), drive)


def flash_fields(n, mod='flash', scope=None, cc=False, start=0):
    """n lockable fields, sound and kit by turns (scope None)."""
    return [(mod, 'f%03d' % i, scope or ('sound', 'kit')[i % 2], True, cc) for i in range(start, start + n)]


DRIVE_MOD = {'id': 'zz-drive', 'storage': [{'sym': 'zd%d' % i, 'scope': 'kit', 'bits': 1, 'lock': True,
                                            'cc': True} for i in range(4)]}


def setup(n_flash=60, n_cc=0):
    """L1: n_flash lockable kit fields (the first n_cc with a CC); then the +Drive places DRIVE_MOD on it."""
    fl = [(m, s, sc, l, i < n_cc) for i, (m, s, sc, l, _c) in enumerate(flash_fields(n_flash))]
    l1, _ = build(None, fl)
    p1 = ds.allocate(flashed(l1, None), None, ds.Want([DRIVE_MOD]))
    return l1, fl, p1


def drive_codes(p):
    return sorted(c['code'] for c in p.state['codes'])


def test_reserve_reads_the_store():
    _l1, _fl, p1 = setup()
    st = ds.decode(p1.bytes)
    r = ds.reserve(st)
    assert r['codes'] == [124, 125, 126, 127] and len(r['ccs']) == 4
    assert r['codes'] == drive_codes(p1)
    assert r['low']['kit'] == min(f['offset'] for f in p1.state['fields'])
    store.check_drive(r, DEV)


def test_a_later_build_skips_the_drives_codes():
    # L1 holds 42..101; the +Drive 124..127. L2 drops 10 fields and adds 30: without the .store its codes run
    # 102..131 into the +Drive's; with it they stop at 123 and take freed ones
    l1, fl, p1 = setup()
    r = ds.reserve(ds.decode(p1.bytes))
    keep = fl[10:] + flash_fields(30, start=100)
    l2, notes = build(l1, keep, r)
    flashed_codes = {c['code'] for c in l2['codes']}
    assert not flashed_codes & set(r['codes']), notes
    assert l2['drive'] == r
    p2 = ds.allocate(flashed(l2, None), ds.decode(p1.bytes), ds.Want([DRIVE_MOD]))
    assert drive_codes(p2) == drive_codes(p1)
    assert not [n for n in p2.notes if 'flashed build' in n], p2.notes
    assert p2.state['fields'] == p1.state['fields'] and p2.generations == p1.generations
    assert p1.id in p2.ancestors                # the projects saved before still load its data
    # without it: the old collision, which the boot allocator then resolves by new codes
    l2x, _ = build(l1, keep)
    assert {c['code'] for c in l2x['codes']} & set(r['codes'])
    p2x = ds.allocate(flashed(l2x, None), ds.decode(p1.bytes), ds.Want([DRIVE_MOD]))
    assert [n for n in p2x.notes if 'is the flashed build\'s now' in n]


def test_the_lockfile_keeps_it_for_the_next_build():
    l1, fl, p1 = setup()
    r = ds.reserve(ds.decode(p1.bytes))
    l2, _ = build(l1, fl, r)
    assert l2['drive'] == r and l2['id'] == l1['id']    # not stored data: the layout ID stays
    store.check_lock(l2, DEV)
    l3, _ = build(l2, fl + flash_fields(22, start=100))
    assert l3['drive'] == r and not {c['code'] for c in l3['codes']} & set(r['codes'])
    l4, _ = build(l3, fl, False)
    assert 'drive' not in l4


def test_only_the_drives_codes_left_takes_one_with_a_note():
    l1, fl, p1 = setup(82)                       # 42..123: the +Drive has 124..127
    r = ds.reserve(ds.decode(p1.bytes))
    l2, notes = build(l1, fl + flash_fields(1, start=200), r)
    taken = [n for n in notes if 'a +Drive field holds' in n]
    assert taken and {c['code'] for c in l2['codes']} & set(r['codes'])
    try:
        build(l1, fl + flash_fields(5, start=200), r)
        raise AssertionError('more than 86 codes')
    except ModError:
        pass


def test_ccs_skip_the_drives():
    pool = store._spec(DEV)['ccs']
    l1, fl, p1 = setup(40, n_cc=25)
    r = ds.reserve(ds.decode(p1.bytes))
    assert len(r["ccs"]) == 4 and set(r["ccs"]) <= set(pool[25:])
    # drop 5 with a CC, add 6: 4 never-used are left besides the +Drive's 4, then the freed ones
    keep = fl[5:] + [(m, s, sc, True, True) for m, s, sc, _l, _c in flash_fields(6, start=100)]
    l2, notes = build(l1, keep, r)
    assert not {c['cc'] for c in l2['ccs']} & set(r['ccs']), notes
    p2 = ds.allocate(flashed(l2, None), ds.decode(p1.bytes), ds.Want([DRIVE_MOD]))
    assert sorted(c['cc'] for c in p2.state['ccs']) == sorted(c['cc'] for c in p1.state['ccs'])
    l2x, _ = build(l1, keep)
    assert {c['cc'] for c in l2x['ccs']} & set(r['ccs'])


def test_bits_past_the_low_mark_get_a_note():
    l1, fl, p1 = setup(10)                       # 5 kit bits
    r = ds.reserve(ds.decode(p1.bytes))
    low = r['low']['kit']
    assert low == 60
    l2, notes = build(l1, fl + flash_fields(low - 5 + 1, scope='kit', start=100), r)
    assert [n for n in notes if n.startswith('+Drive kit storage (from bit 60)')], notes
    assert ds.fit(flashed(l2, None), ds.Want([DRIVE_MOD])) == [('zz-drive', 'kit', 1)]
    l3, notes = build(l1, fl + flash_fields(low - 5, scope='kit', start=100), r)
    assert not [n for n in notes if n.startswith('+Drive')], notes


def test_a_bad_drive_key_is_refused():
    l1, fl, p1 = setup(10)
    r = ds.reserve(ds.decode(p1.bytes))
    for bad in (dict(r, codes=[200]), dict(r, ccs=[1]), dict(r, codes=[127, 124]), {'id': r['id']}):
        try:
            build(l1, fl, bad)
            raise AssertionError(bad)
        except ModError:
            pass
    l2, _ = build(l1, fl, r)
    try:
        store.check_lock(dict(l2, drive=dict(r, low={})), DEV)
        raise AssertionError('low')
    except ModError:
        pass


def test_patch_reads_the_store_as_mods_get_saves_it():
    import tempfile
    _l1, _fl, p1 = setup()
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, '.store')
        with open(raw, 'wb') as fh:
            fh.write(p1.bytes)
        assert patch.read_drive_store(raw) == ds.reserve(p1.state)
        wrapped = os.path.join(d, 'wrapped.store')
        with open(wrapped, 'wb') as fh:
            fh.write(container.wrap(p1.bytes, 2, 0x0d, 0x0174))
        assert patch.read_drive_store(wrapped) == ds.reserve(p1.state)
        with open(raw, 'wb') as fh:
            fh.write(p1.bytes[:-1])
        try:
            patch.read_drive_store(raw)
            raise AssertionError('a cut .store')
        except patch.PatchError as e:
            assert '--drive-store none' in str(e)


# ---- the Analog Four MKII: lock codes per track kind ---------------------------------------------------------

A4 = 'analog-four-mk2'
A4_DRIVE_MOD = {'id': 'zz-drive', 'storage': [
    {'sym': 'zs0', 'scope': 'sound', 'bits': 1, 'lock': True},
    {'sym': 'zf0', 'scope': 'kit', 'bits': 1, 'lock': True},
    {'sym': 'zf1', 'scope': 'kit', 'bits': 1, 'lock': True},
    {'sym': 'zc0', 'scope': 'kit', 'bits': 1, 'lock': True, 'track': 'cv'}]}


def a4_flashed(lock):
    """An A4 lockfile -> the Base its build gives the boot allocator (a CV field's code with CV_FLAG)."""
    cap = store.capacity(A4, lock['split'])
    desc = store.descriptors(lock)
    codes = [(desc[(c['mod'], c['sym'])], c['code'] | (store.CV_FLAG if c.get('kind') == 'cv' else 0))
             for c in lock.get('codes', [])]
    return ds.Base(A4, {s: (cap[s], lock['next'][s], lock['generation'][s]) for s in store.SCOPES},
                   int(lock['id'], 16), [int(a, 16) for a in lock['ancestors']], codes, [], 0,
                   {t: 64 for t in ds.TABLES})


def a4_build(lock, fields, drive=None):
    """fields: [(mod, sym, scope, cv?)], 1 bit each, all lockable -> (lockfile, notes)."""
    decls = [(m, s, sc, 1) for m, s, sc, _cv in fields]
    locks = [(m, s) for m, s, _sc, _cv in fields]
    cvs = [(m, s) for m, s, _sc, cv in fields if cv]
    return store.allocate(lock, decls, A4, None, locks, (), {}, cvs, drive)


def a4_fields(n, kind, start=0):
    scope = 'sound' if kind == 'synth' else 'kit'
    return [('flash', '%s%03d' % (kind[0], i), scope, kind == 'cv') for i in range(start, start + n)]


def a4_setup():
    """L1: 10 synth, 4 FX and 2 CV lockable fields (0x70 up in each kind); the +Drive places A4_DRIVE_MOD on it."""
    fl = a4_fields(10, 'synth') + a4_fields(4, 'fx') + a4_fields(2, 'cv')
    l1, _ = a4_build(None, fl)
    p1 = ds.allocate(a4_flashed(l1), None, ds.Want([A4_DRIVE_MOD]))
    return l1, fl, p1


def a4_drive_codes(p):
    return {k: sorted(c['code'] for c in p.state['codes'] if c['kind'] == k) for k in ds.KINDS}


def test_a4_reserve_keeps_codes_per_kind():
    _l1, _fl, p1 = a4_setup()
    r = ds.reserve(ds.decode(p1.bytes))
    assert r['codes'] == {'synth': [127], 'fx': [126, 127], 'cv': [127]}, r
    assert r['codes'] == a4_drive_codes(p1)
    store.check_drive(r, A4)
    assert store.drive_codes(r) == 4


def test_a4_a_later_build_skips_the_drives_codes_of_its_own_kind():
    # L2 drops s000, s001 and f000 and adds 6 synth and 11 FX fields: without the .store the synth codes run
    # 122..127 and the FX ones 116..126, into the +Drive's; with it they stop below them and take freed ones.
    # The CV kind grows into 126, which only the FX kind's +Drive field holds
    l1, fl, p1 = a4_setup()
    r = ds.reserve(ds.decode(p1.bytes))
    keep = fl[2:10] + fl[11:] + a4_fields(6, 'synth', 100) + a4_fields(11, 'fx', 100) + a4_fields(13, 'cv', 100)
    l2, notes = a4_build(l1, keep, r)
    for k in ds.KINDS:
        mine = {c['code'] for c in l2['codes'] if c['kind'] == k}
        assert not mine & set(r['codes'][k]), (k, notes)
    assert 126 in {c['code'] for c in l2['codes'] if c['kind'] == 'cv'}
    assert not [n for n in notes if 'a +Drive field holds' in n], notes
    assert l2['drive'] == r
    store.check_lock(l2, A4)
    p2 = ds.allocate(a4_flashed(l2), ds.decode(p1.bytes), ds.Want([A4_DRIVE_MOD]))
    assert a4_drive_codes(p2) == a4_drive_codes(p1)
    assert not [n for n in p2.notes if 'flashed build' in n], p2.notes
    assert p2.state['fields'] == p1.state['fields']
    # without it: the old collision, which the boot allocator then resolves by new codes
    l2x, _ = a4_build(l1, keep)
    assert any({c['code'] for c in l2x['codes'] if c['kind'] == k} & set(r['codes'][k]) for k in ds.KINDS)
    p2x = ds.allocate(a4_flashed(l2x), ds.decode(p1.bytes), ds.Want([A4_DRIVE_MOD]))
    assert [n for n in p2x.notes if 'is the flashed build\'s now' in n]


def test_a4_only_the_drives_codes_left_takes_one_with_a_note():
    l1, fl, p1 = a4_setup()                      # synth 112..121; the +Drive has 127
    r = ds.reserve(ds.decode(p1.bytes))
    l2, notes = a4_build(l1, fl + a4_fields(6, 'synth', 100), r)
    assert [n for n in notes if 'a +Drive field holds' in n and '(synth tracks)' in n], notes
    try:
        a4_build(l1, fl + a4_fields(7, 'synth', 100), r)
        raise AssertionError('more than 16 synth codes')
    except ModError as e:
        assert 'the synth tracks' in str(e)


def test_a4_drive_codes_have_the_devices_shape():
    l1, fl, p1 = a4_setup()
    r = ds.reserve(ds.decode(p1.bytes))
    flat = dict(r, codes=[126, 127])
    for bad in (flat, dict(r, codes={'synth': [127], 'fx': []}), dict(r, codes=dict(r['codes'], cv=[42])),
                dict(r, codes=dict(r['codes'], fx=[127, 126]))):
        try:
            a4_build(l1, fl, bad)
            raise AssertionError(bad)
        except ModError as e:
            assert '"codes"' in str(e)
    try:                                         # the A4's per-kind codes on the Rytm
        store.check_drive(dict(r, codes={'synth': [127], 'fx': [], 'cv': []}, ccs=[]), DEV)
        raise AssertionError('the Rytm')
    except ModError:
        pass


def test_a4_patch_reads_the_store():
    import tempfile
    _l1, _fl, p1 = a4_setup()
    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, '.store')
        with open(raw, 'wb') as fh:
            fh.write(p1.bytes)
        r = patch.read_drive_store(raw)
        assert r == ds.reserve(p1.state) and isinstance(r['codes'], dict)


if __name__ == '__main__':
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith('test_') and callable(fn):
            fn()
            n += 1
    print('%d tests passed' % n)
