# SPDX-License-Identifier: GPL-2.0-or-later
"""Mod storage's lockfile and allocator, elekloader/store.py (pytest, or run
with python). Needs no stock file: js/test/store.test.ts checks the
TypeScript port against the same module's results."""
import json
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


def test_the_analog_four_mk2s_room():
    """core-a4 (mods/core-a4/store.c): 32 bits a sound, 128 a kit, 64 a global
    slot, and the 4,342 bytes of the tagged store for 129 x 6 pattern tracks,
    129 patterns and the project."""
    a4 = 'analog-four-mk2'
    assert store.supports(a4)
    assert store.capacity(a4, {'track': 2, 'pattern': 4}) == {
        'sound': 32, 'kit': 128, 'global': 64, 'track': 16, 'pattern': 32,
        'project': 8 * (4342 - 774 * 2 - 129 * 4)}
    assert 'sound' in refused(store.allocate, None, [('a', 'x', 'sound', 33)], a4)
    assert 'kit' in refused(store.allocate, None, [('a', 'x', 'kit', 32), ('a', 'y', 'kit', 32),
                                                   ('a', 'z', 'kit', 32), ('a', 'w', 'kit', 32),
                                                   ('a', 'v', 'kit', 1)], a4)
    # six tracks a pattern: three bytes a track still fit (the Rytm's 13 do not)
    lock, _ = store.allocate(None, [('a', 'x', 'track', 17)], a4)
    assert lock['split'] == {'track': 3, 'pattern': 4} and lock['device'] == a4


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
    for k, v in (('elekloader_store', 3), ('device', 'digitakt'), ('id', '0x0'), ('fields', {})):
        bad = dict(good, **{k: v})
        refused(store.allocate, bad, [], DEV)


def test_the_build_carries_the_lockfile():
    lock, _ = store.allocate(None, [('a', 'x', 'track', 4)], DEV)
    image = bytes(1000) + b'any image' + store.trailer(lock)
    assert store.from_image(image) == lock
    assert store.from_image(bytes(1000)) is None
    assert 'cannot be read' in refused(store.from_image, store.TRAILER + (5).to_bytes(4, 'big') + b'{nope')


def test_lockable_fields_get_codes_that_follow_them():
    d = [('a', 'x', 'sound', 7), ('a', 'y', 'sound', 8), ('b', 'z', 'sound', 3)]
    l1, _ = store.allocate(None, d, DEV, None, [('a', 'x'), ('b', 'z')])
    assert [(c['sym'], c['code']) for c in l1['codes']] == [('x', 42), ('z', 43)] and l1['next_code'] == 44
    assert store.code_entries(l1)[0] == (1 << 28 | 7 << 22 | 0).to_bytes(4, 'big') + (42).to_bytes(4, 'big')
    l2, notes = store.allocate(l1, d[:1], DEV, None, [('a', 'x')])
    assert l2['freed_codes'] == [{'mod': 'b', 'sym': 'z', 'code': 43}]
    l3, notes = store.allocate(l2, d, DEV, None, [('a', 'x'), ('b', 'z')])
    assert 'b z: back with lock code 43' in notes and l3['id'] == l1['id']
    store.check_lock(l3, DEV)


def test_ccs_follow_their_fields_and_not_the_layout():
    d = [('a', 'x', 'sound', 7), ('b', 'z', 'sound', 3)]
    xz = [('a', 'x'), ('b', 'z')]
    l1, _ = store.allocate(None, d, DEV, None, xz, xz)
    assert [(c['sym'], c['cc']) for c in l1['ccs']] == [('x', 8), ('z', 9)]
    assert store.cc_entries(l1)[1] == (1 << 28 | 3 << 22 | 7).to_bytes(4, 'big') + (9).to_bytes(4, 'big')
    l0, _ = store.allocate(None, d, DEV, None, xz)
    assert l1['id'] == l0['id']                     # a CC is not where data is
    l2, notes = store.allocate(l1, [('a', 'x', 'sound', 9), d[1]], DEV, None, xz, xz)
    assert l2['codes'][-1]['code'] == 44 and l2['ccs'] == l1['ccs']
    l3, notes = store.allocate(l2, d[1:], DEV, None, xz[1:], xz[1:])
    assert l3['freed_ccs'] == [{'mod': 'a', 'sym': 'x', 'cc': 8}]
    store.check_lock(l3, DEV)
    assert 'only a lockable field' in refused(store.parse_decls,
                                              {'storage': [{'sym': 's', 'scope': 'sound', 'bits': 3, 'cc': True}]})
    assert 'no lock code' in refused(store.check_lock, dict(store.fresh(DEV), ccs=[{'mod': 'a', 'sym': 'x', 'cc': 8}],
                                                            freed_ccs=[]), DEV)


def test_a_lockfile_without_lockable_fields_keeps_its_id():
    l1, _ = store.allocate(None, [('a', 'x', 'kit', 4)], DEV)
    l2, _ = store.allocate(l1, [('a', 'x', 'kit', 4)], DEV, None, [])
    assert l2 == l1 and 'codes' not in l2


def test_only_sound_and_kit_fields_can_be_lockable():
    assert 'only sound and kit fields' in refused(store.parse_decls,
                                                  {'storage': [{'sym': 'g', 'scope': 'global', 'bits': 3, 'lock': True}]})


def test_a_field_with_an_init_is_kept_xored_and_its_init_is_part_of_the_layout():
    """"init" (core-ar2 0.10): kept with the field in the lockfile (format 2), in the descriptor's
    bits 16..21 as a slot of core_field_inits, and part of the layout ID."""
    assert store.parse_inits({'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 7, 'init': 100},
                                          {'sym': 'b', 'scope': 'kit', 'bits': 7, 'init': 0}]}) == {'a': 100}
    refused(store.parse_decls, {'storage': [{'sym': 'a', 'scope': 'kit', 'bits': 7, 'init': 128}]})
    plain, _ = store.allocate(None, [('m', 'a', 'kit', 7)], DEV)
    inited, _ = store.allocate(None, [('m', 'a', 'kit', 7)], DEV, inits={('m', 'a'): 100})
    assert plain['elekloader_store'] == 1 and inited['elekloader_store'] == 2
    assert inited['fields'][0]['init'] == 100 and inited['id'] != plain['id']
    syms, _ = store.symbols(inited, DEV)
    assert syms['a'] == 2 << 28 | 7 << 22 | 1 << 16 | 0
    assert store.init_entries(inited) == [(100).to_bytes(4, 'big')]
    assert store.symbols(plain, DEV)[0]['a'] == 2 << 28 | 7 << 22
    assert store.parse_locks({'storage': [{'sym': 's', 'scope': 'sound', 'bits': 3, 'lock': True},
                                          {'sym': 't', 'scope': 'sound', 'bits': 3}]}) == ['s']



def test_a_handle_field_is_placed_like_any_and_listed_in_core_handles():
    """"handle": true (core-ar2 0.11): not in the lockfile, not part of the layout; core_handles lists
    the handle fields' descriptors in the lockfile's order. A handle has no lock and no init."""
    doc = {'storage': [{'sym': 'h', 'scope': 'project', 'bits': 16, 'handle': True},
                       {'sym': 'k', 'scope': 'kit', 'bits': 4, 'handle': False}]}
    assert store.parse_handles(doc) == ['h'] and store.parse_decls(doc) == [('h', 'project', 16), ('k', 'kit', 4)]
    d = [('m', 'k', 'kit', 4), ('m', 'h', 'project', 16)]
    lock, _ = store.allocate(None, d, DEV)
    assert 'handle' not in json.dumps(lock)
    syms, _ = store.symbols(lock, DEV)
    assert store.handle_entries(lock, [('m', 'h')]) == [syms['h'].to_bytes(4, 'big')]
    assert store.handle_entries(lock) == []
    assert 'cannot be lockable' in refused(store.parse_decls, {'storage': [
        {'sym': 'h', 'scope': 'sound', 'bits': 8, 'lock': True, 'handle': True}]})
    assert 'has no init' in refused(store.parse_decls, {'storage': [
        {'sym': 'h', 'scope': 'kit', 'bits': 8, 'init': 1, 'handle': True}]})
    assert 'true or false' in refused(store.parse_decls, {'storage': [
        {'sym': 'h', 'scope': 'kit', 'bits': 8, 'handle': 1}]})


A4 = 'analog-four-mk2'


def test_a4_codes_are_per_track_kind():
    """core-a4 0.5: a sound field's code is the synth tracks', a kit field's the FX track's, or the CV
    track's with "track": "cv", each kind from 0x70; a field that changes kind gets a new code, and a
    17th field of a kind is refused."""
    decls = [('a', 's', 'sound', 7), ('a', 'k', 'kit', 7), ('a', 'c', 'kit', 7)]
    locks = [('a', 's'), ('a', 'k'), ('a', 'c')]
    l1, _ = store.allocate(None, decls, A4, None, locks, (), cvs=[('a', 'c')])
    assert [(c['sym'], c['code'], c['kind']) for c in l1['codes']] == \
        [('s', 0x70, 'synth'), ('k', 0x70, 'fx'), ('c', 0x70, 'cv')]
    assert l1['next_code'] == {'synth': 0x71, 'fx': 0x71, 'cv': 0x71}
    assert [int.from_bytes(e[4:], 'big') for e in store.code_entries(l1)] == [0x70, 0x70, 0x70 | store.CV_FLAG]
    l2, _ = store.allocate(l1, decls, A4, None, locks, (), cvs=[('a', 'c')])
    assert l2 == l1
    l3, _ = store.allocate(l1, decls, A4, None, locks, (), cvs=[])
    assert [(c['sym'], c['code'], c['kind']) for c in l3['codes']] == \
        [('s', 0x70, 'synth'), ('k', 0x70, 'fx'), ('c', 0x71, 'fx')] and l3['id'] != l1['id']
    many = [('m', 'f%d' % i, 'kit', 1) for i in range(17)]
    store.allocate(None, many[:16], A4, None, [d[:2] for d in many[:16]])
    assert 'more than 16 lockable parameters on the FX track' in \
        refused(store.allocate, None, many, A4, None, [d[:2] for d in many])


def test_a4_ccs():
    """core-a4 0.5: a synth field's CC comes first from those the FX and CV tables map, a kit field's (FX or
    CV) only from those every kind leaves free."""
    decls = [('m', 's', 'sound', 7), ('m', 'k', 'kit', 7), ('m', 'c', 'kit', 7)]
    locks = [('m', 's'), ('m', 'k'), ('m', 'c')]
    l1, _ = store.allocate(None, decls, A4, None, locks, locks, cvs=[('m', 'c')])
    assert [(c['sym'], c['cc']) for c in l1['ccs']] == [('s', 12), ('c', 35), ('k', 36)]
    assert len(store.cc_entries(l1)) == 3


def test_a4_flags():
    """On the A4 "lfo" is refused (every source is a destination of a "mod" field), "mod" is taken on
    a kit field, and "cc" is taken (core-a4 0.5's CC pool); the Rytm has no CV track."""
    scopes = {('m', 'a'): 'sound', ('m', 'k'): 'kit'}
    store.check_flags(A4, scopes, (), [('m', 'a'), ('m', 'k')], [('m', 'k')])
    assert '"lfo" is the Analog Rytm MKII' in refused(store.check_flags, A4, scopes, [('m', 'a')])
    store.check_flags(A4, scopes, (), (), (), [('m', 'a'), ('m', 'k')])
    assert 'has no CV track' in refused(store.check_flags, 'analog-rytm-mk2', scopes, (), (), [('m', 'k')])
    assert 'the FX track has no routes' in refused(store.check_flags, 'analog-rytm-mk2', scopes, (), [('m', 'k')])
    assert store.parse_cvs({'storage': [{'sym': 'c', 'scope': 'kit', 'bits': 3, 'lock': True, 'track': 'cv'}]}) == ['c']
    refused(store.parse_decls, {'storage': [{'sym': 'c', 'scope': 'sound', 'bits': 3, 'lock': True, 'track': 'cv'}]})


def test_knob_ids_follow_their_mods_and_not_the_layout():
    lock = store.fresh(DEV)
    lid = lock['id']
    base, notes = store.knobs(lock, {'b': 4, 'a': 2}, 555, [(560, 573)])
    assert base == {'a': 555, 'b': 574} and lock['next_knob'] == 578       # a fits under 560, b past 573
    assert notes == ['a: knob ids 555..556', 'b: knob ids 574..577']
    assert store.layout_id(lock) == store.layout_id(store.fresh(DEV)) and lock['id'] == lid
    store.check_lock(lock, DEV)
    base, notes = store.knobs(lock, {'b': 4, 'a': 2}, 555, [(560, 573)])
    assert base == {'a': 555, 'b': 574} and notes == []                     # the same build: the same ids
    base, notes = store.knobs(lock, {'b': 4}, 555, [(560, 573)])           # a leaves: its ids are kept
    assert base == {'b': 574} and lock['freed_knobs'] == [{'mod': 'a', 'first': 555, 'count': 2}]
    assert notes == ['a: knob ids 555..556 freed, and kept for it until the unused ids run out']
    base, notes = store.knobs(lock, {'b': 4, 'c': 1}, 555, [(560, 573)])   # c takes never-used ids
    assert base == {'b': 574, 'c': 578}
    base, notes = store.knobs(lock, {'a': 2, 'b': 6, 'c': 1}, 555, [(560, 573)])
    assert base == {'a': 555, 'b': 579, 'c': 578}                          # a back; b grew past c
    assert notes == ['a: back with knob ids 555..556', 'b: knob ids 579..584']
    base, _ = store.knobs(lock, {'a': 2, 'b': 5, 'c': 1}, 555, [(560, 573)])
    assert base['b'] == 579                                                 # b shrank: same first id
    store.check_lock(lock, DEV)


def test_knob_ids_run_out_then_freed_runs_are_given_again():
    lock = {}
    store.knobs(lock, {'a': 4, 'b': 4}, 10, (), top=20)
    store.knobs(lock, {'b': 4}, 10, (), top=20)
    base, notes = store.knobs(lock, {'b': 4, 'c': 4}, 10, (), top=20)    # 18..21 is past 20
    assert base == {'b': 14, 'c': 10} and lock['freed_knobs'] == []
    assert notes == ['knob ids freed by a given out again', 'c: knob ids 10..13']
    assert refused(store.knobs, {}, {'d': 30}, 10, (), 20) == \
        'd asks for 30 knob ids, and ids 10..20 have no run that long free'


def test_knob_lockfiles_it_refuses():
    lock = store.fresh(DEV)
    store.knobs(lock, {'a': 2, 'b': 2}, 555)
    for edit in (lambda l: l.update(next_knob=556), lambda l: l['knobs'][1].update(first=556),
                 lambda l: l['knobs'].append(dict(l['knobs'][0])), lambda l: l.update(freed_knobs={})):
        bad = json.loads(json.dumps(lock))
        edit(bad)
        assert refused(store.check_lock, bad, DEV).startswith('lockfile: ')


if __name__ == '__main__':
    for name, fn in sorted(globals().items()):
        if name.startswith('test_'):
            fn()
            print('ok', name)
