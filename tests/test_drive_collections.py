# SPDX-License-Identifier: GPL-2.0-or-later
"""+Drive collections (docs/DRIVE.md, "+Drive collections"): the A4 loader's boot linker
(mods/drive-loader-a4mk2/dlink.c, DL_COLLECTIONS) links tables +Drive mods declare, and its
descriptor says so (F_COLLECTIONS, the loader's export drive_collections), so the host twin
(elekloader/drive.py, Build.collections) does the same on that build only.

Built for the computer and run against the twin: a declaring mod and the mods adding to its table load
in any /mods/active order, the table is placed after every .bss with its living entries sorted as the
merge sorts them and a zero entry, and NAME and NAME_n resolve; a contribution whose table no living mod
declares, or of another entry size, is refused, also when its declarer goes in a later round; two mods
declaring one name, a name the build has, a DSP table and a bad entry size are refused alike; the
MODS page's check agrees; and the Rytm's loader (drive-loader-ar2, no F_COLLECTIONS) refuses
"collections" as before, as does the twin on its build.

    python tests/test_drive_collections.py

Needs a C compiler (cc)."""
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from elekloader import desc                                        # noqa: E402
from test_link_refusals import Bench                               # noqa: E402
import test_drive as TD                                            # noqa: E402
from test_drive_top_stage import boot, compare, states, twin      # noqa: E402
from test_drive_abs16 import A4, AR2, Built, build_host           # noqa: E402

FEATURE, TABLE, EXPORT, FORMAT, ROOM = 7, 15, 12, 5, 17


def decl(B, ident='sc-core', tables=None, **extra):
    """A mod declaring tables (sc_cmds, 8-byte entries, by default) whose .run holds, at 12 and 16,
    sc_cmds' address and sc_cmds_n (abs32)."""
    d = TD.dmod(B, ident, **extra)
    d['sections']['.run'] = {'align': 4, 'len': 20, 'parts': [['hex', '4e714e714e750000' + '00' * 12]]}
    d['collections'] = tables if tables is not None else {'sc_cmds': {'entry': 8}}
    if 'sc_cmds' in d['collections']:
        d['relocs'] = d['relocs'] + [['.run', 12, 'abs32', 'sym:sc_cmds', 0], ['.run', 16, 'abs32', 'sym:sc_cmds_n', 0]]
        d['imports'] = d['imports'] + ['sc_cmds', 'sc_cmds_n']
    return d


def adder(B, ident, order=50, to='sc_cmds', data=None, relocs=None, **extra):
    """A mod adding one entry to `to`: its function's address, then its id's length."""
    d = TD.dmod(B, ident, **extra)
    d['contribute'] = [{'to': to, 'order': order,
                        'data': data if data is not None else '00000000%08x' % len(ident),
                        'relocs': relocs if relocs is not None else [[0, 'abs32', 'sym:fn_' + ident, 0]]}]
    return d


def main():
    work = tempfile.mkdtemp()
    a4_exe = build_host(work, A4, 'drive_link_host_a4')
    ar2_exe = build_host(work, AR2, 'drive_link_host_ar2')
    fails = []

    def check(name, cond, info=''):
        print('%s %s %s' % ('ok  ' if cond else 'FAIL', name, info if not cond else ''))
        if not cond:
            fails.append(name)

    def details(sm):
        return [x.get('detail', '') for x in sm['status']]

    with Bench('analog-rytm-mk2') as B:
        F = Built(B)
        check('a loader exporting drive_collections sets the descriptor\'s F_COLLECTIONS (bit 4)',
              F.build.d['flags'] & desc.F_COLLECTIONS == 16 and F.build.collections, F.build.d['flags'])
        check('the descriptor\'s text names it', '+Drive collections' in desc.dump(F.build.d))

        def alike(name, mods, want_states, want_details=None):
            sm, ram = boot(a4_exe, F, mods, work)
            r = twin(F, mods)
            check(name + ': ' + str(want_states), states(sm) == want_states, sm['status'])
            bad = compare(F, sm, ram, r)
            if details(sm) != [c.detail[:31] for c in r.choices]:
                bad.append('details %s, twin %s' % (details(sm), [c.detail for c in r.choices]))
            check(name + ': C and twin agree', not bad, bad)
            if want_details is not None:
                check(name + ': details ' + str(want_details), details(sm) == want_details, details(sm))
            sc, _ram = boot(a4_exe, F, mods, work, check=True)
            rc = twin(F, mods, deep=False)
            check(name + ': the MODS page\'s check agrees', states(sc) == [(c.state, c.code, c.arg) for c in rc.choices]
                  and states(sc) == states(sm), (sc['status'], sm['status']))
            return sm, ram, r

        def word(ram, a):
            return struct.unpack('>I', ram[a - F.ram_base:a - F.ram_base + 4])[0]

        # ---- a declarer and two adders, the adders first in /mods/active ----
        mods = [adder(B, 'sc-zz', 40), adder(B, 'sc-aa', 60), decl(B)]
        sm, ram, r = alike('a table and two adders', mods, [(1, 0, 0)] * 3)
        t = r.tables['sc_cmds']
        core = r.base[('sc-core', '.run')]
        check('the table is after every .bss, its NAME and NAME_n in the declarer\'s code',
              t['address'] >= max(a + 8 for (_i, s), a in r.base.items() if s == '.bss')
              and word(ram, core + 12) == t['address'] and word(ram, core + 16) == 2,
              (hex(t['address']), hex(word(ram, core + 12)), word(ram, core + 16)))
        check('entries sorted by order: sc-zz (40) then sc-aa (60), each its function\'s address, then a zero entry',
              t['entries'] == [('sc-zz', 40), ('sc-aa', 60)]
              and word(ram, t['address']) == r.base[('sc-zz', '.run')]
              and word(ram, t['address'] + 8) == r.base[('sc-aa', '.run')]
              and word(ram, t['address'] + 4) == 5 and ram[t['address'] + 16 - F.ram_base:][:8] == bytes(8))
        check('the area ends after the table', r.end == t['address'] + 3 * 8, (hex(r.end), hex(t['address'])))

        # the declarer adds to its own table, an entry of two with relocations in the second
        d = decl(B)
        d['contribute'] = [{'to': 'sc_cmds', 'order': 50, 'data': '00' * 16,
                            'relocs': [[8, 'abs32', 'sym:fn_sc-core', 0], [12, 'abs32', 'sym:sc_cmds_n', 0]]}]
        sm, ram, r = alike('the declarer adds two entries itself', [d, adder(B, 'sc-b', 50)], [(1, 0, 0)] * 2)
        t = r.tables['sc_cmds']
        check('its second entry is relocated where the merge put it',
              t['entries'] == [('sc-b', 50), ('sc-core', 50), ('sc-core', 50)]
              and word(ram, t['address'] + 16) == r.base[('sc-core', '.run')] and word(ram, t['address'] + 20) == 3)

        # a declared table with no entries: a zero entry, NAME_n 0
        sm, ram, r = alike('a table nothing adds to', [decl(B)], [(1, 0, 0)])
        core = r.base[('sc-core', '.run')]
        check('nothing added: NAME_n is 0 and the table one zero entry',
              word(ram, core + 16) == 0 and r.end == word(ram, core + 12) + 8)

        # ---- refusals ----
        alike('no mod declares the table', [adder(B, 'sc-a')], [(2, TABLE, 0)], ['sc_cmds'])
        alike('an entry of another size', [decl(B), adder(B, 'sc-a', data='00000000')],
              [(1, 0, 0), (2, TABLE, 0)], ['', 'sc_cmds'])
        gone = decl(B, imports=['core_fn', 'nothing_has_this', 'sc_cmds', 'sc_cmds_n'])
        alike('the declarer is refused: its adder goes in the next round',
              [adder(B, 'sc-a'), gone], [(2, TABLE, 0), (2, 14, 0)], ['sc_cmds', 'nothing_has_this'])
        alike('two declare one name: the later in id order goes, the adder joins the earlier',
              [decl(B, 'sc-b'), decl(B, 'sc-a'), adder(B, 'sc-x')], [(2, EXPORT, 0), (1, 0, 0), (1, 0, 0)],
              ['sc_cmds', '', ''])
        alike('a table named as a flashed export', [decl(B, tables={'core_fn': {'entry': 4}})], [(2, EXPORT, 0)],
              ['core_fn'])
        alike('a table whose NAME_n is a flashed table\'s', [decl(B, tables={'ev_tick': {'entry': 4}})],
              [(2, EXPORT, 0)], ['ev_tick'])
        alike('a table named as another mod\'s export', [decl(B, tables={'sc-a_count': {'entry': 4}}), TD.dmod(B, 'sc-a')],
              [(2, EXPORT, 0), (1, 0, 0)], ['sc-a_count', ''])
        alike('a DSP table', [decl(B, tables={'dsp_t': {'space': 'dsp', 'entry': 3}})], [(2, FEATURE, 0)],
              ['collections'])
        for name, tables in (('an entry of 6', {'t6': {'entry': 6}}), ('an entry of 0', {'t0': {'entry': 0}}),
                             ('no entry', {'tn': {}}), ('an entry of true', {'tt': {'entry': True}}),
                             ('a name of 60 bytes', {'x' * 60: {'entry': 4}}), ('an empty name', {'': {'entry': 4}}),
                             ('a list', [])):
            alike('collections: ' + name, [decl(B, tables=tables)],
                  [(1, 0, 0)] if name == 'a list' else [(2, FORMAT, 0)])
        alike('a name of 59 bytes loads', [decl(B, tables={'y' * 59: {'entry': 4}})], [(1, 0, 0)])
        alike('spare, max and render are a flashed build\'s: ignored',
              [decl(B, tables={'sc_cmds': {'entry': 8, 'spare': 4, 'max': 1, 'render': True}}),
               adder(B, 'sc-a'), adder(B, 'sc-b')], [(1, 0, 0)] * 3)
        alike('a contribution to a flashed table that takes no more is still a table refusal',
              [adder(B, 'sc-a', to='core_handles', data='00000000', relocs=[])], [(2, TABLE, 0)], ['core_handles'])

        # the room: the tables count in the layout
        S = Built(B)
        S.out, S.build = TD.built(B, ('a4', 3000))
        ddr_base, run_end = S.out.layout['ddr']
        o = S.out.layout['run_load'] - B.dev.main_load
        S.ram_base, S.ram, S.desc_at = ddr_base, bytes(S.out.image[o:o + run_end - ddr_base]), S.out.map['core_desc']
        many = [adder(B, 'sc-%03d' % i) for i in range(40)]
        for m in many:
            m['sections']['.bss'] = {'size': 96}
        sm, ram = boot(a4_exe, S, [decl(B)] + many, work)
        r = twin(S, [decl(B)] + many)
        bad = compare(S, sm, ram, r)
        check('a small area: C and twin agree on what the room refuses', not bad and any(
            c.code == ROOM for c in r.choices), (bad, [(c.id, c.code) for c in r.choices][:6]))

        # ---- the Rytm's loader: no F_COLLECTIONS ----
        R = Built(B, abs16=False)
        check('without drive_collections: no F_COLLECTIONS', not R.build.d['flags'] & desc.F_COLLECTIONS
              and not R.build.collections)
        for name, mods, want in (('a mod declaring a table', [decl(B)], [(2, FEATURE, 0)]),
                                 ('a mod adding to one no build has', [adder(B, 'sc-a')], [(2, TABLE, 0)])):
            sm, ram = boot(ar2_exe, R, mods, work)
            r = twin(R, mods)
            check('drive-loader-ar2: %s refused' % name, states(sm) == want, sm['status'])
            check('drive-loader-ar2: %s, C and twin agree' % name, not compare(R, sm, ram, r)
                  and details(sm) == [c.detail for c in r.choices], (compare(R, sm, ram, r), details(sm)))

    print('all passed' if not fails else '%d failed' % len(fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
