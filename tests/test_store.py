# SPDX-License-Identifier: GPL-2.0-or-later
"""Mod storage's lockfile and allocator, elekloader/store.py (pytest, or run
with python). Needs no stock file: js/test/store.test.ts checks the
TypeScript port against the same module's results."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from elekloader import store                 # noqa: E402
from elekloader.elemod import ModError       # noqa: E402

DEV = 'analog-rytm-mk2'


def where(lock):
    return {f['sym']: (f['scope'], f['offset'], f['bits']) for f in lock['fields']}


def refused(fn, *args):
    try:
        fn(*args)
    except ModError as e:
        return str(e)
    raise AssertionError('not refused')


def test_a_new_lockfile_packs_from_bit_0():
    lock, _ = store.allocate(None, [('a', 'x', 'track', 4), ('a', 'y', 'track', 3),
                                    ('b', 'z', 'sound', 24)], DEV)
    assert where(lock) == {'x': ('track', 0, 4), 'y': ('track', 4, 3), 'z': ('sound', 0, 24)}
    assert lock['ancestors'] == [] and lock['id'] == '0x%08x' % store.layout_id(lock)
    assert lock['next']['track'] == 7 and lock['split'] == {'track': 2, 'pattern': 4}


def test_the_same_mods_from_no_lockfile_get_the_same_layout_and_id():
    decls = [('a', 'x', 'track', 4), ('b', 'z', 'kit', 9)]
    l1, _ = store.allocate(None, decls, DEV)
    l2, _ = store.allocate(None, list(reversed(decls)), DEV)
    assert l1 == l2
    l3, _ = store.allocate(None, decls + [('c', 'w', 'kit', 1)], DEV)
    assert l3['id'] != l1['id']


def test_an_unchanged_build_keeps_the_layout_and_its_id():
    decls = [('a', 'x', 'track', 4), ('b', 'z', 'kit', 9)]
    l1, _ = store.allocate(None, decls, DEV)
    l2, notes = store.allocate(l1, list(reversed(decls)), DEV)
    assert l2 == l1 and notes == []
    # an ID not from a hash (an older lockfile's) stays while nothing changes
    old = dict(l1, id='0x5a17c0de')
    assert store.allocate(old, decls, DEV)[0] == old


def test_fields_keep_their_place_and_new_ones_take_never_used_bits():
    l1, _ = store.allocate(None, [('a', 'x', 'pattern', 8), ('a', 'y', 'pattern', 8)], DEV)
    l2, _ = store.allocate(l1, [('a', 'y', 'pattern', 8), ('b', 'w', 'pattern', 5)], DEV)
    # x is freed; w does not reuse its bits, so w reads zero in every old project
    assert where(l2) == {'y': ('pattern', 8, 8), 'w': ('pattern', 16, 5)}
    assert [f['sym'] for f in l2['freed']] == ['x']
    assert l2['id'] == '0x%08x' % store.layout_id(l2) and l2['ancestors'] == [l1['id']]
    assert l2['generation'] == l1['generation']


def test_a_field_back_in_the_build_takes_its_old_place():
    both = [('a', 'x', 'track', 4), ('b', 'y', 'track', 4)]
    l1, _ = store.allocate(None, both, DEV)
    l2, notes = store.allocate(l1, [('b', 'y', 'track', 4)], DEV)
    assert any('gets its values back if it returns' in n for n in notes)
    lock = l2
    for _ in range(5):              # off and on, more times than the scope has spare bits for
        lock, notes = store.allocate(lock, both, DEV)
        assert where(lock) == where(l1) and lock['freed'] == [] and lock['next'] == l1['next']
        assert lock['generation'] == l1['generation']
        assert any('back at bit 0 of track storage' in n for n in notes)
        # the same layout as before it left, so the same ID, never its own ancestor
        assert lock['id'] == l1['id'] and lock['ancestors'] == [l2['id']]
        lock, _ = store.allocate(lock, [('b', 'y', 'track', 4)], DEV)
        assert lock == l2


def test_a_changed_width_is_a_new_field():
    l1, _ = store.allocate(None, [('a', 'x', 'global', 8)], DEV)
    l2, notes = store.allocate(l1, [('a', 'x', 'global', 9)], DEV)
    assert where(l2) == {'x': ('global', 8, 9)}
    assert any('changed size' in n for n in notes)
    l3, _ = store.allocate(l2, [('a', 'x', 'global', 8)], DEV)
    assert where(l3) == {'x': ('global', 0, 8)}


def test_freed_bits_count_in_the_id():
    l1, _ = store.allocate(None, [('a', 'x', 'kit', 8)], DEV)
    l2, _ = store.allocate(l1, [('a', 'x', 'kit', 8), ('b', 'y', 'kit', 8)], DEV)
    gone, _ = store.allocate(l2, [('a', 'x', 'kit', 8)], DEV)
    assert gone['next'] == l2['next'] and where(gone) == where(l1)
    assert gone['id'] not in (l1['id'], l2['id'])      # y's bits still hold y's values


def test_a_full_scope_is_repacked_under_a_new_generation():
    l1, _ = store.allocate(None, [('a', 'x', 'kit', 32), ('a', 'y', 'kit', 32)], DEV)
    l2, notes = store.allocate(l1, [('a', 'y', 'kit', 32), ('b', 'z', 'kit', 16)], DEV)
    assert l2['generation']['kit'] == l1['generation']['kit'] + 1
    assert where(l2) == {'y': ('kit', 0, 32), 'z': ('kit', 32, 16)}
    assert l2['freed'] == [] and l2['next']['kit'] == 48
    assert all(l2['generation'][s] == l1['generation'][s] for s in store.SCOPES if s != 'kit')
    assert 'a x (kit): no longer in the build, its data is dropped' in notes


def test_the_split_grows_and_moves_the_store():
    l1, _ = store.allocate(None, [('a', 'x', 'pattern', 32), ('a', 'p', 'project', 8),
                                  ('a', 't', 'track', 4)], DEV)
    l2, _ = store.allocate(l1, [('a', 'x', 'pattern', 32), ('a', 'y', 'pattern', 8),
                                ('a', 'p', 'project', 8), ('a', 't', 'track', 4)], DEV)
    assert l2['split'] == {'track': 2, 'pattern': 5}
    for s in ('pattern', 'project'):
        assert l2['generation'][s] == l1['generation'][s] + 1
    # the pattern tracks come first in the store and kept their size: they stay
    for s in ('sound', 'kit', 'global', 'track'):
        assert l2['generation'][s] == l1['generation'][s]
    assert where(l2)['t'] == where(l1)['t']


def test_what_does_not_fit_is_refused():
    assert 'sound' in refused(store.allocate, None, [('a', 'x', 'sound', 32), ('a', 'y', 'sound', 25)], DEV)
    # core-ar2 0.2 keeps 24 bits a sound, 0.3 (OS 1.74 only) 56
    # (the core named exists: on 1.73, which 0.3 is not made for, building with it says so)
    assert 'with core 0.2 (56 with core 0.3)' in refused(store.allocate, None,
                                                         [('a', 'x', 'sound', 25)], DEV, '0.2')
    # three bytes a pattern track would not fit in the store
    assert refused(store.allocate, None, [('a', 'x', 'track', 17)], DEV)
    assert 'two mods' in refused(store.allocate, None, [('a', 'x', 'kit', 1), ('b', 'x', 'kit', 1)],
                                 DEV)


def test_a_sounds_room_is_its_cores():
    assert store.capacity(DEV, {'track': 2, 'pattern': 4}, '0.2')['sound'] == 24
    assert store.capacity(DEV, {'track': 2, 'pattern': 4}, '0.3')['sound'] == 56
    assert store.capacity(DEV, {'track': 2, 'pattern': 4})['sound'] == 56
    # bits 0..23 mean the same with either core: a 0.2 build's fields keep their places with 0.3
    l2, _ = store.allocate(None, [('a', 'x', 'sound', 24)], DEV, '0.2')
    l3, notes = store.allocate(l2, [('a', 'x', 'sound', 24), ('b', 'y', 'sound', 32)], DEV, '0.3')
    assert where(l3) == {'x': ('sound', 0, 24), 'y': ('sound', 24, 32)}
    assert l3['generation']['sound'] == l2['generation']['sound']
    # a field a 0.3 build placed past bit 24 repacks the scope when built with 0.2
    back, notes = store.allocate(l3, [('b', 'y', 'sound', 8)], DEV, '0.2')
    assert where(back) == {'y': ('sound', 0, 8)} and back['generation']['sound'] == 2
    assert any('repacked' in n for n in notes)
    # and one that fits stays where it is
    l4, _ = store.allocate(None, [('a', 'x', 'sound', 8)], DEV, '0.3')
    assert store.allocate(l4, [('a', 'x', 'sound', 8)], DEV, '0.2')[0] == l4
    # the same mods rebuilt with 0.2: the repack alone moves a field, so the layout takes its new ID
    l5, _ = store.allocate(None, [('a', 'x', 'sound', 24)], DEV, '0.3')
    l6, _ = store.allocate(l5, [('b', 'y', 'sound', 8)], DEV, '0.3')
    assert where(l6) == {'y': ('sound', 24, 8)}
    l7, notes = store.allocate(l6, [('b', 'y', 'sound', 8)], DEV, '0.2')
    assert where(l7) == {'y': ('sound', 0, 8)} and l7['generation']['sound'] == l6['generation']['sound'] + 1
    assert l7['id'] == '0x%08x' % store.layout_id(l7) and l7['id'] != l6['id']
    assert l7['ancestors'][0] == l6['id'] and any('layout ID' in n for n in notes)


def test_ancestors_keep_the_newest_15():
    lock, _ = store.allocate(None, [], DEV)
    seen = [lock['id']]
    for i in range(20):
        lock, _ = store.allocate(lock, [('a', 'f%d' % i, 'project', 1)], DEV)
        seen.append(lock['id'])
    assert len(set(seen)) == 21
    assert lock['ancestors'] == list(reversed(seen[-16:-1])) and lock['id'] == seen[-1]


def test_symbols_and_the_id_table():
    l1, _ = store.allocate(None, [('a', 'x', 'track', 4), ('a', 'y', 'project', 7)], DEV)
    lock, _ = store.allocate(l1, [('a', 'x', 'track', 4), ('a', 'y', 'project', 7),
                                  ('a', 'z', 'sound', 3)], DEV)
    syms, tab = store.symbols(lock, DEV)
    assert syms['x'] == 4 << 28 | 4 << 22 | 0
    assert syms['y'] == 6 << 28 | 7 << 22 | 0
    assert syms['z'] == 1 << 28 | 3 << 22 | 0
    assert syms['__store_gen'] == 0x01010101 and syms['__store_gen2'] == 0x01010204
    assert tab == [bytes.fromhex(lock['id'][2:]), bytes.fromhex(l1['id'][2:])]


def test_a_mods_storage_declarations():
    assert store.parse_decls({'storage': [{'sym': 'a_b', 'scope': 'kit', 'bits': 32}]}) == [('a_b', 'kit', 32)]
    for bad in ([{'sym': '1x', 'scope': 'kit', 'bits': 1}], [{'sym': 'x', 'scope': 'song', 'bits': 1}],
                [{'sym': 'x', 'scope': 'kit', 'bits': 33}], [{'sym': 'x', 'scope': 'kit', 'bits': 0}],
                [{'sym': 'x', 'scope': 'kit', 'bits': 1, 'more': 1}],
                [{'sym': 'x', 'scope': 'kit', 'bits': 1}, {'sym': 'x', 'scope': 'kit', 'bits': 1}], {}):
        refused(store.parse_decls, {'storage': bad})


def test_lockfiles_it_refuses():
    good, _ = store.allocate(None, [], DEV)
    for k, v in (('elekloader_store', 2), ('device', 'digitakt'), ('id', '0x0'), ('fields', {})):
        bad = dict(good, **{k: v})
        refused(store.allocate, bad, [], DEV)


def test_the_build_carries_the_lockfile():
    lock, _ = store.allocate(None, [('a', 'x', 'track', 4)], DEV)
    image = bytes(1000) + b'any image' + store.trailer(lock)
    assert store.from_image(image) == lock
    assert store.from_image(bytes(1000)) is None
    assert 'cannot be read' in refused(store.from_image, store.TRAILER + (5).to_bytes(4, 'big') + b'{nope')


if __name__ == '__main__':
    for name, fn in sorted(globals().items()):
        if name.startswith('test_'):
            fn()
            print('ok', name)
