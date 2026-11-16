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


if __name__ == '__main__':
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith('test_') and callable(fn):
            fn()
            n += 1
    print('%d tests passed' % n)
