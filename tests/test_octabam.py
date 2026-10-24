# SPDX-License-Identifier: GPL-2.0-or-later
"""elekloader.sdk.octabam: octabam modules -> Octatrack mods (pytest, or run with python).

Needs files named by environment variables; a test whose inputs are missing
is skipped, not passed:
  ELEKLOADER_OT_SYX    OCTATRACK_OS1.40C.syx
  ELEKLOADER_OCTABAM   optional: a sambanks/octabam checkout; without one, only
                       the synthetic module below is converted. The remix test
                       also runs octabam's own build there (its dsp_asm, m68k-elf).
                       OCTA303's test needs a checkout that has it (b085c4d).
Building and checking also needs the cross binutils (m68k-linux-gnu-as, -ld,
-objcopy); the machines tests also the cross compiler (mods/machines-ot is C).
"""
import os
import shutil
import sys
import tempfile
import textwrap
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from elekloader import devices, elemod, formats, link   # noqa: E402
from elekloader.sdk import build, octabam               # noqa: E402

SYX = os.environ.get('ELEKLOADER_OT_SYX', '')
OCTABAM = os.environ.get('ELEKLOADER_OCTABAM', '')
DEV = [d for d in devices.devices() if d.key == 'octatrack'][0]


class Skip(Exception):
    pass


_c = {}


def stock():
    if not SYX or not os.path.exists(SYX):
        raise Skip('missing ELEKLOADER_OT_SYX')
    if 'img' not in _c:
        st, dev, _rel = formats.load(SYX)
        _c['img'] = formats.main_image(st, dev)
    return _c['img']


def tools():
    for t in ('as', 'ld', 'objcopy'):
        if not shutil.which(os.environ.get('ELEKLOADER_CROSS', DEV.toolchain['prefix']) + t):
            raise Skip('no m68k cross binutils')


def core(out):
    if 'core' not in _c:
        _c['core'], _m = build.build(os.path.join(ROOT, 'mods', 'core-ot'), SYX,
                                     tempfile.mkdtemp())
    return _c['core']


# ---- a synthetic checkout: every mapping, no octabam needed ---------------------------------
# The sites are real 1.40C ones (octabam's FLEX SEEK BIND, FLEX SEEK BIND CTR and RLEN
# PLEN hook these), so their stock bytes check against the user's image.

MANIFEST = '''
from types import SimpleNamespace as NS

def emit(addr):
    return b"", ((0x4002fb10, bytes.fromhex("2f04487af710"),
                  bytes.fromhex("4eb9") + (addr + 8).to_bytes(4, "big")),
                 (0x400d3d4e, (65).to_bytes(4, "big"), (66).to_bytes(4, "big")))

CAVE = NS(label="a cave", cave_addr=None, pinned=b"", source="modules/fake/cave.s",
          hook_addr=0x4000f8cc, hook_stock=bytes.fromhex("4a2f00376718"),
          registers_formatter=None, emit=emit, defsyms=(("STOCK_RTS", 0x40027e1a),),
          cpu="5475", reference=None, pool_base_literals=1)
UNIT = NS(label="unit", source="modules/fake/unit.s", cave_addr=None, cpu="54455",
          reference=None, dram=True, include=None)
DETOUR = NS(site=0x4000f834, expect=bytes.fromhex("52aa009025480098"), unit="unit",
            symbol="fake_entry", note="", kind="jmp", target=None, pad_to=None,
            subst_return=False)
MODULE = NS(name="fake", key="FAKE", doc="a test module", dsp=None, menu=None, params=(),
            runtime=None, arena=None, overrides=(), cf_patches=(CAVE,), linked=(UNIT,),
            detours=(DETOUR,), symbol_refs=(), tables=(), pokes=(), requires=(),
            claims=NS(part_window=((0x90492, 20, "x"),), sram=()), category=None,
            author="test")
'''

CAVE_S = '''
        .text
        cmpi.l  #0x40a955e0,%d0         | the arena's stock base: follows the core's
        beq.s   1f
        rts
1:      jmp     STOCK_RTS
        nop
screen: rts
'''

UNIT_S = '''
        .text
        .globl  fake_entry
fake_entry:
        addq.l  #1,(144,%a2)
        move.l  %a0,(152,%a2)
        jmp     0x4000f83c
'''


# A bridge: it stands in for FAKE's detour at 0x4000f834, so it carries FAKE.
BRIDGE = '''
from types import SimpleNamespace as NS
UNIT = NS(label="bunit", source="modules/bridge/bunit.s", cave_addr=None, cpu="54455",
          reference=None, dram=True, include=None)
MODULE = NS(name="bridge", key="BRIDGE", doc="a bridge", dsp=None, menu=None, params=(),
            runtime=None, arena=None, cf_patches=(), linked=(UNIT,),
            overrides=(NS(site=0x4000f834, module="FAKE", write=None, defsym=None),),
            detours=(NS(site=0x4000f834, expect=bytes.fromhex("52aa009025480098"),
                        unit="bunit", symbol="bridge_entry", note="", kind="jmp", target=None,
                        pad_to=8, subst_return=False),),
            symbol_refs=(), tables=(), pokes=(), requires=(), claims=None, category=None,
            author="test")
'''

BUNIT_S = '''
        .text
        .globl  bridge_entry
bridge_entry:
        jmp     fake_entry
'''

# A unit whose generated include changes with FAKE (which converts) and with DSPMOD
# (which does not).
INCMOD = '''
from types import SimpleNamespace as NS

def inc(mods):
    return ".set HAS_FAKE, %d\\n.set HAS_DSP, %d\\n" % ("FAKE" in mods, "DSPMOD" in mods)

UNIT = NS(label="iunit", source="modules/incmod/iunit.s", cave_addr=None, cpu="54455",
          reference=None, dram=True, include=inc)
MODULE = NS(name="incmod", key="INCMOD", doc="an include", dsp=None, menu=None, params=(),
            runtime=None, arena=None, overrides=(), cf_patches=(), linked=(UNIT,), detours=(),
            symbol_refs=(), tables=(), pokes=(), requires=(), claims=None, category=None,
            author="test")
'''

IUNIT_S = '''
        .include "remix.inc"
        .text
        .globl  incmod_flags
incmod_flags:
        .byte   HAS_FAKE, HAS_DSP
'''

DSPMOD = '''
from types import SimpleNamespace as NS
MODULE = NS(name="dspmod", key="DSPMOD", doc="DSP", dsp=object(), menu=None, params=(),
            overrides=(), linked=(), cf_patches=(), requires=())
'''

# Pinned code: a cave and a unit at fixed addresses in the image's free run.
PINNED = '''
from types import SimpleNamespace as NS
CAVE = NS(label="pcave", cave_addr=0x400d64e0, pinned=b"", source="modules/pinned/pcave.s",
          hook_addr=0x4000f8cc, hook_stock=bytes.fromhex("4a2f00376718"),
          registers_formatter=None, emit=None, defsyms=(), cpu="5475", reference=None,
          pool_base_literals=0)
UNIT = NS(label="punit", source="modules/pinned/punit.s", cave_addr=0x400d6500, cpu="5407",
          reference=None, dram=False, include=None)
MODULE = NS(name="pinned", key="PINNED", doc="pinned code", dsp=None, menu=None, params=(),
            runtime=None, arena=None, overrides=(), cf_patches=(CAVE,), linked=(UNIT,),
            detours=(), symbol_refs=(), tables=(), pokes=(), requires=(), claims=None,
            category=None, author="test")
'''

PCAVE_S = '''
        .text
        tst.b   (55,%sp)
        jmp     punit_entry
'''

PUNIT_S = '''
        .text
        .globl  punit_entry
punit_entry:
        lea     punit_entry,%a0
        rts
'''


# DSP code reached only by a hook into stock DSP code (USB AUDIO IN's shape), with a ColdFire
# detour of its own: it converts only as part of a remix.
HOOKMOD = '''
from types import SimpleNamespace as NS
UNIT = NS(label="hunit", source="modules/hookmod/hunit.s", cave_addr=None, cpu="54455",
          reference=None, dram=True, include=None)
MODULE = NS(name="hookmod", key="HOOKMOD", doc="a hook", menu=None, params=(),
            dsp=NS(asm="modules/hookmod/inject.asm", payloads=frozenset({"A"}),
                   hooks=(NS(site=0x88, stock=(0x627000, 0x000204), label="inject"),)),
            runtime=None, arena=None, overrides=(), cf_patches=(), linked=(UNIT,),
            detours=(NS(site=0x4001de6e, expect=bytes.fromhex("23c0fc0b01c0"), unit="hunit",
                        symbol="hook_entry", note="", kind="jmp", target=None, pad_to=None,
                        subst_return=False),),
            symbol_refs=(), tables=(), pokes=(), requires=(), claims=None, category=None,
            author="test")
'''

HUNIT_S = '''
        .text
        .globl  hook_entry
hook_entry:
        rts
'''


# A source machine: two of the machine row's places and the chooser's row count (all
# machines-ot's), the kind table's FLEX entry, the CHROMATIC pitch (an intercept) and a site
# of its own (OCTA303's shape, at its sites).
MACHMOD = '''
from types import SimpleNamespace as NS
H = bytes.fromhex
UNIT = NS(label="munit", source="modules/machmod/munit.s", cave_addr=None, cpu="54455",
          reference=None, dram=True, include=None)

def D(site, expect, symbol, kind="jmp", pad_to=None):
    return NS(site=site, expect=H(expect), unit="munit", symbol=symbol, note="", kind=kind,
              target=None, pad_to=pad_to, subst_return=False)

MODULE = NS(name="machmod", key="MACHMOD", doc="a machine", dsp=None, menu=None, params=(),
            runtime=None, arena=None, overrides=(), cf_patches=(), linked=(UNIT,),
            detours=(D(0x400334d8, "2f02222f0008", "mm_name"),
                     D(0x4007981c, "77101084d081", "mm_commit"),
                     D(0x4004fc58, "45f2ac0471b9100b14cf", "mm_chrom", kind="jsr", pad_to=10),
                     D(0x4000c634, "226f007210309800", "mm_retrig", pad_to=8)),
            symbol_refs=(NS(addr=0x400d6438, expect=0x40004008, unit="munit", symbol="mm_render",
                            addend=0, note=""),),
            tables=(), pokes=(NS(addr=0x40079248, expect=H("48780005"), write=H("48780006"),
                                 note=""),),
            requires=(), claims=None, category=None, author="test")
'''

MUNIT_S = '''
        .text
        .set    STOCK_RENDER, 0x40004008        | the FLEX renderer
        .set    MM_LEN, 16
        .globl  mm_render, mm_name, mm_commit, mm_chrom, mm_retrig
mm_render:
        jmp     STOCK_RENDER
mm_name:
        rts
mm_commit:
        rts
mm_chrom:
        rts
mm_retrig:
        jmp     0x4000c63c
mm_build:
        lea     mm_desc, %a0
        move.l  %a0, %d0
        rts
mm_sign:
        rts
mm_title:
        .asciz  "MACH"
mm_defaults:
        .byte   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
        .balign 4
mm_desc:
        .fill   2 * MM_LEN, 1, 0
'''

MACHMOD_RECORD = dict(sig='MM', versions=(1, 2), label='mm_title', title='MACH',
                      flags=octabam.OT_PROTECT, defaults='mm_defaults', page='mm_desc + MM_LEN',
                      build='mm_build', assign='mm_sign',
                      stock_render=r'^(\s*\.set\s+STOCK_RENDER,\s*)0x40004008\b')


def fake_checkout(tmp):
    root = os.path.join(tmp, 'octabam')
    for rel, text in (('tools/remix/schema.py', ''), ('modules/fake/manifest.py', MANIFEST),
                      ('modules/machmod/manifest.py', MACHMOD), ('modules/machmod/munit.s', MUNIT_S),
                      ('modules/hookmod/manifest.py', HOOKMOD),
                      ('modules/hookmod/hunit.s', HUNIT_S),
                      ('modules/hookmod/inject.asm', '; DSP56300\n'),
                      ('modules/fake/cave.s', CAVE_S), ('modules/fake/unit.s', UNIT_S),
                      ('modules/bridge/manifest.py', BRIDGE), ('modules/bridge/bunit.s', BUNIT_S),
                      ('modules/incmod/manifest.py', INCMOD), ('modules/incmod/iunit.s', IUNIT_S),
                      ('modules/dspmod/manifest.py', DSPMOD),
                      ('modules/pinned/manifest.py', PINNED), ('modules/pinned/pcave.s', PCAVE_S),
                      ('modules/pinned/punit.s', PUNIT_S),
                      ('LICENSE', 'MIT, for the test\n')):
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w') as fh:
            fh.write(textwrap.dedent(text))
    return octabam.Octabam(root)


def test_the_mappings():
    img = stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        plan = octabam.convert(ob, 'fake', img, DEV)
    j = plan['json']
    by = {s['addr']: s for s in j['sites']}
    sym = 'ob_fake_a_cave'
    assert by['0x4000f8cc'] == {'addr': '0x4000f8cc', 'stock': '4a2f00376718', 'op': 'jsr',
                                'target': sym}
    # emit's poke that holds the cave's address + 8: a jsr against the cave, addend 8
    assert by['0x4002fb10']['op'] == 'jsr' and by['0x4002fb10']['addend'] == 8
    assert by['0x400d3d4e'] == {'addr': '0x400d3d4e', 'stock': '00000041', 'op': 'bytes',
                                'new': '00000042'}
    # a six-byte jmp would cut `move.l a0,(152,a2)`: padded to the instruction's end
    assert by['0x4000f834']['op'] == 'jmp' and len(by['0x4000f834']['stock']) == 16
    assert j['defsym'] == {'STOCK_RTS': '0x40027e1a'}
    assert j['requires'] == ['core'] and j['license'] == 'MIT'
    assert j['resources']['names'] == ['octabam:part-window@0x90490', 'octabam:part-window@0x904a0']
    assert b'cmpi.l  #arena_base' in plan['files']['modules/fake/cave.s']
    assert b'.include "modules/fake/cave.s"' in plan['files']['glue/%s.s' % sym]


def test_the_synthetic_module_builds_and_checks():
    img = stock()
    tools()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        plan = octabam.convert(ob, 'fake', img, DEV)
        d = octabam.write(ob, plan, tmp)
        assert os.path.exists(os.path.join(d, 'LICENSE.octabam'))
        path, mod = build.build(d, SYX, tmp)
        lines = octabam.check(ob, plan, path, core(tmp), SYX, os.path.join(tmp, 'check'))
    assert any('1 arena-base literal(s) moved' in x for x in lines), lines
    assert any('as GNU ld links them' in x for x in lines), lines
    assert 'arena_base' in mod.imports


def build_and_check(ob, name, tmp):
    plan = octabam.convert(ob, name, stock(), DEV)
    d = octabam.write(ob, plan, tmp)
    path, mod = build.build(d, SYX, tmp)
    lines = octabam.check(ob, plan, path, core(tmp), SYX, os.path.join(tmp, name + '.check'))
    return plan, mod, lines


def test_a_bridge_carries_what_it_bridges():
    """BRIDGE stands in for FAKE's detour: one mod with both, the bridge's detour at
    the shared site, and a conflict with FAKE alone."""
    stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        plan = octabam.convert(ob, 'bridge', stock(), DEV)
        j = plan['json']
        assert plan['members'] == ['bridge', 'fake'] and j['conflicts'] == ['octabam-fake']
        assert j['title'] == 'BRIDGE (with FAKE)'
        at = [x for x in j['sites'] if x['addr'] == '0x4000f834']
        assert at == [{'addr': '0x4000f834', 'stock': '52aa009025480098', 'op': 'jmp',
                       'target': 'bridge_entry'}], at
        tools()
        plan, mod, lines = build_and_check(ob, 'bridge', tmp)
    assert any('(1 stood in for)' in x for x in lines), lines
    assert 'bridge_entry' in mod.exports and 'fake_entry' in mod.exports


def test_a_generated_include_that_changes_with_another_module():
    stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        plan = octabam.convert(ob, 'incmod', stock(), DEV)
    j = plan['json']
    assert j['conflicts'] == ['octabam-fake'], j.get('conflicts')
    assert plan['files']['glue/incmod_iunit.inc'] == b'.set HAS_FAKE, 0\n.set HAS_DSP, 0\n'
    assert b'.include "glue/incmod_iunit.inc"' in plan['files']['modules/incmod/iunit.s']
    assert any('dspmod' in n and 'do not convert' in n for n in plan['notes']), plan['notes']


def test_pinned_code_stays_where_it_is_pinned():
    stock()
    tools()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        plan, mod, lines = build_and_check(ob, 'pinned', tmp)
    assert plan['json']['fixed'] == [
        {'source': 'modules/pinned/pcave.s', 'addr': '0x400d64e0', 'symbol': 'ob_pinned_pcave'},
        {'source': 'modules/pinned/punit.s', 'addr': '0x400d6500'}]
    assert mod.symbols['ob_pinned_pcave'] == ('abs', 0x400d64e0)
    assert mod.symbols['punit_entry'] == ('abs', 0x400d6500)
    assert not mod.size('.run')
    assert sum('fixed code' in x for x in lines) == 2, lines
    assert any('the same bytes for 5407 as for the chip' in x for x in lines), lines


def test_a_poke_into_relocatable_data_is_served_from_a_copy():
    """USB AUDIO OUT's poke of the device descriptor's class (0x400e2004, inside the
    protected bootloader copy): the mod serves its own copy through the descriptor's one
    reference, and the protected bytes stay stock."""
    from types import SimpleNamespace as NS
    img = stock()
    poke = NS(addr=0x400e2004, expect=bytes(3), write=bytes.fromhex('ef0201'), note='')
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        m = ob.modules['fake']
        ob.modules['fake'] = m.__class__(**dict(vars(m), pokes=(poke,)))
        plan = octabam.convert(ob, 'fake', img, DEV)
        sym = 'ob_fake_copy_400e2000'
        by = {s['addr']: s for s in plan['json']['sites']}
        assert '0x400e2004' not in by
        assert by['0x4001d82e'] == {'addr': '0x4001d82e', 'stock': '400e2000', 'op': 'ptr',
                                    'target': sym}
        want = bytearray(img[0x400e2000 - DEV.main_load:][:18])
        want[4:7] = poke.write
        assert plan['relocs'][0]['bytes'] == bytes(want)
        glue = plan['files']['glue/%s.s' % sym].decode()
        assert '.balign 32' in glue and ', '.join('0x%02x' % b for b in want) in glue
        tools()
        d = octabam.write(ob, plan, tmp)
        path, mod = build.build(d, SYX, tmp)
        lines = octabam.check(ob, plan, path, core(tmp), SYX, os.path.join(tmp, 'check'))
        ln = link.link([elemod.load_any(core(tmp)), elemod.load_any(path)], img)
    assert any('served from' in x and 'protected original is stock' in x for x in lines), lines
    lo, hi, _w = DEV.protected[0]
    assert ln.image[lo - DEV.main_load:hi - DEV.main_load] == \
        img[lo - DEV.main_load:hi - DEV.main_load]


def fake_remix(img, ref=None):
    """A remix of FAKE and HOOKMOD on a stock effect; its build (ref) writes, besides their
    sites, 8 bytes in the image's free run and a 2-byte list reference."""
    if ref is None:
        ref = bytearray(img)
        o = 0x400d7bbc - DEV.main_load
        ref[o:o + 8] = bytes(range(1, 9))
        o = 0x400375f6 - DEV.main_load
        ref[o:o + 2] = bytes([img[o] ^ 0x10, img[o + 1] ^ 0x01])
    return {'name': 'rx-test', 'doc': 'a test remix', 'modules': ('FAKE', 'HOOKMOD', 'FILTER'),
            'stock': frozenset({'FILTER', 'SPATIALIZER'}), 'harvested': ['SPATIALIZER'],
            'ref': bytes(ref)}


def test_a_remix_carries_its_modules_and_the_rest_of_its_build():
    """A remix converts as one mod: its modules (a hook-only DSP module among them, which
    alone refuses), and as data sites every other byte its build writes."""
    img = stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        assert 'DSP' in octabam.refusal(ob, 'hookmod')
        assert octabam.refusal(ob, 'hookmod', hooks=True) is None
        assert 'DSP' in octabam.refusal(ob, 'dspmod', hooks=True)    # no hooks: still refused
        plan = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img))
        j = plan['json']
        assert plan['members'] == ['fake', 'hookmod'] and plan['id'] == 'octabam-rx-test'
        assert j['conflicts'] == ['octabam-fake', 'octabam-hookmod']
        by = {s['addr']: s for s in j['sites']}
        assert by['0x400d7bbc'] == {'addr': '0x400d7bbc', 'stock': '00' * 8, 'op': 'bytes',
                                    'new': '0102030405060708'}
        assert by['0x400375f6']['op'] == 'bytes' and len(by['0x400375f6']['stock']) == 4
        assert by['0x4001de6e']['op'] == 'jmp'      # HOOKMOD's own detour, not a data site
        assert 'SPATIALIZER' in j['description']
        assert 'dsp/inject.asm' in plan['files']
        # a build that writes into the protected range refuses (outside a served copy)
        bad = bytearray(fake_remix(img)['ref'])
        bad[0x400e18f1 - DEV.main_load] ^= 1
        try:
            octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img, bad))
        except octabam.Refused as e:
            assert 'protects' in str(e), str(e)
        else:
            raise AssertionError('not refused')


def test_a_remix_on_the_dsp_bus():
    """--bus: a remix whose hook-only DSP code hooks P:0x88, and which gives up SPATIALIZER,
    leaves the hook, the menus and the effect's words to the DSP bus (mods/dspbus-ot): its
    DSP source goes into the mod, subscribed to ev_dsp_rx past its replay, and it requires the
    bus. A remix that gives up another effect, or hooks another site, is refused."""
    img = stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        plan = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img), bus=True)
        j = plan['json']
        assert j['requires'] == ['core', 'dspbus'] and j['version'].endswith('-bus')
        assert j['dsp'] == [{'source': 'dsp/inject.asm', 'payload': 'A'}]
        assert j['subscribe_dsp'] == [{'event': 'ev_dsp_rx', 'fn': 'inject', 'addend': 2,
                                       'order': octabam.BUS_ORDER}]
        assert 'dsp/inject.asm' in plan['files'] and 'DSP bus' in j['description']
        at = {s['addr'] for s in j['sites']}
        assert '0x4001de6e' in at                   # HOOKMOD's own detour stays
        assert not at & {'0x400d7bbc', '0x400375f6'}    # what its build writes there is the bus's
        rx = fake_remix(img)
        rx['harvested'] = ['PLATE REV']
        try:
            octabam.convert(ob, 'rx-test', img, DEV, rx, bus=True)
        except octabam.Refused as e:
            assert 'gives up PLATE REV, and the DSP bus gives up SPATIALIZER' in str(e), str(e)
        else:
            raise AssertionError('not refused')
        m = ob.modules['hookmod']
        m.dsp.hooks = (types.SimpleNamespace(site=0x90, stock=(0x627000, 0x000204),
                                             label='inject'),)
        try:
            octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img), bus=True)
        except octabam.Refused as e:
            assert 'its DSP hook at payload A P:0x90 is not one the DSP bus serves' in str(e), e
        else:
            raise AssertionError('not refused')
        # without --bus, as before: the build's bytes are the mod's
        m.dsp.hooks = (types.SimpleNamespace(site=0x88, stock=(0x627000, 0x000204),
                                             label='inject'),)
        j = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img))['json']
        assert 'dsp' not in j and j['requires'] == ['core']


def test_a_remix_is_checked_against_its_build_byte_for_byte():
    """The linked OS image must equal the remix's build in every byte, but for the address
    operands of our placed code: a build whose detour is a jsr where ours is a jmp fails."""
    img = stock()
    tools()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        plan = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img))
        path, mod = build.build(octabam.write(ob, plan, tmp), SYX, tmp)
        ln = link.link([elemod.load_any(core(tmp)), elemod.load_any(path)], img)
        ref = bytearray(ln.image[:len(img)])
        for s in mod.sites:                         # octabam's units sit elsewhere
            for o, _t, _tgt, _a in s['relocs']:
                a = s['addr'] + o - DEV.main_load
                ref[a:a + 4] = (int.from_bytes(ref[a:a + 4], 'big') + 0x40).to_bytes(4, 'big')
        plan = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img, ref))
        lines = octabam.check(ob, plan, path, core(tmp), SYX, os.path.join(tmp, 'c1'))
        assert any('equals octabam\'s build of rx-test' in x for x in lines), lines
        ref[0x4001de6e - DEV.main_load + 1] = 0xb9  # jsr, where ours is jmp
        plan = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img, ref))
        try:
            octabam.check(ob, plan, path, core(tmp), SYX, os.path.join(tmp, 'c2'))
        except octabam.CheckError as e:
            assert '0x4001de6f' in str(e), str(e)
        else:
            raise AssertionError('the check passed a different detour')
        ref[0x4001de6e - DEV.main_load + 1] = 0xf9
        # The core's own sites (its hook bus): octabam's builds have no bus, so those bytes
        # are stock in its build and ours differ. That is passed, and counted ...
        cs = [s for s in elemod.load_any(core(tmp)).sites
              if ref[s['addr'] - DEV.main_load:s['addr'] - DEV.main_load + s['len']]
              != img[s['addr'] - DEV.main_load:s['addr'] - DEV.main_load + s['len']]]
        assert cs
        for s in cs:
            o = s['addr'] - DEV.main_load
            ref[o:o + s['len']] = img[o:o + s['len']]
        plan = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img, ref))
        lines = octabam.check(ob, plan, path, core(tmp), SYX, os.path.join(tmp, 'c3'))
        n = sum(sum(1 for i in range(s['len']) if ln.image[s['addr'] - DEV.main_load + i]
                    != img[s['addr'] - DEV.main_load + i]) for s in cs)
        assert any('%d bytes of the core\'s own sites differ' % n in x for x in lines), lines
        # ... but a core site's byte that octabam's build changes must still be ours
        a = cs[0]['addr'] + 1
        ref[a - DEV.main_load] ^= 0x10
        plan = octabam.convert(ob, 'rx-test', img, DEV, fake_remix(img, ref))
        try:
            octabam.check(ob, plan, path, core(tmp), SYX, os.path.join(tmp, 'c4'))
        except octabam.CheckError as e:
            assert '0x%08x' % a in str(e), str(e)
        else:
            raise AssertionError('the check passed a core site octabam\'s build changes')


def test_converted_mods_stay_out_of_the_checkout():
    assert octabam.inside_checkout(os.path.join(ROOT, 'octabam-mods')) == \
        os.path.exists(os.path.join(ROOT, '.git'))
    assert not octabam.inside_checkout(tempfile.gettempdir())


def test_refusals():
    from types import SimpleNamespace as NS
    img = stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        m = ob.modules['fake']
        cases = (
            ({'dsp': object()}, 'DSP code'),
            ({'runtime': type('R', (), {'recipe': 'r.json'})()}, 'runtime'),
            ({'overrides': (NS(site=0x4000f834, module='NOTHING', write=None, defsym=None),)},
             'needs NOTHING'),
            ({'overrides': (NS(site=0x4000f834, module='BRIDGE', write='w', defsym=None),)},
             'runtime write w'),
            # the device qualifier's class, beside the relocatable device descriptor
            ({'pokes': (NS(addr=0x400e2016, expect=img[0x400e2016 - DEV.main_load:][:3],
                           write=bytes.fromhex('ef0201'), note=''),)}, 'protects'),
            # the descriptor's last two bytes and the next two: not wholly inside it
            ({'pokes': (NS(addr=0x400e2010, expect=img[0x400e2010 - DEV.main_load:][:4],
                           write=bytes(4), note=''),)}, 'protects'),
            ({'pokes': (NS(addr=0x400e2004, expect=bytes(3), write=bytes.fromhex('ef0201'),
                           note=''),
                        NS(addr=0x400e2006, expect=bytes(1), write=b'\x02', note=''))},
             'two pokes write the USB device descriptor'),
            ({'cf_patches': (m.cf_patches[0].__class__(**dict(vars(m.cf_patches[0]),
                                                                cave_addr=0x400d2000)),)},
             'pinned'),
            ({'requires': ('NOTHING',)}, 'needs NOTHING'),
        )
        for change, words in cases:
            ob.modules['fake'] = m.__class__(**dict(vars(m), **change))
            try:
                octabam.convert(ob, 'fake', img, DEV)
            except octabam.Refused as e:
                assert words in str(e), (words, str(e))
            else:
                raise AssertionError('not refused: %s' % words)
        ob.modules['fake'] = m
        with open(os.path.join(ob.root, 'modules', 'fake', 'x.asm'), 'w') as fh:
            fh.write('; DSP56300\n')
        assert 'DSP56300' in octabam.refusal(ob, 'fake')


def _plan_code_sites(plan):
    """A plan's code sites as sdk.bounties reads them from the built mod: (addr, len, label)."""
    out = set()
    for s in plan['json']['sites']:
        n = len(s['stock']) // 2
        if s['op'] in ('jsr', 'jmp', 'keep2'):
            out.add((int(s['addr'], 16), n, s['target']))
        elif s['op'] == 'bytes' and s.get('kind') == 'code':
            out.add((int(s['addr'], 16), n, '?'))
    return out


def test_the_code_sites_of_a_module():
    """--sites lists the stock code a module hooks as its conversion's code sites, for any
    module: emit's data poke is not one, and a bridge's overridden detour is its own."""
    img = stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        for n in ('fake', 'bridge'):
            got, notes = octabam.code_sites(ob, n, img, DEV)
            assert set(got) == _plan_code_sites(octabam.convert(ob, n, img, DEV)) and not notes, n
        assert sorted(octabam.code_sites(ob, 'fake', img, DEV)[0]) == [
            (0x4000f834, 8, 'fake_entry'), (0x4000f8cc, 6, 'ob_fake_a_cave'), (0x4002fb10, 6, 'ob_fake_a_cave')]
        # a module that does not convert still has its stock code listed, with why it is refused
        up = octabam.upstream(ob, img, DEV)
        by = {e['name']: e for e in up['mods']}
        assert by['hookmod']['sites'] == [['0x4001de6e', 6, 'hook_entry']] and 'DSP' in by['hookmod']['refused']
        assert 'refused' not in by['fake'] and by['fake']['id'] == 'octabam-fake'
        assert (up['device'], up['os'], up['from']) == ('octatrack', '1.40C', 'sambanks/octabam')


def test_refusals_of_octabams_later_schema():
    """What octabam's later manifests declare and the converter does not serve yet refuses the
    module, by name; a manifest without those fields (octabam 363861e's) is as before."""
    from types import SimpleNamespace as NS
    img = stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        m = ob.modules['fake']
        bare = {k: v for k, v in vars(m).items() if k not in ('runtime', 'arena')}
        ob.modules['fake'] = NS(**bare)          # no runtime, no arena: the later schema's shape
        assert octabam.refusal(ob, 'fake') is None
        for change, words in (({'requires_brain': True}, 'needs BRAIN'),
                              ({'settings': (NS(name='MODE'),)}, 'settings (MODE)'),
                              ({'arena_reserves': (NS(label='stage'),)}, 'arena bytes of its own (stage)'),
                              ({'dram_regions': (NS(symbol='ring'),)}, 'DRAM regions of its own (ring)'),
                              ({'keeps': (NS(addr=0x400449be),)}, 'keeps stock bytes'),
                              ({'linked': (NS(**dict(vars(m.linked[0]), loader=True)),)}, 'octabam\'s loader')):
            ob.modules['fake'] = NS(**dict(bare, **change))
            assert words in (octabam.refusal(ob, 'fake') or ''), (words, octabam.refusal(ob, 'fake'))
        # a submodule's .asm is another repository's (Zac Kyoti's modules check out all of theirs);
        # the manifest's dsp_source says what DSP code the module builds
        ob.modules['fake'] = NS(**bare)
        sub = os.path.join(ob.root, 'modules', 'fake', 'upstream')
        os.makedirs(os.path.join(sub, 'other'))
        open(os.path.join(sub, '.git'), 'w').close()
        open(os.path.join(sub, 'other', 'fx.asm'), 'w').close()
        assert octabam.refusal(ob, 'fake') is None
        ob.modules['fake'] = NS(**dict(bare, dsp_source='modules/fake/upstream/image.py'))
        assert 'integrates DSP code' in octabam.refusal(ob, 'fake')


class machines_record:
    """MACHINES with MACHMOD's record while it is open."""

    def __enter__(self):
        octabam.MACHINES['MACHMOD'] = dict(MACHMOD_RECORD)
        return octabam.MACHINES['MACHMOD']

    def __exit__(self, *exc):
        octabam.MACHINES.pop('MACHMOD', None)


def test_a_machine_goes_on_the_machines_companion():
    """--machines: a module that writes the machine row's places leaves them to machines-ot and
    its kind-table entry stock, puts its CHROMATIC detour on the intercepts, keeps its other
    sites, and adds its record to ot_machines; the line naming the FLEX renderer names
    ot_stock_render, and the record's names are appended to its source. Without --machines,
    or for a module that writes none of those places, nothing changes."""
    img = stock()
    with tempfile.TemporaryDirectory() as tmp, machines_record():
        ob = fake_checkout(tmp)
        plain = octabam.convert(ob, 'machmod', img, DEV)
        plan = octabam.convert(ob, 'machmod', img, DEV, machines=True)
        fake0, fake1 = (octabam.convert(ob, 'fake', img, DEV, machines=x) for x in (False, True))
    assert not plain['machine'] and plain['json']['version'] == ob.commit
    assert {s['addr'] for s in plain['json']['sites']} == {
        '0x400334d8', '0x4007981c', '0x4004fc58', '0x4000c634', '0x400d6438', '0x40079248'}
    j, mach = plan['json'], plan['machine']
    assert [s['addr'] for s in j['sites']] == ['0x4000c634']           # its own, the rest left stock
    assert j['version'] == ob.commit + '-machines'
    assert j['requires'] == ['core', 'intercepts', 'machines']
    assert [t['event'] for t in plan['intercepts']] == ['ev_chromatic_pitch']
    assert j['contribute'][-1] == {'to': 'ot_machines', 'order': octabam.MACHINE_ORDER,
                                   'data': '00000000',
                                   'relocs': [[0, 'abs32', 'sym:ob_machmod_machine', 0]]}
    assert mach['served'] == [0x400334d8, 0x40079248, 0x4007981c] and mach['render'] == 'mm_render'
    src = plan['files']['modules/machmod/munit.s']
    assert b'.set    STOCK_RENDER, ot_stock_render' in src
    assert b'.set    ob_machmod_page, mm_desc + MM_LEN' in src
    assert src.startswith(plain['files']['modules/machmod/munit.s'].replace(
        b'STOCK_RENDER, 0x40004008', b'STOCK_RENDER, ot_stock_render'))
    glue = plan['files']['glue/ob_machmod_machine.s']
    assert b'.byte   0x4d, 0x4d, 1, 2' in glue and b'.long   mm_render' in glue
    assert 'glue/ob_machmod_machine.s' in j['sources']
    assert (fake0['json'], fake0['files']) == (fake1['json'], fake1['files'])


def test_a_machine_without_a_record_or_its_source_is_refused():
    """--machines refuses a module that writes the machine row's places with no record in
    MACHINES, whose source no longer says what the record names, or that keeps a site inside
    machines-ot's; a remix carrying it; and without --machines it converts as before."""
    from types import SimpleNamespace as NS
    img = stock()
    with tempfile.TemporaryDirectory() as tmp:
        ob = fake_checkout(tmp)
        for args, words in (({'machines': True}, 'no record for MACHMOD'),
                            ({'machines': True, 'rx': {'modules': ('MACHMOD',), 'stock': ()}},
                             'not in a remix')):
            try:
                octabam.convert(ob, 'machmod', img, DEV, **args)
            except octabam.Refused as e:
                assert words in str(e), (words, str(e))
            else:
                raise AssertionError('not refused: %s' % words)
        octabam.convert(ob, 'machmod', img, DEV)
        m = ob.modules['machmod']
        with machines_record() as rec:
            resolver = NS(**dict(vars(m.detours[0]), site=0x40031e74,
                                 expect=bytes.fromhex('710541f9400d5f386050'), pad_to=10))
            for change, words in (
                    (lambda: rec.update(defaults='mm_gone'), 'does not define mm_gone'),
                    (lambda: rec.update(stock_render=r'^(\s*\.set\s+NO_SUCH,\s*)0x1\b'),
                     'in 0 places'),
                    (lambda: setattr(ob.modules['machmod'], 'detours', m.detours + (resolver,)),
                     'inside machines-ot\'s site 0x40031e74')):
                rec.clear()
                rec.update(MACHMOD_RECORD)
                ob.modules['machmod'] = NS(**vars(m))
                change()
                try:
                    octabam.convert(ob, 'machmod', img, DEV, machines=True)
                except octabam.Refused as e:
                    assert words in str(e), (words, str(e))
                else:
                    raise AssertionError('not refused: %s' % words)


def companions(out):
    """mods/intercepts-ot and mods/machines-ot, built -> (intercepts, machines)."""
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', DEV.toolchain['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler (machines-ot is C)')
    if 'companions' not in _c:
        _c['companions'] = tuple(build.build(d, SYX, os.path.join(out, os.path.basename(d)))[0]
                                 for d in (octabam.INTERCEPTS_DIR, octabam.MACHINES_DIR))
    return _c['companions']


def test_a_machine_builds_and_checks_on_the_companion():
    """The machine links with core-ot, the intercepts and machines-ot, and the check proves its
    places are the companion's sites or stock, its record holds what MACHINES says (name,
    signature, flags, page, defaults, signing, render), and its source assembles to its own
    bytes but for the call to the renderer. It refuses a record that is not what MACHINES says,
    a source that still calls the renderer's entry, and a check without the companion."""
    img = stock()
    tools()
    out = tempfile.mkdtemp()
    icpt, machs = companions(out)
    with machines_record():
        ob = fake_checkout(out)
        plan = octabam.convert(ob, 'machmod', img, DEV, machines=True)

        def checked(p, tag, **kw):
            path, _mod = build.build(octabam.write(ob, p, out), SYX, out)
            return octabam.check(ob, p, path, core(out), SYX, os.path.join(out, tag),
                                 **dict(dict(intercepts_path=icpt, machines_path=machs), **kw))
        lines = checked(plan, 'm')
        assert any('its 3 places of the machine row' in x and 'no site of its own' in x
                   for x in lines), lines
        assert any('its record ob_machmod_machine in ot_machines (1 of 1): signature "MM" 1-2, '
                   'name "MACH" (mm_title), flags 1' in x and '(1 2 3 4 5 6 | 7 8 9 10 11 12)' in x
                   for x in lines), lines
        assert any('but for 1 call(s) to the FLEX renderer (0x40004008)' in x for x in lines), lines
        for tamper, kw, words in (
                (lambda p: None, {'machines_path': None}, 'needs it (mods/machines-ot)'),
                (lambda p: p['files'].update({'glue/ob_machmod_machine.s': p['files'][
                    'glue/ob_machmod_machine.s'].replace(b'0x4d, 0x4d, 1, 2', b'0x4d, 0x4e, 1, 2')}),
                 {}, 'its record ob_machmod_machine: sig is 4d4e0102'),
                (lambda p: p['files'].update({'modules/machmod/munit.s': p['files'][
                    'modules/machmod/munit.s'].replace(b', ot_stock_render', b', 0x40004008')}),
                 {}, 'still calls the FLEX renderer\'s entry')):
            p = octabam.convert(ob, 'machmod', img, DEV, machines=True)
            tamper(p)
            try:
                checked(p, 'x', **kw)
            except octabam.CheckError as e:
                assert words in str(e), (words, str(e))
            else:
                raise AssertionError('checked: %s' % words)


# ---- a real checkout ------------------------------------------------------------------------

def real():
    if not OCTABAM or not os.path.isdir(OCTABAM):
        raise Skip('missing ELEKLOADER_OCTABAM')
    if 'ob' not in _c:
        _c['ob'] = octabam.Octabam(OCTABAM)
    return _c['ob']


def convert_all(names):
    ob, img = real(), stock()
    tools()
    out = tempfile.mkdtemp()
    done = {}
    for n in names:
        plan = octabam.convert(ob, n, img, DEV)
        d = octabam.write(ob, plan, out)
        path, mod = build.build(d, SYX, out)
        octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, n + '.check'))
        done[n] = mod
    return done


def test_real_modules_convert_and_check():
    """One of each shape: a pool-literal cave, emit's pokes (a nop-padded jsr among
    them), defsyms with a ratified reference(), DRAM units with detours, a
    floating unit with symbol refs and pokes."""
    done = convert_all(['recorder-hold', 'rlen-plen', 'direct-jump', 'cc-map', 'tuner',
                        'repitch', 'quantizer', 'lofi-amf-fix'])
    assert len(done['recorder-hold'].sites) == 3 and 'arena_base' in done['recorder-hold'].imports
    # pinned code, at octabam's addresses: QUANTIZER's two units
    assert {s['addr'] for s in done['quantizer'].sites} >= {0x400d2ca8, 0x400d2cb0}
    assert not done['lofi-amf-fix'].size('.run') and len(done['lofi-amf-fix'].sites) == 2
    # the category as octabam titles it for people, not its enum value
    assert done['tuner'].doc['category'] == 'Machines and the sequencer'
    assert done['lofi-amf-fix'].doc['category'] == 'Fixes'


def test_real_synth_is_refused_over_the_kept_bytes():
    """SYNTH MACHINE, before octabam's PLACE setting, pins its page cave at 0x400d24d0, over
    the 16 bytes the stock DELAY reads (the profile's `keep`, octabam #771): it is refused,
    with the range and the reason."""
    ob, img = real(), stock()
    m = ob.modules.get('synth')
    if m is None or not any(c.cave_addr == 0x400d24d0 for c in m.cf_patches):
        raise Skip('this checkout\'s SYNTH MACHINE does not pin a cave at 0x400d24d0')
    (lo, hi, why), = DEV.keep
    try:
        octabam.convert(ob, 'synth', img, DEV)
    except octabam.Refused as e:
        assert 'its cave "synth page" is pinned at 0x400d24d0, inside 0x400d24d0-0x400d24e0, ' \
               'which stays stock on the Octatrack: %s' % why in str(e), str(e)
    else:
        raise AssertionError('SYNTH MACHINE converted with its page over the kept bytes')


def test_real_usb_midi_with_a_bare_metal_assembler():
    """USB MIDI's unit reproduces its author's bytes only with an assembler that resolves
    references to its own global labels, as octabam's m68k-elf does."""
    real()
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')
    done = convert_all(['usb-midi'])
    assert done['usb-midi'].size('.run') > 0


def test_real_refusals():
    ob = real()
    for n, words in (('busverb', 'DSP code'), ('analog-bassdrum', 'DSP56300'),
                     ('octakit', 'runtime'), ('tempo-sync', 'formatter'),
                     ('kits-reload', 'runtime write'), ('cfmeter-idle', 'needs CF METER'),
                     ('rig-hosts', 'serves DSP'), ('tempo-bus', 'serves DSP'),
                     ('mode-defaults', 'serves DSP')):
        if n in ob.modules:
            assert words in (octabam.refusal(ob, n) or ''), (n, octabam.refusal(ob, n))


def test_real_usb_audio_out_serves_its_device_descriptor():
    """USB AUDIO OUT's five layouts convert: each carries USB MIDI (the bridge), serves the
    device descriptor with the composite class from .run, and leaves the bootloader copy
    stock. Any two of them, or one with USB MIDI alone, are refused together."""
    ob = real()
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')
    names = [n for n in ('usb-audio-out-main', 'usb-audio-out-main-cue', 'usb-audio-out-master',
                         'usb-audio-out-tracks', 'usb-audio-out-tracks-main-cue')
             if n in ob.modules]
    done = convert_all(names + ['usb-midi'])
    img = stock()
    core_mod = elemod.load_any(_c['core'])
    lo, hi, _w = DEV.protected[0]
    for n in names:
        mod = done[n]
        assert {s['addr'] for s in mod.sites} >= {0x4001d82e}
        ln = link.link([core_mod, mod], img)
        a = int.from_bytes(ln.image[0x4001d82e - DEV.main_load:][:4], 'big')
        o = ln.layout['run_load'] - DEV.main_load + a - ln.layout['ddr'][0]
        assert ln.image[o:o + 18][4:7] == bytes.fromhex('ef0201'), n
        assert ln.image[lo - DEV.main_load:hi - DEV.main_load] == \
            img[lo - DEV.main_load:hi - DEV.main_load]
    for pair in ((names[0], names[1]), (names[0], 'usb-midi')):
        probs = link.check([core_mod] + [done[n] for n in pair], img)
        assert probs, pair


def test_real_usb_io_remix_equals_octabams_build():
    """USB AUDIO IN converts as part of its remix: usb-io-tracks-main-cue-ab, built by
    octabam's own build_bus in the checkout, is one mod whose linked OS image equals that
    build but for our code's addresses and the descriptor served from a copy."""
    ob, img = real(), stock()
    tools()
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')
    name = 'usb-io-tracks-main-cue-ab'
    try:
        octabam.stock_raw(ob, img)
        rx = octabam.load_remix(ob, name)
        rx['ref'] = octabam.build_reference(ob, name, img)
    except octabam.Refused as e:
        raise Skip(str(e).splitlines()[0])
    plan = octabam.convert(ob, name, img, DEV, rx)
    assert plan['members'] == ['usb-midi', 'usb-audio-out-tracks-main-cue', 'usb-crossbar',
                               'usb-audio-in-ab']
    assert rx['harvested'] == ['SPATIALIZER']
    at = {s['addr'] for s in plan['json']['sites']}
    assert '0x400ef758' in at                       # payload A, P:0x88: the inject's hook
    # octabam's build (before its #753) moved PURGE SAMPLES' banks end with the arena's
    # base; the mod leaves it stock, so PURGE does not freeze the unit
    purge, base = octabam.PURGE_BANKS_END, DEV.main_load
    assert rx['ref'][purge - base:purge - base + 4] != img[purge - base:purge - base + 4]
    assert not any(int(s['addr'], 16) < purge + 4 and purge < int(s['addr'], 16)
                   + len(s['stock']) // 2 for s in plan['json']['sites'])
    out = tempfile.mkdtemp()
    path, mod = build.build(octabam.write(ob, plan, out), SYX, out)
    lines = octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, 'check'))
    assert any('equals octabam\'s build of %s' % name in x for x in lines), lines


def test_real_usb_io_remix_on_the_dsp_bus():
    """--bus: usb-io-tracks-main-cue-ab's inject is assembled into the mod and subscribes to the
    DSP bus. Linked with core and the bus, the image equals octabam's build of the remix but
    for our code's addresses, the descriptor served from a copy, the core's sites and the DSP
    bus's hook and table: the inject is where octabam's build placed it, and the bus writes
    the menus as that build does. Without the bus, the check refuses."""
    ob, img = real(), stock()
    tools()
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')
    asm = os.environ.get('ELEKLOADER_DSP_ASM') or os.path.join(
        OCTABAM, 'vendor', 'dsp56300', 'build', 'source', 'dsp_host', 'dsp_asm')
    if not os.path.isfile(asm):
        raise Skip('no dsp_asm (ELEKLOADER_DSP_ASM, or `make setup` in the checkout)')
    name = 'usb-io-tracks-main-cue-ab'
    try:
        octabam.stock_raw(ob, img)
        rx = octabam.load_remix(ob, name)
        rx['ref'] = octabam.build_reference(ob, name, img)
    except octabam.Refused as e:
        raise Skip(str(e).splitlines()[0])
    plan = octabam.convert(ob, name, img, DEV, rx, bus=True)
    j = plan['json']
    assert j['subscribe_dsp'] == [{'event': 'ev_dsp_rx', 'fn': 'inject', 'addend': 2,
                                   'order': octabam.BUS_ORDER}]
    assert 'dspbus' in j['requires']
    at = {int(s['addr'], 16) for s in j['sites']}
    assert not at & {lo for lo, _hi in octabam._dspbus_spans(img, DEV)}
    old = os.environ.get('ELEKLOADER_DSP_ASM')
    os.environ['ELEKLOADER_DSP_ASM'] = asm
    try:
        out = tempfile.mkdtemp()
        path, mod = build.build(octabam.write(ob, plan, out), SYX, out)
        bus, _m = build.build(octabam.DSPBUS_DIR, SYX, out)
    finally:
        if old is None:
            os.environ.pop('ELEKLOADER_DSP_ASM')
        else:
            os.environ['ELEKLOADER_DSP_ASM'] = old
    lines = octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, 'check'), bus)
    assert any('equals octabam\'s build of %s' % name in x and 'the DSP bus\'s hook and table'
               in x for x in lines), lines
    try:
        octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, 'check2'))
    except octabam.CheckError as e:
        assert 'needs it (mods/dspbus-ot)' in str(e), str(e)
    else:
        raise AssertionError('checked without the DSP bus')


def convert_bus(names, tamper=None):
    """Real modules converted with bus=True, written, built and checked -> {name: (plan, mod,
    lines)}. `tamper(plan)` may change a plan before it is written."""
    ob, img = real(), stock()
    tools()
    out = tempfile.mkdtemp()
    done = {}
    for n in names:
        plan = octabam.convert(ob, n, img, DEV, bus=True)
        if tamper:
            tamper(plan)
        path, mod = build.build(octabam.write(ob, plan, out), SYX, out)
        lines = octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, n + '.check'))
        done[n] = (plan, mod, lines)
    return done


def test_real_modules_on_the_bus():
    """--bus: TUNER, CC FEEDBACK, CC MAP and USB AUDIO OUT's producer subscribe to core-ot's
    events through generated glue, with their hook sites left stock, and pass the check; a
    module the bus does not serve converts exactly as without it."""
    ob, img = real(), stock()
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')
    done = convert_bus(['tuner', 'cc-feedback', 'cc-map', 'usb-audio-out-main', 'quantizer'])
    events = {'tuner': ['ev_frame', 'ev_tick'], 'cc-feedback': ['ev_tick'],
              'cc-map': ['ev_midi'], 'usb-audio-out-main': ['ev_frame']}
    for n, ev in events.items():
        plan, mod, lines = done[n]
        assert mod.version == ob.commit + '-bus', mod.version
        assert [s['event'] for s in plan['json']['subscribe']] == ev, n
        at = {s['addr'] for s in mod.sites}
        assert not at & {h['site'] for h in plan['bus']}, n          # left stock
        assert all(any('0x%08x (%s): left stock' % (h['site'], h['entry']) in x for x in lines)
                   for h in plan['bus']), lines
    assert any('but for 2 jump(s) back to 0x4000d9a0, 0x40056c78' in x
               for x in done['tuner'][2]), done['tuner'][2]
    assert any('but for 1 jump(s) back to 0x4000d9a6' in x
               for x in done['usb-audio-out-main'][2]), done['usb-audio-out-main'][2]
    assert 'CC_NEXT' not in done['cc-map'][0]['json'].get('defsym', {})
    assert {s['addr'] for s in done['tuner'][1].sites} == {0x40059ef0}   # its TEMPO opener
    plan0 = octabam.convert(ob, 'quantizer', img, DEV)
    assert not done['quantizer'][0]['bus']
    assert (plan0['json'], plan0['files']) == (done['quantizer'][0]['json'],
                                               done['quantizer'][0]['files'])


def test_real_modules_on_the_intercepts():
    """--intercepts: SCALE QUANTIZER's 17 detours at the intercepts' sites are left stock and
    become records {0, the detour's label} in the events' tables; each mod requires intercepts, is versioned <commit>-intercepts and passes the check linked
    with mods/intercepts-ot, which proves each site is the event's jump to its stub and the
    table holds the record. A module with no detour there converts exactly as without
    --intercepts, and the check refuses a record without the intercepts."""
    ob, img = real(), stock()
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')
    tools()
    out = tempfile.mkdtemp()
    icpt, _m = build.build(octabam.INTERCEPTS_DIR, SYX, os.path.join(out, 'intercepts'))
    want = {'quantizer': 17, 'tuner': 0}
    for n, count in want.items():
        plan = octabam.convert(ob, n, img, DEV, intercepts=True)
        assert len(plan['intercepts']) == count, (n, len(plan['intercepts']))
        if not count:
            plan0 = octabam.convert(ob, n, img, DEV)
            assert (plan0['json'], plan0['files']) == (plan['json'], plan['files']), n
            continue
        assert plan['json']['version'] == ob.commit + '-intercepts'
        assert 'intercepts' in plan['json']['requires']
        recs = plan['json']['contribute']
        assert sorted(c['to'] for c in recs) == sorted(t['event'] for t in plan['intercepts'])
        assert all(c['data'] == '00' * 8 and c['relocs'][0][:3] == [4, 'abs32', 'sym:' + t['entry']]
                   for c, t in zip(recs, plan['intercepts']))
        path, mod = build.build(octabam.write(ob, plan, out), SYX, out)
        assert not {s['addr'] for s in mod.sites} & {t['site'] for t in plan['intercepts']}   # left stock
        lines = octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, n + '.check'),
                              intercepts_path=icpt)
        assert sum('intercepts every call' in x for x in lines) == count, lines
        if n == 'quantizer':
            try:
                octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, 'nt.check'))
            except octabam.CheckError as e:
                assert 'needs them (mods/intercepts-ot)' in str(e), str(e)
            else:
                raise AssertionError('checked without the intercepts')


def test_real_modules_without_a_machine_row_convert_as_without_machines():
    """--machines changes only modules that write the machine row's places: every other module
    that converts is the same mod with it or without it (octabam 363861e: all of them)."""
    ob, img = real(), stock()
    seen = 0
    for n in sorted(ob.modules):
        if octabam.refusal(ob, n) or octabam._machine_writes(ob.modules[n]):
            continue
        try:
            plan0 = octabam.convert(ob, n, img, DEV)
        except octabam.Refused:
            continue
        plan1 = octabam.convert(ob, n, img, DEV, machines=True)
        assert (plan0['json'], plan0['files']) == (plan1['json'], plan1['files']), n
        seen += 1
    assert seen


def test_real_octa303_on_the_machines():
    """--machines: OCTA303 (octabam b085c4d) leaves its 15 detours and 6 pokes of the machine row
    and its kind-table entry to machines-ot, its CHROMATIC pitch to the intercepts, and keeps
    its PLAYBACK hook (0x40031ece) and retrig (0x4000c634). It checks linked with core-ot, the
    intercepts and machines-ot, and links beside the CRUSH example: ot_machines lists both. The
    conversion without --machines is refused beside machines-ot."""
    ob, img = real(), stock()
    if 'octa303' not in ob.modules:
        raise Skip('no OCTA303 in this checkout (octabam b085c4d has it)')
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')
    tools()
    out = tempfile.mkdtemp()
    icpt, machs = companions(out)
    plan = octabam.convert(ob, 'octa303', img, DEV, machines=True)
    j = plan['json']
    assert sorted(s['addr'] for s in j['sites']) == ['0x4000c634', '0x40031ece']
    assert j['version'] == ob.commit + '-machines'
    assert j['requires'] == ['core', 'intercepts', 'machines']
    assert [t['event'] for t in plan['intercepts']] == ['ev_chromatic_pitch']
    assert len(plan['machine']['served']) == 21 and plan['machine']['render'] == 'sy_render'
    path, mod = build.build(octabam.write(ob, plan, out), SYX, out)
    lines = octabam.check(ob, plan, path, core(out), SYX, os.path.join(out, 'o3.check'),
                          intercepts_path=icpt, machines_path=machs)
    assert any('signature "TB" 1-1, name "OCTA-303" (o3_title), flags 5' in x for x in lines), lines
    crush, _m = build.build(os.path.join(ROOT, 'examples', 'crush-machine-ot'), SYX, out)
    mods = [elemod.load_any(p) for p in (core(out), icpt, machs, path, crush)]
    assert link.check(mods, img) == []
    ln = link.link(mods, img)
    at, count, _w = ln.tables['ot_machines']
    o = ln.layout['run_load'] - DEV.main_load + at - ln.layout['ddr'][0]
    assert {int.from_bytes(ln.image[o + 4 * k:o + 4 * k + 4], 'big') for k in range(count)} == {
        ln.map['crush_machine'], ln.map['ob_octa303_machine']}
    plain = octabam.convert(ob, 'octa303', img, DEV)
    p0, _m = build.build(octabam.write(ob, plain, os.path.join(out, 'plain')), SYX,
                         os.path.join(out, 'plain'))
    assert link.check([elemod.load_any(p) for p in (core(out), machs, p0)], img)


def test_real_bus_glue_is_checked():
    """The check proves a source rewritten for the bus differs from octabam's only at its
    jumps back to the firmware, and that a stub entered past its replay starts with it:
    any other change, or a stub that does not, is refused."""
    real()
    if not octabam.bare_metal(DEV):
        raise Skip('the configured assembler is not bare metal (ELEKLOADER_CROSS=m68k-elf-)')

    def slower(plan):
        (f,) = plan['bus_sources']
        assert b'UPDATE_FRAMES, 400' in plan['files'][f]
        plan['files'][f] = plan['files'][f].replace(b'UPDATE_FRAMES, 400', b'UPDATE_FRAMES, 401')
    try:
        convert_bus(['tuner'], slower)
    except octabam.CheckError as e:
        assert 'is not a jump back to the firmware' in str(e), str(e)
    else:
        raise AssertionError('a source changed beyond its jumps back passed')
    spec = octabam.BUS[(0x4000D9A0, 'audio_frame_shim')]
    spec['skip'] = True                             # its replay is at its end, not its start
    try:
        convert_bus(['usb-audio-out-main'])
    except octabam.CheckError as e:
        assert 'does not start with its replay' in str(e), str(e)
    else:
        raise AssertionError('a stub entered past a replay it does not start with passed')
    finally:
        spec['skip'] = False


def test_real_code_sites_are_the_conversions():
    """For every module of the checkout that converts, --sites lists its conversion's code sites."""
    ob, img = real(), stock()
    seen = 0
    for n in sorted(ob.modules):
        if octabam.refusal(ob, n):
            continue
        try:
            plan = octabam.convert(ob, n, img, DEV)
        except octabam.Refused:
            continue
        got, _notes = octabam.code_sites(ob, n, img, DEV)
        assert set(got) == _plan_code_sites(plan), n
        seen += 1
    assert seen, 'no module of the checkout converts'


def test_real_overlapping_claims_do_not_combine():
    """octabam's ledger refuses MIDI SCENES with SCENES P2 (their Part-window
    claims overlap); so does the linker, by the claims' block names."""
    done = convert_all(['midi-scenes', 'scenes-p2'])
    img = stock()
    probs = link.check([elemod.load_any(_c['core'])] + list(done.values()), img)
    assert any('octabam:part-window@' in p for p in probs), probs


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
