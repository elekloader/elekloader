# SPDX-License-Identifier: GPL-2.0-or-later
"""The Analog Rytm MKII: its profile, its file, and core-ar2's sites (pytest,
or run with python).

Needs a stock file, which never goes in the repo, named by an environment
variable; a test whose input is missing is skipped, not passed:
  ELEKLOADER_AR2_SYX   Analog-Rytm_MKII_OS1.73.syx or ...OS1.74.syx

The addresses below are 1.73's. 1.74's main OS is 1.73's with code added in
two places (Outbox 8's main-output routing), so what follows each moved:
see moved(). Core-ar2 0.3 is for 1.74 only (0.2 is the last core with 1.73).
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from elekloader import devices, elemod, formats, syx   # noqa: E402
from elekloader.sdk import build                        # noqa: E402

SYX = os.environ.get('ELEKLOADER_AR2_SYX', '')
DEV = [d for d in devices.devices() if d.key == 'analog-rytm-mk2'][0]
REL = None                                              # the stock file's


def moved(addr):
    """1.73's address -> the same code's in the stock file's release."""
    if REL.version == '1.73' or addr < 0x40057000:
        return addr
    if 0x40057080 <= addr < 0x40123e00:
        return addr + 0x6c
    if 0x40123e80 <= addr < 0x401b4f00:
        return addr + 0x1cc
    raise ValueError('no 1.74 address for %#x' % addr)


class Skip(Exception):
    pass


_c = {}


def stock():
    if 'st' not in _c:
        if not SYX or not os.path.exists(SYX):
            raise Skip('missing %s' % (SYX or 'ELEKLOADER_AR2_SYX'))
        with open(SYX, 'rb') as fh:
            _c['raw'] = fh.read()
        _c['st'], dev, rel = formats.load(_c['raw'])
        assert dev is DEV
        global REL
        REL = rel
        _c['img'] = formats.main_image(_c['st'], DEV)
    return _c['raw'], _c['st'], _c['img']


def at(img, addr, n):
    a = moved(addr)
    return img[a - DEV.main_load:a - DEV.main_load + n]


def test_profile():
    assert DEV.linkable() and DEV.flash_limit == 0x300000 and DEV.trailer is None
    assert (DEV.flash_limit - DEV.flash_at) % 0x40000 == 0      # whole 256 KB units
    assert DEV.ddr == DEV.areas['ddr'] == (0x47BE0000, 0x47C00000)


def test_the_file_and_the_writer():
    """Five sections (5, 2, 1, 3, 8), no trailer; from its own main OS
    stream the writer gives the stock file back byte for byte."""
    raw, st, img = stock()
    assert [s for s, _o, _l, _d in st.table] == [5, 2, 1, 3, 8]
    assert syx.sha(img) == REL.main_sha256 and len(img) == REL.main_len
    assert formats.write(st, st.stored[DEV.main_section], DEV)['syx'] == raw
    _im, gap = syx.inplace_depack(st.stored[DEV.main_section], DEV)
    assert gap > 0


def test_a_repack_verifies_inside_the_flash_limit():
    raw, st, img = stock()
    out = formats.write(st, formats.pack_main(img, DEV), DEV, version='AR01')
    facts = formats.verify(out, st, img, DEV, version='AR01')
    assert facts['flash_headroom'] > 0 and facts['main']['inplace_min_gap'] > 0


def test_the_os_entry_is_the_digitakts():
    """SP := 0x48000000, the same peripheral writes, and the third setup
    call (the boot site) sets VBR to 0x40000000."""
    _r, _s, img = stock()
    assert struct.unpack('>I', img[:4])[0] == 0x40000870
    assert at(img, 0x4000087a, 6) == bytes.fromhex('2e7c48000000')
    assert at(img, 0x40000882, 6) == bytes.fromhex('41f9fc04002d')
    assert at(img, 0x400008c0, 6) == bytes.fromhex('4eb9400014cc')
    assert at(img, 0x400014cc, 12) == bytes.fromhex('203c400000004e7b08014e75')


def test_the_os_stores_from_0x300000():
    """The settings store at 0x380000-0x400000, as on the Digitakt mk1, and a
    second at 0x300000-0x380000: flash_limit is 0x300000."""
    _r, _s, img = stock()
    push = lambda v: bytes.fromhex('2f3c') + v.to_bytes(4, 'big')
    assert img.count(push(0x380000)) == 42 and img.count(push(0x3c0000)) == 16
    assert img.count(push(0x300000)) == 7 and img.count(push(0x340000)) == 7
    # read(0x300000, 0x1b072, 0x4198911a) at 0x400fad5a
    assert at(img, 0x400fad5a, 20) == bytes.fromhex('48794198911a2f3c0001b0722f3c003000004eb9')
    assert at(img, 0x400fad5a + 20, 4) == moved(0x40136faa).to_bytes(4, 'big')


def test_core_ar2_sites():
    """Every site's stock bytes, and that each covers whole instructions.
    Core-ar2 0.3 and later are for 1.74 only (0.2 is the last core with 1.73): on 1.73
    its build is refused."""
    _r, _s, img = stock()
    with open(os.path.join(ROOT, 'mods', 'core-ar2', 'mod.json')) as fh:
        j = json.load(fh)
    assert j['os'] == '1.74' and not j.get('ports')
    if REL.version != '1.74':
        try:
            build.for_release(j, DEV, REL)
        except build.BuildError as e:
            assert 'is for analog-rytm-mk2 1.74' in str(e), e
            return
        raise AssertionError('core-ar2 %s built for %s' % (j['version'], REL.version))
    mod, port = build.for_release(j, DEV, REL)
    assert not port and mod['device'] == DEV.key and mod['os'] == REL.version
    for s in mod['sites']:
        a, want = int(s['addr'], 16), bytes.fromhex(s['stock'])
        assert img[a - DEV.main_load:a - DEV.main_load + len(want)] == want, s
        if s['op'] == 'ptr':            # data: the sound set's vtable entries, not code: +76
            # (0.8, the slot id) and +60..+72 (0.8, the lock get, set, clear and turn); and from
            # 0.9 the FX set's (vtable 0x401dcea0), the same five
            ptrs = {0x401dce24 + o: v for o, v in
                    ((76, 0x400a86dc), (60, 0x400a98b4), (64, 0x400a9926), (68, 0x400a99c2), (72, 0x400a8560))}
            ptrs.update({0x401dcea0 + o: v for o, v in
                         ((76, 0x400a870c), (60, 0x400a9a26), (64, 0x400a9a98), (68, 0x400a9b0e), (72, 0x400a8560))})
            assert a in ptrs and want == ptrs[a].to_bytes(4, 'big'), s
            continue
        ok, note = elemod.insn_check(img, a, len(want), DEV)
        assert ok, (s, note)
    d = mod['defsym']
    # the render handler's frame, which core_render_out restores from
    assert at(img, 0x4011fafa, 4) == bytes.fromhex('4e56') + (-int(d['RENDER_FRAME'], 16) & 0xffff).to_bytes(2, 'big')
    # SETTINGS: the jmp's 6 bytes, then where core_settings rejoins
    assert int(d['SETTINGS_RET'], 16) == moved(0x400db87e) + 6
    # SoundConfigChangedInfo's vtable (data, which moves otherwise): the
    # immediate the velocity setAmount site displaces
    vset = [x for x in mod['sites'] if x.get('target') == 'st_vel_set'][0]
    assert d['SOUND_CHANGED'] == '0x' + vset['stock'][-8:]
    # each jmp stub's resume address: its site plus the bytes it displaces
    # (st_vel_get also does the read after them and resumes at the exit)
    for k, t in (('VEL_PUSH_RET', 'st_vel_push'), ('VEL_SET_RET', 'st_vel_set'),
                 ('VEL_ROUTE_RET', 'st_vel_route'), ('AT_ROUTE_RET', 'st_at_route')):
        x = [x for x in mod['sites'] if x.get('target') == t][0]
        assert int(d[k], 16) == int(x['addr'], 16) + len(x['stock']) // 2, k
    # parameter pages (core-ar2 0.4): a jmp at each routine's entry, and each
    # stub resumes after the instructions it displaces
    for k, t in (('PAGE_INFO_RET', 'core_page_info'), ('GET_RET', 'core_param_get'),
                 ('DELTA_RET', 'core_param_delta'), ('TEXT_RET', 'core_param_text')):
        x = [x for x in mod['sites'] if x.get('target') == t][0]
        assert x['op'] == 'jmp' and len(x['stock']) // 2 in (6, 8), x
        assert int(d[k], 16) == int(x['addr'], 16) + len(x['stock']) // 2, k
    # pages whose entry a function answers (core-ar2 0.6), asked before core_pages
    assert mod['collections'].get('core_page_fns') == 4 and mod['collections'].get('core_pages') == 4
    # the page keys' lists (core-ar2 0.4): each view's `moveq #n,%d1; movel
    # %d1,%sp@-; movel #list,%d0` is a site, and defsym has its list, length
    # and resume
    views = sorted((x for x in mod['sites'] if x.get('target', '').startswith('core_view_')),
                   key=lambda x: int(x['target'][10:]))
    assert len(views) == 10
    for i, x in enumerate(views):
        a, b = int(x['addr'], 16), bytes.fromhex(x['stock'])
        assert b[0] == 0x72 and b[1] == int(d['VIEW%d_N' % i]) and b[2:6] == bytes.fromhex('2f01203c'), x
        assert int.from_bytes(b[6:10], 'big') == int(d['VIEW%d_LIST' % i], 16), x
        assert int(d['VIEW%d_RET' % i], 16) == a + 10, x
        assert img[a + 10 - DEV.main_load:a + 12 - DEV.main_load] == bytes.fromhex('2f00'), x   # the list's push
    # the page key's step: from the index's load to the invalidate's call,
    # resuming at the `pea %a2@(144)` stock follows it with; and the FX page
    # code's `beqs` around its go-back-to-the-first-page call, made a `bras`
    x = [x for x in mod['sites'] if x.get('target') == 'core_cycle'][0]
    a = int(x['addr'], 16)
    assert x['op'] == 'jmp' and x['stock'].startswith('222a008c') and x['stock'].endswith('2f0a4eb940078580'), x
    assert int(d['CYCLE_RET'], 16) == a + len(x['stock']) // 2, x
    assert img[a + 22 - DEV.main_load:a + 26 - DEV.main_load] == bytes.fromhex('486a0090'), x
    x = [x for x in mod['sites'] if x['addr'] == '0x400cbd3e'][0]
    assert x['op'] == 'bytes' and x['stock'] == '6710' and x['new'] == '6010', x
    o = 0x400cbd3e + 2 + 0x10 - DEV.main_load                # the branch skips setsel(view, 0)
    assert img[o - 16:o] == bytes.fromhex('20532250 42a72f08 20690060 4e90508f'.replace(' ', '')), x
    # mod storage's sites (core-ar2 0.3: 32 of the 40), the 4 page sites,
    # the 10 view sites, the step and the FX keep, 0.7's OS upgrade restart, the 13 lock
    # sites (0.8), the 18 lock row sites (0.8), the 7 scene and performance sites (0.8)
    # and the NRPN and CC in sites (0.8), and the engine push's velocity and
    # aftertouch index sites and the routes' knob label (0.9), the FX set's five vtable entries,
    # the FX words' save and the FX and sound edits' write into the stored kit (0.9),
    # the project save to a +Drive slot (0.10, ev_project_save), and the six
    # length and scale setters (0.11, ev_steps' LENGTH), and the five moments (0.11: the SysEx
    # router, the kit select and push, the slot clear and paste), and the sequencer clock's two
    # (0.11, ev_seq_step)
    assert len(mod['sites']) == 122
    for addr in ('0x400c0ba2', '0x400c0c8c', '0x400ab8d0', '0x400aba02', '0x400aba7e', '0x400ab096'):
        assert [x for x in mod['sites'] if x['addr'] == addr and x['op'] == 'jmp'], addr
    # each moment's site is its routine's first whole instructions, and the body in moments.s
    # rejoins right after them
    body = open(os.path.join(ROOT, 'mods', 'core-ar2', 'moments.s')).read()
    for target, addr, n in (('core_sysex_site', 0x40083dd8, 8), ('core_kit_select', 0x400a65f6, 8),
                            ('core_kit_push', 0x4011f90a, 8), ('core_slot_clear', 0x40034bd2, 6),
                            ('core_slot_paste', 0x400351d2, 8)):
        x = [x for x in mod['sites'] if x.get('target') == target][0]
        assert x['op'] == 'jmp' and int(x['addr'], 16) == addr and len(x['stock']) == 2 * n, x
        assert img[addr - DEV.main_load:addr + n - DEV.main_load].hex() == x['stock'], x
        assert 'jmp     0x%08x' % (addr + n) in body, target
    x = [x for x in mod['sites'] if x.get('target') == 'st_proj_save'][0]
    assert (x['addr'], x['op'], x['stock']) == ('0x400b07a4', 'jmp', '4fefffd048d77c1c'), x
    assert img[0x400b07a4 - DEV.main_load:0x400b07ac - DEV.main_load].hex() == x['stock'], x
    # the file save site (0.2) is inside it: the serializer's call into the slot's image
    assert img[0x400b0870 - DEV.main_load:0x400b0876 - DEV.main_load].hex() == '4eb94000d10e'
    for t, addr, was in (('st_fx_through', 0x400a5692, '4fefffc448d73c1c'),
                         ('st_sound_through', 0x400bd412, '4fefffbc48d77c3c')):
        x = [x for x in mod['sites'] if x.get('target') == t][0]
        assert x['op'] == 'jmp' and int(x['addr'], 16) == addr and x['stock'] == was, x
    # 0.9: each index site is the route's code load and its add, 6 bytes,
    # rejoining at the index's store
    for addr, target, want in ((0x4011f6b2, 'core_vel_index', '7729006fd680'),
                                (0x4011f6d8, 'core_at_index', '752a008bd3c2')):
        x = [x for x in mod['sites'] if x.get('target') == target][0]
        assert (int(x["addr"], 16), x["op"], x["stock"]) == (addr, "jmp", want), x
        assert img[addr - DEV.main_load:addr + 6 - DEV.main_load].hex() == want, x
    x = [x for x in mod['sites'] if x.get('target') == 'core_route_label'][0]
    assert (x['addr'], x['op']) == ('0x4004b81a', 'jmp'), x
    assert img[0x4004b81a - DEV.main_load:0x4004b82a - DEV.main_load].hex() == x['stock'] == \
        '2f06' '4eb940006eca' '2f00' '4eb940007296', x
    lock = {int(x['addr'], 16): (x['op'], x['target']) for x in mod['sites']
            if x.get('target') in ('core_lfo_apply', 'core_id_slot', 'core_dst_list_end', 'core_dst_label',
                                   'core_dst_text', 'core_dst_slot_id', 'core_rand_param', 'core_reload',
                                   'core_knob_label', 'core_knob_name')}
    assert lock == {0x4009eac0: ('jmp', 'core_lfo_apply'), 0x40006ea8: ('jmp', 'core_id_slot'),
                    0x400aa31e: ('jmp', 'core_dst_list_end'), 0x4004a7e8: ('jmp', 'core_dst_label'),
                    0x400ffab2: ('jmp', 'core_dst_text'), 0x401dce70: ('ptr', 'core_dst_slot_id'),
                    0x400aa32a: ('jmp', 'core_rand_param'), 0x400af32c: ('jmp', 'core_reload'),
                    0x40037c1a: ('jsr', 'core_knob_label'), 0x40037d42: ('jsr', 'core_knob_label'),
                    0x400d37fa: ('jsr', 'core_knob_label'),
                    0x4003a1c6: ('jsr', 'core_knob_name'), 0x4003a8d6: ('jsr', 'core_knob_name')}, lock
    for x in mod['sites']:                  # a knob label site calls stock's label
        if x.get('target') == 'core_knob_label':
            a = int(x['addr'], 16)
            assert x['stock'] == '4eb9400a9000' and img[a - DEV.main_load:a + 6 - DEV.main_load].hex() == x['stock'], x
    x = [x for x in mod['sites'] if x.get('target') == 'core_finish_drain'][0]
    a = int(x['addr'], 16)                  # move #0x2700,sr then jsr to the drain wait
    assert x['op'] == 'jsr' and img[a - DEV.main_load:a + 10 - DEV.main_load].hex() == x['stock'], x
    assert x['stock'] == '46fc27004eb9' + d['FINISH_DRAIN'][2:], x
    # the lock rows (0.8): the routines that move and clear step records and
    # list a step's locks, their first instructions as stock has them, and
    # the sound set's lock get, set, clear and turn
    rows = {int(x['addr'], 16): (x['op'], x['target'], x['stock']) for x in mod['sites']
            if x.get('target') in ('core_memcpy', 'core_row_load', 'core_rebuild', 'core_trig_locks',
                                   'core_lock_get_vt', 'core_lock_set_vt', 'core_lock_clear_vt',
                                   'core_lock_turn_vt', 'core_memset', 'core_new', 'core_new_array',
                                   'core_track_clear', 'core_step_clear', 'core_trig_list',
                                   'core_euclid_list', 'core_lock_paste', 'core_dump_out',
                                   'core_dump_check')}
    assert rows == {0x40098bc0: ('jmp', 'core_memcpy', '226f0004206f0008'),
                    0x40098c08: ('jmp', 'core_memset', '206f000871af000b'),
                    0x40082a40: ('jmp', 'core_new', '4ef9400827c4'),
                    0x40082a46: ('jmp', 'core_new_array', '4ef9400827c4'),
                    0x4001197c: ('jmp', 'core_track_clear', '4feffff048d70c0c'),
                    0x400ac86e: ('jmp', 'core_step_clear', '4fefffe448d704fc'),
                    0x400c4d62: ('jmp', 'core_trig_list', '286effac202c0008'),
                    0x400c3eb0: ('jmp', 'core_euclid_list', '2f022e3c400c070a'),
                    0x400c058c: ('jmp', 'core_lock_paste', '4feffff448d7040c'),
                    0x4000eff2: ('jsr', 'core_dump_out', '4eb94000937c'),
                    0x4000d79c: ('jmp', 'core_dump_check', '2f4200102f43000c'),
                    0x40008f98: ('jmp', 'core_row_load', '4fefffdc48d73c7c'),
                    0x40009216: ('jmp', 'core_rebuild', '4fefffd048d77cfc'),
                    0x4009a954: ('jmp', 'core_trig_locks', '42404fefffe0'),
                    0x401dce60: ('ptr', 'core_lock_get_vt', '400a98b4'),
                    0x401dce64: ('ptr', 'core_lock_set_vt', '400a9926'),
                    0x401dce68: ('ptr', 'core_lock_clear_vt', '400a99c2'),
                    0x401dce6c: ('ptr', 'core_lock_turn_vt', '400a8560'),
                    0x401dcee8: ('ptr', 'core_lock_turn_vt', '400a8560')}, rows   # the FX set's turn (0.9)
    for a, (_op, _t, b) in rows.items():
        assert img[a - DEV.main_load:a - DEV.main_load + len(b) // 2] == bytes.fromhex(b), hex(a)
    # scenes and performances (0.8): a knob's turn and push on a parameter
    # view, the audio side's two list builds (an entry's code read where
    # stock reads it), the kit file's code translations both ways and the
    # kit's SysEx dump
    scenes = {int(x['addr'], 16): (x['op'], x['target'], x['stock']) for x in mod['sites']
              if x.get('target') in ('core_scene_turn_site', 'core_scene_push_site', 'core_perf_entry',
                                     'core_scene_entry', 'core_code_id', 'core_id_code', 'core_kit_dump_out')}
    assert scenes == {0x400385f8: ('jmp', 'core_scene_turn_site', '4fefffd048d70cfc'),
                      0x400387ba: ('jmp', 'core_scene_push_site', '4fefffd048d71c3c'),
                      0x4011f744: ('jsr', 'core_perf_entry', '12290003760c'),
                      0x4011f844: ('jsr', 'core_scene_entry', '7528000410280002'),
                      0x40007602: ('jmp', 'core_code_id', '2f02740c222f0008'),
                      0x400075c8: ('jmp', 'core_id_code', '2f02740c222f0008'),
                      0x4000f2a2: ('jsr', 'core_kit_dump_out', '4eb9400088d6')}, scenes
    for a, (_op, _t, b) in scenes.items():
        assert img[a - DEV.main_load:a - DEV.main_load + len(b) // 2] == bytes.fromhex(b), hex(a)
    # MIDI (0.8): a track's NRPN in and CC in, their first instructions as stock has them
    midi = {int(x['addr'], 16): (x['op'], x['target'], x['stock']) for x in mod['sites']
            if x.get('target') in ('core_nrpn_site', 'core_cc_site')}
    assert midi == {0x400b2b26: ('jmp', 'core_nrpn_site', '4fefffe448d70c7c'),
                    0x400b0544: ('jmp', 'core_cc_site', '4fefffec48d7043c')}, midi
    for a, (_op, _t, b) in midi.items():
        assert img[a - DEV.main_load:a - DEV.main_load + 8] == bytes.fromhex(b), hex(a)
    # the audio load (0.4): core times its own render events, first in and last out
    sub = {x['event']: x for x in mod['subscribe']}
    assert sub['ev_render_in']['fn'] == 'core_load_in' and sub['ev_render_in']['order'] == 0
    assert sub['ev_render_out']['fn'] == 'core_load_out' and sub['ev_render_out']['order'] == 100
    # mod storage's own sites, with the FX and sound edits' writes into the stored kit (0.9)
    # and the project save (0.10)
    assert len([x for x in mod['sites'] if x.get('target', '').startswith('st_')]) == 35



def test_core_ar2_machines():
    """core-ar2 0.5's machines (machines.c): the tables it writes at boot hold what it assumes.
    Slots 34..52 are hidden by their voice mask alone; each owns its parameter records at
    sound indices 9..16 (all eight in slots 35..42); the engine table is data in the image (BD HARD's engine first). And
    the core runs core_boot_init from its .boot and has the table core_machines."""
    _r, _s, img = stock()
    if REL.version != '1.74':
        raise Skip('core-ar2 0.5 is for 1.74')

    def l(a):
        return struct.unpack('>i', img[a - DEV.main_load:a - DEV.main_load + 4])[0]
    assert l(0x402906dc) == 0x40111360                    # engine[0], BD HARD
    assert l(0x402906dc + 4 * 27) == 0x40113f68            # DISABLE
    for m in range(53):
        mask = img[0x401bf42c + 2 * m + 1 - DEV.main_load]
        assert (mask == 0) == (m >= 34), m
        k = sorted(l(0x401bfbc8 + 0x34 * i + 4) for i in range(469) if l(0x401bfbc8 + 0x34 * i) == m)
        assert len(set(k)) == len(k) and set(k) <= set(range(9, 17)), (m, k)
        assert 35 <= m <= 42 and k == list(range(9, 17)) or not 35 <= m <= 42, (m, k)
    with open(os.path.join(ROOT, 'mods', 'core-ar2', 'mod.json')) as fh:
        j = json.load(fh)
    assert j['defsym'].get('BOOT_INIT') == '1' and 'core_machines' in j['collections']
    assert 'machines.c' in j['sources']
    assert ('core_machines', 0, 53, 'machine') in DEV.slot_claims


def test_core_ar2_machine_slots_44_52():
    """core-ar2 0.13 (machines.c): slots 44..52 are offered. Their TEST records have no MIDI
    (+0x18 and +0x1c -1) and the indices machines.h lists; slot 35's record at index 9 + k
    carries CC 16 + k (no LSB) and NRPN 1:k, which core copies; +0x20 is 0 on every hidden
    record. The trig getters' sources: the owner bytes (8 voices, tracks 0..11) and the engine
    table's 53 entries are data in the image."""
    _r, _s, img = stock()
    if REL.version != '1.74':
        raise Skip('core-ar2 0.13 is for 1.74')

    def l(a):
        return struct.unpack('>I', img[a - DEV.main_load:a - DEV.main_load + 4])[0]
    recs = {}
    for i in range(469):
        r = 0x401bfbc8 + 0x34 * i
        recs.setdefault(l(r), {})[l(r + 4)] = r
    for k in range(8):
        r = recs[35][9 + k]
        assert l(r + 0x18) == (16 + k) << 16 | 0xffff and l(r + 0x1c) == 1 << 7 | k, k
    want = {44: range(9, 17), 45: [9, 10, 11, 12, 15, 16], 46: range(9, 17), 47: range(9, 14),
            48: range(9, 13), 49: range(9, 13), 50: [9, 10, 11, 12, 13, 16], 51: range(9, 17),
            52: [10, 11, 12, 16]}
    for m, idx in want.items():
        assert sorted(recs[m]) == list(idx), (m, sorted(recs[m]))
        for r in recs[m].values():
            assert l(r + 0x18) == 0xffffffff and l(r + 0x1c) == 0xffffffff, (m, hex(r))
    for m in range(34, 53):
        assert all(l(r + 0x20) == 0 for r in recs[m].values()), m
    owner = img[0x40291138 - DEV.main_load:0x40291138 - DEV.main_load + 8]
    assert all(t < 12 for t in owner), owner.hex()
    with open(os.path.join(ROOT, 'mods', 'core-ar2', 'machines.c')) as fh:
        src = fh.read()
    assert '#define SLOT_LAST 52' in src and '0x40291138' in src and '0x8000e508' in src

def test_no_copy_of_the_bootstrap_in_the_main_os():
    """The main OS holds no copy of the bootstrap (section 2). No 512 bytes of
    its first 0x7d94 (zero-padded tables and data follow them) are in
    the main OS, no 2 KB anywhere in it with more than 32 distinct byte
    values is, and its stored (packed) bytes share nothing. What the two do
    share is pieces of tables and data."""
    _r, st, img = stock()
    boot, packed = st.section(2), st.stored[2]
    for o in range(0, 0x7d94 - 512, 64):
        assert boot[o:o + 512] not in img, hex(o)
    for o in range(0, len(boot) - 2048, 64):
        w = boot[o:o + 2048]
        assert len(set(w)) <= 32 or w not in img, hex(o)
    for o in range(0, len(packed) - 32, 32):
        assert packed[o:o + 32] not in img, hex(o)


def test_every_flash_write_and_erase_in_the_os_is_known():
    """The SPI NOR routines (program 0x40136fae; erase 0x4013702a, 0x40136bae,
    0x40136cd0, 0x40136aaa, 0x40136e88, 0x401366d6) are reached only from:
    the store at 0x300000 (0x400fac00-0x400fb200), the settings store at
    0x380000 (0x40139100-0x40139b10), the factory console (0x4013a068-
    0x4013bf98: "READY FOR BOOTSTRAP" writes a received bootstrap at
    0x40000, "READY FOR OS" a received container from 0x80000), the in-OS
    upgrade (0x4009f3a0-0x4009f460: erase and write from 0x80000), and an
    erase of 0x400000, 0x800000 and 0xc00000 (0x401229f0-0x40122a30). Each
    writes what it received or a store, never bytes of the main OS image:
    nothing in it is a bootstrap the OS could re-flash, so no protected range."""
    _r, _s, img = stock()
    known = [(0x400fac00, 0x400fb200), (0x40139100, 0x40139b10), (0x4013a068, 0x4013bf98),
             (0x4009f3a0, 0x4009f460), (0x401229f0, 0x40122a30)]
    known = [(moved(lo), moved(hi)) for lo, hi in known]
    for routine in (0x40136fae, 0x4013702a, 0x40136bae, 0x40136cd0, 0x40136aaa, 0x40136e88, 0x401366d6):
        word, i, refs = moved(routine).to_bytes(4, 'big'), 0, []
        while True:
            i = img.find(word, i)
            if i < 0:
                break
            refs.append(DEV.main_load + i - 2)
            i += 2
        assert refs, hex(routine)
        for r in refs:
            assert any(lo <= r < hi for lo, hi in known), (hex(routine), hex(r))
    assert at(img, 0x4009f3c2, 6) == bytes.fromhex('267c00080000')        # a3 = 0x80000
    assert at(img, 0x4013bea0, 6) == bytes.fromhex('2f3c00040000')        # the bootstrap at 0x40000


if __name__ == '__main__':
    import traceback
    ok = skipped = failed = 0
    for n in sorted(k for k in globals() if k.startswith('test_')):
        try:
            globals()[n]()
            print('ok      %s' % n)
            ok += 1
        except Skip as e:
            print('SKIP    %s (%s)' % (n, e))
            skipped += 1
        except Exception:
            print('FAIL    %s' % n)
            traceback.print_exc()
            failed += 1
    print('%d passed, %d failed, %d skipped' % (ok, failed, skipped))
    sys.exit(1 if failed else 0)
