# SPDX-License-Identifier: GPL-2.0-or-later
"""+Drive storage (elekloader/drive_store.py): the boot allocator's twin and /mods/.store (pytest, or run
with python). Needs no firmware: the flashed builds are made-up Bases of the Analog Rytm MKII's rooms.
With ELEKLOADER_DRIVE_BUILD=<a built .syx that carries the Drive Loader> it also reads a real build's
Base, from the image and from its descriptor alone.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from elekloader import desc, drive, store                    # noqa: E402
from elekloader import drive_store as ds                      # noqa: E402
from elekloader.elemod import ModError                        # noqa: E402

DEV = 'analog-rytm-mk2'
ROOM = {'sound': 56, 'kit': 64, 'global': 40, 'track': 16, 'pattern': 32, 'project': 3776}
FID = 0x5a17c0de


def base(used=None, gen=None, codes=(), ccs=(), inits=0, spare=32, fid=FID, room=None, live=False):
    used, gen, room = used or {}, gen or {}, dict(ROOM, **(room or {}))
    return ds.Base(DEV, {s: (room[s], used.get(s, 0), gen.get(s, 1)) for s in store.SCOPES}, fid, [0x1234],
                   codes, ccs, inits, {t: spare for t in ds.TABLES}, live_dst=live)


def mod(ident, *fields):
    return {'id': ident, 'storage': [dict(f) for f in fields]}


def f(sym, scope, bits, **kw):
    return dict({'sym': sym, 'scope': scope, 'bits': bits}, **kw)


DUCK = mod('duck', f('duck_src', 'kit', 4, lock=True, cc=True), f('duck_amt', 'kit', 28, lock=True, init=5))
LFO = mod('lfo-rnd', f('lr_a', 'sound', 16, lock=True, lfo=True))
KLFO = mod('kit-lfo', f('kl_a', 'kit', 8, lock=True, lfo=True))
FX = mod('fx-sends', f('fx_k', 'kit', 25), f('fx_k2', 'kit', 24), f('fx_s', 'sound', 7, lock=True, cc=True))
VDAC = mod('voice-dac', f('vd_s', 'sound', 32), f('vd_s2', 'sound', 7), f('vd_g', 'global', 1))


def place(b, prev, *mods):
    return ds.allocate(b, prev, ds.Want(list(mods)))


def where(p):
    return {'%s/%s' % k: (d >> 28, d >> 22 & 63, d & 0xFFFF) for k, d in p.descriptors.items()}


def test_a_set_with_no_storage_keeps_the_flashed_layout():
    p = place(base(), None)
    assert p.id == FID and p.ancestors == [0x1234] and p.state['ids'] == []
    assert p.generations == {s: 1 for s in store.SCOPES}
    assert ds.decode(p.bytes) == p.state


def test_fields_go_into_the_free_bits_from_the_top_down():
    b = base(used={'sound': 20, 'kit': 8})
    p = place(b, None, DUCK, LFO)
    # by (mod id, symbol), each below the last: the flashed build grows from its used bits up
    assert where(p) == {'duck/duck_amt': (2, 28, 36), 'duck/duck_src': (2, 4, 32), 'lfo-rnd/lr_a': (1, 16, 40)}
    assert p.state['scopes']['kit']['low'] == 32 and p.state['scopes']['sound']['low'] == 40
    assert p.id != FID and p.ancestors == [FID, 0x1234]
    assert p.init_entries == [(5).to_bytes(4, 'big')]
    assert p.descriptors[('duck', 'duck_amt')] >> 16 & 63 == 1            # its init's slot


def test_init_slots_follow_the_build_s_own():
    p = place(base(inits=3), None, DUCK)
    assert p.descriptors[('duck', 'duck_amt')] >> 16 & 63 == 4


def test_the_same_inputs_give_the_same_bytes_and_the_store_round_trips():
    a = place(base(), None, LFO, DUCK)
    b = place(base(), None, DUCK, LFO)
    assert a.bytes == b.bytes and a.summary() == b.summary()
    again = place(base(), ds.decode(a.bytes), DUCK, LFO)
    assert again.bytes == a.bytes and again.id == a.id
    assert [n for n in again.notes if 'LFO' not in n] == []                # nothing moved, nothing to say


def test_a_removed_mod_is_parked_and_gets_its_bits_back():
    p1 = place(base(), None, DUCK, LFO)
    p2 = place(base(), p1.state, LFO)
    assert ('duck', 'duck_src') not in p2.descriptors
    assert sorted(k for k, _d in p2.parked) == [('duck', 'duck_amt'), ('duck', 'duck_src')]
    assert p2.id == p1.id                                                 # a park moves no bit
    assert any('parked' in n for n in p2.notes)
    p3 = place(base(), p2.state, DUCK, LFO)
    assert where(p3) == where(p1) and p3.id == p1.id
    assert p3.code_entries == p1.code_entries and p3.cc_entries == p1.cc_entries
    assert any('back at bit 32 of kit storage' in n for n in p3.notes)


def test_a_new_field_never_takes_a_parked_field_s_bits_while_fresh_bits_last():
    p1 = place(base(), None, DUCK)
    p2 = place(base(), p1.state, LFO)                                     # duck parked at kit 32..63
    small = mod('small', f('sm', 'kit', 8))
    p3 = place(base(), p2.state, LFO, small)
    assert where(p3)['small/sm'] == (2, 8, 24) and p3.generations['kit'] == 1


def test_when_fresh_bits_run_out_the_scope_is_repacked_and_parked_fields_dropped():
    p1 = place(base(), None, DUCK)
    p2 = place(base(), p1.state)
    assert len(p2.parked) == 2
    p3 = place(base(), p2.state, FX)                                      # 49 kit bits: only 32 never used
    assert p3.generations['kit'] == 2 and p3.generations['sound'] == 1
    assert p3.parked == []
    assert where(p3) == {'fx-sends/fx_k': (2, 25, 39), 'fx-sends/fx_k2': (2, 24, 15), 'fx-sends/fx_s': (1, 7, 49)}
    assert any('kit storage repacked (generation 2)' in n for n in p3.notes)
    assert p3.ancestors[0] == p2.id


def test_forget_data_drops_a_parked_mod_and_its_bits_stay_used():
    p1 = place(base(), None, DUCK, LFO)
    p2 = place(base(), p1.state, LFO)
    st, changed = ds.forget(p2.state, 'duck')
    assert changed and st['parked'] == [] and st['freed_codes'] == [] and st['freed_ccs'] == []
    assert ds.forget(st, 'duck')[1] is False
    assert p2.state['parked']                                             # not changed in place
    p3 = place(base(), st, LFO, DUCK)
    assert where(p3)['duck/duck_src'] == (2, 4, 0)                        # below the forgotten bits: no reuse
    assert p3.generations['kit'] == 1 and p3.id != p1.id


A4 = mod('a', f('a1', 'sound', 4))
B4 = mod('b', f('b1', 'sound', 4))


def test_the_flashed_build_growing_into_a_field_repacks_its_scope():
    p1 = place(base(), None, A4, B4)
    assert where(p1) == {'a/a1': (1, 4, 52), 'b/b1': (1, 4, 48)}
    p2 = place(base(), p1.state, A4)                                      # b parked at 48..51
    p3 = place(base(used={'sound': 50}), p2.state, A4)                    # the build grew into it
    assert p3.generations['sound'] == 2 and p3.parked == []
    assert where(p3) == {'a/a1': (1, 4, 52)}
    assert any('the flashed build took its bits' in n for n in p3.notes)


def test_a_flashed_repack_gives_the_scope_the_build_s_generation():
    p1 = place(base(), None, DUCK, LFO)
    p2 = place(base(gen={'kit': 2}), p1.state, DUCK, LFO)
    assert p2.generations == dict({s: 1 for s in store.SCOPES}, kit=2)
    assert where(p2) == where(p1)
    # once the +Drive has bumped a scope itself, a flashed repack bumps its live generation again
    q1 = place(base(), None, A4, B4)
    q2 = place(base(), q1.state, A4)
    q3 = place(base(used={'sound': 50}), q2.state, A4)
    assert q3.generations['sound'] == 2
    q4 = place(base(used={'sound': 50}, gen={'sound': 2}), q3.state, A4)
    assert q4.generations['sound'] == 3


def test_a_flashed_repack_of_a_scope_the_drive_never_used_keeps_the_flashed_layout():
    p1 = place(base(), None)
    p2 = place(base(gen={'kit': 2}, fid=0x0badcafe), p1.state)
    assert p2.id == 0x0badcafe and p2.generations['kit'] == 2


def test_lock_codes_come_from_the_top_past_the_build_s_own():
    codes = [(1 << 28 | 8 << 22 | c - 42, c) for c in (42, 43, 120)]
    p = place(base(codes=codes), None, DUCK, LFO)
    got = [int.from_bytes(e[4:], 'big') & 0xFF for e in p.code_entries]
    assert sorted(got) == [125, 126, 127]                                 # 120 is below: never in the way
    p2 = place(base(codes=codes + [(1 << 28, 126)]), p.state, DUCK, LFO)
    assert any('lock code 126 is the flashed build\'s now' in n for n in p2.notes)
    assert 126 not in [int.from_bytes(e[4:], 'big') & 0xFF for e in p2.code_entries]


def test_codes_run_out_into_the_freed_ones_then_refuse():
    many = [(1 << 28, c) for c in range(42, 126)]                         # the build leaves 126 and 127
    one = mod('one', f('o1', 'sound', 1, lock=True))
    two = mod('two', f('t1', 'sound', 1, lock=True), f('t2', 'sound', 1, lock=True))
    p1 = place(base(codes=many), None, two)
    p2 = place(base(codes=many), p1.state, one)                           # two parked: its codes freed
    assert [int.from_bytes(e[4:], 'big') & 0xFF for e in p2.code_entries] == [126]
    assert any("(was two t2's)" in n for n in p2.notes)
    assert ds.fit(base(codes=many), ds.Want([one, two])) == [('two', 'codes', 1)]


def test_lfo_flag_only_where_the_build_places_the_destination():
    p = place(base(), None, LFO)
    assert int.from_bytes(p.code_entries[0][4:], 'big') & store.LFO_FLAG == 0
    has = base(codes=[(1 << 28 | 3 << 22, 42 | store.LFO_FLAG)])
    p = place(has, None, LFO)
    assert int.from_bytes(p.code_entries[0][4:], 'big') == 127 | store.LFO_FLAG


def test_a_live_core_places_its_lfo_fields_only_for_lfo_fields():
    p = place(base(live=True), None, DUCK)                           # no "lfo" field: nothing of the core's
    assert p.lfo_dsts == {} and not any(k[0] == 'core' for k in p.descriptors)
    p = place(base(live=True), None, DUCK, LFO)
    d = p.descriptors[('core', store.LOCK_DST)]
    assert p.lfo_dsts == {'sound': d} and d >> 28 == 1 and d >> 22 & 63 == 7
    assert ('core', store.FX_LOCK_DST) not in p.descriptors          # duck's kit fields are no LFO's
    mine = p.descriptors[('lfo-rnd', 'lr_a')].to_bytes(4, 'big')
    assert [int.from_bytes(e[4:], 'big') & store.LFO_FLAG for e in p.code_entries if e[:4] == mine] == [store.LFO_FLAG]
    p2 = place(base(live=True), p.state, DUCK)                      # lfo-rnd gone: the core's field parks too
    assert p2.lfo_dsts == {} and any(x[0] == ('core', store.LOCK_DST) for x in p2.parked)
    p3 = place(base(live=True), p2.state, DUCK, LFO)                # and comes back to its bits
    assert p3.lfo_dsts == {'sound': d}
    p = place(base(live=True), None, KLFO)
    assert set(p.lfo_dsts) == {'kit'}
    # the flashed build places it itself: the +Drive needs none
    p = place(base(live=True, codes=[(1 << 28 | 3 << 22, 42 | store.LFO_FLAG)]), None, LFO)
    assert p.lfo_dsts == {} and int.from_bytes(p.code_entries[0][4:], 'big') & store.LFO_FLAG


def test_the_core_s_lfo_field_counts_in_the_room():
    assert ds.fit(base(), ds.Want([LFO, VDAC])) == []                # 16 + 39 of 56 sound bits
    assert ds.fit(base(live=True), ds.Want([LFO, VDAC])) == [('voice-dac', 'sound', 6)]    # and 7 more


def test_ccs_come_from_the_pool_s_end_and_a_kit_field_skips_the_fx_channel_s():
    pool = store.DEVICES[DEV]['ccs']
    p = place(base(ccs=[(0, 119)]), None, DUCK, mod('s', f('s1', 'sound', 1, lock=True, cc=True)))
    ccs = {e[:4]: int.from_bytes(e[4:], 'big') for e in p.cc_entries}
    assert ccs == {p.descriptors[('duck', 'duck_src')].to_bytes(4, 'big'): 116,     # 119 is the build's
                   p.descriptors[('s', 's1')].to_bytes(4, 'big'): 115}
    kit = mod('kitcc', f('kc', 'kit', 1, lock=True, cc=True))
    taken = [(0, cc) for cc in pool if cc not in (84, 85)]               # only the FX channel's are left
    p = place(base(ccs=taken), None, kit)
    assert p.cc_entries == [] and any('no CC left' in n for n in p.notes)


def test_fit_refuses_the_last_mod_in_id_order_scope_by_scope():
    b = base()
    assert ds.fit(b, ds.Want([DUCK, FX])) == [('fx-sends', 'kit', 17)]
    assert ds.fit(b, ds.Want([FX, LFO, VDAC])) == [('voice-dac', 'sound', 6)]
    assert ds.fit(base(used={'kit': 40}), ds.Want([DUCK])) == [('duck', 'kit', 8)]
    assert ds.fit(base(spare=0), ds.Want([LFO])) == [('lfo-rnd', store.CODES_TABLE, 1)]
    assert ds.fit(base(spare=0), ds.Want([VDAC])) == []
    try:
        place(b, None, DUCK, FX)
        raise AssertionError('placed a set that does not fit')
    except ModError as e:
        assert 'fx-sends does not fit' in str(e)


def test_a_bad_store_reads_as_none():
    good = place(base(), None, DUCK).bytes
    assert ds.decode(good) is not None
    for bad in (None, b'', good[:-1], good + b'\0', b'XLST' + good[4:],
                good[:0x20] + bytes([good[0x20] ^ 1]) + good[0x21:]):
        assert ds.decode(bad) is None


def test_a_record_that_does_not_fit_the_build_is_placed_again():
    p1 = place(base(), None, DUCK)
    st = ds.decode(p1.bytes)
    st['fields'][0]['offset'] = 60                                        # past the room, as a bad write
    p2 = place(base(), st, DUCK)
    assert p2.generations['kit'] == 2 and where(p2) == where(p1)


def test_handles_get_their_table_entries():
    h = mod('sidecar', f('sc_h', 'project', 16, handle=True))
    p = place(base(), None, h)
    assert p.handle_entries == [p.descriptors[('sidecar', 'sc_h')].to_bytes(4, 'big')]


def test_a_descriptor_alone_fits_but_does_not_place():
    b = base()
    b.codes = b.ccs = None
    b.flashed_id = None
    assert ds.fit(b, ds.Want([DUCK])) == []
    try:
        place(b, None, DUCK)
        raise AssertionError('placed without the build\'s tables')
    except ModError as e:
        assert 'descriptor alone' in str(e)


def test_a_real_build():
    path = os.environ.get('ELEKLOADER_DRIVE_BUILD')
    if not path:
        print('skipped: ELEKLOADER_DRIVE_BUILD is not set')
        return
    build = drive.load_build(path)
    b = ds.Base.from_build(build)
    assert b.flashed_id and b.codes is not None
    alone = ds.Base.from_build(drive.load_build(desc.encode(build.d)))
    assert alone.scopes == b.scopes and alone.ncodes == len(b.codes)
    w = ds.Want([LFO])
    assert ds.fit(alone, w) == ds.fit(b, w)


if __name__ == '__main__':
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith('test_') and callable(fn):
            fn()
            n += 1
    print('%d tests passed' % n)
