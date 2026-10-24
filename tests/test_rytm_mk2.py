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
    # the 10 view sites, the step and the FX keep
    assert len(mod['sites']) == 56
    # the audio load (0.4): core times its own render events, first in and last out
    sub = {x['event']: x for x in mod['subscribe']}
    assert sub['ev_render_in']['fn'] == 'core_load_in' and sub['ev_render_in']['order'] == 0
    assert sub['ev_render_out']['fn'] == 'core_load_out' and sub['ev_render_out']['order'] == 100
    assert len([x for x in mod['sites'] if x.get('target', '').startswith('st_')]) == 32



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
