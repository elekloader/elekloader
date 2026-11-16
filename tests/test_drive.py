# SPDX-License-Identifier: GPL-2.0-or-later
"""The +Drive tools (elekloader/drive.py, container.py, datarpc.py): the build-checker, the boot
linker's twin, the data container and `elekloader mods` (pytest, or run with python). Needs no
firmware, except the tests at the end, which take the owner's files:

    ELEKLOADER_AR2_SYX=<stock Rytm MKII 1.74 .syx> ELEKLOADER_CORE_AR2=<core-ar2 0.12 .elemod>
        [ELEKLOADER_DRIVE_MODS=<folder with drive-hello and drive-hello-user .elemod>]
        a real build of core-ar2 0.12 and a loader stub, and the example mods linked into it
    ELEKLOADER_AR2_DATA=<a stock project download, as the Data router sends it>
        the container round trip

The made-up build is test_link_refusals.Bench's (a release of nops of the real Rytm profile): a
core with ev_tick, ev_draw, ev_x (one spare entry) and the closed core_handles; a loader stub that
imports core_desc and exports a small staging buffer; and a flashed mod `flash-a` with an ev_draw
entry at order 60. Every refusal code the host can meet has a fixture that is refused and one that
loads (ROWS), and test_every_code_has_a_fixture fails for a code without them.
"""
import dataclasses
import json
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import container, datarpc, desc, devices, drive, link   # noqa: E402
from elekloader.elemod import ModError                                    # noqa: E402
from test_link_refusals import Bench                                      # noqa: E402

STAGING = 0x10000
# the made-up core's std map: ev_boot to a table of another name, ev_tick to its own, ev_closed to the
# closed core_handles (std1's names are the core's to choose; these only test the mapping)
STD = {'ev_boot': 'ev_x', 'ev_tick': 'ev_tick', 'ev_closed': 'core_handles'}


def flashed(B, pad=0, store=False, stage_top=False, payload_skip=False, abs16=False, std_map=False,
            collections=False, claims=False):
    core = B.core(collections={'ev_tick': {'entry': 4}, 'ev_draw': {'entry': 4},
                               'ev_x': {'entry': 4, 'spare': 1}, 'core_handles': {'entry': 4}})
    core['symbols']['core_zero'] = ['.boot', 0]
    core['exports'] = ['core_fn', 'core_zero']
    if store:                                   # +Drive storage: core-ar2 0.14's side of it
        core['imports'] = ['__store_gen', '__store_gen2']
        core['collections'].update({k: {'entry': e} for k, e in (('core_store_ids', 4), ('core_lock_codes', 8),
                                                                 ('core_lock_ccs', 8), ('core_field_inits', 4))})
        core['sections']['.bss'] = {'align': 4, 'size': 16}
        core['symbols']['core_store_gens'] = ['.bss', 0]
        core['symbols']['core_lfo_dsts'] = ['.bss', 8]
        core['exports'] += ['core_store_gens', 'core_lfo_dsts']
    loader = B.other('drive-loader', imports=['core_desc'],
                     relocs=[['.run', 4, 'abs32', 'sym:core_desc', 0]])
    loader['sections']['.bss'] = {'size': STAGING + pad}   # pad: a bigger build, a smaller area
    loader['symbols'].update({'drive_staging': ['.bss', 0], 'drive_staging_size': ['abs', STAGING]})
    loader['exports'] = ['drive_staging', 'drive_staging_size']
    if stage_top:                               # the loader stages files at the area's top (ABI 1.3)
        loader['symbols']['drive_stage_top'] = ['abs', 1]
        loader['exports'].append('drive_stage_top')
    if payload_skip:                            # and stages them without a .payload's hex
        loader['symbols']['drive_payload_skip'] = ['abs', 1]
        loader['exports'].append('drive_payload_skip')
    if abs16:                                   # and its boot linker takes abs16 relocations
        loader['symbols']['drive_abs16'] = ['abs', 1]
        loader['exports'].append('drive_abs16')
    if std_map:                                 # the core's std map, and a loader that maps std1 with it
        core['std'] = STD
        loader['symbols']['drive_std_map'] = ['abs', 1]
        loader['exports'].append('drive_std_map')
    if collections:                             # and links +Drive mods' own tables (the A4's)
        loader['symbols']['drive_collections'] = ['abs', 1]
        loader['exports'].append('drive_collections')
    fa = B.mod('flash-a', resources={'names': ['settings:A']})
    fa['contribute'] = [{'to': 'ev_draw', 'order': 60, 'data': '00000000',
                         'relocs': [[0, 'abs32', 'sym:fn_flash-a', 0]]}]
    fa['symbols']['flash_fn'] = ['.run', 4]
    fa['exports'] = ['flash_fn']
    fa['sites'] = [B.jmp_site(B.at(0x2000), 'flash_fn')]   # in the site map, for the +Drive sites' rows
    if claims:                                  # a boot linker that takes claims, and a flashed claim
        loader['symbols']['drive_claims'] = ['abs', 1]
        loader['exports'].append('drive_claims')
        fa['contribute'][0]['claims'] = [[B.at(0x2800), B.at(0x2810)]]
    return [core, loader, fa]


def built(B, area=None):
    """The flashed build, and the Build the boot linker sees; with `area`, its loader's .bss grown
    until the +Drive area is about that many bytes (its pools are sized to the area)."""
    top = area in ('top', 'skip', 'a4') or isinstance(area, tuple)   # 'top', or ('top', N): a loader that
    kind = area[0] if isinstance(area, tuple) else area   # stages files at the area's top, and its area;
    skip = kind in ('skip', 'a4')                    # 'skip', ('skip', N): one that also skips payloads;
    abs16 = coll = kind == 'a4'                      # 'a4', ('a4', N): and takes abs16 and +Drive
                                                     # collections (the A4's)
    if isinstance(area, tuple):
        area = area[1]
    elif area in ('top', 'skip', 'a4'):
        area = None
    if top:
        out = link.link([link.Mod2(d, d['id']) for d in flashed(B, stage_top=True, payload_skip=skip,
                                                                 abs16=abs16, collections=coll)], B.img)
        b = drive.load_build(out.image)
        if area is not None:
            out = link.link([link.Mod2(d, d['id']) for d in flashed(B, b.hi - b.lo - area, stage_top=True,
                                                                     payload_skip=skip, abs16=abs16, collections=coll)], B.img)
            b = drive.load_build(out.image)
        return out, b
    if area == 'claims':                        # a build whose boot linker takes contributions' claims
        out = link.link([link.Mod2(d, d['id']) for d in flashed(B, claims=True)], B.img)
        return out, drive.load_build(out.image)
    if area == 'std':                           # a build whose loader maps std1 subscriptions
        out = link.link([link.Mod2(d, d['id']) for d in flashed(B, std_map=True)], B.img)
        return out, drive.load_build(out.image)
    if area == 'store':                         # a build that keeps +Drive storage
        out = link.link([link.Mod2(d, d['id']) for d in flashed(B, store=True)], B.img)
        return out, drive.load_build(out.image)
    out = link.link([link.Mod2(d, d['id']) for d in flashed(B)], B.img)
    b = drive.load_build(out.image)
    if area is not None:
        out = link.link([link.Mod2(d, d['id']) for d in flashed(B, b.hi - b.lo - area)], B.img)
        b = drive.load_build(out.image)
    return out, b


def dmod(B, ident, **extra):
    """A +Drive mod: 12 bytes of .run (code, then a word it relocates), 8 of .bss, an export."""
    d = {'elemod': 2, 'id': ident, 'version': '1', 'target': B.target, 'requires': ['core'],
         'sections': {'.run': {'align': 4, 'len': 12, 'parts': [['hex', '4e714e714e750000' + '00000000']]},
                      '.bss': {'size': 8}},
         'symbols': {'fn_' + ident: ['.run', 0], ident + '_count': ['.bss', 0]},
         'exports': [ident + '_count'], 'imports': ['core_fn'],
         'relocs': [['.run', 8, 'abs32', 'sym:core_fn', 0], ['.run', 6, 'pc16', 'sym:fn_' + ident, 0]]}
    d.update(extra)
    return d


def hello(B, **extra):
    d = dmod(B, 'drive-hello', **extra)
    d.setdefault('contribute', [{'to': 'ev_tick', 'order': 50, 'data': '00000000',
                                 'relocs': [[0, 'abs32', 'sym:fn_drive-hello', 0]]}])
    return d


def user(B, **extra):
    d = dmod(B, 'drive-hello-user', requires=['core', 'drive-hello'],
             imports=['core_fn', 'drive-hello_count'],
             relocs=[['.run', 8, 'abs32', 'sym:drive-hello_count', 0]])
    d.update({'contribute': [{'to': 'ev_draw', 'order': 10, 'data': '00000000',
                              'relocs': [[0, 'abs32', 'sym:fn_drive-hello-user', 0]]}]})
    d.update(extra)
    return d


def files(*docs):
    out = []
    for d in docs:
        if isinstance(d, tuple):
            out.append(d)
        else:
            out.append((d['id'] + '.elemod', json.dumps(d, sort_keys=True).encode('utf-8')))
    return out


def test_the_two_example_mods_link_as_the_unit_links_them():
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        # the area: what the flashed build leaves of core 0.11's, from a 4 KB boundary
        lo = link._align(out.layout['bss'][1], 0x1000)
        assert (b.ram_end, b.lo, b.hi, b.staging_size) == (out.layout['bss'][1], lo, 0x47FFF000, STAGING)
        r = drive.link(b, files(user(B), hello(B)))
        assert [c.state for c in r.choices] == [drive.LOADED] * 2, [c.why() for c in r.choices]
        assert [c.id for c in r.live] == ['drive-hello', 'drive-hello-user']
        # .run of each in id order from the area's start, then each .bss
        h, u = lo, lo + 0xC
        assert r.base[('drive-hello', '.run')] == h and r.base[('drive-hello-user', '.run')] == u
        assert r.base[('drive-hello', '.bss')] == lo + 0x18
        assert r.base[('drive-hello-user', '.bss')] == lo + 0x20 and r.end == lo + 0x28
        core_fn = out.map['core_fn']
        assert r.area[:12] == bytes.fromhex('4e714e714e75fffa') + struct.pack('>I', core_fn)
        assert r.area[12:24] == bytes.fromhex('4e714e714e750000') + struct.pack('>I', lo + 0x18)
        assert len(r.area) == r.end - b.lo and r.area[24:] == bytes(16)
        # ev_draw: the +Drive entry at order 10 before the flashed one at 60, then zeros
        t = r.tables['ev_draw']
        assert t['entries'] == [('drive-hello-user', 10), ('flash-a', 60)]
        a, n, e = out.tables['ev_draw']
        assert t['address'] == a and len(t['bytes']) == 4 * (t['capacity'] + 1)
        assert t['bytes'][:8] == struct.pack('>II', u, out.map['flash-a:fn_flash-a'])
        assert t['bytes'][8:] == bytes(len(t['bytes']) - 8)
        assert r.tables['ev_tick']['bytes'][:8] == struct.pack('>II', h, 0)
        assert sorted(r.tables) == ['ev_draw', 'ev_tick']          # untouched tables aren't listed
        st, dt = drive.records(r)
        assert st[:4] == bytes((1, 0, 0, 0)) and st[4:4 + 17] == b'drive-hello-user\0'
        assert st[32:36] == bytes((1, 0, 0, 0)) and len(st) == len(dt) == 2 * 32   # one per line


def test_the_same_inputs_give_the_same_bytes():
    """Link twice, in fresh objects, and compare every file the twin writes."""
    with Bench('analog-rytm-mk2') as B:
        out, _b = built(B)
        got = []
        for _ in range(2):
            b = drive.load_build(bytes(out.image))
            r = drive.link(b, files(user(B), hello(B), ('bad.elemod', b'{')))
            with tempfile.TemporaryDirectory() as tmp:
                paths = drive.write_layout(r, tmp)
                got.append({os.path.relpath(p, tmp): open(p, 'rb').read() for p in paths})
        assert got[0] == got[1]
        assert sorted(got[0]) == ['area.bin', 'detail.bin', 'status.bin', 'status.txt', 'summary.json',
                                  os.path.join('tables', 'ev_draw.bin'), os.path.join('tables', 'ev_tick.bin')]


def test_a_build_file_alone_checks_and_lays_out_but_has_no_table_bytes():
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        blk = out.image[desc.find(out.image)[0]:]
        only = drive.load_build(blk[:struct.unpack_from('>I', blk, 8)[0]])
        r = drive.link(only, files(hello(B), user(B)))
        full = drive.link(b, files(hello(B), user(B)))
        assert r.area == full.area and r.tables['ev_draw']['bytes'] is None
        assert r.tables['ev_draw']['entries'] == full.tables['ev_draw']['entries']
        wrapped = container.wrap(blk[:struct.unpack_from('>I', blk, 8)[0]], 2, 5, 0x30313736)
        assert drive.load_build(wrapped).crc == b.crc


def test_core_desc_and_table_counts_are_imports_a_mod_may_take():
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        d = hello(B, imports=['core_desc', 'ev_draw_n', 'ev_draw'],
                  relocs=[['.run', 8, 'abs32', 'sym:ev_draw_n', 0]])
        u = user(B)
        r = drive.link(b, files(d, u))
        assert all(c.state == drive.LOADED for c in r.choices), [c.why() for c in r.choices]
        assert r.area[8:12] == struct.pack('>I', 2)               # flash-a's and the user's entry
        assert b.desc_at == out.map['core_desc']


def test_drive_init_is_each_mods_own_and_never_an_export():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        h = hello(B)
        u = user(B)
        for d, at in ((h, 0), (u, 2)):
            d['symbols']['drive_init'] = ['.run', at]
            d['exports'] = d['exports'] + ['drive_init']
        r = drive.link(b, files(h, u))
        assert all(c.state == drive.LOADED for c in r.choices), [c.why() for c in r.choices]
        assert r.inits == [('drive-hello', b.lo), ('drive-hello-user', b.lo + 0xE)]
        assert 'drive_init' not in r.glob


def test_active_reads_as_the_boot_linker_reads_it():
    text = drive.active_text([('a.elemod', b'x'), ('b c.elemod', b'')])
    assert text == 'a.elemod 8cdc1683\nb c.elemod 00000000\n'
    assert drive.parse_active(text) == [('a.elemod', 0x8cdc1683), ('b c.elemod', 0)]
    assert drive.parse_active('# x\n\n  a.elemod   0x1F \r\nnocrc\nb.elemod zz\n') == [('a.elemod', 0x1f)]
    assert drive.parse_active(''.join('m%d.elemod 1\n' % i for i in range(257))) is None
    assert len(drive.parse_active(''.join('m%d.elemod 1\n' % i for i in range(256)))) == 256
    # paths below /mods, folders included (decided 2026-11-08), to 127 bytes
    deep = 'drums/kicks/' + 'k' * (127 - 12 - 7) + '.elemod'
    assert len(deep) == 127 and drive.parse_active('%s 1\n%sx 2\nfx/a b/c.elemod 3\n' % (deep, deep)) == \
        [(deep, 1), ('fx/a b/c.elemod', 3)]
    assert drive.active_text([('a/b.elemod', b'x')]).startswith('a/b.elemod ')
    for bad in ('#x', ' a', 'a\nb', ''):
        try:
            drive.active_text([(bad, b'')])
            raise AssertionError(bad)
        except ModError:
            pass


# ---- every refusal: one fixture refused, one that loads ------------------------------------------

ROWS = {}


def row(code, line=0, detail=None, arg=None, deep=False, area=None):
    """A fixture: f(B) -> (bad files, good files); line `line` of the bad set gets `code`, on
    built(B, area)."""
    def deco(f):
        ROWS[code] = (f, line, detail, arg, deep, area)
        return f
    return deco


@row('too-big')
def _(B):
    return files(hello(B, description='x' * STAGING)), files(hello(B, description='x'))


@row('json')
def _(B):
    return files(('bad.elemod', b'{"elemod": 2,')), files(hello(B))


@row('format')
def _(B):
    return files(hello(B, version=1)), files(hello(B))


@row('target', 0)
def _(B):
    t = dict(B.target, os='1.73')
    return files(hello(B, target=t)), files(hello(B))


@row('feature', detail='collections')
def _(B):
    return files(hello(B, collections={'ev_mine': {'entry': 4}})), files(hello(B, collections={}))


@row('clash', detail='%08x flash-a' % 0x40002402, arg=drive.SITE_CORE << 8)
def _(B):
    return files(hello(B, sites=[B.jmp_site(B.at(0x2002), 'fn_drive-hello')])), \
        files(hello(B, sites=[B.jmp_site(B.at(0x1000), 'fn_drive-hello')]))


@row('duplicate', 1, detail='drive-hello')
def _(B):
    return files(hello(B), hello(B)), files(hello(B))


@row('core', detail='2')
def _(B):
    return files(hello(B, resources={'core': '2'})), files(hello(B, resources={'core': '1'}))


@row('requires', detail='drive-hello')
def _(B):
    return files(user(B)), files(user(B), hello(B))


@row('conflicts', detail='flash-a')
def _(B):
    return files(hello(B, conflicts=['flash-a'])), files(hello(B, conflicts=['flash-b']))


@row('export', detail='flash_fn')
def _(B):
    d = hello(B)
    d['symbols']['flash_fn'] = ['.run', 0]
    d['exports'] = ['flash_fn']
    g = hello(B)
    g['symbols']['flash_fn2'] = ['.run', 0]
    g['exports'] = ['flash_fn2']
    return files(d), files(g)


@row('claim', detail='settings:A')
def _(B):
    return files(hello(B, resources={'names': ['settings:A']})), files(hello(B, resources={'names': ['settings:B']}))


@row('import', detail='nothing_here')
def _(B):
    return files(hello(B, imports=['core_fn', 'nothing_here'])), \
        files(hello(B, imports=['core_fn', 'nothing_here'], weak=['nothing_here']))


@row('table', detail='core_handles')
def _(B):
    c = [{'to': 'core_handles', 'order': 50, 'data': '00000000', 'relocs': []}]
    return files(hello(B, contribute=c)), files(hello(B, contribute=[dict(c[0], to='ev_tick')]))


@row('full', 1, detail='ev_x', arg=1)
def _(B):
    c = {'to': 'ev_x', 'order': 50, 'data': '00000000', 'relocs': []}
    return files(hello(B, contribute=[c]), user(B, contribute=[c])), \
        files(hello(B, contribute=[c]), user(B))


@row('room', arg=1)
def _(B):
    _out, b = built(B)
    room = b.hi - b.lo - 12
    return files(hello(B, sections={'.run': dmod(B, 'x')['sections']['.run'], '.bss': {'size': room + 1}})), \
        files(hello(B, sections={'.run': dmod(B, 'x')['sections']['.run'], '.bss': {'size': room}}))


@row('range', deep=True)
def _(B):
    far = [['.run', 6, 'pc16', 'sym:core_fn', 0]]
    return files(hello(B, relocs=far)), files(hello(B, relocs=[['.run', 6, 'pc32', 'sym:core_fn', 0]][:0]))


@row('limit', area=0xC0000)
def _(B):
    _out, b = built(B, area=0xC0000)           # 1 MB of pools: one file can pass them
    many, some = hello(B), hello(B)
    for i in range(b.pools['syms']):
        many['symbols']['s%d' % i] = ['abs', i]
    for i in range(1000):
        some['symbols']['s%d' % i] = ['abs', i]
    return files(dict(many, exports=sorted(many['symbols']))), files(dict(some, exports=sorted(some['symbols'])))


def test_the_boot_linkers_other_rooms_refuse_as_limit():
    """The names' arena and lists, shared by every file and not given back by a refused one, and
    one file's own symbols in pass 2."""
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B, area=0xC0000)       # 1 MB of pools: one file can pass them
        assert b.pools['locals'] == drive.POOL_PER_MB['locals']
        big = ['n%014d' % i for i in range(b.pools['arena'] // 16)]   # 16 bytes each: past the arena
        assert len(big) < b.pools['lists']                            # ... before the lists
        half = len(big) // 2
        first = hello(B, resources={'names': big[:half]}, exports=['not_a_symbol'])
        r = drive.check(b, files(first, hello(B, resources={'names': big[half:]})))
        assert [(c.code, c.note) for c in r.choices] == [
            (drive.CODE['format'], 'exports not_a_symbol'), (drive.CODE['limit'], 'resources.names')], \
            [c.why() for c in r.choices]
        assert drive.check(b, files(hello(B, resources={'names': big[half:]}))).choices[0].state == drive.LOADED
        locs = hello(B)
        for i in range(b.pools['locals']):
            locs['symbols']['s%d' % i] = ['abs', i]
        assert drive.check(b, files(locs)).choices[0].state == drive.LOADED     # pass 1 cannot tell
        r = drive.link(b, files(locs))
        assert (r.choices[0].code, r.skip) == (drive.CODE['limit'], 'none'), r.choices[0].why()


def std_mod(B, event, ident='drive-std', **extra):
    """A std1 +Drive mod subscribing to `event` (\"std\": true)."""
    d = dmod(B, ident, **dict({'events': 'std1'}, **extra))
    d.setdefault('contribute', [{'to': event, 'std': True, 'order': 50, 'data': '00000000',
                                 'relocs': [[0, 'abs32', 'sym:fn_' + ident, 0]]}])
    return d


@row('std', detail='ev_nope', area='std')
def _(B):
    return files(std_mod(B, 'ev_nope')), files(std_mod(B, 'ev_boot'))


@row('count')
def _(B):
    good = [dmod(B, 'm%03d' % i) for i in range(256)]
    return files(*(good + [dmod(B, 'm256')])), files(*good)


@row('store', 1, detail='sound 60 56', arg=0, deep=True, area='store')
def _(B):
    a = dmod(B, 'st-a', storage=[{'sym': 'st_a', 'scope': 'sound', 'bits': 32, 'lock': True}])
    b = dmod(B, 'st-b', storage=[{'sym': 'st_b', 'scope': 'sound', 'bits': 28}])
    return files(a, b), files(a)


def test_every_refusal_has_a_fixture_that_is_refused_and_one_that_loads():
    with Bench('analog-rytm-mk2') as B:
        builds = {}
        for code, (f, line, detail, arg, deep, area) in sorted(ROWS.items()):
            if area not in builds:
                builds[area] = built(B, area)[1]
            b = builds[area]
            bad, good = f(B)
            run = drive.link if deep else drive.check
            rb, rg = run(b, bad), run(b, good)
            c = rb.choices[line]
            assert c.code == drive.CODE[code], (code, c.why(), [x.why() for x in rb.choices])
            if detail is not None:
                assert c.detail == detail, (code, c.detail)
            if arg is not None:
                assert c.arg == arg, (code, c.arg)
            assert all(x.state == drive.LOADED for x in rg.choices), (code, [x.why() for x in rg.choices])
            if deep:
                continue
            # the twin agrees with the checker on every row
            rl = drive.link(b, bad)
            assert [x.code for x in rl.choices] == [x.code for x in rb.choices], code


TELOS_CODES = {'site', 'flashed', 'stock', 'region', 'outside'}   # test_telos_sites_and_regions has theirs


def test_every_code_has_a_fixture():
    host = set(drive.CODE) - set(drive.BOOT_ONLY) - {'ok'} - TELOS_CODES
    assert host == set(ROWS), sorted(host ^ set(ROWS))


def test_std1_subscriptions_go_through_the_cores_std_map():
    """A build whose loader exports drive_std_map (F_STD_MAP) carries the core's std map as claims of
    kind 3, and the twin sends a std1 mod's "std": true contribution to the table it names, as the
    linker does at build; an unmapped event is a `std` refusal, a mapped one to a closed table a
    `table` one. Without the flag the descriptor and the twin are as before."""
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B, 'std')
        d = desc.decode(out.image, desc.find(out.image)[0])
        assert d['flags'] & desc.F_STD_MAP
        assert [(k, n) for k, _mi, n in d['claims'] if k == desc.CLAIM_STD] == \
            [(desc.CLAIM_STD, '%s %s' % x) for x in sorted(STD.items())]
        assert b.std_map and b.std == STD
        r = drive.link(b, files(std_mod(B, 'ev_boot'), std_mod(B, 'ev_tick', 'drive-std2')))
        assert all(c.state == drive.LOADED for c in r.choices), [c.why() for c in r.choices]
        assert [x['to'] for c in r.choices for x in c.mod.contribute] == ['ev_x', 'ev_tick']
        # ev_x had one spare: its new entry is the +Drive mod's
        assert len(r.tables['ev_x']['entries']) == b.tables[b.table['ev_x']]['count'] + 1
        # the refusals
        for doc, code, detail in (
                (std_mod(B, 'ev_nope'), 'std', 'ev_nope'),
                (std_mod(B, 'ev_closed'), 'table', 'core_handles'),
                (std_mod(B, 'ev_x'), 'std', 'ev_x'),           # a table's name is not a standard event
                (std_mod(B, 'ev_boot', events=None), 'format', None),
                (std_mod(B, 'ev_boot', events='std2'), 'format', None),
                (std_mod(B, 'ev_boot', contribute=[{'to': 'ev_boot', 'std': False, 'data': '00000000'}]),
                 'format', None)):
            c = drive.check(b, files(doc)).choices[0]
            assert (c.code, detail is None or c.detail == detail) == (drive.CODE[code], True), (code, c.why())
        # a native contribution beside the map is as before
        assert drive.check(b, files(hello(B))).choices[0].state == drive.LOADED
        # without the flag: no claims of kind 3, and "std" is not read (ev_boot is no table)
        out0, b0 = built(B)
        d0 = desc.decode(out0.image, desc.find(out0.image)[0])
        assert not d0['flags'] & desc.F_STD_MAP and all(k != desc.CLAIM_STD for k, _m, _n in d0['claims'])
        c = drive.check(b0, files(std_mod(B, 'ev_boot'))).choices[0]
        assert (c.code, c.detail) == (drive.CODE['table'], 'ev_boot'), c.why()


def test_telos_sites_and_regions():
    """POC: on a build whose descriptor carries the stock shadow (the Digitone mk1, drive_shadow), the twin
    takes sites and regions as TELOS's boot linker (DL_SITES, DL_REGIONS): a site over the flashed build's
    is refused 22, one whose bytes are not stock's 24, the later of two overlapping 21; a region over a
    flashed mod's or an earlier mod's 27; and a stock part over a flashed site copies stock's bytes."""
    with Bench('digitone-mk1') as B:
        core = B.core(version='5.0', collections={'ev_tick': {'entry': 4}, 'ev_x': {'entry': 4}})
        loader = B.other('telos', imports=['core_desc'], relocs=[['.run', 4, 'abs32', 'sym:core_desc', 0]])
        fa = B.mod('flash-a', sites=[B.jmp_site(B.at(0x1000), 'fn_flash-a')],
                   resources={'regions': [{'name': 'flash-a buf', 'lo': '0x45000000', 'hi': '0x45100000'}]})
        out = link.link([link.Mod2(d, d['id']) for d in (core, loader, fa)], B.img)
        b = drive.load_build(out.image)
        assert b.telos and b.shadow and b.regions == [(0x45000000, 0x45100000, 'flash-a')]

        def codes(*docs):
            return [(c.code, c.detail) for c in drive.check(b, files(*docs)).choices]
        site = lambda off: B.jmp_site(B.at(off), 'fn_drive-hello')
        assert codes(hello(B, sites=[site(0x2000)])) == [(0, '')]
        assert codes(hello(B, sites=[site(0x1000)])) == [(drive.CODE['flashed'], '0x%08x' % B.at(0x1000))]
        bad = dict(site(0x2000), stock_sha256='00' * 32)
        assert codes(hello(B, sites=[bad])) == [(drive.CODE['stock'], '0x%08x' % B.at(0x2000))]
        later = dmod(B, 'later', sites=[dict(site(0x2002), relocs=[[2, 'abs32', 'sym:fn_later', 0]])])
        assert codes(hello(B, sites=[site(0x2000)]), later) == [(0, ''), (drive.CODE['site'], 'drive-hello')]
        reg = lambda lo, hi: {'regions': [{'name': 'r', 'lo': hex(lo), 'hi': hex(hi)}]}
        assert codes(hello(B, resources=reg(0x45100000, 0x45200000))) == [(0, '')]
        assert codes(hello(B, resources=reg(0x450f0000, 0x45110000))) == [(drive.CODE['region'], 'flash-a')]
        assert codes(hello(B, resources=reg(0x43f00000, 0x44100000))) == [(drive.CODE['outside'], '0x43f00000')]
        assert codes(hello(B, resources=reg(0x47bf0000, 0x47c00000))) == [(0, '')]   # the ddr area
        assert codes(hello(B, resources=reg(0x45100000, 0x45200000)),
                     dmod(B, 'later', resources=reg(0x451f0000, 0x45300000))) == \
            [(0, ''), (drive.CODE['region'], 'drive-hello')]
        # a stock part over flash-a's site: stock's bytes (the bench's nops), not the jmp the build wrote
        d = hello(B)
        d['sections']['.run']['parts'] = [['hex', '4e714e714e750000'], ['stock', '0x%08x' % B.at(0x1000), 4]]
        d['relocs'] = [r for r in d['relocs'] if r[1] != 8]      # its word at +8 is the stock part here
        r = drive.link(b, files(d))
        assert r.choices[0].code == 0 and r.area[8:12] == B.img[0x1000:0x1004] != out.image[0x1000:0x1004]
    with Bench('analog-rytm-mk2') as B:                  # no shadow: the boot-linked sites (DRIVE.md, "Sites")
        _out, b = built(B)
        assert not b.telos and b.main_hi
        r = drive.check(b, files(hello(B, sites=[B.jmp_site(B.at(0x1000), 'fn_drive-hello')])))
        assert r.choices[0].state == drive.LOADED, r.choices[0].why()


def test_each_feature_and_section_a_drive_mod_cannot_use_is_refused():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        for field, value in (('collections', {'ev_mine': {'entry': 4}}),
                             ('storage', [{'sym': 'st', 'scope': 'kit', 'bits': 1}]), ('sharc', {})):
            r = drive.check(b, files(hello(B, **{field: value})))
            want = 'sharc' if field == 'sharc' else field
            if field == 'sharc':                # an empty object is absent, as the loader reads it
                assert r.choices[0].code != drive.CODE['feature'] or r.choices[0].detail == want
                continue
            assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], field), r.choices[0].why()
        for sec in ('.boot', '.fast', '.payload'):
            d = hello(B)
            d['sections'][sec] = {'align': 4, 'len': 2, 'parts': [['hex', '4e75']]}
            r = drive.check(b, files(d))
            assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], sec)
        r = drive.check(b, files(hello(B, resources={'regions': [{'name': 'r', 'lo': 1, 'hi': 2}]})))
        assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], 'regions')
        c = [{'to': 'ev_tick', 'order': 50, 'data': '00000000', 'relocs': [], 'claims': [[1, 2]]}]
        r = drive.check(b, files(hello(B, contribute=c)))
        assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], 'claims')


def test_sites_load_and_each_clash_is_refused():
    """+Drive sites (ABI 1.2's site map): one loads, relocated to its mod's code; one over a flashed
    site, in a pre-link range or not over its stock bytes is refused, as is the later of two +Drive
    mods whose sites (or a site and a copied block) share a byte; two copied blocks may."""
    with Bench('analog-rytm-mk2') as B:
        pre = (B.at(0x3000), B.at(0x3010), desc.PRE_CODE, 'test')
        B.dev.releases['SYN'] = B.rel = dataclasses.replace(B.rel, prelink=(pre,))
        docs = flashed(B)
        out = link.link([link.Mod2(d, d['id']) for d in docs], B.img)
        b = drive.load_build(out.image)
        assert b.main_hi == B.at(len(B.img)), hex(b.main_hi)
        bad = B.jmp_site(B.at(0x1000), 'fn_drive-hello')
        bad['stock_sha256'] = '00' * 32
        assert [(c.code, c.arg, c.detail) for c in drive.check(b, files(hello(B, sites=[bad]))).choices] == \
            [(drive.CODE['clash'], drive.SITE_STOCK << 8, '%08x' % B.at(0x1000))]
        blk = drive.load_build(out.image[desc.find(out.image)[0]:])   # the descriptor alone: no stock bytes
        assert drive.check(blk, files(hello(B, sites=[bad]))).choices[0].state == drive.LOADED
        site = B.jmp_site(B.at(0x1000), 'fn_drive-hello')
        r = drive.link(b, files(hello(B, sites=[site])))
        assert r.choices[0].state == drive.LOADED, r.choices[0].why()
        assert r.sites == [('drive-hello', 0, B.at(0x1000), bytes.fromhex('4ef9') + struct.pack('>I', b.lo))], r.sites

        def why(*docs, deep=False):
            r = (drive.link if deep else drive.check)(b, files(*docs))
            return [(c.code, c.arg, c.detail) if c.state != drive.LOADED else 0 for c in r.choices]
        S = drive.CODE['clash']
        assert why(hello(B, sites=[B.jmp_site(B.at(0x2002), 'fn_drive-hello')])) == \
            [(S, drive.SITE_CORE << 8, '%08x flash-a' % B.at(0x2002))]
        assert why(hello(B, sites=[B.jmp_site(B.at(0x300c), 'fn_drive-hello')])) == \
            [(S, drive.SITE_PRELINK << 8, '%08x' % B.at(0x300c))]
        assert why(hello(B, sites=[B.jmp_site(B.at(0x3010), 'fn_drive-hello')])) == [0]
        a, z = dmod(B, 'a-mod', sites=[B.jmp_site(B.at(0x1000), 'fn_a-mod')]), \
            dmod(B, 'z-mod', sites=[B.site(B.at(0x1004), 4)])
        assert why(z, a) == [(S, drive.SITE_MOD << 8 | 1, '%08x' % B.at(0x1004)), 0]
        assert drive.message(S, '%08x' % B.at(0x1004), drive.SITE_MOD << 8 | 1) == \
            'site 0x40001404: taken by another +Drive mod (line 2)'
        block = {'lo': B.at(0x1000), 'hi': B.at(0x1100), 'to': 0}
        assert why(dmod(B, 'a-mod', copied=[block]), dmod(B, 'z-mod', sites=[B.site(B.at(0x1000), 4)])) == \
            [0, (S, drive.SITE_MOD << 8, '%08x' % B.at(0x1000))]
        assert why(dmod(B, 'a-mod', copied=[block]), dmod(B, 'z-mod', copied=[block])) == [0, 0]
        assert why(hello(B, copied=[dict(block, lo=B.at(0x1f00), hi=B.at(0x2001))])) == \
            [(S, drive.SITE_CORE << 8, '%08x flash-a' % B.at(0x1f00))]
        for bad in (dict(site, addr=B.at(len(B.img) - 2)), dict(site, len=4), dict(site, kind='both'),
                    dict(site, relocs=[[2, 'abs16', 'sym:fn_drive-hello', 0]]), dict(site, stock_sha256='x' * 64)):
            assert why(hello(B, sites=[bad])) == [(drive.CODE['format'], 0, '')], bad
        assert why(hello(B, copied=[dict(block, hi=block['lo'])])) == [(drive.CODE['format'], 0, '')]
        # after the boot wrote them, the MODS page's check reads through the stock bytes it kept
        r = drive.link(b, files(hello(B, sites=[site])))
        drive.apply_sites(b, r)
        assert b.stock(B.at(0x1000), 6) == r.sites[0][3]
        assert why(hello(B, sites=[site])) == [0]
        # a descriptor without a site map (ABI 1.1) takes no sites, as before
        old = drive.Build(dict(b.d, abi=(1, 1)), b.image, b.desc_at + b._ram)
        r = drive.check(old, files(hello(B, sites=[site])))
        assert (r.choices[0].code, r.choices[0].detail) == (drive.CODE['feature'], 'sites')


def test_of_a_conflicting_pair_the_later_by_id_goes():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        a = hello(B, id='a-mod', conflicts=['b-mod'])
        z = hello(B, id='b-mod')
        for order in ((a, z), (z, a)):
            r = drive.check(b, files(*order))
            st = {c.id: (c.state, c.detail) for c in r.choices}
            assert st == {'a-mod': (drive.LOADED, ''), 'b-mod': (drive.REFUSED, 'a-mod')}


def test_a_refusal_in_pass_2_starts_the_check_again():
    """A mod refused for a pc16 out of range is dropped and the set laid out again: one that
    requires it goes too, and the rest move down."""
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        a = dmod(B, 'a-far', relocs=[['.run', 6, 'pc16', 'sym:core_fn', 0]])
        r = drive.link(b, files(a, hello(B, requires=['a-far'])))
        st = [(c.id, c.code) for c in r.choices]
        assert st == [('a-far', drive.CODE['range']), ('drive-hello', drive.CODE['requires'])]
        r = drive.link(b, files(a, hello(B)))
        assert r.base[('drive-hello', '.run')] == b.lo


def test_a_missing_file_is_dropped():
    with Bench('analog-rytm-mk2') as B:
        _out, b = built(B)
        r = drive.check(b, [('gone.elemod', None)] + files(hello(B)))
        assert (r.choices[0].state, r.choices[0].code) == (drive.DROPPED, drive.CODE['missing'])
        assert r.choices[1].state == drive.LOADED


def test_a_spare_count_sizes_a_table():
    """A collection's "spare" is the room its table keeps for +Drive entries, not the default 16
    (found 2026-11-07: a 17th ev_tick handler was refused `full`)."""
    # the linker gives a table what its collection declares
    with Bench('analog-rytm-mk2') as B:
        docs = flashed(B)
        docs[0]['collections']['ev_tick'] = {'entry': 4, 'spare': 40}
        out = link.link([link.Mod2(d, d['id']) for d in docs], B.img)
        b = drive.load_build(out.image)
        t = b.tables[b.table['ev_tick']]
        assert t['capacity'] == t['count'] + 40, t
        many = [dmod(B, 'm%02d' % i, contribute=[{'to': 'ev_tick', 'order': 50, 'data': '00000000', 'relocs': []}])
                for i in range(41)]
        r = drive.check(b, files(*many))
        assert [c.state for c in r.choices[:40]] == [drive.LOADED] * 40
        assert r.choices[40].code == drive.CODE['full'], r.choices[40].why()



def test_a_max_caps_a_table():
    """A collection's "max" stops its table at that many entries, flashed and spare together."""
    # a "max" counts the flashed entries: the spare ones take only what is left
    with Bench('analog-rytm-mk2') as B:
        docs = flashed(B)
        docs[0]['collections']['ev_tick'] = {'entry': 4, 'spare': 40, 'max': 5}
        out = link.link([link.Mod2(d, d['id']) for d in docs], B.img)
        t = drive.load_build(out.image)
        t = t.tables[t.table['ev_tick']]
        assert t['capacity'] == 5 and t['count'] <= 5, t
        docs[0]['collections']['ev_tick'] = {'entry': 4, 'spare': 40, 'max': 0}
        try:
            link.link([link.Mod2(d, d['id']) for d in docs], B.img)
        except ModError as e:
            assert 'max 0' in str(e), e
        else:
            raise AssertionError('max 0 was taken')


def test_format_md_lists_every_code_and_state():
    with open(os.path.join(ROOT, 'docs', 'FORMAT.md'), encoding='utf-8') as fh:
        doc = fh.read()
    sec = doc[doc.index('### +Drive refusal codes'):]
    for code, (name, _m) in drive.CODES.items():
        assert '| %d | `%s` |' % (code, name) in sec, (code, name)
    for v, name in drive.SKIPS.items():
        assert '| %d | `%s`' % (v, name) in sec, (v, name)


# ---- the data container and the Data router --------------------------------------------------------

def test_the_container_wraps_and_reads_back():
    body = b'{"elemod": 2}'
    raw = container.wrap(body, 2, 5, 0x30313736)
    assert len(raw) == container.HEADER + len(body) + container.FOOTER
    assert raw[:31] == struct.pack('>IBHHIIiiIBB', 0xac11d303, 2, 2, 5, 0x30313736, 0x454d4f44, 2, 0,
                                   len(body), 0, 12)
    assert raw[-12:] == struct.pack('>III', container.crc(body), len(body), 0xaaa1daaa)
    head, got = container.parse(raw)
    assert got == body and (head['family'], head['device'], head['type']) == (2, 5, container.EMOD)
    assert container.rewrap(head, got) == raw
    for i, why in ((40, 'hash'), (len(raw) - 1, 'magic')):
        bad = bytearray(raw)
        bad[i] ^= 1
        try:
            container.parse(bytes(bad))
            raise AssertionError(why)
        except ModError as e:
            assert why in str(e)


def test_the_container_round_trips_a_stock_project_download():
    path = os.environ.get('ELEKLOADER_AR2_DATA')
    if not path:
        print('SKIP: ELEKLOADER_AR2_DATA')
        return
    raw = open(path, 'rb').read()
    head, body = container.parse(raw)
    assert (head['family'], head['device'], head['version']) == (2, 5, 2) and head['compressed']
    assert head['release'] == devices.for_target(devices.target_of(
        *[(d, d.releases['1.74']) for d in devices.devices() if d.key == 'analog-rytm-mk2'][0]))[1].data_release
    assert container.rewrap(head, body) == raw


class FakeUnit:
    """The Drive Loader's /mods route as the Data router answers it, in memory: lists, reads, writes
    and clears; checks each chunk's CRC and the container on close (family 2, type EMOD,
    uncompressed, the hash), as stock and the route do."""

    def __init__(self, build_blk):
        self.files = {'.build': build_blk, '.status': b''}
        self.jobs, self.out, self.nj, self.chunks = {}, [], 1, {}

    def send(self, raw):
        msg = datarpc.unframe(raw)
        seq, cmd, a = struct.unpack_from('>H', msg)[0], msg[4], msg[5:]
        ok, extra = 1, b''
        if cmd == datarpc.LIST:                 # one folder: its folders, then its files
            sub = self.rel(a)
            pre = sub + '/' if sub else ''
            kids = sorted({n[len(pre):].split('/')[0] for n in self.files if n.startswith(pre) and '/' in n[len(pre):]})
            for d in kids:                      # as the route: an item whose children byte is 1
                extra += d.encode('latin-1') + b'\0' + bytes((1, 2)) + struct.pack('>iIHBB', 0, 0, 0, 1, 0)
            for n in sorted(self.files):
                if n.startswith(pre) and '/' not in n[len(pre):]:
                    extra += n[len(pre):].encode('latin-1') + b'\0' + bytes((0, 2)) + struct.pack('>iIHBB', 0, len(self.files[n]), 0, 1, 0)
            extra = bytes(12) + extra
        elif cmd == datarpc.ROPEN:
            name = self.rel(a)
            if name not in self.files:
                ok = 0
            else:
                j = self.nj = self.nj + 1
                body = self.files[name]
                self.jobs[j] = container.wrap(body, 2, 5, 0x30313736)
                self.chunks[j] = struct.unpack_from('>I', a, a.index(b'\0') + 1)[0]   # the size asked for
                extra = struct.pack('>IIB', j, self.chunks[j], 0)
        elif cmd == datarpc.READ:
            j, s = struct.unpack_from('>II', a)
            c = self.chunks[j]
            d = self.jobs[j][s * c:(s + 1) * c]
            last = (s + 1) * c >= len(self.jobs[j])
            extra = struct.pack('>IIIBII', j, s, 0, last, 0, len(d)) + d
        elif cmd in (datarpc.RCLOSE,):
            extra = a[:4] + bytes(4)
        elif cmd == datarpc.WOPEN:
            size = struct.unpack_from('>I', a)[0]
            j = self.nj = self.nj + 1
            self.jobs[j] = [self.rel(a[4:]), bytearray(), size]
            extra = struct.pack('>I', j)
        elif cmd == datarpc.WRITE:
            j, s, crc, n = struct.unpack_from('>IIII', a)
            d = a[16:16 + n]
            if container.crc(d) != crc:
                ok = 0
            else:
                self.jobs[j][1] += d
                extra = struct.pack('>III', j, s, len(self.jobs[j][1]))
        elif cmd == datarpc.WCLOSE:
            j, size = struct.unpack_from('>II', a)
            name, data, _want = self.jobs.pop(j)
            try:
                head, body = container.parse(bytes(data))
                ok = int(head['family'] == 2 and head['type'] == container.EMOD and not head['compressed'])
            except ModError:
                ok = 0
            if ok:
                self.files[name] = body
            extra = struct.pack('>II', j, len(data))
        elif cmd == datarpc.CLEAR:
            name = self.rel(a)
            ok = int(self.files.pop(name, None) is not None)
        self.out.append(datarpc.frame(struct.pack('>HH', 0, seq) + bytes((cmd | 0x80, ok)) + extra))

    @staticmethod
    def rel(a):
        """A request's path, below /mods ('' for /mods itself)."""
        p = a[:a.index(b'\0')].decode('latin-1')           # the route's names are latin-1, as the requests send them
        return p[len('/mods/'):] if p.startswith('/mods/') else ''

    def receive(self, timeout):
        return self.out.pop(0) if self.out else None


class LossyUnit(FakeUnit):
    """A FakeUnit whose port loses the reply to the first read of a chunk (as python-rtmidi on
    Windows loses any SysEx over 1 KB) and that, as stock, refuses to close an unfinished read."""

    def __init__(self, build_blk):
        super().__init__(build_blk)
        self.lose, self.done = 1, set()             # how many read replies to lose

    def send(self, raw):
        msg = datarpc.unframe(raw)
        seq, cmd, a = struct.unpack_from('>H', msg)[0], msg[4], msg[5:]
        if cmd == datarpc.READ and self.lose:
            self.lose -= 1
            return
        if cmd == datarpc.RCLOSE and struct.unpack_from('>I', a)[0] not in self.done:
            self.out.append(datarpc.frame(struct.pack('>HH', 0, seq) + bytes((cmd | 0x80, 0))
                                          + b'Reader did not complete\0'))
            return
        super().send(raw)
        if cmd == datarpc.READ and datarpc.unframe(self.out[-1])[18]:
            self.done.add(struct.unpack_from('>I', a)[0])


class WrappingUnit(FakeUnit):
    """A FakeUnit whose read replies carry stock's per-mille progress word, wrapping as stock's does."""

    def send(self, raw):
        super().send(raw)
        msg = datarpc.unframe(raw)
        if msg[4] == datarpc.READ:
            rep = bytearray(datarpc.unframe(self.out[-1]))
            j, s = struct.unpack_from('>II', msg, 5)
            done = min((s + 1) * self.chunks[j], len(self.jobs[j]))
            struct.pack_into('>I', rep, 14, (done * 1000 & 0xFFFFFFFF) // len(self.jobs[j]))
            self.out[-1] = datarpc.frame(bytes(rep))


def test_a_lost_read_reply_is_the_error_not_the_refused_close():
    """Every read reply lost: after the smaller chunk fails too, the error is the lost reply."""
    with Bench('analog-rytm-mk2') as B:
        out, _b = built(B)
        blk = out.image[desc.find(out.image)[0]:]
        unit = LossyUnit(blk[:struct.unpack_from('>I', blk, 8)[0]])
        mods = datarpc.Mods(datarpc.Client(unit, timeout=0.2))
        unit.lose = 2                                  # both sizes' first replies
        try:
            mods.build()
        except datarpc.RpcError as e:
            assert str(e) == 'reading /mods/.build, chunk 0: no reply from the unit to command 0x55', e
        else:
            raise AssertionError('the lost reply was not an error')
        assert mods.build()[0].crc                     # the next read is whole


class SmallBufferUnit(LossyUnit):
    """A FakeUnit behind a host MIDI input that drops every SysEx message over 1 KB (Firefox's Web
    MIDI, python-rtmidi on Windows): every reply longer than that is lost."""

    def __init__(self, build_blk):
        super().__init__(build_blk)
        self.lose = 0                       # no one-off loss: only the long replies

    def send(self, raw):
        n = len(self.out)
        super().send(raw)
        if len(self.out) > n and len(self.out[-1]) > 1024:
            self.out.pop()


def test_a_read_whose_replies_are_lost_starts_again_with_small_chunks():
    """An 11 KB file (as the unit's .build) through a host that loses SysEx over 1 KB: the 8 KB
    chunk's reply never arrives, so the read starts again with 0x300-byte chunks, and the client
    keeps that size."""
    body = bytes(range(256)) * 44
    unit = SmallBufferUnit(b'')
    unit.files['big.elemod'] = body
    client = datarpc.Client(unit, timeout=0.2)
    mods = datarpc.Mods(client)
    assert mods.get('big.elemod')[1] == body
    assert client.read_chunk == 0x300
    assert mods.get('big.elemod')[1] == body          # whole, and at once this time
    assert client.read_chunk == 0x300


def test_a_loader_that_stages_at_the_top_says_so_in_the_descriptor():
    """ABI 1.3: the loader's export drive_stage_top sets the descriptor's F_STAGE_TOP, and the host's
    twin then lays the set out as the boot does: a file over the staging buffer (now the floor)
    but within the area passes; one over the area is too big."""
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B, 'top')
        assert b.d['abi'] == desc.ABI == (1, 3) and b.d['flags'] == desc.F_STAGE_TOP and b.stage_top
        assert 'files staged at the area\'s top' in desc.dump(b.d)
        _o, plain = built(B)
        assert plain.d['flags'] == 0 and not plain.stage_top
        big = files(hello(B, description='x' * STAGING))
        assert drive.check(plain, big).choices[0].code == drive.CODE['too-big']
        assert drive.check(b, big).choices[0].state == drive.LOADED
        huge = files(hello(B, description='x' * (b.hi - b.lo)))
        assert drive.check(b, huge).choices[0].code == drive.CODE['too-big']


def test_the_live_stage_from_the_status_limits_uploads():
    """`elekloader mods` reads /mods/.status's stage line (route.c): the most one upload or read
    takes on that boot. A file the build takes but the stage can't hold is refused before any
    upload; with no stage line, the build's staging buffer is the limit."""
    import argparse
    assert drive.stage_of('drive-loader route\nstage: 3932160 bytes at 0x40c40000, free tail\n') == 3932160
    assert drive.stage_of('drive-loader route\nlast: -\n') is None
    # drive-loader-a4mk2: its sector store's largest free run (seen in the emulator 2026-11-15)
    assert drive.stage_of('drive-loader-a4mk2 route\nroom: 249229312 bytes in one run, 249229312 free; '
                          'directory generation 0\n') == 249229312
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B, 'top')
        blk = out.image[desc.find(out.image)[0]:]
        unit = FakeUnit(blk[:struct.unpack_from('>I', blk, 8)[0]])
        mods = datarpc.Mods(datarpc.Client(unit, timeout=0.5))
        name, raw = files(hello(B, description='x' * (3 * STAGING)))[0]
        assert drive.upload_room(b, None) == STAGING
        log = []
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, name)
            open(p, 'wb').write(raw)
            unit.files['.status'] = b'drive-loader route\nstage: %d bytes at 0x40c40000, free tail\n' % (len(raw) - 1)
            assert datarpc.run(argparse.Namespace(op='room'), mods, log.append) == 0
            assert log[-1] == 'ROOM %d bytes' % (len(raw) - 1)
            assert datarpc.run(argparse.Namespace(op='put', files=[p]), mods, log.append) == 1
            assert name not in unit.files and 'the unit takes up to %d now' % (len(raw) - 1) in log[-2]
            unit.files['.status'] = b'drive-loader route\nstage: %d bytes at 0x40c40000, free tail\n' % len(raw)
            assert datarpc.run(argparse.Namespace(op='put', files=[p]), mods, log.append) == 0
            assert unit.files[name] == raw
            unit.files['.status'] = b'drive-loader route\n'          # no stage line: the floor
            assert datarpc.run(argparse.Namespace(op='put', files=[p]), mods, log.append) == 1


def test_a_read_ignores_the_wrapping_progress_word():
    """Stock's ReadPartial reports per-mille progress as done * 1000 in 32 bits, which wraps past
    4,294,967 bytes: the client counts the bytes it gets and never reads that word."""
    body = bytes(range(256)) * (4300000 // 256 + 1)
    unit = WrappingUnit(b'')
    unit.files['big.elemod'] = body
    mods = datarpc.Mods(datarpc.Client(unit, timeout=0.5))
    assert mods.get('big.elemod')[1] == body


def test_mods_put_ls_get_active_and_rm_over_the_data_router():
    import argparse
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        blk = out.image[desc.find(out.image)[0]:]
        unit = FakeUnit(blk[:struct.unpack_from('>I', blk, 8)[0]])
        mods = datarpc.Mods(datarpc.Client(unit, timeout=0.5))
        log = []
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for name, raw in files(hello(B), user(B)) + [('big.elemod', b'x' * (STAGING + 1))]:
                p = os.path.join(tmp, name)
                open(p, 'wb').write(raw)
                paths.append(p)
            a = argparse.Namespace(op='put', files=paths, port=None, folder='/mods')
            assert datarpc.run(a, mods, log.append) == 1 and 'big.elemod' not in unit.files
            assert datarpc.run(argparse.Namespace(op='put', files=paths[:2]), mods, log.append) == 0
            assert unit.files['drive-hello.elemod'] == open(paths[0], 'rb').read()
            names = [n for n, _k, _s in mods.ls()]
            assert {'drive-hello.elemod', 'drive-hello-user.elemod', '.build'} <= set(names)
            assert datarpc.run(argparse.Namespace(op='active', names=['drive-hello-user.elemod', paths[0]]),
                               mods, log.append) == 0
            act = drive.parse_active(unit.files['active'].decode())
            assert act == [('drive-hello-user.elemod', drive.file_crc(open(paths[1], 'rb').read())),
                           ('drive-hello.elemod', drive.file_crc(open(paths[0], 'rb').read()))]
            g = os.path.join(tmp, 'got')
            assert datarpc.run(argparse.Namespace(op='get', name='drive-hello.elemod', out=g), mods, log.append) == 0
            assert open(g, 'rb').read() == open(paths[0], 'rb').read()
            assert datarpc.run(argparse.Namespace(op='rm', name='drive-hello.elemod'), mods, log.append) == 0
            assert 'drive-hello.elemod' not in unit.files
            # folders (decided 2026-11-08): an upload into one, the walk, a set from two folders, rm
            assert datarpc.run(argparse.Namespace(op='put', files=paths[:1], into='fx/delays'), mods, log.append) == 0
            assert unit.files['fx/delays/drive-hello.elemod'] == open(paths[0], 'rb').read()
            assert ('fx/delays/drive-hello.elemod', len(unit.files['fx/delays/drive-hello.elemod'])) in mods.walk()
            assert [(n, k) for n, k, _s in mods.ls('fx')] == [('delays', 1)]
            assert datarpc.run(argparse.Namespace(op='active', names=['fx/delays/drive-hello.elemod',
                                                                         'drive-hello-user.elemod']), mods, log.append) == 0
            assert [n for n, _c in drive.parse_active(unit.files['active'].decode())] == \
                ['fx/delays/drive-hello.elemod', 'drive-hello-user.elemod']
            try:
                datarpc.run(argparse.Namespace(op='put', files=paths[:1], into='a/b/c/d'), mods, log.append)
                assert False, 'four folders deep'
            except ModError:
                pass
            assert datarpc.run(argparse.Namespace(op='rm', name='fx/delays/drive-hello.elemod'), mods, log.append) == 0
            assert not any(n.startswith('fx/') for n in unit.files)


class NotingUnit(FakeUnit):
    """A FakeUnit whose .status carries the route's `last:` note (route.c `note`), and whose store,
    as drive-loader-a4mk2's before it made a file's folders at commit, keeps no file whose folder
    isn't there: the upload still closes, the commit fails, and only the note says so."""

    def __init__(self, build_blk, folders=()):
        super().__init__(build_blk)
        self.folders = set(folders)
        self.lose = set()                       # names whose commit fails anyway (a store error)
        self.note('-')

    def note(self, text):
        self.files['.status'] = b'drive-loader-a4mk2 route\nfolder /mods\nlast: %s\nio: 0 0 0 0\n' % text.encode('latin-1')

    def send(self, raw):
        msg = datarpc.unframe(raw)
        if msg[4] == datarpc.WCLOSE:
            j = struct.unpack_from('>I', msg, 5)[0]
            name = self.jobs[j][0]
            had = dict(self.files)
            if not container.parse(bytes(self.jobs[j][1]))[1]:     # an empty file: the store reserves no run
                super().send(raw)
                self.files = had
                self.note(('upload dropped: %s' % name)[:63])
                return
            super().send(raw)
            if name in self.files and self.files.get(name) is not had.get(name):
                parent = name.rpartition('/')[0]
                if (parent and parent not in self.folders) or name in self.lose:
                    if name in had:
                        self.files[name] = had[name]
                    else:
                        del self.files[name]
                    self.note(('commit failed: %s' % name)[:63])
                else:
                    self.note(('saved %s' % name)[:63])
            return
        super().send(raw)


def test_an_upload_the_unit_did_not_save_is_an_error_not_uploaded():
    """`elekloader mods put` reads the route's `last:` note after each upload (it can't refuse once
    the upload is in): a commit that failed, as into a folder the unit couldn't make, is the error,
    not UPLOADED; one it saved passes; a name past the note's 63 bytes compares as cut."""
    import argparse
    with Bench('analog-rytm-mk2') as B:
        out, b = built(B)
        blk = out.image[desc.find(out.image)[0]:]
        unit = NotingUnit(blk[:struct.unpack_from('>I', blk, 8)[0]], folders={'fx'})
        mods = datarpc.Mods(datarpc.Client(unit, timeout=0.5))
        log = []
        with tempfile.TemporaryDirectory() as tmp:
            name, raw = files(hello(B))[0]
            p = os.path.join(tmp, name)
            open(p, 'wb').write(raw)
            try:
                datarpc.run(argparse.Namespace(op='put', files=[p], into='gone'), mods, log.append)
                assert False, 'a lost upload passed'
            except ModError as e:
                assert str(e) == 'the unit did not save /mods/gone/%s: commit failed: gone/%s' % (name, name), e
            assert not any(l.startswith('UPLOADED') for l in log) and 'gone/' + name not in unit.files
            assert datarpc.run(argparse.Namespace(op='put', files=[p], into='fx'), mods, log.append) == 0
            assert log[-1].startswith('UPLOADED /mods/fx/%s' % name) and unit.files['fx/' + name] == raw
            deep = 'a-folder-name-of-twenty-six/and-another-of-twenty-one'
            unit.folders.add(deep)
            assert datarpc.run(argparse.Namespace(op='put', files=[p], into=deep), mods, log.append) == 0
            assert datarpc.last_of(unit.files['.status'].decode()) == ('saved %s/%s' % (deep, name))[:63]
            # a name past ASCII: the route's names (and its note) are latin-1 bytes, as the requests send them
            unit.folders.add('café')
            assert datarpc.run(argparse.Namespace(op='put', files=[p], into='café'), mods, log.append) == 0
            assert unit.files['café/' + name] == raw
            # mods active goes through the same check: a lost commit of /mods/active is the error, not WROTE
            assert datarpc.run(argparse.Namespace(op='active', names=['fx/' + name]), mods, log.append) == 0
            assert log[-1] == 'WROTE /mods/active (1 mods)' and datarpc.last_of(unit.files['.status'].decode()) == 'saved active'
            kept = unit.files['active']
            unit.lose.add('active')
            log.clear()
            try:
                datarpc.run(argparse.Namespace(op='active', names=['café/' + name]), mods, log.append)
                assert False, 'a lost /mods/active passed'
            except ModError as e:
                assert str(e) == 'the unit did not save /mods/active: commit failed: active', e
            assert not any(l.startswith('WROTE') for l in log) and unit.files['active'] == kept
            # no mods: a # line the boot skips, not an empty upload the A4's store drops
            unit.lose.clear()
            assert datarpc.run(argparse.Namespace(op='active', names=[]), mods, log.append) == 0
            assert drive.parse_active(unit.files['active'].decode()) == [] and unit.files['active'].startswith(b'#')
    assert datarpc.last_of('drive-loader route\nstage: 1 bytes\n') is None
    assert datarpc.last_of(None) is None


def test_the_7_bit_packing_round_trips():
    for n in (0, 1, 6, 7, 8, 13, 14, 15, 300):
        data = bytes((i * 37 + 0x81) & 0xFF for i in range(n))
        packed = datarpc.pack7(data)
        assert all(b < 0x80 for b in packed) and datarpc.unpack7(packed) == data
    assert datarpc.pack7(b'\x80\x01') == b'\x40\x00\x01'


# ---- a real build (the owner's files) --------------------------------------------------------------

def test_a_real_core_ar2_build_with_a_loader_stub_and_the_example_mods():
    stock, core = os.environ.get('ELEKLOADER_AR2_SYX'), os.environ.get('ELEKLOADER_CORE_AR2')
    if not stock or not core:
        print('SKIP: ELEKLOADER_AR2_SYX, ELEKLOADER_CORE_AR2')
        return
    from elekloader import formats
    raw, dev, rel = formats.load(stock)
    image = formats.main_image(raw, dev)
    tgt = devices.target_of(dev, rel)
    stub = {'elemod': 2, 'id': 'drive-loader', 'version': '0.0', 'target': tgt, 'requires': ['core'],
            'sections': {'.run': {'align': 4, 'len': 8, 'parts': [['hex', '0000000000000000']]},
                         '.bss': {'size': 0x80000}},
            'symbols': {'drive_staging': ['.bss', 0], 'drive_staging_size': ['abs', 0x80000]},
            'exports': ['drive_staging', 'drive_staging_size'], 'imports': ['core_desc'],
            'relocs': [['.run', 0, 'abs32', 'sym:core_desc', 0]]}
    with open(core, 'rb') as fh:
        cdoc = json.load(fh)
    out = link.link([link.Mod2(cdoc, 'core'), link.Mod2(stub, 'stub')], image)
    b = drive.load_build(out.image)
    assert (b.lo, b.hi, b.staging_size) == (link._align(out.layout['bss'][1], 0x1000), 0x47FFF000, 0x80000)
    assert (b.d['family'], b.d['device'], b.d['release']) == (2, 5, 0x30313736)
    assert b.desc_at == out.map['core_desc']
    folder = os.environ.get('ELEKLOADER_DRIVE_MODS')
    if not folder:
        print('SKIP: ELEKLOADER_DRIVE_MODS (the example mods)')
        return
    mods = sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.startswith('drive-hello') and f.endswith('.elemod'))
    r = drive.link(b, drive._files(mods))
    assert all(c.state == drive.LOADED for c in r.choices), [c.why() for c in r.choices]
    draw = r.tables['ev_draw']['entries']
    assert draw[0] == ('drive-hello-user', 5), draw
    assert ('drive-hello', 50) in r.tables['ev_tick']['entries']
    first = drive.write_layout(r, tempfile.mkdtemp())
    again = drive.write_layout(drive.link(drive.load_build(out.image), drive._files(mods)), tempfile.mkdtemp())
    assert [open(p, 'rb').read() for p in first] == [open(p, 'rb').read() for p in again]


if __name__ == '__main__':
    import traceback
    ok = failed = 0
    for n in sorted(k for k in globals() if k.startswith('test_')):
        try:
            globals()[n]()
            print('ok     ', n)
            ok += 1
        except Exception:
            print('FAIL   ', n)
            traceback.print_exc()
            failed += 1
    print('%d passed, %d failed' % (ok, failed))
    sys.exit(1 if failed else 0)
