# SPDX-License-Identifier: GPL-2.0-or-later
"""The mod author's tools: sdk.build and lint (pytest, or run with python).

Needs files named by environment variables; a test whose inputs are
missing is skipped, not passed:
  ELEKLOADER_STOCK      the stock Digitakt_OS1.53.syx
  ELEKLOADER_STOCK_154  optional: Digitakt_OS1.54.syx, for the ports to 1.54
  ELEKLOADER_MODS       optional: a folder with the core mod (core-*.elemod or
                        .dtmod); without one, mods/core is built
  ELEKLOADER_CORE_21    optional: the released core-2.1.elemod (v0.4.0), with
                        core-2.1-os1.54.elemod beside it: core 3.0 must keep
                        all of it
  ELEKLOADER_DN_SYX, ELEKLOADER_DN_SYX_144  optional: the Digitone's 1.43
                        and 1.44 files, for mods/core-dn1 and its port
  ELEKLOADER_CORE_DN1_23  optional: the released core-dn1-2.3.elemod (the
                        core-dn1-v2.3 pre-release), with
                        core-dn1-2.3-os1.44.elemod beside it: core-dn1 3.x
                        must keep all of it
  ELEKLOADER_CORE_DN1_32  optional: the released core-dn1-3.2.elemod (the
                        core-dn1-v3.2 pre-release), with
                        core-dn1-3.2-os1.44.elemod beside it: core-dn1 3.4
                        must keep all of it too
  ELEKLOADER_CORE_DN1_40, ELEKLOADER_CORE_DN1_41  optional: the released
                        core-dn1-4.0.elemod and core-dn1-4.1.elemod, with
                        their -os1.44 files beside them, which the newest
                        must keep too
Building mods/core or the example also needs the cross toolchain
(m68k-linux-gnu-as, -gcc and -ld).
"""
import contextlib
import glob
import io
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from elekloader import devices, elemod, formats, lint, link        # noqa: E402
from elekloader.sdk import build                           # noqa: E402

STOCK = os.environ.get('ELEKLOADER_STOCK', '')
with open(os.path.join(ROOT, 'mods', 'core', 'mod.json')) as _fh:
    DT_VERSION = json.load(_fh)['version']     # the Digitakt mk1 core these tests build
MODS = os.environ.get('ELEKLOADER_MODS', '')


class Skip(Exception):
    pass


def stock():
    if not STOCK or not os.path.exists(STOCK):
        raise Skip('missing ELEKLOADER_STOCK')
    return STOCK


_BUILT_CORE = []


def built_core():
    """mods/core, built once with the SDK into a temporary folder."""
    if not _BUILT_CORE:
        tc = devices.devices()[0].toolchain
        if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'as'):
            raise Skip('no m68k cross assembler to build mods/core')
        path, _m = build.build(os.path.join(ROOT, 'mods', 'core'), stock(), tempfile.mkdtemp())
        _BUILT_CORE.append(path)
    return _BUILT_CORE[0]


def core():
    paths = elemod.mod_files(MODS) if MODS else []
    paths = [p for p in paths if os.path.basename(p).startswith('core-')]
    return paths[-1] if paths else built_core()


def lint_json(*args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = lint.main(list(args) + ['--json'])
    return rc, json.loads(out.getvalue())


def data_only_mod(tmp):
    d = os.path.join(tmp, 'grid-max')
    os.makedirs(d)
    with open(os.path.join(d, 'mod.json'), 'w') as fh:
        json.dump({'id': 'grid-max', 'version': '0.1', 'device': 'digitakt-mk1', 'os': '1.53',
                   'sites': [{'addr': '0x401ab9b0', 'stock': '00000400', 'op': 'bytes',
                              'new': '00000500'}],
                   'requires': ['core']}, fh)
    return d


def test_data_only_mod_builds_without_a_toolchain():
    with tempfile.TemporaryDirectory() as tmp:
        path, m = build.build(data_only_mod(tmp), stock())
        assert path.endswith('grid-max-0.1.elemod')
        with open(path) as fh:
            assert json.load(fh)['elemod'] == 2
        assert isinstance(m, link.Mod2) and len(m.sites) == 1 and not m.sections


def test_build_refuses_wrong_stock_bytes():
    with tempfile.TemporaryDirectory() as tmp:
        d = data_only_mod(tmp)
        with open(os.path.join(d, 'mod.json')) as fh:
            j = json.load(fh)
        j['sites'][0]['stock'] = '00000401'
        with open(os.path.join(d, 'mod.json'), 'w') as fh:
            json.dump(j, fh)
        try:
            build.build(d, stock())
        except build.BuildError as e:
            assert 'stock bytes' in str(e)
        else:
            raise AssertionError('built with the wrong stock bytes')


def test_lint():
    with tempfile.TemporaryDirectory() as tmp:
        path, _m = build.build(data_only_mod(tmp), stock())
        rc, r = lint_json(path)
        assert rc == 0 and r['mods'][0]['id'] == 'grid-max'
        rc, r = lint_json(path, '--stock', stock())
        assert rc == 1 and any('requires core' in x for x in r['problems'])
        rc, r = lint_json(path, '--stock', stock(), '--with', core())
        assert rc == 0 and r['link']['order'][0].startswith('core')
        slicer = [p for p in elemod.mod_files(MODS) if os.path.basename(p).startswith('slicer-')]
        if slicer:
            rc, r = lint_json(path, '--stock', stock(), '--with', core(), '--with', slicer[-1])
            assert rc == 1 and any('overlap' in x for x in r['problems'])


# The hook bus's events (core.s), which every device's core provides.
HOOK_BUS = ['ev_draw', 'ev_enc', 'ev_key', 'ev_render_in', 'ev_render_out', 'ev_settings',
            'ev_tick']


FW = ['fw_active_track', 'fw_amp_env', 'fw_bitmap_vt', 'fw_blit', 'fw_fillrect', 'fw_font5',
      'fw_framerect', 'fw_kit', 'fw_op_new', 'fw_pitch_tab', 'fw_render_machine', 'fw_set_param',
      'fw_slice_layout', 'fw_textf', 'fw_track_blocks', 'fw_voice_note', 'fw_voice_params',
      'fw_voice_start', 'fw_voice_vel']


# What core 4.1 (Digitakt mk1) adds, in master.s after everything 4.0 has: the master output
# (ev_render_master), the SETTINGS > GLOBAL FX/MIX rows (ev_fxmix) and the master pages
# (core_mpages: four pointers of the master view's vtables).
CORE_41_SITES = ['core_render_master', 'core_fxmix', 'core_mview_key', 'core_mview_draw',
                 'core_mview_title', 'core_mview_enc', 'core_mview_clear']
CORE_41_TABLES = ['ev_render_master', 'core_mpages', 'ev_fxmix']
CORE_41_EXPORTS = ['core_mpage_shown', 'core_mview', 'core_mview_draws', 'fw_invalidate']


def site_targets(sites):
    """The routine each built site calls or points to (its relocation's symbol)."""
    return [[r[2][4:] for r in x.get('relocs', []) if r[2].startswith('sym:')][-1:] for x in sites]


def test_core_builds_and_lints():
    """core (3.0 and 4.0, the same code, and 4.1): the hook bus (core.s, 8 sites), the SRC machine slots
    (machines.s, Digitakt mk1 only: 31 sites and the table core_machines),
    the machine page reader (core_machine_ui), the firmware locations
    (fw.s: absolute exports, no site) and from 4.1 the master output and pages (master.s: 7 sites
    and three tables)."""
    path = built_core()
    with open(path) as fh:
        doc = json.load(fh)
    assert doc['id'] == 'core' and doc['version'] == DT_VERSION
    assert '.boot' in doc['sections'] and len(doc['sites']) == 39 + len(CORE_41_SITES)
    assert site_targets(doc['sites'][39:]) == [[x] for x in CORE_41_SITES]
    assert sorted(doc['collections']) == sorted(HOOK_BUS + ['core_machines'] + CORE_41_TABLES)
    assert set(FW + CORE_41_EXPORTS + ['core_machine', 'core_machine_ui']) <= set(doc['exports'])
    assert all(doc['symbols'][x][0] == 'abs' for x in FW)
    rc, r = lint_json(path, '--stock', stock())
    assert rc == 0 and r['link']['order'][0].startswith('core')


with open(os.path.join(ROOT, 'mods', 'machines-dn1', 'mod.json')) as _fh:
    MACH_VERSION = json.load(_fh)['version']          # the machines mod's version, for the link orders below
with open(os.path.join(ROOT, 'examples', 'dn-sine-machine', 'mod.json')) as _fh:
    SINE_VERSION = json.load(_fh)['version']


CORE_21 = os.environ.get('ELEKLOADER_CORE_21', '')


def check_superset(old_path, new_path, st):
    """Core 3.0 (and 4.0, the same code) is core 2.1 and more: the same sites
    with the same bytes, the same tables, every 2.1 symbol where 2.1 has it,
    2.1's .run at the start of the new core's and the same .bss."""
    with open(old_path) as fh:
        old = json.load(fh)
    with open(new_path) as fh:
        new = json.load(fh)
    assert old['version'] == '2.1' and new['version'] == DT_VERSION
    assert old['target'] == new['target']
    check_41_adds(old, new)
    assert new['contribute'] == old['contribute'] and new['resources'] == old['resources']
    assert set(old['exports']) <= set(new['exports'])
    assert all(new['symbols'][k] == v for k, v in old['symbols'].items())
    stock_, dev, _rel = formats.load(st)
    image = formats.main_image(stock_, dev)
    m_old, m_new = link.Mod2(old, 'old'), link.Mod2(new, 'new')
    run_old = elemod.parts_bytes(m_old.sections['.run']['parts'], image, dev)
    run_new = elemod.parts_bytes(m_new.sections['.run']['parts'], image, dev)
    assert run_new[:len(run_old)] == run_old and len(run_new) > len(run_old)
    assert [r for r in new['relocs'] if r[1] < len(run_old) or r[0] != '.run'] == old['relocs']


def check_41_adds(old, new):
    """What 4.1 adds to a core before it (2.1, 3.0, 4.0: the same sites and tables): its sites
    after the old ones, its tables, and its words of .bss after the old .bss."""
    n = len(old['sites'])
    assert new['sites'][:n] == old['sites']
    assert site_targets(new['sites'][n:]) == [[x] for x in CORE_41_SITES]
    assert new['collections'] == dict(old['collections'], **{c: {'entry': 4} for c in CORE_41_TABLES})
    ob, nb = old['sections']['.bss'], new['sections']['.bss']
    assert ob['align'] == nb['align'] and nb['size'] == ob['size'] + 8, (ob, nb)


CORE_40 = os.environ.get('ELEKLOADER_CORE_40', '')


def test_digitakt_core_4_1_is_4_0_and_more():
    """core 4.1 is 4.0 and the master output and pages: 4.0's sites with the same bytes, its
    tables and exports, every symbol where it was, 4.0's .run at the start of 4.1's."""
    if not CORE_40 or not os.path.exists(CORE_40):
        raise Skip('missing ELEKLOADER_CORE_40 (the released core-4.0.elemod)')
    new_path = built_core()
    with open(CORE_40) as fh:
        old = json.load(fh)
    with open(new_path) as fh:
        new = json.load(fh)
    assert old['version'] == '4.0' and new['version'] == DT_VERSION
    check_41_adds(old, new)
    assert sorted(set(new['exports']) - set(old['exports'])) == sorted(CORE_41_EXPORTS + CORE_41_SITES)
    assert all(new['symbols'][k] == v for k, v in old['symbols'].items())
    stock_, dev, _rel = formats.load(stock())
    image = formats.main_image(stock_, dev)
    run_old = elemod.parts_bytes(link.Mod2(old, 'old').sections['.run']['parts'], image, dev)
    run_new = elemod.parts_bytes(link.Mod2(new, 'new').sections['.run']['parts'], image, dev)
    assert run_new[:len(run_old)] == run_old and len(run_new) > len(run_old)


def test_core_3_is_core_2_1_and_more():
    if not CORE_21 or not os.path.exists(CORE_21):
        raise Skip('missing ELEKLOADER_CORE_21 (the released core-2.1.elemod)')
    check_superset(CORE_21, built_core(), stock())
    other = CORE_21.replace('core-2.1.elemod', 'core-2.1-os1.54.elemod')
    if STOCK_154 and os.path.exists(STOCK_154) and os.path.exists(other):
        with tempfile.TemporaryDirectory() as tmp:
            path, _m = build.build(os.path.join(ROOT, 'mods', 'core'), STOCK_154, tmp)
            check_superset(other, path, STOCK_154)


def test_digitakt_core_4_gives_the_mods_256_kb():
    """From core 4.0 the Digitakt mk1's linker lays the mods out from 0x47BC0000
    to 0x47C00000 (256 KB, the profile's core_ddr), the gap between the sample
    pool's end and the audio input ring, in place of the 128 KB at 0x47BE0000:
    mods that need 200 KB link with 4.0, and beside the released 2.1 they are
    refused, the message naming the core that has room."""
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    dev = [x for x in devices.devices() if x.key == 'digitakt-mk1'][0]
    assert dev.ddr_for('3.0') == dev.ddr_for('2.1') == (0x47BE0000, 0x47C00000) == dev.ddr
    assert dev.ddr_for('4.0') == dev.ddr_for('4.1') == (0x47BC0000, 0x47C00000)
    st = stock()
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, 'big')
        os.makedirs(d)
        with open(os.path.join(d, 'big.c'), 'w') as fh:
            fh.write('char big_buf[200000];\nvoid big_tick(void *ctrl) { (void)ctrl; big_buf[0]++; }\n')
        with open(os.path.join(d, 'mod.json'), 'w') as fh:
            json.dump({'id': 'big', 'version': '0.1', 'license': 'GPL-2.0-or-later', 'device': 'digitakt-mk1',
                       'os': '1.53', 'sources': ['big.c'],
                       'subscribe': [{'event': 'ev_tick', 'fn': 'big_tick'}], 'requires': ['core']}, fh)
        bpath, _b = build.build(d, st, tmp)
        rc, r = lint_json(bpath, '--stock', st, '--with', built_core())
        assert rc == 0, r['problems']
        assert r['link']['ram_size'] == 0x40000 and 200000 < r['link']['ram_used'] < 0x40000, r['link']
        if CORE_21 and os.path.exists(CORE_21):
            rc, r = lint_json(bpath, '--stock', st, '--with', CORE_21)
            assert rc == 1 and any('core 4.0 or newer gives them 262144 bytes' in x for x in r['problems']), \
                r['problems']


def test_core_too_old_is_said_plainly():
    """A mod whose resources.core asks for 3.0 is refused beside an older core
    with that, and not with the imports that follow from it."""
    if not CORE_21 or not os.path.exists(CORE_21):
        raise Skip('missing ELEKLOADER_CORE_21 (the released core-2.1.elemod)')
    with tempfile.TemporaryDirectory() as tmp:
        d = data_only_mod(tmp)
        with open(os.path.join(d, 'mod.json')) as fh:
            j = json.load(fh)
        j['resources'] = {'core': '3.0'}
        with open(os.path.join(d, 'mod.json'), 'w') as fh:
            json.dump(j, fh)
        path, m = build.build(d, stock())
        assert m.needs_core == '3.0'
        rc, r = lint_json(path, '--stock', stock(), '--with', CORE_21)
        assert rc == 1 and r['problems'] == [
            'grid-max 0.1 needs core 3.0 or newer, and this build has core 2.1'], r['problems']
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core())
        assert rc == 0, r['problems']


C_MACHINE = r'''
#include "digitakt-mk1/core3.h"
static int32_t pg_text(char *buf, int32_t v, int32_t ctx, int32_t machine)
{
    buf[0] = (char)('0' + ((v >> 8) & 7)); buf[1] = ctx ? '!' : 0; buf[2] = 0;
    (void)machine;
    return 1;
}
static const struct cm_ui pg_page = {
    .abi = cm_ui_v3, .page_from = 3,
    .knob = { [1] = { .flags = CM_HIDDEN },
              [4] = { .name = "SEVN", .lname = "Seven", .flags = CM_RANGE, .max = 7 << 8, .fmt = pg_text },
              [5] = { .look = 0x86 } },
};
const struct cm_machine pg_machine = { 30, "PAGETEST", "PGT", 0, 3, 3, CM_UI_TAG, &pg_page };
void pg_render(int32_t *blocks) { blocks[32 * fw_active_track] = fw_voice_params[0]; }
'''


def machine_pages(st, tmp):
    return build.build(os.path.join(ROOT, 'mods', 'machine-pages'), st, tmp)[0]


def test_a_machine_in_c_describes_its_page():
    """A machine written against digitakt-mk1/core3.h, with mod.json's
    "machines": it links with core 3.0 and machine-pages, and names only
    core's and machine-pages' symbols, so its 1.54 port is empty."""
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, 'pagetest')
        os.makedirs(d)
        with open(os.path.join(d, 'pagetest.c'), 'w') as fh:
            fh.write(C_MACHINE)
        with open(os.path.join(d, 'mod.json'), 'w') as fh:
            json.dump({'id': 'pagetest', 'version': '1.0', 'device': 'digitakt-mk1', 'os': '1.53',
                       'sources': ['pagetest.c'], 'ports': {'1.54': {}},
                       'machines': [{'id': 30, 'descriptor': 'pg_machine'}],
                       'subscribe': [{'event': 'ev_render_voices', 'fn': 'pg_render', 'order': 30}],
                       'requires': ['core', 'machine-pages'], 'resources': {'core': '3.0'}}, fh)
        path, m = build.build(d, stock(), tmp)
        assert m.needs_core == '3.0' and m.names == ['machine:30'] and not m.sites
        assert sorted(c['to'] for c in m.contribute) == ['core_machines', 'ev_render_voices']
        assert set(m.imports) == {'cm_ui_v3', 'fw_active_track', 'fw_voice_params'}
        mp = machine_pages(stock(), tmp)
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(), '--with', mp)
        assert rc == 0, r['problems']
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core())
        assert rc == 1 and r['problems'] == ['pagetest 1.0 requires machine-pages'], r['problems']
        if CORE_21 and os.path.exists(CORE_21):
            rc, r = lint_json(path, '--stock', stock(), '--with', CORE_21, '--with', mp)
            assert rc == 1 and set(r['problems']) == {
                'pagetest 1.0 needs core 3.0 or newer, and this build has core 2.1',
                'machine-pages 1.1 needs core 3.0 or newer, and this build has core 2.1'}, r['problems']
        if STOCK_154 and os.path.exists(STOCK_154):
            path4, m4 = build.build(d, STOCK_154, tmp)
            assert path4.endswith('pagetest-1.0-os1.54.elemod')
            with open(path) as fh:
                a = json.load(fh)
            with open(path4) as fh:
                b = json.load(fh)
            def sizes(x):
                return {s: v.get('len', v.get('size')) for s, v in x['sections'].items()}
            assert sizes(a) == sizes(b) and a['relocs'] == b['relocs'] and a['symbols'] == b['symbols']


def test_a_knob_that_draws_itself_needs_machine_pages_1_1():
    """A page that names cm_ui_v31 may give a knob CM_DRAW and a draw function
    in gfx's place (the same 40-byte knob). It imports cm_ui_v31, which only
    machine-pages 1.1 and newer export, and links beside it. Beside 1.0, which
    exports cm_ui_v3 alone and would call draw as a gfx, it is refused."""
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    src = C_MACHINE.replace('.abi = cm_ui_v3,', '.abi = cm_ui_v31,').replace(
        '[5] = { .look = 0x86 } },',
        '[5] = { .look = 0x86, .flags = CM_DRAW, .draw = pg_draw } },').replace(
        'static const struct cm_ui pg_page',
        'static int32_t pg_draw(void *bmp, int32_t x, int32_t y, int32_t v, int32_t flag)\n'
        '{ (void)flag; fw_fillrect(bmp, x + 1, y, x + 1 + (v >> 11), y + 16, 1); return 1; }\n'
        'static const struct cm_ui pg_page')
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, 'drawtest')
        os.makedirs(d)
        with open(os.path.join(d, 'drawtest.c'), 'w') as fh:
            fh.write(src)
        with open(os.path.join(d, 'mod.json'), 'w') as fh:
            json.dump({'id': 'drawtest', 'version': '1.0', 'device': 'digitakt-mk1', 'os': '1.53',
                       'sources': ['drawtest.c'],
                       'machines': [{'id': 30, 'descriptor': 'pg_machine'}],
                       'subscribe': [{'event': 'ev_render_voices', 'fn': 'pg_render', 'order': 30}],
                       'requires': ['core', 'machine-pages'], 'resources': {'core': '3.0'}}, fh)
        path, m = build.build(d, stock(), tmp)
        assert set(m.imports) == {'cm_ui_v31', 'fw_active_track', 'fw_fillrect', 'fw_voice_params'}
        # knob F: flag 16, and draw (pg_draw) at +36, where a gfx would be
        s, dev, _rel = formats.load(stock())
        run = bytes(elemod.parts_bytes(m.sections['.run']['parts'], formats.main_image(s, dev), dev))
        knob = m.symbols['pg_page'][1] + 16 + 5 * 40
        assert run[knob + 17] == 16, run[knob + 17]
        draw = [r[3:] for r in m.relocs if r[:3] == ('.run', knob + 36, 'abs32')]
        assert draw in ([('sec:.run', m.symbols['pg_draw'][1])], [('sym:pg_draw', 0)]), draw
        mp = machine_pages(stock(), tmp)
        with open(mp) as fh:
            doc = json.load(fh)
        assert {'cm_ui_v3', 'cm_ui_v31'} <= set(doc['exports'])
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(), '--with', mp)
        assert rc == 0, r['problems']
        # machine-pages 1.0: the same mod but for the marker
        doc.update(version='1.0', exports=[x for x in doc['exports'] if x != 'cm_ui_v31'])
        mp10 = os.path.join(tmp, 'machine-pages-1.0.elemod')
        with open(mp10, 'w') as fh:
            json.dump(doc, fh)
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(), '--with', mp10)
        assert rc == 1 and r['problems'] == [
            'drawtest 1.0 imports cm_ui_v31, which no given mod exports'], r['problems']


def test_extra_values_with_ccs_need_machine_pages_1_4():
    """A page that names cm_ui_v33 gives its extra values MIDI CCs (+18 of
    each knob): the PARAM fixture's are 24-30. It imports cm_ui_v33, which
    only machine-pages 1.4 and newer export, and links beside it. Beside 1.3,
    which would refuse the page for the byte, it is refused. machine-pages
    answers the CCs at the lookup's generic audio-track table, 0x400791fe."""
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        path, m = build.build(os.path.join(ROOT, 'tests', 'fixtures', 'digitakt-src-parameters'), stock(), tmp)
        assert 'cm_ui_v33' in m.imports and 'cm_ui_v32' not in m.imports, m.imports
        s, dev, _rel = formats.load(stock())
        run = bytes(elemod.parts_bytes(m.sections['.run']['parts'], formats.main_image(s, dev), dev))
        extra = m.symbols['ui'][1] + 336
        assert [run[extra + 40 * k + 18] for k in range(7)] == list(range(24, 31))
        assert all(run[extra + 40 * k + 19] == 0 for k in range(7))
        mp = machine_pages(stock(), tmp)
        with open(mp) as fh:
            doc = json.load(fh)
        assert {'cm_ui_v32', 'cm_ui_v33'} <= set(doc['exports'])
        assert [(x['new'][:4], x['relocs']) for x in doc['sites'] if x['addr'] == '0x400791fe'] == [
            ('4ef9', [[2, 'abs32', 'sym:mp_cc_s', 0]])]
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(), '--with', mp)
        assert rc == 0, r['problems']
        # machine-pages 1.3: the same mod but for the marker
        doc.update(version='1.3', exports=[x for x in doc['exports'] if x != 'cm_ui_v33'])
        mp13 = os.path.join(tmp, 'machine-pages-1.3.elemod')
        with open(mp13, 'w') as fh:
            json.dump(doc, fh)
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(), '--with', mp13)
        assert rc == 1 and r['problems'] == [
            'src-parameters-test 0.1 imports cm_ui_v33, which no given mod exports'], r['problems']


def test_a_graphic_that_reads_another_knob_needs_machine_pages_1_5():
    """examples/linked-knobs: SHAPE's draw reads FORM with cm_src_shown, which
    only machine-pages 1.5 and newer export. It links beside 1.5 and is refused
    beside 1.4. It names no firmware address: its 1.54 port is the same code."""
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    src = os.path.join(ROOT, 'examples', 'linked-knobs')
    with tempfile.TemporaryDirectory() as tmp:
        path, m = build.build(src, stock(), tmp)
        assert not m.sites and m.names == ['machine:121']
        assert set(m.imports) == {'cm_src_shown', 'cm_ui_v31', 'fw_fillrect'}, m.imports
        mp = machine_pages(stock(), tmp)
        with open(mp) as fh:
            doc = json.load(fh)
        assert 'cm_src_shown' in doc['exports']
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(), '--with', mp)
        assert rc == 0, r['problems']
        # machine-pages 1.4: the same mod but for the export
        doc.update(version='1.4', exports=[x for x in doc['exports'] if x != 'cm_src_shown'])
        mp14 = os.path.join(tmp, 'machine-pages-1.4.elemod')
        with open(mp14, 'w') as fh:
            json.dump(doc, fh)
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(), '--with', mp14)
        assert rc == 1 and r['problems'] == [
            'linked-knobs 1.0 imports cm_src_shown, which no given mod exports'], r['problems']
        if STOCK_154 and os.path.exists(STOCK_154):
            path4, m4 = build.build(src, STOCK_154, tmp)
            images = []
            for st, mod in ((stock(), m), (STOCK_154, m4)):
                s, dev, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(mod.sections['.run']['parts'],
                                                       formats.main_image(s, dev), dev)))
            assert images[0] == images[1] and m.relocs == m4.relocs


def test_the_sine_machine_example_builds_for_each_os_unchanged():
    """examples/sine-machine names no firmware address: its 1.54 port is
    empty, and both builds carry the same code."""
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    src = os.path.join(ROOT, 'examples', 'sine-machine')
    with tempfile.TemporaryDirectory() as tmp:
        path, m = build.build(src, stock(), tmp)
        assert not m.sites and m.needs_core == '3.0' and m.names == ['machine:120']
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core(),
                          '--with', machine_pages(stock(), tmp))
        assert rc == 0, r['problems']
        if STOCK_154 and os.path.exists(STOCK_154):
            path4, m4 = build.build(src, STOCK_154, tmp)
            images = []
            for st, mod in ((stock(), m), (STOCK_154, m4)):
                s, dev, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(mod.sections['.run']['parts'],
                                                       formats.main_image(s, dev), dev)))
            assert images[0] == images[1] and m.relocs == m4.relocs


def test_the_master_mono_example_builds_for_each_os_unchanged():
    """examples/master-mono, on core 4.1's master output and master pages: it
    patches no site, names no firmware address (its 1.54 port is empty and
    both builds carry the same code), links with core 4.1 and is refused
    with 4.0, which has neither table."""
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    src = os.path.join(ROOT, 'examples', 'master-mono')
    with tempfile.TemporaryDirectory() as tmp:
        path, m = build.build(src, stock(), tmp)
        assert not m.sites and m.needs_core == '4.1'
        assert sorted(c['to'] for c in m.contribute) == ['core_mpages', 'ev_render_master']
        rc, r = lint_json(path, '--stock', stock(), '--with', built_core())
        assert rc == 0, r['problems']
        if CORE_40 and os.path.exists(CORE_40):
            rc, r = lint_json(path, '--stock', stock(), '--with', CORE_40)
            assert rc == 1, r
        if STOCK_154 and os.path.exists(STOCK_154):
            path4, m4 = build.build(src, STOCK_154, tmp)
            images = []
            for st, mod in ((stock(), m), (STOCK_154, m4)):
                s, dev, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(mod.sections['.run']['parts'],
                                                       formats.main_image(s, dev), dev)))
            assert images[0] == images[1] and m.relocs == m4.relocs


DN_STOCK = os.environ.get('ELEKLOADER_DN_SYX', '')
with open(os.path.join(ROOT, 'mods', 'machines-dn1', 'mod.json')) as _fh:
    MACH_VERSION = json.load(_fh)['version']


def dn_core(tmp, drop=None):
    """mods/core-dn1 built with the SDK (one defsym left out if `drop`)."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'as'):
        raise Skip('no m68k cross assembler to build mods/core-dn1')
    src = os.path.join(ROOT, 'mods', 'core-dn1')
    if drop:
        with open(os.path.join(src, 'mod.json')) as fh:
            j = json.load(fh)
        del j['defsym'][drop]
        j['sources'] = [os.path.join(src, s) for s in j['sources']]
        src = os.path.join(tmp, 'core-dn1')
        os.makedirs(src)
        with open(os.path.join(src, 'mod.json'), 'w') as fh:
            json.dump(j, fh)
    path, _m = build.build(src, DN_STOCK, tmp)
    return path


def test_digitone_core_builds_and_links():
    """core-dn1: core 2.1's core.s (the hook bus, without the Digitakt's
    machine slots), voice.s (ev_voice_on), params.s (parameter slots, ids
    182-184), pages.s (mod pages, 27-30), projdata.s (mods' data saved with
    the project), menu.s (the Mod Menu, a grid of icons from 2.3, submenus
    from 3.1), fw.s (firmware locations as exports, from 3.0; the C library,
    the text box and the pattern from 3.3) and voices.s
    (ev_render_voices, for machines, from 3.1), audio.s (exclusive audio,
    from 3.2, and insert audio, from 4.0), midi.s (ev_midi_cc, from 3.2, and ev_midi_pc,
    4.0), leds.s (ev_key_leds, 3.3), block.s (ev_render_params, 3.3) and queue.s
    (ev_note_queue, 4.1), the last twelve Digitone only."""
    with tempfile.TemporaryDirectory() as tmp:
        path = dn_core(tmp)
        with open(path) as fh:
            dn = json.load(fh)
        with open(built_core()) as fh:
            dt = json.load(fh)
        # 8 hook bus sites, the voice note-on, the parameter slots' 64 (3
        # table moves, 2 UI record lookups, 58 raised id bounds and the short
        # name), the mod pages' 2 (the page record lookup and the views'
        # builder), the project data's 5 (the serializer, two loads, two new
        # projects), the voices' block, exclusive audio's 2 (the voices'
        # filters, the master stage), the CC router's, the key-LED frame's, the
        # controller stage's, the program change handler's and the note queue's.
        assert dn['target']['device'] == 'digitone-mk1' and len(dn['sites']) == 88
        assert sorted(dn['collections']) == sorted(
            HOOK_BUS + ['core_menu', 'core_pages', 'core_params', 'core_projdata',
                        'ev_hold', 'ev_voice_on', 'core_param_override', 'ev_render_voices',
                        'core_audio', 'ev_midi_cc', 'ev_key_leds', 'ev_render_params', 'ev_midi_pc',
                        'ev_note_queue'])
        # The same core.s as core 2.1's, with the Digitone's addresses, and
        # each of its labels where core 2.1 has it.
        assert sorted(dn['build']['sources']) == sorted(['audio.s', 'block.s', 'core.s', 'fw.s', 'leds.s',
                                                  'menu.s', 'midi.s', 'pages.s', 'params.s',
                                                  'projdata.s', 'queue.s', 'render.s', 'rows.s',
                                                  'settings.s', 'voice.s', 'voices.s', 'sound.c', 'selection.c', 'param_names.c', 'param_names.s'])
        assert dn['build']['sources']['core.s'] == dt['build']['sources']['core.s']
        placed = {k: v for k, v in dn['symbols'].items() if v[0] != 'abs'}
        shared = {k: v for k, v in placed.items() if k in dt['symbols']}
        assert shared == {k: dt['symbols'][k] for k in shared}
        assert {'core_voice_on', 'core_dn_boot', 'core_param_ui', 'core_page_rec',
                'core_view_init', 'core_page_open', 'core_page_shown', 'core_proj_save',
                'core_proj_load', 'core_proj_import', 'core_proj_new',
                'core_proj_init', 'core_dn_key', 'core_dn_enc', 'core_dn_tick',
                'core_dn_draw', 'core_menu_open', 'core_param_short', 'core_param_ui_make',
                'core_sound_set', 'core_render_voices', 'core_voices_gate',
                'core_render_master', 'core_midi_cc', 'core_key_leds',
                'core_render_params', 'core_midi_pc', 'core_note_queue'} <= set(placed) - set(shared)
        # voice.s follows core.s's code in .run.
        last = max(off for sec, off in shared.values() if sec == '.run')
        assert placed['core_voice_on'][0] == '.run' and placed['core_voice_on'][1] > last
        rc, r = lint_json(path, '--stock', DN_STOCK)
        assert rc == 0 and r['link']['order'] == ['core ' + DN_VERSION]
        # the firmware locations: absolute symbols, exported
        assert all(dn['symbols'][k][0] == 'abs' and k in dn['exports'] for k in FW_DN)


def test_digitone_core_needs_every_constant():
    """A device constant left out of mod.json is an import nothing provides,
    so the link refuses it, rather than falling back to another device's."""
    with tempfile.TemporaryDirectory() as tmp:
        path = dn_core(tmp, drop='FN_MGR')
        rc, r = lint_json(path, '--stock', DN_STOCK)
        assert rc == 1 and any('FN_MGR' in x for x in r['problems']), r['problems']


CORE_DN23 = os.environ.get('ELEKLOADER_CORE_DN1_23', '')
with open(os.path.join(ROOT, 'mods', 'core-dn1', 'mod.json')) as _fh:
    DN_VERSION = json.load(_fh)['version']     # the core-dn1 these tests build
FW_DN = ['fw_active_track', 'fw_blit', 'fw_ev_alloc', 'fw_ev_free', 'fw_ev_queue', 'fw_fillrect',
         'fw_font5', 'fw_framerect', 'fw_gate_off', 'fw_gate_on', 'fw_kit', 'fw_lfo_state',
         'fw_lock_alloc', 'fw_locks_free', 'fw_nodes_free', 'fw_op_new', 'fw_params', 'fw_slot_ids',
         'fw_str_amp', 'fw_str_empty', 'fw_textf', 'fw_timeline', 'fw_transpose', 'fw_uirecs',
         'fw_tempo', 'fw_font_label', 'fw_font_title', 'fw_voice_len', 'fw_voice_params', 'fw_voice_pitch', 'fw_voice_track', 'fw_voices',
         'fw_memcpy', 'fw_memset', 'fw_memchr', 'fw_strcmp', 'fw_strncmp', 'fw_strchr', 'fw_strncpy', 'fw_crc32',
         '__ashldi3', '__lshrdi3', '__ashrdi3', 'fw_textbox', 'fw_transport', 'fw_pattern', 'fw_project']
FW_DN42 = ['fw_sound_live', 'fw_voice_pitch_processed', 'fw_project_get', 'fw_sound_ref', 'fw_sound_change_event_vtable',
           'fw_ui_get', 'fw_view_param_value', 'fw_ui_grid_edit', 'fw_ui_step_edit', 'fw_key_held', 'fw_track_get',
           'fw_track_length', 'fw_track_has_note', 'fw_track_has_lock', 'fw_track_set_trig',
           'fw_track_set_lock', 'fw_ui_refresh', 'fw_syn_key', 'fw_syn1_draw', 'fw_syn2_draw',
           'fw_string_ctor', 'fw_param_value_text', 'fw_lfo_name_format', 'fw_lfo_choices']
CORE_DN42 = ['core_lock_selected', 'core_param_dest_visible', 'core_param_lfo_choices', 'core_param_lfo_filter', 'core_param_lfo_knob_name', 'core_param_lfo_list_fallback', 'core_param_lfo_list_name', 'core_param_lfo_name', 'core_param_lfo_step', 'core_param_lfo_step_bridge', 'core_param_lfo_value_name', 'core_param_long_name', 'core_param_name', 'core_param_name_call', 'core_param_short_native', 'core_param_notification_value', 'core_param_present_v2', 'core_param_value_text', 'core_sound_apply', 'core_step_selection']


def dn42_exports(doc):
    return CORE_DN42 + FW_DN42 if doc['target']['os'] == '1.44' else []


CORE_DN41 = ['core_note_queue']   # 4.1's
CORE_DN411 = ['core_row_add', 'core_row_new']   # 4.1.1's: the SETTINGS rows' storage
CORE_DN3 = ['core_note_queue', 'core_audio_caps', 'core_key_leds', 'core_menu_open', 'core_midi_cc', 'core_midi_pc', 'core_param_short',
            'core_param_ui_make', 'core_render_master', 'core_render_params', 'core_render_voices', 'core_sound_set',
            'core_voices_gate']


def check_dn_superset(old_path, new_path):
    """core-dn1 keeps all of 2.3: its sites, tables, contributions,
    resources and exports, and every symbol in the same section; it adds
    the firmware exports, the machines' hooks (3.1: two sites, two tables),
    exclusive audio and the MIDI CCs (3.2: three sites, two tables), the
    key LEDs and the block's parameters (3.3: two sites, two tables), their
    calls and 3.3's firmware exports, the program changes (4.0: a site,
    a table), the note queue (4.1: a site, a table) and the rows' storage
    (4.1.1: two routines)."""
    with open(old_path) as fh:
        old = json.load(fh)
    with open(new_path) as fh:
        new = json.load(fh)
    assert old['version'] == '2.3' and new['version'] == DN_VERSION
    for k in ('target', 'contribute', 'resources'):
        assert new[k] == old[k], k
    assert all(s in new['sites'] for s in old['sites'])
    assert len(new['sites']) == len(old['sites']) + 9
    assert sorted(set(new['collections']) - set(old['collections'])) == [
        'core_audio', 'core_param_override', 'ev_key_leds', 'ev_midi_cc', 'ev_midi_pc', 'ev_note_queue',
        'ev_render_params', 'ev_render_voices']
    assert all(new['collections'][k] == v for k, v in old['collections'].items())
    assert sorted(set(new['exports']) - set(old['exports'])) == sorted(CORE_DN3 + FW_DN + CORE_DN411 + dn42_exports(new))
    assert all(new['symbols'][k][0] == v[0] for k, v in old['symbols'].items())


def test_digitone_core_is_2_3_and_more():
    if not CORE_DN23 or not os.path.exists(CORE_DN23):
        raise Skip('missing ELEKLOADER_CORE_DN1_23 (the released core-dn1-2.3.elemod)')
    with tempfile.TemporaryDirectory() as tmp:
        check_dn_superset(CORE_DN23, dn_core(tmp))
        other = CORE_DN23.replace('core-dn1-2.3.elemod', 'core-dn1-2.3-os1.44.elemod')
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144) and os.path.exists(other):
            path, _m = build.build(os.path.join(ROOT, 'mods', 'core-dn1'), DN_STOCK_144, tmp)
            check_dn_superset(other, path)


CORE_DN411_REL = os.environ.get('ELEKLOADER_CORE_DN1_411', '')


def test_digitone_core_4_2_preserves_4_1_1_with_sound_set_fix():
    """Old offsets/ABI/hooks stay. OS1.44 changes one setter JSR operand,
    adds eight shared name/LFO hooks, one optional table and16B value buffer.
    OS1.43 retains exact4.1.1 payload; no native port is assumed."""
    if not CORE_DN411_REL or not os.path.exists(CORE_DN411_REL):
        raise Skip('missing ELEKLOADER_CORE_DN1_411 (pinned upstream 4.1.1 baseline)')
    for st, baseline in ((DN_STOCK, CORE_DN411_REL),
                         (DN_STOCK_144, CORE_DN411_REL.replace('.elemod', '-os1.44.elemod'))):
        if not st or not os.path.exists(st) or not os.path.exists(baseline):
            raise Skip('missing core4.2 compatibility stock/baseline')
        with tempfile.TemporaryDirectory() as tmp:
            path, candidate = build.build(os.path.join(ROOT, 'mods/core-dn1'), st, tmp)
            old = elemod.load_any(baseline)
            with open(baseline) as fh: before = json.load(fh)
            with open(path) as fh: after = json.load(fh)
            assert before['version'] == '4.1.1' and after['version'] == '4.2'
            native = after['target']['os'] == '1.44'
            for key in ('target', 'contribute', 'resources'):
                assert after[key] == before[key], key
            expected_sites = json.loads(json.dumps(before['sites']))
            if native:
                short, = [site for site in expected_sites if site['addr'] == '0x400207b8']
                assert short['relocs'] == [[2, 'abs32', 'sym:core_param_short', 0]]
                short['relocs'] = [[2, 'abs32', 'sym:core_param_short_native', 0]]
            assert after['sites'][:len(before['sites'])] == expected_sites
            assert len(after['sites']) == len(before['sites']) + (8 if native else 0)
            assert after['collections'] == dict(before['collections'], **({'core_param_presenters': {'entry': 4}} if native else {}))
            assert set(after['imports']) == set(before['imports']) | ({'core_param_presenters'} if native else set())
            assert all(after['symbols'][k] == v for k, v in before['symbols'].items())
            assert sorted(set(after['exports']) - set(before['exports'])) == sorted(dn42_exports(after))
            assert set(before['exports']) <= set(after['exports'])
            assert after['sections']['.bss']['size'] == before['sections']['.bss']['size'] + (16 if native else 0)
            s, dev, _release = formats.load(st)
            image = formats.main_image(s, dev)
            for section in ('.boot', '.run'):
                previous = bytes(elemod.parts_bytes(old.sections[section]['parts'], image, dev))
                current = bytes(elemod.parts_bytes(candidate.sections[section]['parts'], image, dev))
                if native and section == '.run':
                    pos = previous.index(bytes.fromhex('4eb94009c992'), before['symbols']['core_sound_set'][1])
                    assert current[:pos] == previous[:pos]
                    assert current[pos:pos+6] == bytes.fromhex('4eb900000000')
                    assert current[pos+6:len(previous)] == previous[pos+6:]
                    assert ['.run', pos+2, 'abs32', 'sec:.run', after['symbols']['core_sound_apply'][1]] in after['relocs']
                else:
                    assert current[:len(previous)] == previous, section
                if after['target']['os'] == '1.43' or section == '.boot':
                    assert current == previous
            assert all(reloc in after['relocs'] for reloc in before['relocs'])
            rc, report = lint_json(path, '--stock', st)
            assert rc == 0, report['problems']
            if after['target']['os'] == '1.44':
                ex, mod = build.build(os.path.join(ROOT, 'examples/dn-sound-edit'), st, tmp)
                assert not mod.sites and mod.needs_core == '4.2'
                assert set(mod.imports) <= {'core_sound_set'}
                rc, report = lint_json(ex, '--stock', st, '--with', path)
                assert rc == 0, report['problems']
                rc, report = lint_json(ex, '--stock', st, '--with', baseline)
                assert rc == 1 and any('needs core 4.2' in x for x in report['problems']), report['problems']


C_DN = """
#include "digitone-mk1/core3.h"
static uint32_t dn_when;
void dn_voice_on(int32_t voice, int32_t track, void *event)
{
    fw_voice_pitch[voice] += (uint32_t)FW_VOICE_PARAM(voice, 26) << 8;
    dn_when = fw_timeline;
    (void)track; (void)event;
}
static void dn_open(void *brain, void *event, int32_t track)
{
    (void)brain; (void)event; (void)track;
}
static const uint16_t dn_icon[16] = { 0x8001, 0x4002 };
const struct core_menu_item dn_menu = { "DN TEST", dn_open, CORE_MENU_ICON, dn_icon };
"""


def test_a_digitone_mod_in_c_names_only_core_exports():
    """A Digitone mod written against digitone-mk1/core3.h: it needs core-dn1
    3.0 and names only core's symbols, so its 1.44 port is empty and both
    builds carry the same code; beside core-dn1 2.3 it is refused plainly."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, 'dntest')
        os.makedirs(d)
        with open(os.path.join(d, 'dntest.c'), 'w') as fh:
            fh.write(C_DN)
        with open(os.path.join(d, 'mod.json'), 'w') as fh:
            json.dump({'id': 'dntest', 'version': '1.0', 'device': 'digitone-mk1', 'os': '1.43',
                       'sources': ['dntest.c'], 'ports': {'1.44': {}},
                       'subscribe': [{'event': 'ev_voice_on', 'fn': 'dn_voice_on', 'order': 50}],
                       'contribute': [{'to': 'core_menu', 'order': 50, 'data': '00000000',
                                       'relocs': [[0, 'abs32', 'sym:dn_menu', 0]]}],
                       'requires': ['core'], 'resources': {'core': '3.0'}}, fh)
        path, m = build.build(d, DN_STOCK, tmp)
        assert m.needs_core == '3.0' and not m.sites
        assert set(m.imports) == {'fw_voice_pitch', 'fw_voice_params', 'fw_timeline'}
        rc, r = lint_json(path, '--stock', DN_STOCK, '--with', dn_core(tmp))
        assert rc == 0, r['problems']
        if CORE_DN23 and os.path.exists(CORE_DN23):
            rc, r = lint_json(path, '--stock', DN_STOCK, '--with', CORE_DN23)
            assert rc == 1 and r['problems'] == [
                'dntest 1.0 needs core 3.0 or newer, and this build has core 2.3'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            path4, m4 = build.build(d, DN_STOCK_144, tmp)
            assert path4.endswith('dntest-1.0-os1.44.elemod')
            images = []
            for st, mod in ((DN_STOCK, m), (DN_STOCK_144, m4)):
                s, dev, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(mod.sections['.run']['parts'],
                                                       formats.main_image(s, dev), dev)))
            assert images[0] == images[1] and m.relocs == m4.relocs
            core4, _m = build.build(os.path.join(ROOT, 'mods', 'core-dn1'), DN_STOCK_144, tmp)
            rc, r = lint_json(path4, '--stock', DN_STOCK_144, '--with', core4)
            assert rc == 0, r['problems']


CORE_DN32 = os.environ.get('ELEKLOADER_CORE_DN1_32', '')


DN33_EXPORTS = ['core_key_leds', 'core_render_params', 'fw_memcpy', 'fw_memset', 'fw_memchr', 'fw_strcmp',
                'fw_strncmp', 'fw_strchr', 'fw_strncpy', 'fw_crc32', '__ashldi3', '__lshrdi3', '__ashrdi3',
                'fw_textbox', 'fw_transport', 'fw_pattern', 'fw_project']


def check_dn_32(old_path, new_path):
    """core-dn1 4.0 keeps all of 3.2: its sites, tables, contributions,
    resources and exports, every symbol of 3.2's in the same section. It
    adds 3.3's (the key LEDs and the block's parameters: two sites, two
    tables, their calls and the firmware exports), 4.0's insert
    audio (a flag and core_audio_caps, with no site) and program changes (a
    site and a table) (its bigger area is the profile's, not the core's),
    4.1's note queue (a site and a table) and 4.1.1's rows' storage (two
    routines)."""
    with open(old_path) as fh:
        old = json.load(fh)
    with open(new_path) as fh:
        new = json.load(fh)
    assert old['version'] == '3.2' and new['version'] == DN_VERSION
    for k in ('target', 'contribute', 'resources'):
        assert new[k] == old[k], k
    assert sorted(set(new['collections']) - set(old['collections'])) == ['ev_key_leds', 'ev_midi_pc',
                                                                         'ev_note_queue', 'ev_render_params']
    assert all(new['collections'][k] == v for k, v in old['collections'].items())
    assert sorted(set(new['exports']) - set(old['exports'])) == sorted(DN33_EXPORTS + ['core_audio_caps',
                                                                                     'core_midi_pc']
                                                                       + CORE_DN41 + CORE_DN411 + dn42_exports(new))

    def what(d):
        return [(s['addr'], s['len'], s['kind'], s['new'], s.get('relocs')) for s in d['sites']]
    assert all(x in what(new) for x in what(old)) and len(new['sites']) == len(old['sites']) + 4
    assert all(new['symbols'][k][0] == v[0] for k, v in old['symbols'].items())


def test_digitone_core_4_is_3_2_and_more():
    if not CORE_DN32 or not os.path.exists(CORE_DN32):
        raise Skip('missing ELEKLOADER_CORE_DN1_32 (the released core-dn1-3.2.elemod)')
    with tempfile.TemporaryDirectory() as tmp:
        check_dn_32(CORE_DN32, dn_core(tmp))
        other = CORE_DN32.replace('core-dn1-3.2.elemod', 'core-dn1-3.2-os1.44.elemod')
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144) and os.path.exists(other):
            path4, _m = build.build(os.path.join(ROOT, 'mods', 'core-dn1'), DN_STOCK_144, tmp)
            check_dn_32(other, path4)


CORE_DN40 = os.environ.get('ELEKLOADER_CORE_DN1_40', '')


def check_dn_40(old_path, new_path):
    """core-dn1 4.1 keeps all of 4.0: its sites, tables, contributions, resources
    and exports, every symbol of 4.0's in the same section; it adds the note
    queue (a site at the queue's entry, the table ev_note_queue), and 4.1.1
    the rows' storage (two routines)."""
    with open(old_path) as fh:
        old = json.load(fh)
    with open(new_path) as fh:
        new = json.load(fh)
    assert old['version'] == '4.0' and new['version'] == DN_VERSION
    for k in ('target', 'contribute', 'resources'):
        assert new[k] == old[k], k
    assert sorted(set(new['collections']) - set(old['collections'])) == ['ev_note_queue']
    assert all(new['collections'][k] == v for k, v in old['collections'].items())
    assert sorted(set(new['exports']) - set(old['exports'])) == sorted(CORE_DN41 + CORE_DN411 + dn42_exports(new))

    def what(d):
        return [(s['addr'], s['len'], s['kind'], s['new'], s.get('relocs')) for s in d['sites']]
    assert all(x in what(new) for x in what(old)) and len(new['sites']) == len(old['sites']) + 1
    assert all(new['symbols'][k][0] == v[0] for k, v in old['symbols'].items())


def test_digitone_core_4_1_is_4_0_and_more():
    if not CORE_DN40 or not os.path.exists(CORE_DN40):
        raise Skip('missing ELEKLOADER_CORE_DN1_40 (the released core-dn1-4.0.elemod)')
    with tempfile.TemporaryDirectory() as tmp:
        check_dn_40(CORE_DN40, dn_core(tmp))
        other = CORE_DN40.replace('core-dn1-4.0.elemod', 'core-dn1-4.0-os1.44.elemod')
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144) and os.path.exists(other):
            path4, _m = build.build(os.path.join(ROOT, 'mods', 'core-dn1'), DN_STOCK_144, tmp)
            check_dn_40(other, path4)


CORE_DN41_REL = os.environ.get('ELEKLOADER_CORE_DN1_41', '')


def check_dn_41(old_path, new_path):
    """core-dn1 4.1.1 is 4.1 with the SETTINGS rows kept off the heap: the same
    sites, tables, contributions and resources, every symbol of 4.1's in the
    same section, and core_additem where it was; it adds core_row_new and
    core_row_add (rows.s) and their row pool in .bss."""
    with open(old_path) as fh:
        old = json.load(fh)
    with open(new_path) as fh:
        new = json.load(fh)
    assert old['version'] == '4.1' and new['version'] == DN_VERSION
    for k in ('target', 'contribute', 'resources', 'collections', 'sites'):
        assert new[k] == old[k], k
    assert sorted(set(new['exports']) - set(old['exports'])) == sorted(CORE_DN411 + dn42_exports(new))
    assert all(new['symbols'][k] == v for k, v in old['symbols'].items()
               if k in ('core_settings', 'core_additem'))
    assert all(new['symbols'][k][0] == v[0] for k, v in old['symbols'].items())
    assert new['symbols']['core_row_new'][0] == '.run' and new['symbols']['row_pool'][0] == '.bss'


def test_digitone_core_4_1_1_is_4_1_and_rows_off_the_heap():
    if not CORE_DN41_REL or not os.path.exists(CORE_DN41_REL):
        raise Skip('missing ELEKLOADER_CORE_DN1_41 (the released core-dn1-4.1.elemod)')
    with tempfile.TemporaryDirectory() as tmp:
        check_dn_41(CORE_DN41_REL, dn_core(tmp))
        other = CORE_DN41_REL.replace('core-dn1-4.1.elemod', 'core-dn1-4.1-os1.44.elemod')
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144) and os.path.exists(other):
            path4, _m = build.build(os.path.join(ROOT, 'mods', 'core-dn1'), DN_STOCK_144, tmp)
            check_dn_41(other, path4)


def test_digitone_core_4_gives_the_mods_1_25_mb():
    """From core-dn1 4.0 the linker lays the mods out from 0x43EC0000 to
    0x44000000 (1.25 MB, the profile's core_ddr), between the OS's highest
    buffer and the bulk area, in place of the 128 KB at 0x47BE0000: mods
    that need 200 KB link with 4.0, and beside the released 3.2 they are
    refused, the message naming the core that has room."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    dev = [x for x in devices.devices() if x.key == 'digitone-mk1'][0]
    assert dev.ddr_for('3.4') == (0x47BE0000, 0x47C00000) == dev.ddr
    assert dev.ddr_for('4.0') == dev.ddr_for('4.1') == (0x43EC0000, 0x44000000)
    with tempfile.TemporaryDirectory() as tmp:
        core4 = dn_core(tmp)
        d = os.path.join(tmp, 'big')
        os.makedirs(d)
        with open(os.path.join(d, 'big.c'), 'w') as fh:
            fh.write('char big_buf[200000];\nvoid big_tick(void *ctrl) { (void)ctrl; big_buf[0]++; }\n')
        with open(os.path.join(d, 'mod.json'), 'w') as fh:
            json.dump({'id': 'big', 'version': '0.1', 'license': 'GPL-2.0-or-later', 'device': 'digitone-mk1',
                       'os': '1.43', 'sources': ['big.c'], 'resources': {'core': '3.2'},
                       'subscribe': [{'event': 'ev_tick', 'fn': 'big_tick'}], 'requires': ['core']}, fh)
        bpath, _b = build.build(d, DN_STOCK, tmp)
        rc, r = lint_json(bpath, '--stock', DN_STOCK, '--with', core4)
        assert rc == 0, r['problems']
        assert r['link']['ram_size'] == 0x140000 and 200000 < r['link']['ram_used'] < 0x140000, r['link']
        if CORE_DN32 and os.path.exists(CORE_DN32):
            rc, r = lint_json(bpath, '--stock', DN_STOCK, '--with', CORE_DN32)
            assert rc == 1 and any('core 4.0 or newer gives them 1310720 bytes' in x for x in r['problems']), \
                r['problems']


def test_digitone_mods_take_core_4_and_its_ram():
    """A builder (the window, the web page, the kit: gui.with_requirements and
    the kit's prepare, which take the same core) takes the newest core,
    core-dn1 4.0, though the mods need only 3.2: with both listed, a small mod
    takes 4.0; 3.2 chosen by hand is kept for it and gives way to 4.0 when the
    mods' RAM is more than its 128 KB (200 KB link with 4.0); and more than 4.0
    holds is refused with no newer core named."""
    if not CORE_DN32 or not os.path.exists(CORE_DN32):
        raise Skip('missing ELEKLOADER_CORE_DN1_32 (the released core-dn1-3.2.elemod)')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    from elekloader import gui
    with tempfile.TemporaryDirectory() as tmp:
        lib, cores = os.path.join(tmp, 'lib'), os.path.join(tmp, 'cores')
        os.makedirs(lib)
        os.makedirs(cores)
        shutil.copy(dn_core(tmp), cores)
        shutil.copy(CORE_DN32, cores)
        for name, size in (('small', 1000), ('big', 200000), ('huge', 1400000)):
            d = os.path.join(tmp, name)
            os.makedirs(d)
            with open(os.path.join(d, name + '.c'), 'w') as fh:
                fh.write('char %s_buf[%d];\nvoid %s_tick(void *ctrl) { (void)ctrl; %s_buf[0]++; }\n'
                         % (name, size, name, name))
            with open(os.path.join(d, 'mod.json'), 'w') as fh:
                json.dump({'id': name, 'version': '0.1', 'license': 'GPL-2.0-or-later', 'device': 'digitone-mk1',
                           'os': '1.43', 'sources': [name + '.c'], 'resources': {'core': '3.2'},
                           'subscribe': [{'event': 'ev_tick', 'fn': name + '_tick'}], 'requires': ['core']}, fh)
            shutil.copy(build.build(d, DN_STOCK, tmp)[0], lib)
        m = gui.LoaderModel(stock=DN_STOCK, library=lib, also=[cores], settings=os.path.join(tmp, 's.json'))
        descs = {p: m.describe(p) for p in m.files()}
        at = {(x['id'] if x['id'] != 'core' else x['version']): p for p, x in descs.items()}
        assert descs[at['3.2']]['ram_area'] == 0x20000 and descs[at[DN_VERSION]]['ram_area'] == 0x140000

        def core_of(on):
            return [descs[p]['version'] for p in on if descs[p]['id'] == 'core']

        assert core_of(gui.with_requirements(descs, set(), at['small'])) == [DN_VERSION]
        small = gui.with_requirements(descs, {at['3.2']}, at['small'])
        assert core_of(small) == ['3.2'] and m.check(sorted(small))['ok']
        big = gui.with_requirements(descs, small, at['big'])
        assert core_of(big) == [DN_VERSION], big
        r = m.check(sorted(big))
        assert r['ok'] and r['ram'][1] == 0x140000, r.get('problems')
        assert 200000 < r['ram'][0] <= sum(descs[p]['ram_bound'] for p in big), r['ram']
        huge = gui.with_requirements(descs, set(), at['huge'])
        assert core_of(huge) == [DN_VERSION]
        r = m.check(sorted(huge))
        assert not r['ok'] and any('the mods need RAM to' in x and 'or newer gives them' not in x
                                   for x in r['problems']), r['problems']


def test_digitone_program_change_example():
    """examples/dn-progchange, on core-dn1 4.0's ev_midi_pc: it patches no
    site, needs 4.0, carries the same code for 1.43 and 1.44 (an empty port),
    links with 4.0, and is refused beside the released 3.2, which has no
    ev_midi_pc."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        core4 = dn_core(tmp)
        ppath, p = build.build(os.path.join(ROOT, 'examples', 'dn-progchange'), DN_STOCK, tmp)
        assert os.path.basename(ppath) == 'dnprog-1.0.elemod'
        assert p.needs_core == '4.0' and not p.sites
        assert set(p.imports) <= {'fw_fillrect', 'fw_textf', 'fw_font5'}, p.imports
        rc, r = lint_json(ppath, '--stock', DN_STOCK, '--with', core4)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'dnprog 1.0']
        if CORE_DN32 and os.path.exists(CORE_DN32):
            rc, r = lint_json(ppath, '--stock', DN_STOCK, '--with', CORE_DN32)
            assert rc == 1 and 'dnprog 1.0 needs core 4.0 or newer, and this build has core 3.2' \
                in r['problems'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            path4, p4 = build.build(os.path.join(ROOT, 'examples', 'dn-progchange'), DN_STOCK_144, tmp)
            images = []
            for st, x in ((DN_STOCK, p), (DN_STOCK_144, p4)):
                s, dev, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                       formats.main_image(s, dev), dev)))
            assert images[0] == images[1] and p.relocs == p4.relocs


def test_digitone_machines_build_and_link():
    """The machines mod (mods/machines-dn1) and its example machine
    (examples/dn-sine-machine), in C on core-dn1 3.1's names: they patch no
    site, need 3.1, carry the same code for 1.43 and 1.44 (empty ports), link
    with core-dn1 3.2, and are refused beside 2.3."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        core3 = dn_core(tmp)
        mpath, m = build.build(os.path.join(ROOT, 'mods', 'machines-dn1'), DN_STOCK, tmp)
        spath, sm = build.build(os.path.join(ROOT, 'examples', 'dn-sine-machine'), DN_STOCK, tmp)
        assert os.path.basename(mpath) == 'machines-%s.elemod' % MACH_VERSION
        assert m.needs_core == '3.1' and sm.needs_core == '3.1' and not m.sites and not sm.sites
        assert {'core_sound_set', 'core_param_ui_make', 'core_menu_open', 'fw_params',
                'fw_active_track'} <= set(m.imports)
        assert {'dnm_knob', 'dnm_phase_inc', 'dnm_sin'} <= set(m.exports)
        rc, r = lint_json(spath, '--stock', DN_STOCK, '--with', core3, '--with', mpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'dnsine ' + SINE_VERSION, 'machines ' + MACH_VERSION]
        if CORE_DN23 and os.path.exists(CORE_DN23):
            rc, r = lint_json(mpath, '--stock', DN_STOCK, '--with', CORE_DN23)
            assert rc == 1 and 'machines %s needs core 3.1 or newer, and this build has core 2.3' % MACH_VERSION \
                in r['problems'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            for src, mod in (('mods/machines-dn1', m), ('examples/dn-sine-machine', sm)):
                path4, mod4 = build.build(os.path.join(ROOT, src), DN_STOCK_144, tmp)
                images = []
                for st, x in ((DN_STOCK, mod), (DN_STOCK_144, mod4)):
                    s, dev, _rel = formats.load(st)
                    images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                           formats.main_image(s, dev), dev)))
                assert images[0] == images[1] and mod.relocs == mod4.relocs, src


def test_digitone_exclusive_audio_example():
    """examples/dn-thru, on core-dn1 3.2's core_audio: it patches no site,
    needs 3.2, carries the same code for 1.43 and 1.44 (an empty port),
    links with 3.3 beside the machines mod, and is refused beside 2.3. A
    region in the profile's bulk area passes; one past it does not."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    dev = [x for x in devices.devices() if x.key == 'digitone-mk1'][0]
    assert dev.areas['bulk'] == (0x44000000, 0x47BE0000)
    regions, _names = elemod.parse_resources(
        {'resources': {'regions': [{'name': 'buffers', 'lo': '0x44000000', 'hi': '0x45b00000'}]}}, 'x', dev)
    assert regions[0]['area'] == 'bulk'
    try:
        elemod.parse_resources({'resources': {'regions': [{'name': 'past', 'lo': '0x47bd0000',
                                                             'hi': '0x47be8000'}]}}, 'x', dev)
        raise AssertionError('a region across two areas passed')
    except elemod.ModError as e:
        assert 'outside every free area' in str(e)
    with tempfile.TemporaryDirectory() as tmp:
        core3 = dn_core(tmp)
        tpath, t = build.build(os.path.join(ROOT, 'examples', 'dn-thru'), DN_STOCK, tmp)
        mpath, _m = build.build(os.path.join(ROOT, 'mods', 'machines-dn1'), DN_STOCK, tmp)
        assert os.path.basename(tpath) == 'dnthru-1.0.elemod'
        assert t.needs_core == '3.2' and not t.sites
        assert set(t.imports) <= {'fw_fillrect', 'fw_textf', 'fw_font5'}, t.imports
        rc, r = lint_json(tpath, '--stock', DN_STOCK, '--with', core3, '--with', mpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'dnthru 1.0', 'machines ' + MACH_VERSION]
        if CORE_DN23 and os.path.exists(CORE_DN23):
            rc, r = lint_json(tpath, '--stock', DN_STOCK, '--with', CORE_DN23)
            assert rc == 1 and 'dnthru 1.0 needs core 3.2 or newer, and this build has core 2.3' \
                in r['problems'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            path4, t4 = build.build(os.path.join(ROOT, 'examples', 'dn-thru'), DN_STOCK_144, tmp)
            images = []
            for st, x in ((DN_STOCK, t), (DN_STOCK_144, t4)):
                s, d, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                       formats.main_image(s, d), d)))
            assert images[0] == images[1] and t.relocs == t4.relocs


def test_digitone_insert_audio_example():
    """examples/dn-tremolo, on core-dn1 4.0's insert audio
    (CORE_AUDIO_INSERT): it patches no site and names no firmware address,
    needs 3.4, carries the same code for 1.43 and 1.44 (an empty port),
    links with 4.0 beside THRU (an exclusive owner) and the machines mod,
    and is refused beside 3.2."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        core3 = dn_core(tmp)
        path, t = build.build(os.path.join(ROOT, 'examples', 'dn-tremolo'), DN_STOCK, tmp)
        tpath, _t = build.build(os.path.join(ROOT, 'examples', 'dn-thru'), DN_STOCK, tmp)
        mpath, _m = build.build(os.path.join(ROOT, 'mods', 'machines-dn1'), DN_STOCK, tmp)
        assert os.path.basename(path) == 'dntrem-1.0.elemod'
        assert t.needs_core == '4.0' and not t.sites and not t.imports
        rc, r = lint_json(path, '--stock', DN_STOCK, '--with', core3, '--with', tpath, '--with', mpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'dnthru 1.0', 'dntrem 1.0', 'machines ' + MACH_VERSION]
        if CORE_DN32 and os.path.exists(CORE_DN32):
            rc, r = lint_json(path, '--stock', DN_STOCK, '--with', CORE_DN32)
            assert rc == 1 and 'dntrem 1.0 needs core 4.0 or newer, and this build has core 3.2' \
                in r['problems'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            path4, t4 = build.build(os.path.join(ROOT, 'examples', 'dn-tremolo'), DN_STOCK_144, tmp)
            images = []
            for st, x in ((DN_STOCK, t), (DN_STOCK_144, t4)):
                s, d, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                       formats.main_image(s, d), d)))
            assert images[0] == images[1] and t.relocs == t4.relocs


def test_digiswarm_builds_and_links():
    """mods/digiswarm-dn1 (DigiSwarm) 0.2, an effect on fxkit: it patches no
    site, needs core-dn1 4.1 and fxkit, carries the same code for 1.43 and
    1.44 (an empty port), keeps its buffers in a region of the bulk area,
    links beside fxkit, THRU and the machines mod (two owners of the output
    may share a build), and is refused beside the released 4.0."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        core3 = dn_core(tmp)
        fxk = build.build(os.path.join(ROOT, 'mods', 'fxkit-dn1'), DN_STOCK, tmp)[0]
        dpath, d = build.build(os.path.join(ROOT, 'mods', 'digiswarm-dn1'), DN_STOCK, tmp)
        tpath, _t = build.build(os.path.join(ROOT, 'examples', 'dn-thru'), DN_STOCK, tmp)
        mpath, _m = build.build(os.path.join(ROOT, 'mods', 'machines-dn1'), DN_STOCK, tmp)
        assert os.path.basename(dpath) == 'digiswarm-0.2.elemod'
        assert d.needs_core == '4.1' and not d.sites
        assert {'fx_activate', 'fx_deactivate', 'fx_set', 'fx_set_later', 'fx_lock_key', 'fx_turned',
                'fx_pack', 'fx_unpack'} <= set(d.imports), d.imports
        assert [(g['area'], g['lo'], g['hi']) for g in d.regions] == [('bulk', 0x44000000, 0x45B00000)]
        rc, r = lint_json(dpath, '--stock', DN_STOCK, '--with', core3, '--with', fxk, '--with', tpath,
                          '--with', mpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'digiswarm 0.2', 'dnthru 1.0', 'fxkit 0.1',
                                       'machines ' + MACH_VERSION], r['link']['order']
        if CORE_DN40 and os.path.exists(CORE_DN40):
            rc, r = lint_json(dpath, '--stock', DN_STOCK, '--with', CORE_DN40, '--with', fxk)
            assert rc == 1 and 'digiswarm 0.2 needs core 4.1 or newer, and this build has core 4.0' \
                in r['problems'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            path4, d4 = build.build(os.path.join(ROOT, 'mods', 'digiswarm-dn1'), DN_STOCK_144, tmp)
            assert path4.endswith('digiswarm-0.2-os1.44.elemod')
            images = []
            for st, x in ((DN_STOCK, d), (DN_STOCK_144, d4)):
                s, dv, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                       formats.main_image(s, dv), dv)))
            assert images[0] == images[1] and d.relocs == d4.relocs


def test_digiprism_builds_and_links():
    """mods/digiprism-dn1 (DigiPrism) 0.2, an effect on fxkit with an
    insert of its own (core_audio): it patches no site, needs core-dn1 4.1
    and fxkit, carries the same code for 1.43 and 1.44 (an empty port), keeps
    its buffers in a region of the bulk area beside DigiSwarm's, links beside
    fxkit, DigiSwarm, THRU and the machines mod (three owners of the output in
    one build), and is refused beside the released 4.0."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        core3 = dn_core(tmp)
        fxk = build.build(os.path.join(ROOT, 'mods', 'fxkit-dn1'), DN_STOCK, tmp)[0]
        hpath, h = build.build(os.path.join(ROOT, 'mods', 'digiprism-dn1'), DN_STOCK, tmp)
        dpath, _d = build.build(os.path.join(ROOT, 'mods', 'digiswarm-dn1'), DN_STOCK, tmp)
        tpath, _t = build.build(os.path.join(ROOT, 'examples', 'dn-thru'), DN_STOCK, tmp)
        mpath, _m = build.build(os.path.join(ROOT, 'mods', 'machines-dn1'), DN_STOCK, tmp)
        assert os.path.basename(hpath) == 'digiprism-0.2.elemod'
        assert h.needs_core == '4.1' and not h.sites
        assert {'core_audio_caps', 'fx_activate', 'fx_chain', 'fx_deactivate', 'fx_set', 'fx_set_later',
                'fx_lock_key', 'fx_pack'} <= set(h.imports), h.imports
        assert set(h.weak) == {'core_audio_caps'}     # with a core that has no insert, the source is the inputs
        assert [(g['area'], g['lo'], g['hi']) for g in h.regions] == [('bulk', 0x45B00000, 0x46200000)]
        rc, r = lint_json(hpath, '--stock', DN_STOCK, '--with', core3, '--with', fxk, '--with', dpath,
                          '--with', tpath, '--with', mpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'digiprism 0.2', 'digiswarm 0.2', 'dnthru 1.0',
                                       'fxkit 0.1', 'machines ' + MACH_VERSION], r['link']['order']
        if CORE_DN40 and os.path.exists(CORE_DN40):
            rc, r = lint_json(hpath, '--stock', DN_STOCK, '--with', CORE_DN40, '--with', fxk)
            assert rc == 1 and 'digiprism 0.2 needs core 4.1 or newer, and this build has core 4.0' \
                in r['problems'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            path4, h4 = build.build(os.path.join(ROOT, 'mods', 'digiprism-dn1'), DN_STOCK_144, tmp)
            assert path4.endswith('digiprism-0.2-os1.44.elemod')
            images = []
            for st, x in ((DN_STOCK, h), (DN_STOCK_144, h4)):
                s, dv, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                       formats.main_image(s, dv), dv)))
            assert images[0] == images[1] and h.relocs == h4.relocs


def test_seq_events_builds_and_links():
    """mods/seq-events-dn1 and examples/dn-euclid: seq-events patches the
    step fire's 15 sites (1.44's are 1.43's 0x20 on, with the same stock
    bytes), declares the eight event tables and needs only the hook bus, so
    it links with core-dn1 2.3 as with 3.2. EUCLID patches nothing, names no
    firmware address (the same code for 1.43 and 1.44) and links beside it."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    tables = {'ev_fire_step', 'ev_gen_trig', 'ev_fire_trig', 'ev_fire_cond', 'ev_gen_notes',
              'ev_fire_notes', 'ev_fire_vel', 'ev_fire_locks'}
    with tempfile.TemporaryDirectory() as tmp:
        core3 = dn_core(tmp)
        qpath, q = build.build(os.path.join(ROOT, 'mods', 'seq-events-dn1'), DN_STOCK, tmp)
        epath, e = build.build(os.path.join(ROOT, 'examples', 'dn-euclid'), DN_STOCK, tmp)
        assert os.path.basename(qpath) == 'seq-events-0.1.elemod'
        assert len(q.sites) == 15 and set(q.imports) == tables, q.imports
        assert os.path.basename(epath) == 'dneuclid-1.0.elemod'
        assert not e.sites and not e.imports, e.imports
        rc, r = lint_json(epath, '--stock', DN_STOCK, '--with', core3, '--with', qpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'dneuclid 1.0', 'seq-events 0.1']
        if CORE_DN23 and os.path.exists(CORE_DN23):
            rc, r = lint_json(qpath, '--stock', DN_STOCK, '--with', CORE_DN23)
            assert rc == 0, r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            qpath4, q4 = build.build(os.path.join(ROOT, 'mods', 'seq-events-dn1'), DN_STOCK_144, tmp)
            path4, e4 = build.build(os.path.join(ROOT, 'examples', 'dn-euclid'), DN_STOCK_144, tmp)
            assert qpath4.endswith('seq-events-0.1-os1.44.elemod')
            assert [(x['addr'] + 0x20, x['stock_sha256']) for x in q.sites] == \
                [(x['addr'], x['stock_sha256']) for x in q4.sites]
            images = []
            for st, x in ((DN_STOCK, e), (DN_STOCK_144, e4)):
                sd, dv, _rel = formats.load(st)
                images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                       formats.main_image(sd, dv), dv)))
            assert images[0] == images[1] and e.relocs == e4.relocs


def test_seq_events_dt1_builds_and_links():
    """mods/seq-events-dt1 and examples/euclid: the Digitakt mk1's seq-events
    patches the step fire's 15 sites, at the same addresses with the same
    stock bytes in 1.53 and 1.54, declares the eight event tables and needs
    only the hook bus, so it links with core 2.1 as with 3.0, and beside
    machine-pages (no site in common). EUCLID is the Digitone's C with the
    Digitakt's header: it patches nothing and names no firmware address."""
    st = stock()
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    tables = {'ev_fire_step', 'ev_gen_trig', 'ev_fire_trig', 'ev_fire_cond', 'ev_gen_notes',
              'ev_fire_notes', 'ev_fire_vel', 'ev_fire_locks'}
    with tempfile.TemporaryDirectory() as tmp:
        qpath, q = build.build(os.path.join(ROOT, 'mods', 'seq-events-dt1'), st, tmp)
        epath, e = build.build(os.path.join(ROOT, 'examples', 'euclid'), st, tmp)
        assert os.path.basename(qpath) == 'seq-events-0.1.elemod'
        assert len(q.sites) == 15 and set(q.imports) == tables, q.imports
        assert os.path.basename(epath) == 'euclid-1.0.elemod'
        assert not e.sites and not e.imports, e.imports
        rc, r = lint_json(epath, '--stock', st, '--with', built_core(), '--with', qpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DT_VERSION, 'euclid 1.0', 'seq-events 0.1']
        mpath, _m = build.build(os.path.join(ROOT, 'mods', 'machine-pages'), st, tmp)
        rc, r = lint_json(qpath, '--stock', st, '--with', built_core(), '--with', mpath)
        assert rc == 0, r['problems']
        if CORE_21 and os.path.exists(CORE_21):
            rc, r = lint_json(qpath, '--stock', st, '--with', CORE_21)
            assert rc == 0, r['problems']
        if STOCK_154 and os.path.exists(STOCK_154):
            qpath4, q4 = build.build(os.path.join(ROOT, 'mods', 'seq-events-dt1'), STOCK_154, tmp)
            path4, e4 = build.build(os.path.join(ROOT, 'examples', 'euclid'), STOCK_154, tmp)
            assert qpath4.endswith('seq-events-0.1-os1.54.elemod')
            assert [(x['addr'], x['stock_sha256']) for x in q.sites] == \
                [(x['addr'], x['stock_sha256']) for x in q4.sites]
            images = []
            for stf, x in ((st, e), (STOCK_154, e4)):
                sd, dv, _rel = formats.load(stf)
                images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                       formats.main_image(sd, dv), dv)))
            assert images[0] == images[1] and e.relocs == e4.relocs


GAS_PEDALS = [('digitide', 0x47300000, 0x47700000), ('digitriad', 0x47B00000, 0x47BE0000),
              ('digireel', 0x47700000, 0x47900000), ('digihoard', 0x46200000, 0x47300000),
              ('digihalo', 0x47900000, 0x47B00000)]


def test_gas_pedals_build_and_link():
    """The five effects pedals on the pedal shell (mods/digitide-dn1, digitriad-dn1, digireel-dn1,
    digihoard-dn1, digihalo-dn1), 0.2, each a whole Digitone mod and an effect on fxkit: each patches no
    site, needs core-dn1 4.1 and fxkit, keeps its buffers in a region of its own in the bulk area and
    carries the same code for 1.43 and 1.44 (an empty port). All five, DigiSwarm, DigiPrism, fxkit and GAIN
    link together with the tree's core (over 128 KB together, they need its 1.25 MB), and beside the
    released 4.0 each pedal is refused."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        core = dn_core(tmp)
        fxk = build.build(os.path.join(ROOT, 'mods', 'fxkit-dn1'), DN_STOCK, tmp)[0]
        paths = []
        for name, lo, hi in GAS_PEDALS:
            path, m = build.build(os.path.join(ROOT, 'mods', name + '-dn1'), DN_STOCK, tmp)
            assert os.path.basename(path) == name + '-0.2.elemod'
            assert m.needs_core == '4.1' and not m.sites, name
            assert [(g['area'], g['lo'], g['hi']) for g in m.regions] == [('bulk', lo, hi)], (name, m.regions)
            rc, r = lint_json(path, '--stock', DN_STOCK, '--with', core, '--with', fxk)
            assert rc == 0, (name, r['problems'])
            if CORE_DN40 and os.path.exists(CORE_DN40):
                rc, r = lint_json(path, '--stock', DN_STOCK, '--with', CORE_DN40, '--with', fxk)
                assert rc == 1 and '%s 0.2 needs core 4.1 or newer, and this build has core 4.0' % name \
                    in r['problems'], r['problems']
            if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
                path4, m4 = build.build(os.path.join(ROOT, 'mods', name + '-dn1'), DN_STOCK_144, tmp)
                assert path4.endswith(name + '-0.2-os1.44.elemod')
                images = []
                for st, x in ((DN_STOCK, m), (DN_STOCK_144, m4)):
                    sd, dv, _rel = formats.load(st)
                    images.append(bytes(elemod.parts_bytes(x.sections['.run']['parts'],
                                                           formats.main_image(sd, dv), dv)))
                assert images[0] == images[1] and m.relocs == m4.relocs, name
            paths.append(path)
        for name in ('digiswarm-dn1', 'digiprism-dn1'):
            paths.append(build.build(os.path.join(ROOT, 'mods', name), DN_STOCK, tmp)[0])
        paths.append(build.build(os.path.join(ROOT, 'examples', 'dn-fx-gain'), DN_STOCK, tmp)[0])
        others = sum((['--with', x] for x in paths[1:]), [])
        rc, r = lint_json(paths[0], '--stock', DN_STOCK, '--with', core, '--with', fxk, *others)
        assert rc == 0, r['problems']
        assert r['link']['ram_used'] > 0x20000, r['link']        # over 128 KB: 4.0's area


def test_fxkit_and_gain_build_and_link():
    """mods/fxkit-dn1 (what the effects share) and examples/dn-fx-gain (GAIN, the smallest effect): neither
    patches a site; fxkit needs core-dn1 4.1 (its note queue) and takes the audio and its project data from
    core's tables, GAIN needs fxkit; each carries the same code for 1.43 and 1.44 (an empty port: they name
    only core's exports), and they link together with the tree's core; beside the released 4.0 fxkit is
    refused."""
    if not DN_STOCK or not os.path.exists(DN_STOCK):
        raise Skip('missing ELEKLOADER_DN_SYX')
    tc = [x for x in devices.devices() if x.key == 'digitone-mk1'][0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        core = dn_core(tmp)
        fpath, f = build.build(os.path.join(ROOT, 'mods', 'fxkit-dn1'), DN_STOCK, tmp)
        gpath, g = build.build(os.path.join(ROOT, 'examples', 'dn-fx-gain'), DN_STOCK, tmp)
        assert os.path.basename(fpath) == 'fxkit-0.1.elemod' and os.path.basename(gpath) == 'dnfxgain-1.0.elemod'
        assert f.needs_core == '4.1' and not f.sites and g.needs_core == '4.1' and not g.sites
        assert {'fw_pattern', 'fw_timeline', 'fw_tempo', 'fw_transport', 'fx_effects'} <= set(f.imports), f.imports
        assert {'fx_activate', 'fx_deactivate', 'fx_lock_key', 'fx_page_draw', 'fx_pack', 'fx_unpack'} \
            <= set(g.imports), g.imports
        assert {'fx_activate', 'fx_set', 'fx_turned', 'fx_lock_mask', 'fx_pack'} <= set(f.exports), f.exports
        rc, r = lint_json(gpath, '--stock', DN_STOCK, '--with', core, '--with', fpath)
        assert rc == 0, r['problems']
        assert r['link']['order'] == ['core ' + DN_VERSION, 'dnfxgain 1.0', 'fxkit 0.1'], r['link']['order']
        if CORE_DN40 and os.path.exists(CORE_DN40):
            rc, r = lint_json(fpath, '--stock', DN_STOCK, '--with', CORE_DN40)
            assert rc == 1 and 'fxkit 0.1 needs core 4.1 or newer, and this build has core 4.0' \
                in r['problems'], r['problems']
        if DN_STOCK_144 and os.path.exists(DN_STOCK_144):
            for d, x in ((os.path.join(ROOT, 'mods', 'fxkit-dn1'), f), (os.path.join(ROOT, 'examples', 'dn-fx-gain'), g)):
                path4, x4 = build.build(d, DN_STOCK_144, tmp)
                images = []
                for st, y in ((DN_STOCK, x), (DN_STOCK_144, x4)):
                    sd, dv, _rel = formats.load(st)
                    images.append(bytes(elemod.parts_bytes(y.sections['.run']['parts'],
                                                           formats.main_image(sd, dv), dv)))
                assert images[0] == images[1] and x.relocs == x4.relocs, d


def test_example_builds_and_links():
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        path, m = build.build(os.path.join(ROOT, 'examples', 'hello-marker'), stock(), tmp)
        assert m.size('.run') > 0 and 'hello_draw' in m.exports
        rc, r = lint_json(path, '--stock', stock(), '--with', core())
        assert rc == 0 and 'hello_draw' in r['link']['addresses']


STOCK_154 = os.environ.get('ELEKLOADER_STOCK_154', '')


def stock_154():
    if not STOCK_154 or not os.path.exists(STOCK_154):
        raise Skip('missing ELEKLOADER_STOCK_154')
    return STOCK_154


def test_core_port_to_154_is_the_same_core():
    """mods/core's 1.54 port: the same code and the same sites, at 1.54's
    addresses; a 1.53 core is refused on 1.54."""
    st154 = stock_154()
    with tempfile.TemporaryDirectory() as tmp:
        path, _m = build.build(os.path.join(ROOT, 'mods', 'core'), st154, tmp)
        assert os.path.basename(path) == 'core-%s-os1.54.elemod' % DT_VERSION
        with open(path) as fh:
            new = json.load(fh)
        with open(built_core()) as fh:
            old = json.load(fh)
        assert new['target']['os'] == '1.54' and old['target']['os'] == '1.53'
        for k in ('exports', 'collections', 'contribute', 'resources', 'build'):
            assert new[k] == old[k], k
        def sizes(d):
            return {s: v.get('len', v.get('size')) for s, v in d['sections'].items()}
        assert sizes(new) == sizes(old)

        def what(d):                # each site's new bytes and targets, not where it is
            return [(s['len'], s['kind'], s['new'], s.get('relocs')) for s in d['sites']]
        assert what(new) == what(old)
        rc, r = lint_json(path, '--stock', st154)
        assert rc == 0, r['problems']
        rc, r = lint_json(built_core(), '--stock', st154)
        assert rc == 1 and any('1.53' in x and '1.54' in x for x in r['problems']), r['problems']


def test_example_port_to_154_builds_and_links():
    st154 = stock_154()
    tc = devices.devices()[0].toolchain
    if not shutil.which(os.environ.get('ELEKLOADER_CROSS', tc['prefix']) + 'gcc'):
        raise Skip('no m68k cross compiler')
    with tempfile.TemporaryDirectory() as tmp:
        c, _m = build.build(os.path.join(ROOT, 'mods', 'core'), st154, tmp)
        path, m = build.build(os.path.join(ROOT, 'examples', 'hello-marker'), st154, tmp)
        assert path.endswith('hello-marker-1.0-os1.54.elemod') and 'hello_draw' in m.exports
        rc, r = lint_json(path, '--stock', st154, '--with', c)
        assert rc == 0 and 'hello_draw' in r['link']['addresses']


DN_STOCK_144 = os.environ.get('ELEKLOADER_DN_SYX_144', '')


def test_digitone_core_port_to_144_is_the_same_core():
    """Shared core sites/layout stay equal. Only 1.44 has the appended 4.2
    native helpers; the 1.43 build is still refused as a 1.44 core."""
    if not DN_STOCK_144 or not os.path.exists(DN_STOCK_144):
        raise Skip('missing ELEKLOADER_DN_SYX_144')
    with tempfile.TemporaryDirectory() as tmp:
        old_path = dn_core(tmp)
        path, _m = build.build(os.path.join(ROOT, 'mods', 'core-dn1'), DN_STOCK_144, tmp)
        assert os.path.basename(path) == 'core-%s-os1.44.elemod' % DN_VERSION
        with open(path) as fh:
            new = json.load(fh)
        with open(old_path) as fh:
            old = json.load(fh)
        assert new['target']['os'] == '1.44' and old['target']['os'] == '1.43'
        for k in ('contribute', 'resources'):
            assert new[k] == old[k], k
        assert new['collections'] == dict(old['collections'], core_param_presenters={'entry': 4})
        def sizes(d):
            return {s: v.get('len', v.get('size')) for s, v in d['sections'].items()}
        assert sorted(set(new['exports']) - set(old['exports'])) == sorted(CORE_DN42 + FW_DN42)
        assert set(old['exports']) <= set(new['exports'])
        assert new['build']['sources'] == old['build']['sources']
        assert sizes(new)['.bss'] == sizes(old)['.bss'] + 16
        assert sizes(new)['.boot'] == sizes(old)['.boot']
        assert sizes(new)['.run'] > sizes(old)['.run']
        assert all(new['symbols'][k] == v for k, v in old['symbols'].items() if v[0] != 'abs')

        def what(d):                # each site's new bytes and targets, not where it is
            return [(s['len'], s['kind'], s['new'], s.get('relocs')) for s in d['sites']]
        expected = json.loads(json.dumps(old))
        short, = [site for site in expected['sites'] if site['addr'] == '0x400207b8']
        assert short['relocs'] == [[2, 'abs32', 'sym:core_param_short', 0]]
        short['relocs'] = [[2, 'abs32', 'sym:core_param_short_native', 0]]
        assert len(new['sites']) == 96 and what(new)[:88] == what(expected)
        rc, r = lint_json(path, '--stock', DN_STOCK_144)
        assert rc == 0, r['problems']
        rc, r = lint_json(old_path, '--stock', DN_STOCK_144)
        assert rc == 1 and any('1.43' in x and '1.44' in x for x in r['problems']), r['problems']


def test_digitone_core_parameter_presentation_without_machines():
    if not DN_STOCK_144 or not CORE_DN411_REL:
        raise Skip('missing explicit native compatibility inputs')
    with tempfile.TemporaryDirectory() as tmp:
        core, _c = build.build(os.path.join(ROOT, 'mods/core-dn1'), DN_STOCK_144, tmp)
        example, mod = build.build(os.path.join(ROOT, 'examples/dn-param-presentation'), DN_STOCK_144, tmp)
        assert not mod.sites and mod.needs_core == '4.2'
        assert set(mod.requires) == {'core'}
        assert 'core_param_present_v2' in mod.imports
        # An earlier 12-byte draft consumer must not link to this 16-byte ABI.
        with open(example) as fh: prior_marker = fh.read().replace('core_param_present_v2', 'core_param_present_v1')
        prior_example = os.path.join(tmp, 'prior-presenter.elemod')
        with open(prior_example, 'w') as fh: fh.write(prior_marker)
        rc, report = lint_json(prior_example, '--stock', DN_STOCK_144, '--with', core)
        assert rc == 1 and any('core_param_present_v1' in x for x in report['problems']), report['problems']
        rc, report = lint_json(example, '--stock', DN_STOCK_144, '--with', core)
        assert rc == 0, report['problems']
        baseline = CORE_DN411_REL.replace('.elemod', '-os1.44.elemod')
        rc, report = lint_json(example, '--stock', DN_STOCK_144, '--with', baseline)
        assert rc == 1 and any('needs core 4.2' in x for x in report['problems']), report['problems']


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
