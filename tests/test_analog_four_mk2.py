# SPDX-License-Identifier: GPL-2.0-or-later
"""The Analog Four MKII: its profile, its file, and core-a4's sites (pytest,
or run with python).

Needs a stock file, which never goes in the repo, named by an environment
variable; a test whose input is missing is skipped, not passed:
  ELEKLOADER_A4_SYX   Analog-Four_MKII_OS1.56.syx

Each test is the Analog Rytm MKII's (tests/test_rytm_mk2.py) with the A4's
own addresses, measured on 1.56: the two OSes are the same platform, but no
address or count is the Rytm's.
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

SYX = os.environ.get('ELEKLOADER_A4_SYX', '')
DEV = [d for d in devices.devices() if d.key == 'analog-four-mk2'][0]
REL = None                                              # the stock file's


class Skip(Exception):
    pass


_c = {}


def stock():
    if 'st' not in _c:
        if not SYX or not os.path.exists(SYX):
            raise Skip('missing %s' % (SYX or 'ELEKLOADER_A4_SYX'))
        with open(SYX, 'rb') as fh:
            _c['raw'] = fh.read()
        _c['st'], dev, rel = formats.load(_c['raw'])
        assert dev is DEV
        global REL
        REL = rel
        _c['img'] = formats.main_image(_c['st'], DEV)
    return _c['raw'], _c['st'], _c['img']


def at(img, addr, n):
    return img[addr - DEV.main_load:addr - DEV.main_load + n]


def test_profile():
    assert DEV.linkable() and DEV.flash_limit == 0x300000 and DEV.trailer is None
    assert (DEV.flash_limit - DEV.flash_at) % 0x40000 == 0      # whole 256 KB units
    assert DEV.ddr == DEV.areas['ddr'] == (0x47BE0000, 0x47C00000)
    assert DEV.sysex_id == 0x0B
    assert len({d.sysex_id for d in devices.devices() if d.container == 'ele3'}) == \
        len([d for d in devices.devices() if d.container == 'ele3'])


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
    out = formats.write(st, formats.pack_main(img, DEV), DEV, version='A401')
    facts = formats.verify(out, st, img, DEV, version='A401')
    assert facts['flash_headroom'] > 0 and facts['main']['inplace_min_gap'] > 0


def test_the_os_entry_is_the_rytms():
    """SP := 0x48000000, the same peripheral writes, and the third setup
    call (the boot site) sets VBR to 0x40000000."""
    _r, _s, img = stock()
    assert struct.unpack('>I', img[:4])[0] == 0x40001130
    assert at(img, 0x4000113a, 6) == bytes.fromhex('2e7c48000000')
    assert at(img, 0x40001142, 6) == bytes.fromhex('41f9fc04002d')
    assert at(img, 0x40001180, 6) == bytes.fromhex('4eb940002064')
    assert at(img, 0x40002064, 12) == bytes.fromhex('203c400000004e7b08014e75')


def test_the_os_keeps_its_calibration_at_0x300000():
    """The settings store at 0x380000-0x400000, as on the Rytm, and the
    voice calibration record at 0x300000: flash_limit is 0x300000. Unlike
    the Rytm, nothing is kept at 0x340000."""
    _r, _s, img = stock()
    push = lambda v: bytes.fromhex('2f3c') + v.to_bytes(4, 'big')
    assert img.count(push(0x380000)) == 26 and img.count(push(0x3c0000)) == 11
    assert img.count(push(0x300000)) == 2 and img.count(push(0x340000)) == 0
    # read(0x300000, 0xe56, *0x40214c10) at 0x400f18e8, through 0x401155a2
    assert at(img, 0x400f18e8, 22) == bytes.fromhex('2f3940214c1048780e562f3c003000004eb9401155a2')


def test_core_a4_sites():
    """Every site's stock bytes, and that each covers whole instructions."""
    _r, _s, img = stock()
    with open(os.path.join(ROOT, 'mods', 'core-a4', 'mod.json')) as fh:
        j = json.load(fh)
    assert j['os'] == '1.56' and not j.get('ports')
    mod, port = build.for_release(j, DEV, REL)
    assert not port and mod['device'] == DEV.key and mod['os'] == REL.version
    for s in mod['sites']:
        a, want = int(s['addr'], 16), bytes.fromhex(s['stock'])
        assert img[a - DEV.main_load:a - DEV.main_load + len(want)] == want, s
        ok, note = elemod.insn_check(img, a, len(want), DEV)
        assert ok, (s, note)
    d = mod['defsym']
    # the render handler's frame (link a6,#-0xec at 0x401013be), which
    # core_render_out restores from
    assert at(img, 0x401013be, 4) == bytes.fromhex('4e56') + (-int(d['RENDER_FRAME'], 16) & 0xffff).to_bytes(2, 'big')
    # the boot, draw, key and encoder sites call the routines core calls on
    for t, k in (('boot', 'VBR_FN'), ('core_draw', 'DRAWALL'), ('core_key', 'KEYDISP'),
                 ('core_enc', 'ENCDISP')):
        x = [x for x in mod['sites'] if x.get('target') == t][0]
        assert x['stock'] == '4eb9' + d[k][2:].lower().rjust(8, '0'), t
    # SETTINGS: the jmp's 6 bytes, then where core_settings rejoins
    x = [x for x in mod['sites'] if x.get('target') == 'core_settings'][0]
    assert int(d['SETTINGS_RET'], 16) == int(x['addr'], 16) + 6
    # parameter pages (0.2): each page stub resumes after the 6 or 8 bytes
    # its site displaced; page_info's moveq is the last stock page (47), and
    # param_value_text opens with two pushes and a load, not the Rytm's
    # lea and moveml
    for k, t in (('PAGE_INFO_RET', 'core_page_info'), ('GET_RET', 'core_param_get'),
                 ('DELTA_RET', 'core_param_delta'), ('TEXT_RET', 'core_param_text')):
        x = [x for x in mod['sites'] if x.get('target') == t][0]
        assert x['op'] == 'jmp' and int(d[k], 16) == int(x['addr'], 16) + len(x['stock']) // 2, t
    pages = {x['target']: x['stock'] for x in mod['sites'] if x.get('target', '').startswith('core_pa')}
    assert pages['core_page_info'] == '722f202f0004'
    assert pages['core_param_get'] == pages['core_param_delta'] == '4feffff448d7040c'
    assert pages['core_param_text'] == '2f032f02242f0014'
    # the audio load (0.2): core times its own render events, first in and last out
    sub = {x['event']: x for x in mod['subscribe']}
    assert sub['ev_render_in']['fn'] == 'core_load_in' and sub['ev_render_in']['order'] == 0
    assert sub['ev_render_out']['fn'] == 'core_load_out' and sub['ev_render_out']['order'] == 100
    # views (0.2): the hook after the main screen's last view and the page
    # key's step, each resuming after its 8 bytes; and the current view's
    # range, 0..28, widened in the setter and the loader
    for k, t in (('VIEWS_RET', 'core_views'), ('CYCLE_RET', 'core_cycle')):
        x = [x for x in mod['sites'] if x.get('target') == t][0]
        assert x['op'] == 'jmp' and int(d[k], 16) == int(x['addr'], 16) + 8, t
    clamps = [x for x in mod['sites'] if x['op'] == 'bytes']
    assert len(clamps) == 5
    for x in clamps:
        assert x['stock'][2:] == '1c' and x['new'] == x['stock'][:2] + '2c', x
        assert x['stock'][0] == '7' and int(x['stock'][1], 16) % 2 == 0, x     # moveq #28,Dn
    # mod storage's sites (core-a4 0.1: 28 of the 36), and 0.2's 4 page
    # sites, 2 view sites and 5 range bytes
    assert len(mod['sites']) == 47
    assert len([x for x in mod['sites'] if x.get('target', '').startswith('st_')]) == 28


def test_core_a4_store_bodies():
    """store.s: each jmp site's displaced instructions, then a jump back to
    the site plus the bytes it displaced (8 for each)."""
    import re
    with open(os.path.join(ROOT, 'mods', 'core-a4', 'mod.json')) as fh:
        sites = {x['target']: x for x in json.load(fh)['sites'] if x['op'] == 'jmp'}
    with open(os.path.join(ROOT, 'mods', 'core-a4', 'store.s')) as fh:
        src = fh.read()
    bodies = re.findall(r'^(st_\w+)_body:\s*\|\s*(0x[0-9a-f]+).*?^\s+jmp\s+(0x[0-9a-f]+)', src, re.M | re.S)
    assert len(bodies) == 14
    for name, addr, back in bodies:
        x = sites[name]
        assert int(x['addr'], 16) == int(addr, 16), name
        assert int(back, 16) == int(addr, 16) + len(x['stock']) // 2 == int(addr, 16) + 8, name


def _disp16(img, lo, hi, d):
    """how many moves in [lo, hi) read or write (d16,An) with displacement d,
    An not the stack pointer (a frame slot is not an object's byte)"""
    from elekloader.isa import coldfire as cf
    n = 0
    for pc, ins in cf.disassemble_lengths(img, DEV.main_load, lo, hi):
        w = struct.unpack('>H', at(img, pc, 2))[0]
        if not ins.op.startswith('move') or w >> 12 not in (1, 2, 3):
            continue
        ext = [struct.unpack('>H', at(img, pc + k, 2))[0] for k in range(2, ins.length, 2)]
        if (w >> 3 & 7) == 5 and (w & 7) != 7:          # source (d16,An)
            n += ext[0] == d
        if (w >> 6 & 7) == 5 and (w >> 9 & 7) != 7:     # destination (d16,An)
            n += ext[-1] == d
    return n


def test_the_bytes_mod_storage_uses_are_stocks_spare():
    """What store.c relies on, in the serializers' code: the song save
    (0x4000a35e) and load (0x400079a0) use entry bytes +1..+3 (+0x115..+0x117),
    never +0 (+0x114): the tagged store; the sound save (0x400091bc) and load
    (0x40008808) never name stored +0xf1..+0xf3; the kit's FX save and load
    (0x40009300, 0x40008bf2) never name +0x18..+0x37; the global save
    (0x4000837c) writes 0 to each entry's +3..+7 and the load (0x40007bc6)
    never reads them; the stored project's magic, version and end marker."""
    _r, _s, img = stock()
    for lo, hi in ((0x4000a35e, 0x4000a450), (0x400079a0, 0x40007b92)):
        for d in (0x115, 0x116, 0x117):
            assert _disp16(img, lo, hi, d), (hex(lo), hex(d))
        assert not _disp16(img, lo, hi, 0x114), hex(lo)
    for lo, hi in ((0x400091bc, 0x400092b8), (0x40008808, 0x40008b8a)):
        for d in (0xf1, 0xf2, 0xf3):
            assert not _disp16(img, lo, hi, d), (hex(lo), hex(d))
    for lo, hi in ((0x40009300, 0x400094d2), (0x40008bf2, 0x40008dca)):
        for d in range(0x18, 0x38):
            assert not _disp16(img, lo, hi, d), (hex(lo), hex(d))
    # the stored project: 0xbeefbaba, version 0x17 and 0xbabeface at +0x1f72d2
    # (the save 0x4001141e writes them, the load 0x40011054 checks them)
    for lo, n in ((0x4001141e, 0x34a), (0x40011054, 0x3ca)):
        code = at(img, lo, n)
        assert code.count(bytes.fromhex('beefbaba')) == 1 and bytes.fromhex('001f72d2') in code, hex(lo)


def test_no_copy_of_the_bootstrap_in_the_main_os():
    """The main OS holds no copy of the bootstrap (section 2). No 512 bytes of
    its first 0x6f94 (its code and data; a zero run and tables follow) are
    in the main OS, no 2 KB anywhere in it with more than 32 distinct byte
    values is, and its stored (packed) bytes share nothing. The one table
    the two share is the font widths, 1,175 bytes at +0x71e8 and
    0x40203e20."""
    _r, st, img = stock()
    boot, packed = st.section(2), st.stored[2]
    for o in range(0, 0x6f94 - 512, 64):
        assert boot[o:o + 512] not in img, hex(o)
    for o in range(0, len(boot) - 2048, 64):
        w = boot[o:o + 2048]
        assert len(set(w)) <= 32 or w not in img, hex(o)
    for o in range(0, len(packed) - 32, 32):
        assert packed[o:o + 32] not in img, hex(o)
    assert boot[0x71e8:0x71e8 + 1175] == at(img, 0x40203e20, 1175)


def test_every_flash_write_and_erase_in_the_os_is_known():
    """The SPI NOR routines (program 0x401155a6; erase 0x40115622, 0x401151a6,
    0x401152c8, 0x401150a2, 0x40115480, 0x40114cce) are reached, by
    absolute address, only from: the calibration record at 0x300000
    (0x400f18e8-0x400f1a10), the settings store at 0x380000
    (0x40118c4e-0x40119370), the factory console (0x4011988a-0x4011b0e0:
    a received bootstrap at 0x40000, a received container from 0x80000),
    the in-OS upgrade (0x4007da3c-0x4007db40: erase and write from 0x80000),
    and the COKI copy (0x40106f9c-0x401070b0: 0x400000, 0x800000 and
    0xc00000, copied to the card, then erased). The library's own bra.w
    into 0x40114cce (0x4011567e) is PC-relative. Each writes what it
    received or a store, never bytes of the main OS image: nothing in it
    is a bootstrap the OS could re-flash, so no protected range."""
    _r, _s, img = stock()
    known = [(0x400f18e8, 0x400f1a10), (0x40118c4e, 0x40119370), (0x4011988a, 0x4011b0e0),
             (0x4007da3c, 0x4007db40), (0x40106f9c, 0x401070b0)]
    for routine in (0x401155a6, 0x40115622, 0x401151a6, 0x401152c8, 0x401150a2, 0x40115480, 0x40114cce):
        word, i, refs = routine.to_bytes(4, 'big'), 0, []
        while True:
            i = img.find(word, i)
            if i < 0:
                break
            refs.append(DEV.main_load + i - 2)
            i += 2
        assert refs, hex(routine)
        for r in refs:
            assert any(lo <= r < hi for lo, hi in known), (hex(routine), hex(r))
    assert at(img, 0x4007da66, 6) == bytes.fromhex('267c00080000')        # a3 = 0x80000
    assert at(img, 0x4011b008, 6) == bytes.fromhex('2f3c00040000')        # the bootstrap at 0x40000


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
