# SPDX-License-Identifier: GPL-2.0-or-later
"""The Analog Four MKII: its profile, its file, and core-a4mk2's sites (pytest,
or run with python).

Needs a stock file, which never goes in the repo, named by an environment
variable; a test whose input is missing is skipped, not passed:
  ELEKLOADER_A4_SYX   Analog-Four_MKII_OS1.56.syx

Each test is the Analog Rytm MKII's (tests/test_rytm_mk2.py) with the A4's
own addresses, measured on 1.56: the two OSes are the same platform, but no
address or count is the Rytm's.
"""
import hashlib
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


def sb(x, img):
    """A site's stock bytes (hex) from the stock image; the manifest holds only their length and
    SHA-256, which this checks."""
    a, n = int(x['addr'], 16), x['len']
    got = at(img, a, n)
    assert len(got) == n and hashlib.sha256(got).hexdigest() == x['stock_sha256'], x
    return got.hex()


def test_profile():
    assert DEV.linkable() and DEV.flash_limit == 0x300000 and DEV.trailer is None
    assert (DEV.flash_limit - DEV.flash_at) % 0x40000 == 0      # whole 256 KB units
    assert DEV.ddr == DEV.areas['ddr'] == (0x47BE0000, 0x47C00000)
    # core-a4mk2 0.6: the mods' area moves into the gap checked on a unit (ddr-canary-a4mk2),
    # from its first 64 KB boundary, joined to the 128 KB area above it
    assert DEV.ddr_for('0.5') == DEV.ddr
    lo, hi = DEV.ddr_for('0.6')
    assert (lo, hi) == (0x45650000, 0x47C00000) and lo % 0x10000 == 0
    assert DEV.areas['gap'][0] <= lo and DEV.areas['gap'][1] == DEV.areas['ddr'][0] and hi == DEV.areas['ddr'][1]
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
    assert hashlib.sha256(at(img, 0x4000113a, 6)).hexdigest() == 'f66fa54ead8136aec5c9c5d093862f18a95072bc22ade71b489ef0da1cb14b33'
    assert hashlib.sha256(at(img, 0x40001142, 6)).hexdigest() == '06a6a908d74af5c65a5e76a6fafbd9055098b35b7c1c743c41814985329e0c74'
    assert hashlib.sha256(at(img, 0x40001180, 6)).hexdigest() == '526ee1b155acc496fd523a13e9a84ba2dd4f35004ef4a475939bc88bc486ec2a'
    assert hashlib.sha256(at(img, 0x40002064, 12)).hexdigest() == '91e184609ecdefbf5e5eb6d5c683ce075e8cf7095ea765408ef4eacd4714cc7f'


def test_the_os_keeps_its_calibration_at_0x300000():
    """The settings store at 0x380000-0x400000, as on the Rytm, and the
    voice calibration record at 0x300000: flash_limit is 0x300000. Unlike
    the Rytm, nothing is kept at 0x340000."""
    _r, _s, img = stock()
    push = lambda v: bytes.fromhex('2f3c') + v.to_bytes(4, 'big')
    assert img.count(push(0x380000)) == 26 and img.count(push(0x3c0000)) == 11
    assert img.count(push(0x300000)) == 2 and img.count(push(0x340000)) == 0
    # read(0x300000, 0xe56, *0x40214c10) at 0x400f18e8, through 0x401155a2
    assert hashlib.sha256(at(img, 0x400f18e8, 22)).hexdigest() == 'ffdd2aae574e0b64e4a740e3c191f4d57f08d6553dd45653333ac72e4dfd93b5'


def test_core_a4mk2_sites():
    """Every site's stock bytes, and that each covers whole instructions."""
    _r, _s, img = stock()
    with open(os.path.join(ROOT, 'mods', 'core-a4mk2', 'mod.json')) as fh:
        j = json.load(fh)
    assert j['os'] == '1.56' and not j.get('ports')
    mod, port = build.for_release(j, DEV, REL)
    assert not port and mod['device'] == DEV.key and mod['os'] == REL.version
    for s in mod['sites']:
        a, want = int(s['addr'], 16), bytes.fromhex(sb(s, img))
        assert 'stock' not in s, s              # the manifest holds no stock bytes
        if s['op'] == 'ptr':            # data (0.5): the lock get, set, clear and turn
            # (+0x48..+0x54) of the synth, FX and CV parameter sets' vtables; the turn is shared;
            # and (part 2) their short and long names (+0x68, +0x6c), shared too; and (0.7, ui.c)
            # their two value setters (+0x30, +0x38), ev_param_set's
            ptrs = {}
            for vt, (g, st, c) in ((0x401a164c, (0x400a0190, 0x400a0218, 0x400a02a4)),
                                   (0x401a16ec, (0x400a044a, 0x400a04d2, 0x400a055e)),
                                   (0x401a178c, (0x400a0704, 0x400a078c, 0x400a0818))):
                ptrs.update({vt + 0x48: g, vt + 0x4c: st, vt + 0x50: c, vt + 0x54: 0x4009e74c,
                             vt + 0x68: 0x4009e79e, vt + 0x6c: 0x4009e7c2})
            ptrs.update({0x401a1684: 0x400a0cec, 0x401a1724: 0x400a0e1e, 0x401a17c4: 0x400a0f08,
                         0x401a167c: 0x400a0046, 0x401a171c: 0x400a0308, 0x401a17bc: 0x400a05c2})
            assert a in ptrs and want == ptrs[a].to_bytes(4, 'big'), s
            continue
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
        assert sb(x, img) == '4eb9' + d[k][2:].lower().rjust(8, '0'), t
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
        assert x['op'] == 'jmp' and int(d[k], 16) == int(x['addr'], 16) + x['len'], t
    pages = {x['target']: (sb(x, img), x['stock_sha256'])[1] for x in mod['sites'] if x.get('target', '').startswith('core_pa')}
    assert pages['core_page_info'] == '248c76631d87a332e3da30dd7cfa93feae90d47092e46072d6a5b0e04a609461'
    assert pages['core_param_get'] == pages['core_param_delta'] == 'd43e64e0210fdd98e9dd8476d7cea57c93492ec611d50eddcbf6fd90ea80cef7'
    assert pages['core_param_text'] == 'f0a59a90d819068375330c854f0ecd4610de7653746fd59369a25fc9cedcaea6'
    # pages whose entry a function answers (core-a4mk2 0.3, core-ar2 0.6's), asked before core_pages
    # (with the views' room as +Drive spares, tests/test_units.py)
    assert all(mod['collections'].get(c) == {'entry': 4, 'spare': 16} for c in ('core_page_fns', 'core_pages'))
    # the audio load (0.2): core times its own render events, first in and last out
    sub = {x['event']: x for x in mod['subscribe']}
    assert sub['ev_render_in']['fn'] == 'core_load_in' and sub['ev_render_in']['order'] == 0
    assert sub['ev_render_out']['fn'] == 'core_load_out' and sub['ev_render_out']['order'] == 100
    # 0.6: the modified mark's and the screen calls' handlers (store.c, screen.c): the brain from
    # ev_key before every mod, the LEVEL turn from ev_enc after every mod, the controller from ev_tick
    subs = {(x['event'], x['fn']): x['order'] for x in mod['subscribe']}
    assert subs[('ev_tick', 'core_edit_tick')] == 0 and subs[('ev_tick', 'core_screen_tick')] == 1
    assert subs[('ev_key', 'core_screen_key')] == -1000000 and subs[('ev_enc', 'core_screen_enc')] == 1000000
    # views (0.2): the hook after the main screen's last view and the page
    # key's step, each resuming after its 8 bytes; and the current view's
    # range, 0..28, widened in the setter and the loader
    for k, t in (('VIEWS_RET', 'core_views'), ('CYCLE_RET', 'core_cycle')):
        x = [x for x in mod['sites'] if x.get('target') == t][0]
        assert x['op'] == 'jmp' and int(d[k], 16) == int(x['addr'], 16) + 8, t
    clamps = [x for x in mod['sites'] if x['op'] == 'bytes']
    assert len(clamps) == 5
    for x in clamps:
        was = sb(x, img)
        assert was[2:] == '1c' and x['new'] == was[:2] + '2c', x
        assert was[0] == '7' and int(was[1], 16) % 2 == 0, x     # moveq #28,Dn
    # mod storage's sites (core-a4mk2 0.1: 28 of the 36), and 0.2's 4 page
    # sites, 2 view sites and 5 range bytes, 0.4's OS upgrade restart, and
    # 0.5's 32 lock sites: 20 in code (rows.s; the voice apply among them) and 12 vtable entries; and 0.5's
    # 21 destination sites (dests.s): 15 in code and 6 vtable entries; and
    # 0.5's 2 MIDI sites (midi.s), the three sets' NRPN in and CC in, each a
    # jmp resuming after the 8 bytes it displaced; and 0.6's whole project
    # save (store.s) and three knob look sites (looks.s); and 0.7's 10 std1 MIDI and tick sites (stdmidi.s,
    # checked in test_units) and std1 voice frees (voice.s): a jsr over the cut's `jsr 0x40105410` and over
    # the note length's `move.l d2,-(sp); jsr (a1); addq.l #4,sp`
    std07 = [x for x in mod['sites'] if x.get('target', '').startswith('core_std_')]
    assert len(std07) == 10
    voice07 = [x for x in mod['sites'] if x.get('target') in ('core_voice_free_cut', 'core_voice_free_len')]
    assert [(x['addr'], x['op'], (sb(x, img), x['stock_sha256'])[1]) for x in voice07] == [
        ('0x40102174', 'jsr', '8c34f4568e59ebc95fa5468e2b433c2f1a71973e9b734774fc9c7f3c8be371a5'), ('0x40102e10', 'jsr', '40aac3f95f8913dd855bc9c53c5f13fb6ff983e2e6cef949300f242bd3b2dd71')]
    # and 0.7's 70 UI and system sites (ui.s, ui.c; tests/test_units.py, test_core_a4mk2_0_7_ui_sites),
    # 0.8's ev_track_key: a jmp over the sequencer view's key-code call (trackkey.s), ev_voice_params over
    # the audio interrupt's VCA and pan call and ev_sound_setup over SOUND SETUP's next-row read (sound.s),
    # and the two +Drive sites: main()'s call at 0x401061e8 (ev_boot_drive) and FORMAT +DRIVE's
    # constant load at 0x40074af6 (ev_drive_format)
    track08 = [x for x in mod['sites'] if x.get('target') in ('core_track_key_site', 'core_voice_params_site',
                                                                'core_sound_setup_site')]
    assert [(x['addr'], x['op'], (sb(x, img), x['stock_sha256'])[1]) for x in track08] == [
        ('0x400c8ba2', 'jmp', '71b362f711c113689fd93a04c5b98ab51fd214ec779f9ad2bcbfc646d856b734'),
        ('0x40103af6', 'jmp', 'd92ee75213456a7a13d91048d49cefad7ddab6b7d7f7d56eaa61332f042ae107'),
        ('0x400e62c8', 'jmp', 'e8aa53b410decd67ab49630aef6dbe1806885013588d331d080875d7458a7113')]
    assert len(mod['sites']) == 117 + len(voice07) + 70 + len(track08) + 2
    drive = [x for x in mod['sites'] if x.get('target') in ('core_boot_drive_site', 'core_drive_format_site')]
    assert [(x['addr'], x['op'], (sb(x, img), x['stock_sha256'])[1]) for x in drive] == [('0x401061e8', 'jsr', '2191bc0fbd58c2ca2de73154f96cbeefab902e00e9e39d779cd79165b2d2a25c'),
                                                                ('0x40074af6', 'jsr', 'd591193fb5976f498f1504a122969002d042106a574926de3451910bc943c49d')]
    midi = [x for x in mod['sites'] if x.get('target') in ('core_nrpn_site', 'core_cc_site')]
    assert [(x['addr'], x['op'], (sb(x, img), x['stock_sha256'])[1]) for x in midi] == \
        [('0x4009ff56', 'jmp', 'ba7573adceae0b4d98e7fca043628b41525f9bf3ec50663e051c89a57a79cb35'), ('0x400a0b62', 'jmp', '61767a52ec7cf722a3778309a6df16a655ee5b19ddbd6f1d0d24986a897cd064')]
    with open(os.path.join(ROOT, 'mods', 'core-a4mk2', 'midi.s')) as fh:
        src = fh.read()
    assert 'jmp     0x4009ff5e' in src and 'jmp     0x400a0b6a' in src
    assert mod['collections'].get('core_lock_ccs') == 8
    new06 = [x for x in mod['sites'] if x.get('target') in ('st_proj_save', 'core_uirec_hook', 'core_range_hook',
                                                           'core_graphic_hook')]
    assert [(x['addr'], (sb(x, img), x['stock_sha256'])[1]) for x in new06] == [
        ('0x400a771a', '3637a4a24bbaa1fed7a0d33212cb843b64576c085b1121fa1be80bca77fbc54f'),
        ('0x400f6232', '5e7e9348e98e7718ea0935dab80aaad44b1dd7b6edfd3826c5ebb50acaefe751'),
        ('0x40007798', 'd6c9f1f60272197a17511f496fc19dd21ddd7df5f1f1ec0cab7a16cad60b0609'),
        ('0x4009eaaa', '4c0570fc6ef0682508e64de2367e111c27f895b6b78e58c300b3f2e7b8010118')]
    with open(os.path.join(ROOT, 'mods', 'core-a4mk2', 'looks.s')) as fh:
        assert 'jmp     0x4009eab2' in fh.read()
    ui = [x for x in mod['sites'] if x.get('target', '').startswith((
        'core_boot_done_site', 'core_idle_start', 'core_crash_install', 'core_usb_site', 'core_key_leds_site',
        'core_pset', 'core_pattern_', 'core_mute_site', 'core_knob_site', 'core_clip_', 'core_view_push_site',
        'core_view_sweep_site', 'core_view_remove_site', 'core_view_replace_', 'core_level_site', 'core_mixer_site',
        'core_analog_frame_site', 'core_modset_'))]
    assert len(ui) == 70                # 0.7's UI and system sites (ui.s, ui.c)
    modset = [x for x in ui if x['target'].startswith('core_modset_')]
    assert len(modset) == 6 and all(x['op'] == 'jmp' and sb(x, img).startswith('4e56') for x in modset)
    for x in ui:
        assert x['len'] >= (4 if x['op'] == 'ptr' else 6), x
        if x['op'] == 'keep2':
            assert sb(x, img).startswith('4eb9'), x
    first = [y for y in j['sites'] if y.get('target') not in ('core_boot_drive_site', 'core_drive_format_site')][:48]
    locks = [x for x in mod['sites'] if x['addr'] not in {y['addr'] for y in first} and x not in midi
             and x not in new06 and x not in std07 and x not in voice07 and x not in ui
             and x not in track08 and x not in drive]
    dests = [x for x in locks if x.get('target', '').startswith(('core_dst', 'core_id_index', 'core_index_id',
                                                                   'core_push_', 'core_set_'))]
    assert len(dests) == 21 and len([x for x in dests if x['op'] == 'ptr']) == 6
    locks = [x for x in locks if x not in dests]
    assert len(locks) == 32 and len([x for x in locks if x['op'] == 'ptr']) == 12
    # the event accept is a jsr over stock's `lea 0x80006178,a0`, which core's
    # stub does before it returns (rows.s)
    x = [x for x in locks if x.get('target') == 'core_accept'][0]
    assert x['op'] == 'jsr' and (sb(x, img), x['stock_sha256'])[1] == '1952aca0a49462dd5f4afaf4a654e29c405b6dbcdf89426cd8a017fc30dd0622', x
    x = [x for x in mod['sites'] if x.get('target') == 'core_finish_drain'][0]
    assert x['op'] == 'jsr' and x['len'] == 10 and sb(x, img).endswith(d['FINISH_DRAIN'][2:].lower().rjust(8, '0')), x
    assert len([x for x in mod['sites'] if x.get('target', '').startswith('st_')]) == 29


def test_core_a4mk2_store_bodies():
    """store.s: each jmp site's displaced instructions, then a jump back to
    the site plus the bytes it displaced (8 for each)."""
    import re
    with open(os.path.join(ROOT, 'mods', 'core-a4mk2', 'mod.json')) as fh:
        sites = {x['target']: x for x in json.load(fh)['sites'] if x['op'] == 'jmp'}
    with open(os.path.join(ROOT, 'mods', 'core-a4mk2', 'store.s')) as fh:
        src = fh.read()
    bodies = re.findall(r'^(st_\w+)_body:\s*\|\s*(0x[0-9a-f]+).*?^\s+jmp\s+(0x[0-9a-f]+)', src, re.M | re.S)
    assert len(bodies) == 15
    for name, addr, back in bodies:
        x = sites[name]
        assert int(x['addr'], 16) == int(addr, 16), name
        assert int(back, 16) == int(addr, 16) + x['len'] == int(addr, 16) + 8, name


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
    assert hashlib.sha256(at(img, 0x4007da66, 6)).hexdigest() == '4b33e8bb6b8082d96980a4dd3b1bf5d43fb28f4dc4764fdb142e39a1f95eef4e'        # a3 = 0x80000
    assert hashlib.sha256(at(img, 0x4011b008, 6)).hexdigest() == '037dfd3d3c2c02d05b013422de64bc14544ac4b7ebedb40d1a0dba242f3c0ddc'        # the bootstrap at 0x40000


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
