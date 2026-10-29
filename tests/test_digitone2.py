# SPDX-License-Identifier: GPL-2.0-or-later
"""The Digitone II: its profile, its sealed file, whole builds
(pytest, or run with python).

Needs the stock files, which never go in the repo, named by environment
variables; a test whose input is missing is skipped, not passed:
  ELEKLOADER_DN2_SYX   Digitone_II_OS1.12.syx
  ELEKLOADER_DN2_ZIP   Digitone_II_OS1.12_dist.zip (Elektron's download)
  ELEKLOADER_DN2_CORE_01  historical core-dn2 0.1 .elemod (compatibility)
  ELEKLOADER_DN2_DIGIHEALTH  existing DigiHealth .elemod (compatibility)
"""
import dataclasses
import hashlib
import hmac
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from elekloader import devices, elemod, formats, link, patch, syx   # noqa: E402
from elekloader.codec import elz, transport                         # noqa: E402
from elekloader.elemod import sha                                   # noqa: E402

SYX = os.environ.get('ELEKLOADER_DN2_SYX', '')
ZIP = os.environ.get('ELEKLOADER_DN2_ZIP', '')
CORE_01 = os.environ.get('ELEKLOADER_DN2_CORE_01', '')
DIGIHEALTH = os.environ.get('ELEKLOADER_DN2_DIGIHEALTH', '')
DEV = [d for d in devices.devices() if d.key == 'digitone-mk2'][0]
REL = DEV.releases['1.12']
DT2 = [d for d in devices.devices() if d.key == 'digitakt-mk2'][0]


class Skip(Exception):
    pass


def need(p, var='ELEKLOADER_DN2_SYX'):
    if not p or not os.path.exists(p):
        raise Skip('missing %s' % (p or var))
    with open(p, 'rb') as fh:
        return fh.read()


_c = {}


def stock():
    if 'st' not in _c:
        _c['raw'] = need(SYX)
        _c['st'], dev, rel = formats.load(_c['raw'])
        assert dev is DEV and rel == REL
        _c['img'] = formats.main_image(_c['st'], DEV)
    return _c['st'], _c['img']


def expect(fn, *words):
    try:
        fn()
    except (formats.FormatError, elemod.ModError, patch.PatchError, syx.SyxError) as e:
        for w in words:
            assert w in str(e), 'expected %r in: %s' % (w, e)
        return str(e)
    raise AssertionError('not refused')


def bundle(sites=(), blob=b'', version=None, target=None, regions=()):
    """A format-1 mod for the Digitone II: data `sites` [(addr, new)], a blob."""
    st, img = stock()
    doc = {'elemod': 1, 'id': 'dn2-test', 'version': '1',
           'target': target or devices.target_of(DEV, REL), 'sites': []}
    for addr, new in sites:
        o = addr - DEV.main_load
        doc['sites'].append({'addr': '0x%08x' % addr, 'len': len(new), 'kind': 'data',
                             'stock_sha256': sha(img[o:o + len(new)]), 'new': new.hex()})
    if blob:
        doc['blob'] = {'load': '0x%08x' % DEV.image_end(REL), 'len': len(blob),
                       'sha256': sha(blob), 'parts': [['hex', blob.hex()]]}
    if regions:
        doc['resources'] = {'regions': [{'name': n, 'lo': '0x%08x' % lo, 'hi': '0x%08x' % hi}
                                        for n, lo, hi in regions]}
    if version:
        doc['ele3_version'] = version
    return doc


def a_string_site(text=b'FACTORY RESET'):
    """(address, same-length replacement) for a string of the main OS."""
    st, img = stock()
    o = img.find(text)
    assert o > 0, text
    return DEV.main_load + o, text.lower()


def write_mod(tmp, doc, name='dn2-test.elemod'):
    p = os.path.join(tmp, name)
    with open(p, 'w') as fh:
        json.dump(doc, fh)
    return p


def build(doc):
    with tempfile.TemporaryDirectory() as tmp:
        return patch.build(SYX, [write_mod(tmp, doc)], log=lambda *a: None)


def reseal(st, container):
    """-> .syx bytes for `container` (an ELE3 container, trailer included)."""
    stream = (len(container).to_bytes(4, 'big')
              + transport.content_checksum(container).to_bytes(4, 'big') + container)
    return transport.encode_syx(stream, st.device_id, st.framing[0], st.framing[1])


def word(img, addr, n=4):
    return int.from_bytes(img[addr - DEV.main_load:addr - DEV.main_load + n], 'big')


# ---- the file ------------------------------------------------------------------------------

def test_stock_file_parses():
    st, img = stock()
    assert st.device_id == DEV.sysex_id and st.version == '1.12'
    assert [(s, d) for s, _o, _l, d in st.table] == [
        (5, 0), (2, 0x02010000), (3, DEV.main_load), (4, 0x80000400), (7, 0), (8, 0)]
    assert len(img) == REL.main_len and sha(img) == REL.main_sha256


def test_elektrons_zip_is_known():
    raw = need(ZIP, 'ELEKLOADER_DN2_ZIP')
    st, dev, rel = formats.load(raw)
    assert dev is DEV and rel == REL and st.sha256 == REL.syx_sha256


def test_the_seal_is_the_last_32_bytes_and_verifies():
    st, img = stock()
    assert st.total % 16 == 0 and st.total == max(o + l for _s, o, l, _d in st.table) + 8 + 32
    key = syx.seal_key(st, DEV)
    assert hmac.new(key, st.container[:-32], hashlib.sha256).digest() == st.container[-32:]


def test_the_os_checks_the_same_seal_and_the_checksum():
    """The upgrade validator 0x400dbdb0 runs the content checksum (0x401226f8)
    and then the seal (0x400d2bc4), whose key the OS derives from its own copy
    of the seed (0x40210a4a) and the constant (0x4021b9ef)."""
    st, img = stock()
    assert word(img, 0x400dbdb8 + 2) == 0x401226f8 and word(img, 0x400dbdca + 2) == 0x400d2bc4
    seed = img[0x40210a4a - DEV.main_load:][:11]
    assert seed == b'Multiplier\0' and word(img, 0x400d2bd0 + 2) == 0x40210a4a
    const = img[0x4021b9ef - DEV.main_load:][:32]
    a, b = hashlib.sha256(seed[:10]).digest(), hashlib.sha256(seed[9::-1]).digest()
    assert bytes(x ^ y ^ z for x, y, z in zip(const, a, b)) == syx.seal_key(st, DEV)


def test_writer_no_change_is_stock():
    st, img = stock()
    assert syx.write(st, st.stored[3], DEV) == st.raw


def test_inplace_depack_from_the_stage():
    st, img = stock()
    got, gap = syx.inplace_depack(st.stored[3], DEV)
    assert got == img and gap == 2128334


def test_a_repack_verifies_and_keeps_every_other_section():
    st, img = stock()
    packed = syx.pack_main(img)
    assert packed != st.stored[3] and elz.depack_section(packed) == img
    out = syx.write(st, packed, DEV, version='DN01')
    facts = syx.verify(out, st, img, DEV, version='DN01')
    assert facts['main']['inplace_min_gap'] > 0 and facts['flash_headroom'] > 0
    o = syx.Syx(out)
    assert all(o.stored[k] == st.stored[k] for k in (5, 2, 4, 7, 8))


def test_verify_refuses_a_broken_seal():
    st, img = stock()
    bad = bytearray(st.container)
    bad[-1] ^= 1
    expect(lambda: syx.verify(reseal(st, bytes(bad)), st, img, DEV), 'HMAC')
    syx.verify(reseal(st, st.container), st, img, DEV)        # the unbroken one passes


def test_writer_refuses_a_stock_file_whose_seal_fails():
    st, img = stock()
    bad = bytearray(st.container)
    bad[-1] ^= 1
    broken = syx.Syx(reseal(st, bytes(bad)))
    expect(lambda: syx.write(broken, st.stored[3], DEV), 'seal')


def test_writer_refuses_the_digitakt_iis_seed():
    st, img = stock()
    other = dataclasses.replace(DEV, hmac_key_from=DT2.hmac_key_from)
    expect(lambda: syx.write(st, st.stored[3], other), 'seed')


# ---- whole builds ----------------------------------------------------------------------------

def test_whole_build_data_site_and_blob():
    st, img = stock()
    site = a_string_site()
    blob = bytes(range(256)) * 8
    outputs, man = build(bundle([site], blob, version='DN01'))
    assert set(outputs) == {'syx'}
    o = formats.parse(outputs['syx'], DEV)
    new = formats.main_image(o, DEV)
    assert new[site[0] - DEV.main_load:][:len(site[1])] == site[1]
    assert new[len(img):] == blob and len(new) == len(img) + len(blob)
    assert o.version == 'DN01'
    assert all(o.stored[k] == st.stored[k] for k in (5, 2, 4, 7, 8))   # the rest stays stock
    assert hmac.new(syx.seal_key(st, DEV), o.container[:-32],
                    hashlib.sha256).digest() == o.container[-32:]
    assert man['output']['main']['inplace_min_gap'] > 0


def test_a_mod_for_another_firmware_is_refused():
    site = a_string_site()
    expect(lambda: build(bundle([site], target=devices.target_of(DT2, DT2.releases['1.17']))),
           'Digitakt II')


def test_a_build_past_the_flash_store_is_refused():
    """The OS keeps a store at flash 0x380000: the container must end below
    it. A main OS grown with incompressible bytes past the room (a blob that
    big is refused before this, at the DDR area's size)."""
    st, img = stock()
    room = DEV.flash_limit - DEV.flash_at - st.total
    big = img + os.urandom(room + 0x1000)
    out = syx.write(st, syx.pack_main(big), DEV)
    expect(lambda: syx.verify(out, st, big, DEV), '0x380000')
    room = DEV.ddr[1] - DEV.ddr[0]
    expect(lambda: build(bundle([], os.urandom(room + 1), version='DN02')), 'blob', 'allows')
    build(bundle([], bytes(room), version='DN02'))                 # the area's size fits if compressible


def test_a_build_over_the_measured_usb_update_limit_is_refused():
    room = DEV.ddr[1] - DEV.ddr[0]
    expect(lambda: build(bundle([], os.urandom(room), version='DN02')),
           'decoded update stream', 'measured USB update limit of 2097152 bytes')


def test_regions_only_in_the_qualified_ddr():
    """A whole build's RAM goes in the one qualified area; SRAM is not
    qualified, and the Digitakt II's SRAM areas are refused here."""
    assert DEV.areas == {'ddr': (0x47F00000, 0x47F40000)}
    assert DEV.ddr == (0x47F00000, 0x47F40000) and DEV.blob_max is None   # the linker's area too
    assert DEV.sram_code == (0, 0) and not DEV.fast_table
    site = a_string_site()
    build(bundle([site], regions=[('state', 0x47F00000, 0x47F01000)], version='DN03'))
    for lo, hi in (DT2.areas['sram-tail'], DT2.areas['sram-block'], (0x47F3F000, 0x47F41000)):
        expect(lambda: build(bundle([site], regions=[('state', lo, hi)], version='DN03')),
               'outside every free area')


def test_linkable_mods_need_the_digitone_ii_core():
    """Format-2 mods link in the qualified area, with a core."""
    assert DEV.linkable()
    addr, new = a_string_site()
    st, img = stock()
    o = addr - DEV.main_load
    doc = {'elemod': 2, 'id': 'x', 'version': '1', 'target': devices.target_of(DEV, REL),
           'sections': {}, 'requires': ['core'],
           'sites': [{'addr': '0x%08x' % addr, 'len': len(new), 'kind': 'data',
                      'stock_sha256': sha(img[o:o + len(new)]), 'new': new.hex()}]}
    problems = link.check([link.Mod2(doc, 'x')], img)
    assert any('requires core' in p for p in problems), problems


# ---- memory: what the reset clears and copies, and what the OS names above it ----------------

def test_the_area_is_outside_what_the_os_clears_copies_and_uses():
    """The reset (0x400004e8) clears 0x402fd000-0x466b84d0, its heap inside,
    and copies the image's tail into SRAM. Above the clear, the OS's DMA
    buffers end at 0x46709340 (reached through the uncached alias; the last is
    a table of 16 at 0x4e709300), and the stack runs down from 0x48000000."""
    st, img = stock()
    assert word(img, 0x400004bc) == 0x402fd000 and word(img, 0x400004c2) == 0x466b84d0   # the clear
    assert word(img, 0x40000472) == 0x40305000 and word(img, 0x4000049a) == 0x4030c980   # the copies
    assert word(img, 0x400004f4) == 0x48000000                                           # the stack
    arena, end = word(img, 0x4029f0e0), word(img, 0x4029f0e8)                            # the heap
    assert end - arena == 0x2000000 and end <= 0x466b84d0
    assert word(img, 0x4000399e) == 0x4e709300                                           # the DMA top
    lo, hi = DEV.areas['ddr']
    assert 0x46709300 + 16 * 4 <= lo and hi <= 0x48000000 - 0x80000


# ---- the core (mods/core-dn2) and the example ------------------------------------------------

def built(moddir, tmp):
    """-> the .elemod mod.json in `moddir` builds to (needs the cross toolchain)."""
    import shutil
    from elekloader.sdk import build as sdkbuild
    need(SYX)
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', DEV.toolchain['prefix']) + 'as'):
        raise Skip('no cross assembler (ELEKLOADER_CROSS)')
    return sdkbuild.build(os.path.join(os.path.dirname(HERE), moddir), SYX, tmp)[0]


# core's sites (core-dn2 0.1): boot, UI events, menu builders, panel input/clock and SysEx observer
CORE_SITES = ((0x40000538, '4eb9400019b0'), (0x4002e464, '4eb94011d5d4'),
              (0x4002e4ca, '4eb94011d60a'), (0x4002f20c, '4eb94002c5c8'),
              (0x4002f256, '4eb94002c55c'), (0x4009ff88, '205248780002'),
              (0x400987a2, '4cef7c7c0018'), (0x4011fc8c, '4fefffbc48d77cfc'),
              (0x4011f8c4, '52b9466768b0'), (0x4012197a, '47f940207f9e'),
              # 0.3: frames, LEDs, kit and project data, kit copies
              (0x40027196, '4eb9400db390'), (0x4002c87a, '4ebafb4a2f02'),
              (0x400de0d4, '223cbacef00c'), (0x400ddddc, '223cbacef00c'),
              (0x400e18be, '26bcbeefbace'), (0x400e1928, '203cbeefbace'),
              (0x40134770, '226f0004206f0008202f000c'))
SITES_0_3 = CORE_SITES[10:]


def at(img, addr, n):
    return img[addr - DEV.main_load:addr - DEV.main_load + n].hex()


def test_the_cores_addresses_are_the_firmwares_calls():
    """Each defsym is the routine the firmware calls at its site, its only
    caller, with the arguments core.s expects: drawAll(ctrl, bmp), and
    Brain::key/enc(brain, event); the dirty check returns ctrl's byte +0x20,
    as core_tick does after its handlers. VBR_FN sets the vector base."""
    st, img = stock()
    with open(os.path.join(os.path.dirname(HERE), 'mods', 'core-dn2', 'mod.json')) as fh:
        sym = {k: int(v, 0) for k, v in json.load(fh)['defsym'].items()}
    for site, routine in ((0x4002e4ca, 'DRAWALL'), (0x4002f20c, 'KEYDISP'),
                          (0x4002f256, 'ENCDISP'), (0x40000538, 'VBR_FN')):
        assert img[site - DEV.main_load:][:2] == b'\x4e\xb9' and word(img, site + 2) == sym[routine]
        assert refs_to(img, sym[routine]) == [site], routine
    assert at(img, sym['VBR_FN'], 12) == '203c400000004e7b08014e75'
    for site, stock_bytes in CORE_SITES:
        assert at(img, site, len(stock_bytes) // 2) == stock_bytes
    assert at(img, 0x4002e4c6, 4) == '2f0b2f0a'                     # bmp (a3), then ctrl (a2)
    assert at(img, 0x4011d5d4, 10) == '206f0004102800204e75'         # return ctrl->dirty (+0x20)
    assert refs_to(img, 0x4011d5d4) == [0x4002e464]
    for site in (0x4002f20c, 0x4002f256):                            # the event (d2), then brain
        assert at(img, site - 6, 6) == '2f022f2eff80'


def test_the_menu_routines_are_the_builders_own():
    """SETTINGS (0x4009fbe0): the menu in a2, and the site's two instructions
    are what core_settings replays before SETTINGS_RET. PERSONALIZE
    (0x40097eac): its rows are core_additem's: new(0x54) through OP_NEW,
    ITEM_CTOR(item, label, select, draw, change, -1, 8), MENU_ADD(menu, item)
    with the menu in a4, each callback a 16-byte std::function with its
    manager at +8 and its invoker at +12; it ends with the movem, lea 0x80
    and rts personalize.s replays. FN_MGR is the manager of a plain function
    pointer: its type query gives PFSs15FileStorageInfoE's typeinfo."""
    st, img = stock()
    with open(os.path.join(os.path.dirname(HERE), 'mods', 'core-dn2', 'mod.json')) as fh:
        sym = {k: int(v, 0) for k, v in json.load(fh)['defsym'].items()}
    assert at(img, 0x4009fbe8, 4) == '246f0048'                     # a2 = the menu
    assert at(img, 0x4009ff88, 6) == '205248780002' and sym['SETTINGS_RET'] == 0x4009ff8e
    assert at(img, 0x40097eac, 4) == '4fefff98'                     # its 0x80-byte frame
    assert at(img, 0x400987a2, 12) == '4cef7c7c00184fef00804e75'
    assert at(img, 0x40097f06, 6) == '45f9%08x' % sym['OP_NEW']
    assert at(img, 0x40097fe4, 6) == '4bf9%08x' % sym['MENU_ADD']
    assert at(img, 0x4009806a, 6) == '4df9%08x' % sym['ITEM_CTOR']
    assert at(img, 0x4009876c, 4) == '48780054'                     # new(0x54)
    assert at(img, 0x40098772, 8) == '487800084878ffff'             # step 8, id -1
    assert at(img, 0x4009878e, 4) == '2f0c4e95'                     # MENU_ADD(a4, item)
    assert at(img, 0x40098764, 8) == '2f4800642f400068'             # manager +8, invoker +12
    mgr = sym['FN_MGR']
    assert at(img, mgr + 0x1e, 2) == '20bc'                         # op 0: *a0 = &typeinfo
    ti = word(img, mgr + 0x20)
    name = word(img, ti + 4)
    assert img[name - DEV.main_load:][:23] == b'PFSs15FileStorageInfoE\0'


# each site's hook: the instruction and the handler it must reach (fixed here, not read from
# mod.json, so a site dropped from the manifest fails too)
CORE_HOOKS = {0x40000538: ('4eb9', 'boot'), 0x4002e464: ('4eb9', 'core_tick'),
              0x4002e4ca: ('4eb9', 'core_draw'), 0x4002f20c: ('4eb9', 'core_key'),
              0x4002f256: ('4eb9', 'core_enc'), 0x4009ff88: ('4ef9', 'core_settings'),
              0x400987a2: ('4ef9', 'core_personalize'),
              0x4011fc8c: ('4ef9', 'core_panel_buttons'),
              0x4011f8c4: ('4eb9', 'core_panel_clock'),
              0x4012197a: ('4eb9', 'core_sysex'),
              0x40027196: ('4eb9', 'core_frame'), 0x4002c87a: ('4eb9', 'core_key_leds'),
              0x400de0d4: ('4eb9', 'core_kit_save'), 0x400ddddc: ('4eb9', 'core_kit_load'),
              0x400e18be: ('4eb9', 'core_proj_save'), 0x400e1928: ('4eb9', 'core_proj_load'),
              0x40134770: ('4ef9', 'core_memcpy')}


def assert_hooked(new, man):
    """Every core site is its jsr/jmp to its own handler, by the link's map:
    a missing or misdirected hook fails, though stock already has a jsr at
    most of the sites."""
    assert set(CORE_HOOKS) == {a for a, _ in CORE_SITES}
    for a, (op, target) in CORE_HOOKS.items():
        want = op + '%08x' % man['_map'][target]
        assert at(new, a, 6) == want, (hex(a), target, at(new, a, 6), want)
        if a == 0x4011fc8c:
            assert at(new, a + 6, 2) == '4e71'  # the eight-byte prologue, NOP-padded
        if a == 0x40134770:
            assert at(new, a + 6, 6) == '4e71' * 3  # memcpy's twelve bytes, NOP-padded


def test_core_builds_links_and_verifies():
    """mods/core-dn2 alone: only its declared sites and the appended image change,
    nothing else; its RAM is in the area; every other section is stock."""
    with tempfile.TemporaryDirectory() as tmp:
        core = built(os.path.join('mods', 'core-dn2'), tmp)
        outputs, man = patch.build(SYX, [core], version='CO01', log=lambda *a: None)
    st, img = stock()
    o = formats.parse(outputs['syx'], DEV)
    new = formats.main_image(o, DEV)
    changed = [i for i in range(len(img)) if new[i] != img[i]]
    sites = [(s - DEV.main_load, len(stock) // 2) for s, stock in CORE_SITES]
    assert changed and all(any(s <= i < s + width for s, width in sites) for i in changed)
    assert_hooked(new, man)
    lay = man['link']['layout']
    assert DEV.ddr[0] == lay['ddr'][0] < lay['bss'][1] <= DEV.ddr[1]
    assert lay['boot'][0] == DEV.image_end(REL) and len(new) > len(img)
    assert all(o.stored[k] == st.stored[k] for k in (5, 2, 4, 7, 8))
    assert man['output']['main']['inplace_min_gap'] > 0


def test_the_example_links_with_core():
    """examples/hello-marker-dn2: one ev_draw handler; no sites of its own."""
    with tempfile.TemporaryDirectory() as tmp:
        core = built(os.path.join('mods', 'core-dn2'), tmp)
        hello = built(os.path.join('examples', 'hello-marker-dn2'), tmp)
        outputs, man = patch.build(SYX, [core, hello], version='HM01', log=lambda *a: None)
        tables = man['link']['tables']
        assert tables['ev_draw']['entries'] == 1
        assert all(tables[t]['entries'] == 0
                   for t in ('ev_tick', 'ev_key', 'ev_enc', 'ev_settings', 'ev_personalize'))
        st, img = stock()
        new = formats.main_image(formats.parse(outputs['syx'], DEV), DEV)
        changed = {i for i in range(len(img)) if new[i] != img[i]}
        assert changed <= {s - DEV.main_load + k for s, stock in CORE_SITES for k in range(len(stock) // 2)}
        assert_hooked(new, man)
        # without core, or with two, or on core's site, or with .fast code: refused
        expect(lambda: patch.build(SYX, [hello], version='HM01', log=lambda *a: None), 'core')
        with open(core) as fh:
            c2 = json.load(fh)
        c2['id'] = 'core2'
        p2 = write_mod(tmp, c2, 'core2-0.1.elemod')
        expect(lambda: patch.build(SYX, [core, p2], version='HM01', log=lambda *a: None), 'exactly one')
        with open(hello) as fh:
            doc = json.load(fh)
        st, img = stock()
        a = 0x4002e4ca - DEV.main_load
        clash = dict(doc, id='clash', sites=[{'addr': '0x4002e4ca', 'len': 6, 'kind': 'data',
                                              'stock_sha256': sha(img[a:a + 6]), 'new': '4e714e714e71'}])
        clash.pop('signature', None)
        p3 = write_mod(tmp, clash, 'clash-1.0.elemod')
        expect(lambda: patch.build(SYX, [core, p3], version='HM01', log=lambda *a: None), 'overlap')
        doc['sections']['.fast'] = doc['sections'].pop('.run')
        doc['relocs'] = [[('.fast' if r[0] == '.run' else r[0])] + r[1:] for r in doc['relocs']]
        doc['symbols'] = {k: (['.fast', v[1]] if v[0] == '.run' else v) for k, v in doc['symbols'].items()}
        doc.pop('signature', None)
        p4 = write_mod(tmp, doc, 'hello-fast-1.0.elemod')
        expect(lambda: patch.build(SYX, [core, p4], version='HM01', log=lambda *a: None), '.fast')


def test_hooks_check_links_with_core():
    """examples/hooks-check-dn2: a handler on each of the six events, two on
    ev_key in their order, a row in SETTINGS and in PERSONALIZE. An event
    core-dn2 does not have, or a second mod with the same row, is refused."""
    with tempfile.TemporaryDirectory() as tmp:
        core = built(os.path.join('mods', 'core-dn2'), tmp)
        hc = built(os.path.join('examples', 'hooks-check-dn2'), tmp)
        outputs, man = patch.build(SYX, [core, hc], version='HC01', log=lambda *a: None)
        tables = man['link']['tables']
        assert {t: tables[t]['entries'] for t in ('ev_tick', 'ev_draw', 'ev_key', 'ev_enc',
                                                  'ev_settings', 'ev_personalize')} == {
            'ev_tick': 1, 'ev_draw': 1, 'ev_key': 2, 'ev_enc': 1, 'ev_settings': 1, 'ev_personalize': 1}
        o = formats.parse(outputs['syx'], DEV)
        new = formats.main_image(o, DEV)
        assert_hooked(new, man)
        key = int(tables['ev_key']['at'], 16)
        lay = man['link']['layout']
        run = new[lay['run_load'] - DEV.main_load:]                     # the DDR image as loaded
        entries = [int.from_bytes(run[key - DEV.ddr[0] + 4 * i:][:4], 'big') for i in range(3)]
        with open(hc) as fh:
            doc = json.load(fh)
        first, second = (lay['sections']['hooks-check .run'] + doc['symbols'][f][1]
                         for f in ('hc_first', 'hc_key'))
        assert entries == [first, second, 0]                             # order 40, then 60
        tick = [c for c in doc['contribute'] if c['to'] == 'ev_tick'][0]
        d = dict(doc, contribute=doc['contribute'] + [dict(tick, to='ev_render_in')])
        d.pop('signature', None)
        p = write_mod(tmp, d, 'hc-render-0.1.elemod')
        expect(lambda: patch.build(SYX, [core, p], version='HC01', log=lambda *a: None), 'ev_render_in')
        d = json.loads(json.dumps(doc))                                  # RAM past the 256 KB area
        d['sections']['.bss']['size'] = DEV.ddr[1] - DEV.ddr[0]
        d.pop('signature', None)
        p = write_mod(tmp, d, 'hc-big-0.1.elemod')
        expect(lambda: patch.build(SYX, [core, p], version='HC01', log=lambda *a: None), 'RAM')
        d = dict(doc, id='hooks-check-2')
        d.pop('signature', None)
        p = write_mod(tmp, d, 'hooks-check-2-0.1.elemod')
        expect(lambda: patch.build(SYX, [core, hc, p], version='HC01', log=lambda *a: None),
               'settings:HOOK TEST')


def test_core_keeps_the_0_1_control_contract():
    """Each later core keeps every 0.1 site, symbol, table, export and claim as
    it was: 0.2 added the dormant SHARC bridge, 0.3 the sites SITES_0_3 and
    what they need (its new sources come last, so nothing of 0.1 moves)."""
    old = json.loads(need(CORE_01, 'ELEKLOADER_DN2_CORE_01'))
    with tempfile.TemporaryDirectory() as tmp:
        with open(built(os.path.join('mods', 'core-dn2'), tmp)) as fh:
            new = json.load(fh)
    assert old['version'] == '0.1' and new['version'] == '0.3'
    for key in ('target', 'contribute', 'resources', 'copied', 'weak'):
        assert new[key] == old[key], key
    assert new['sites'][:len(old['sites'])] == old['sites']
    assert [int(x['addr'], 0) if isinstance(x['addr'], str) else x['addr']
            for x in new['sites'][len(old['sites']):]] == [a for a, _ in SITES_0_3]
    assert all(new['symbols'].get(k) == v for k, v in old['symbols'].items())
    assert set(old['imports']) <= set(new['imports']) and set(old['exports']) <= set(new['exports'])
    assert all(new['collections'].get(k) == v for k, v in old['collections'].items())
    assert 'sharc' not in old and new['sharc']['entries'] is not None


def check_control_side_kept(old, new, mods):
    """The same mods built with an older core and with this one: the stock
    image is the same but at 0.3's sites and at the mods' own (whose targets
    moved with core's larger RAM image), and the DSP section is stock."""
    images = []
    for core in (old, new):
        output, _man = patch.build(SYX, [core] + list(mods), version='CP02', log=lambda *a: None)
        parsed = formats.parse(output['syx'], DEV)
        assert parsed.stored[7] == stock()[0].stored[7]
        images.append(formats.main_image(parsed, DEV))
    st, img = stock()
    skip = {a - DEV.main_load + k for a, b in SITES_0_3 for k in range(len(b) // 2)}
    for m in mods:
        for site in elemod.load_any(m).sites:
            addr = int(site['addr'], 0) if isinstance(site['addr'], str) else site['addr']
            skip |= {addr - DEV.main_load + k for k in range(site['len'])}
    differ = [i for i in range(len(img)) if images[0][i] != images[1][i] and i not in skip]
    assert not differ, [hex(DEV.main_load + i) for i in differ[:8]]


def test_core_keeps_existing_control_firmware():
    need(CORE_01, 'ELEKLOADER_DN2_CORE_01')
    with tempfile.TemporaryDirectory() as tmp:
        core = built(os.path.join('mods', 'core-dn2'), tmp)
        check_control_side_kept(CORE_01, core, [])
        for example in ('hello-marker-dn2', 'hooks-check-dn2'):
            mod = built(os.path.join('examples', example), tmp)
            check_control_side_kept(CORE_01, core, [mod])


def test_core_keeps_existing_digihealth_firmware():
    need(CORE_01, 'ELEKLOADER_DN2_CORE_01')
    need(DIGIHEALTH, 'ELEKLOADER_DN2_DIGIHEALTH')
    with tempfile.TemporaryDirectory() as tmp:
        core = built(os.path.join('mods', 'core-dn2'), tmp)
        check_control_side_kept(CORE_01, core, [DIGIHEALTH])


def test_sharc_examples_require_and_select_core_0_2():
    """Both published examples refuse 0.1, build with 0.2, and choose it
    in the GUI regardless of discovery order or an already selected 0.1."""
    from elekloader import gui
    need(CORE_01, 'ELEKLOADER_DN2_CORE_01')
    with tempfile.TemporaryDirectory() as tmp:
        core = built(os.path.join('mods', 'core-dn2'), tmp)
        model = gui.LoaderModel(SYX, library=tmp, settings=os.path.join(tmp, 'settings.json'))
        for example in ('sharc-sine', 'sharc-sine-asm'):
            app = built(os.path.join('examples', example), tmp)
            assert elemod.load_any(app).needs_core == '0.2'
            expect(lambda: patch.build(SYX, [CORE_01, app], log=lambda *a: None),
                   'needs core 0.2 or newer, and this build has core 0.1')
            outputs, _man = patch.build(SYX, [core, app], version='SC02', log=lambda *a: None)
            assert formats.parse(outputs['syx'], DEV).stored[7] != stock()[0].stored[7]
            for order in ((CORE_01, core, app), (app, core, CORE_01)):
                descs = {p: model.describe(p) for p in order}
                assert all(d['fits'] and 'error' not in d for d in descs.values())
                assert gui.with_core(descs, {app}) == {app, core}
                assert gui.with_core(descs, {app, CORE_01}) == {app, core}


def test_the_window_decodes_section_7_only_for_a_sharc_build():
    """Choosing the stock file leaves the DSP section packed; the first
    build with a SHARC mod decodes it, once, and it is stock's."""
    from elekloader import gui
    stock()
    with tempfile.TemporaryDirectory() as tmp:
        model = gui.LoaderModel(SYX, library=tmp, settings=os.path.join(tmp, 'settings.json'))
        assert model.stock['ok'] and model._sharc is None
        dsp = model.sharc_stream()
        assert sha(dsp) == REL.sharc['decoded_sha256'] and model.sharc_stream() is dsp


# ---- the flash: what the firmware reads and writes there (docs/DEVICES.md) ------------------

def refs_to(img, target):
    """-> addresses of every jsr/jmp abs.l, jsr/bsr.w pc-relative, bra.w and
    lea abs.l (for a later jsr (An)) to `target`."""
    out = []
    for i in range(0, len(img) - 6, 2):
        a = DEV.main_load + i
        op = img[i:i + 2]
        if (op in (b'\x4e\xb9', b'\x4e\xf9') or (op[0] & 0xf1, op[1]) == (0x41, 0xf9)) \
                and int.from_bytes(img[i + 2:i + 6], 'big') == target:
            out.append(a)
        elif op in (b'\x4e\xba', b'\x61\x00', b'\x60\x00'):
            if a + 2 + int.from_bytes(img[i + 2:i + 4], 'big', signed=True) == target:
                out.append(a)
    return out


def test_the_bootstrap_stages_at_stage_from_flash_at():
    """The loader 0x80000596 (section 2 runs at 0x80000400, after a 4-byte
    prefix): pea stage ; ... addi.l #flash_at, d0 ; jsr flash read ; ...
    pea stage ; bsr depack (0x80000432). The Digitakt II's code, unchanged;
    it reads the container's header from flash_at and wants product 0x34."""
    st, img = stock()
    boot = st.section(2)
    at = lambda a: a - 0x80000400 + 4
    assert boot[at(0x800005c6):at(0x800005c6) + 6] == bytes.fromhex('4879') + DEV.stage.to_bytes(4, 'big')
    assert boot[at(0x800005d4):at(0x800005d4) + 6] == bytes.fromhex('0680') + DEV.flash_at.to_bytes(4, 'big')
    assert boot[at(0x800005e6):at(0x800005e6) + 6] == bytes.fromhex('4879') + DEV.stage.to_bytes(4, 'big')
    bsr = at(0x800005ec)
    assert boot[bsr:bsr + 2] == b'\x4e\xba'
    assert 0x800005ee + int.from_bytes(boot[bsr + 2:bsr + 4], 'big', signed=True) == 0x80000432
    assert boot[at(0x80005406):at(0x80005406) + 6] == bytes.fromhex('2f3c') + DEV.flash_at.to_bytes(4, 'big')
    assert boot[at(0x80005424):at(0x80005424) + 2] == bytes.fromhex('7034') and st.container[4:8] == b'\0\0\0\x34'


def test_the_settings_store_is_the_flash_limit():
    """The OS reads a header at flash 0x380000 and erases 0x380000 and
    0x3c0000 (move.l #imm,-(sp) before each call): the container must end
    below it."""
    st, img = stock()
    push = lambda v: bytes.fromhex('2f3c') + v.to_bytes(4, 'big')
    assert img.count(push(0x380000)) == 26 and img.count(push(0x3c0000)) == 12
    assert DEV.flash_limit == 0x380000
    # the upgrade erases whole sectors from flash_at: one cannot cross the limit
    assert (DEV.flash_limit - DEV.flash_at) % 0x40000 == 0


def test_every_flash_write_and_erase_in_the_os_is_known():
    """The OS's SPI NOR driver: write 0x401360a6, erase 0x40136122,
    0x4013618c (256 KB) and 0x401357ce (64 KB). Their only callers: the
    settings store (0x400ccb86-0x400cd21e), the factory console's upload
    (0x400cd5f2-0x400ce41c: 'READY FOR OS', the container at 0x80000), the
    in-OS upgrade (0x40139104: validated, then erase and write from 0x80000)
    and a migration (0x400f2022) that erases only the 0x400000, 0x800000 and
    0xc00000 sectors. None writes from the main OS image, so no range of it
    is a bootstrap the OS could re-flash."""
    st, img = stock()
    known = [(0x400ccb86, 0x400cd21e), (0x400cd5f2, 0x400ce41c), (0x40139104, 0x4013924c),
             (0x400f2022, 0x400f2168), (0x401360a2, 0x401361f6)]   # the last: the driver's own wrappers
    for routine in (0x401360a6, 0x40136122, 0x4013618c, 0x401357ce, 0x40135850, 0x401354d6):
        for c in refs_to(img, routine):
            assert any(lo <= c < hi for lo, hi in known), (hex(routine), hex(c))
    assert word(img, 0x40139114 + 2) == 0x400dbdb0             # the validator runs first
    assert img[0x4013912e - DEV.main_load:][:6] == bytes.fromhex('267c00080000')   # a3 = 0x80000
    assert word(img, 0x400cd8ea + 2) == DEV.flash_at
    assert [word(img, a) for a in (0x401ffafc, 0x401ffb00, 0x401ffb04)] == [0x400000, 0x800000, 0xc00000]
    assert all(a >= DEV.flash_limit for a in (0x400000, 0x800000, 0xc00000))


def test_the_version_the_unit_reports_is_the_containers():
    """The OS caches the container's first 0x20 bytes from flash 0x80000
    (0x40134804), as the Digitakt II does; there is no version string."""
    st, img = stock()
    assert img[0x40134816 - DEV.main_load:][:12] == bytes.fromhex('2f3c000800004eb9401353ae')
    assert st.version == '1.12' and b'\x001.12\x00' not in img     # (1.125 is a parameter's label)




def test_remote_input_uses_the_stock_panel_queue_and_periodic_callback():
    """Core 0.1: addresses follow producers and consumer, not nearby globals.
    The panel's queue pointer cell is not the queue object used for bounds.
    PIT2's callback advances the separate monotonic clock; DTIM0 stays off.
    """
    st, img = stock()
    with open(os.path.join(os.path.dirname(HERE), 'mods/core-dn2/mod.json')) as fh:
        mod = json.load(fh)
    sym = {k: int(v, 0) for k, v in mod['defsym'].items()}
    assert sym['KEY_CONTINUE'] == 0x4011fc94 and sym['KEY_STACK'] == 68
    assert sym['KEY_STATE'] == 0x445a2e30 and sym['UI_QUEUE'] == 0x4059e214
    assert sym['PANEL_ENCODER'] == 0x4011ff0c and sym['PANEL_COUNTER'] == 0x466768b0
    assert sym['SYSEX_SEND'] == 0x401236d2 and sym['SYSEX_TABLE'] == 0x40207f9e
    assert at(img, 0x4011fc8c, 14) == '4fefffbc48d77cfc242f00482e02'
    assert at(img, 0x4011fc9a, 6) == '41f9445a2e30'
    assert at(img, 0x4002f178, 12) == '48794059e2144eb940001928'
    assert at(img, 0x4002ee96, 12) == '48794059e2144eb94011fa7c'
    assert at(img, 0x4011fe10, 6) == '2f394029f0d0'
    assert at(img, 0x4011fa7c, 12) == '202f0004670623c04029f0d0'
    assert at(img, 0x4011ff0c, 12) == '4fefffe47227202f002048d7'
    assert [word(img, 0x401f3c10 + 4*i) for i in range(9)] == list(range(1, 10))
    assert at(img, 0x401200c4, 14) == '48780001487af7f24eb940002aa4'
    assert at(img, 0x4011f8c4, 6) == '52b9466768b0'
    assert at(img, 0x40002c5e, 22) == '303c432313c1fc04002d30bc0636720933c0fc088002'
    assert at(img, 0x4012194e, 24) == '4feffff048d70c0c226f0014222f0018242f001c71910c80'
    assert at(img, 0x40121978, 8) == '204947f940207f9e'
    assert at(img, 0x401236d2, 22) == '42004fefffe848d70c1c282f001c242f0020246f0024'
    assert mod['collections']['ev_sysex'] == 4 and sym['NO_DTIM0'] == 1


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
