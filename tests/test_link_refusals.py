# SPDX-License-Identifier: GPL-2.0-or-later
"""Every refusal the linker and the mod loader make, each with a fixture that must be refused and
one that must link (pytest, or run with python). Needs no firmware.

The fixtures run on made-up releases of three real device profiles (the Octatrack's, the Digitakt
mk1's and the Analog Rytm MKII's): an image of nops, as long as the real one, registered for the
test only. The profile is the real one, so its protected and kept ranges, its free areas, its RAM
and its fast SRAM are the ones a build meets. A fixture's must-pass twin is the smallest change that
makes the must-refuse one link, and it goes through `link.link` whole, the image check included.

A message is a class. `test_every_refusal_has_a_fixture` reads the messages out of elekloader/link.py
and the loader's functions in elekloader/elemod.py (by their syntax, each field blanked) and fails for
one that no row below names, so a new refusal comes with its fixtures or is not merged. The DSP
refusals have their fixtures in tests/test_octatrack.py, which needs the real Octatrack OS for the DSP
payload. SHARC linker refusals are in tests/test_sharc_active.py and tests/test_sharc_filter.py
with the owner's DN2 stock and cross compiler; `external` says where, and the test checks the text
is still there.
"""
import ast
import copy
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from elekloader import devices, elemod, link, store               # noqa: E402
from elekloader.devices import Release                             # noqa: E402
from elekloader.elemod import ModError, sha                        # noqa: E402

NOPS = b'\x4e\x71'
_images = {}


class Bench:
    """One made-up release of a real device, for the length of a `with`."""

    def __init__(self, key):
        self.dev = [d for d in devices.devices() if d.key == key][0]
        real = next(iter(self.dev.releases.values()))
        if key not in _images:
            _images[key] = NOPS * (real.main_len // 2)
        self.img = _images[key]
        assert len(self.img) == real.main_len
        self.rel = Release('SYN', sha(b'synthetic ' + key.encode()), sha(self.img), len(self.img))
        self.target = devices.target_of(self.dev, self.rel)

    def __enter__(self):
        self.dev.releases['SYN'] = self.rel
        return self

    def __exit__(self, *exc):
        del self.dev.releases['SYN']

    # -- addresses and sites
    def at(self, off):
        return self.dev.main_load + off

    def site(self, addr, n=4, kind='data', new=None, relocs=None):
        o = addr - self.dev.main_load
        s = {'addr': '0x%08x' % addr, 'len': n, 'kind': kind,
             'new': new if new is not None else ('ff' * n if kind == 'data' else '4e71' * (n // 2)),
             'stock_sha256': sha(self.img[o:o + n])}
        if relocs:
            s['relocs'] = relocs
        return s

    def jmp_site(self, addr, sym):
        """A code site that replaces three nops with a jmp to `sym`."""
        return self.site(addr, 6, 'code', '4ef900000000', [[2, 'abs32', 'sym:' + sym, 0]])

    # -- the mods
    def core(self, ident='core', **extra):
        d = {'elemod': 2, 'id': ident, 'version': '1', 'target': self.target,
             'sections': {'.boot': {'align': 4, 'len': 2, 'parts': [['hex', '4e75']]}},
             'symbols': {'core_fn': ['.boot', 0]}, 'exports': ['core_fn'],
             'collections': {'ev_x': {'entry': 4}}}
        d.update(extra)
        return d

    def mod(self, ident='m', **extra):
        d = {'elemod': 2, 'id': ident, 'version': '1', 'target': self.target, 'requires': ['core'],
             'sections': {'.run': {'align': 4, 'len': 8, 'parts': [['hex', '4e714e714e714e75']]},
                          '.bss': {'size': 16}},
             'symbols': {'fn_' + ident: ['.run', 0]}, 'imports': ['core_fn'],
             'contribute': [{'to': 'ev_x', 'data': '00000000',
                             'relocs': [[0, 'abs32', 'sym:fn_' + ident, 0]]}]}
        d.update(extra)
        return d

    def other(self, ident='n', **extra):
        """A second mod with nothing but its RAM image."""
        d = self.mod(ident, **extra)
        d['contribute'] = []
        return d

    def plain(self):
        """A core and a mod `m`: a RAM image, a table entry and a code site; links as it is."""
        m = self.mod()
        m['sites'] = [self.jmp_site(self.at(0x1000), 'fn_m')]
        return [self.core(), m]

    def cases(self, bads, good=None):
        """-> ([(expect, docs)], docs). `bads` is [(expect, edit)], each edit changing a copy of
        the plain docs in place; `good` edits another copy (none: as it is)."""
        out = []
        for expect, edit in bads:
            docs = self.plain()
            edit(docs)
            out.append((expect, docs))
        docs = self.plain()
        if good:
            good(docs)
        return out, docs

    def pair(self, expect, bad, good=None):
        return self.cases([(expect, bad)], good)

    def run(self, x):
        """Link what `x` is: a list of mod documents, or a function that does it. -> None when it
        links, else the refusal's text."""
        try:
            if callable(x):
                x()
            else:
                link.link([link.Mod2(d, d['id']) for d in x], self.img)
        except ModError as e:
            return str(e)
        return None


ROWS = {}               # (function, message) -> (device, the function that makes its fixtures)
EXTERNAL = {}           # (function, message) -> (a test file, text it holds)


def row(fn, template, device='octatrack'):
    def deco(f):
        assert (fn, template) not in ROWS, (fn, template)
        ROWS[(fn, template)] = (device, f)
        return f
    return deco


def external(fn, template, path, text):
    EXTERNAL[(fn, template)] = (path, text)


external('check', 'SHARC mods may combine only one DN2 voice and one DN2 filter',
         'tests/test_sharc_filter.py', ['SHARC mods may combine only one DN2 voice and one DN2 filter',
                                      'test_pair_binds_both_hooks_and_is_order_independent'])
external('check', 'active SHARC mod requires a qualified matching core bridge', 'tests/test_sharc_active.py',
         'active SHARC mod requires a qualified matching core bridge')
external('check', '<> has unqualified active SHARC core bytes or entry', 'tests/test_sharc_active.py',
         'unqualified active SHARC core bytes or entry')
external('check', 'active SHARC stack exceeds the profile limit', 'tests/test_sharc_active.py',
         'active SHARC stack exceeds the profile limit')
external('check', '<>: DN2 <> registration requires a matching SHARC <> payload',
         'tests/test_sharc_filter.py', ['DN2 %s registration requires a matching SHARC %s payload',
                                      'test_pair_binds_both_hooks_and_is_order_independent'])
external('check', '<>: SHARC <> machine_id must match its single native <> registration',
         'tests/test_sharc_filter.py', ['SHARC %s machine_id must match its single native %s registration',
                                      'test_pair_binds_both_hooks_and_is_order_independent'])
external('check', '<>: DN2 filter G needs a label of 1..5 printable ASCII characters and a default of 0..127',
         'tests/test_sharc_filter.py', ['DN2 filter G needs a label', 'test_refuses_an_invalid_g_and_links_a_valid_one'])
external('parse_sites', '<>: unqualified SHARC data site', 'tests/test_sharc_filter.py',
         ['unqualified SHARC data site', 'test_only_the_exact_profiled_sharc_data_site_is_accepted'])
external('check', '<>: SHARC data site requires a SHARC mod', 'tests/test_sharc_filter.py',
         ['SHARC data site requires a SHARC mod', 'test_busy_companion_requires_a_sharc_mod_and_owns_only_its_site'])
external('link', 'SHARC mod needs the stock section-7 stream', 'tests/test_sharc_active.py',
         'SHARC mod needs the stock section-7 stream')
external('link', 'SHARC stored section exceeds the profile limit', 'tests/test_sharc_active.py',
         'SHARC stored section exceeds the profile limit')


def edit_region(name, lo, hi):
    return lambda d: d[1].update(resources={'regions': [{'name': name, 'lo': lo, 'hi': hi}]})


# ---- a mod that cannot be loaded (link.Mod2) -----------------------------------------------------

@row('Mod2.__init__', '<>: not a format-2 .elemod file')
def _(B):
    return B.pair('m: not a format-2 .elemod file', lambda d: d[1].update(elemod=1))


@row('Mod2.__init__', '<>: unknown fields <>')
def _(B):
    return B.pair("m: unknown fields ['bogus']", lambda d: d[1].update(bogus=1),
                  lambda d: d[1].update(notes='a known field'))


@row('Mod2.__init__', '<>: no "<>"')
def _(B):
    return B.pair('m: no "sections"', lambda d: d[1].pop('sections'))


@row('Mod2.__init__', '<>: unknown section <>')
def _(B):
    return B.pair('m: unknown section .text',
                  lambda d: d[1]['sections'].update({'.text': {'len': 2, 'parts': [['hex', '4e75']]}}))


@row('Mod2.__init__', '<> <>: alignment <> (a power of two up to <>)')
def _(B):
    return B.pair('m .run: alignment 3 (a power of two up to 4096)',
                  lambda d: d[1]['sections']['.run'].update(align=3),
                  lambda d: d[1]['sections']['.run'].update(align=32))


@row('Mod2.__init__', '<> <>: parts make <> bytes, not <>')
def _(B):
    return B.pair('m .run: parts make 8 bytes, not 9', lambda d: d[1]['sections']['.run'].update(len=9))


@row('Mod2.__init__', '<>: symbol <>')
def _(B):
    return B.pair('m: symbol x', lambda d: d[1]['symbols'].update(x='.run'),
                  lambda d: d[1]['symbols'].update(x=['.run', 4]))


@row('Mod2.__init__', '<>: symbol <> in a section it lacks (<>)')
def _(B):
    return B.pair('m: symbol x in a section it lacks (.fast)',
                  lambda d: d[1]['symbols'].update(x=['.fast', 0]),
                  lambda d: d[1]['symbols'].update(x=['.bss', 0]))


@row('Mod2.__init__', '<>: exports <>, which it does not define')
def _(B):
    return B.pair('m: exports ghost, which it does not define',
                  lambda d: d[1].update(exports=['ghost']), lambda d: d[1].update(exports=['fn_m']))


@row('Mod2.__init__', '<>: a weak name it does not import')
def _(B):
    return B.pair('m: a weak name it does not import', lambda d: d[1].update(weak=['core_fn', 'zz']),
                  lambda d: d[1].update(weak=['core_fn']))


@row('Mod2.__init__', '<>: table <> entry size <>')
def _(B):
    return B.pair('m: table t entry size 6', lambda d: d[1].update(collections={'t': {'entry': 6}}),
                  lambda d: d[1].update(collections={'t': {'entry': 8}}))


@row('Mod2.__init__', '<>: bad relocation <>')
def _(B):
    def entry(at):
        return lambda d: d[1]['contribute'][0].update(relocs=[[at, 'abs32', 'sym:fn_m', 0]])
    return B.pair('m contribution 0: bad relocation', entry(2), entry(0))


@row('Mod2.__init__', '<>: <>')
def _(B):               # the storage module's reading of the mod's "storage"
    def storage(value):
        return lambda d: d[1].update(storage=value)
    return B.cases([('m: "storage" is a list of fields', storage('x')),
                    ('m: a storage field is {"sym", "scope", "bits"}', storage([{'sym': 'a', 'x': 1}])),
                    ('m: storage field \'1x\': "sym" is a C name',
                     storage([{'sym': '1x', 'scope': 'project', 'bits': 1}])),
                    ("m: storage field a: scope 'lunch'", storage([{'sym': 'a', 'scope': 'lunch', 'bits': 1}])),
                    ('m: storage field a: bits 33 (1 to 32)',
                     storage([{'sym': 'a', 'scope': 'project', 'bits': 33}])),
                    ('m: storage field a is declared twice',
                     storage([{'sym': 'a', 'scope': 'project', 'bits': 1}] * 2))], storage([]))


@row('Mod2._reloc_ok', '<>: relocation in <>')
def _(B):
    return B.pair('m: relocation in .bss', lambda d: d[1].update(relocs=[['.bss', 0, 'abs32', 'abs', 0]]),
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'abs', 0]]))


@row('Mod2._reloc_ok', '<>: relocation type <>')
def _(B):
    return B.pair('m: relocation type abs64',
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs64', 'abs', 0]]),
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'abs', 0]]))


@row('Mod2._reloc_ok', '<>: a <> relocation in <>')
def _(B):
    return B.pair('m: a dsp24 relocation in .run',
                  lambda d: d[1].update(relocs=[['.run', 0, 'dsp24', 'abs', 0]]),
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'abs', 0]]))


@row('Mod2._reloc_ok', '<>: relocation at <>+<> is outside it')
def _(B):
    return B.pair('m: relocation at .run+6 is outside it',
                  lambda d: d[1].update(relocs=[['.run', 6, 'abs32', 'abs', 0]]),
                  lambda d: d[1].update(relocs=[['.run', 4, 'abs32', 'abs', 0]]))


@row('Mod2._reloc_ok', '<>: relocation target <>')
def _(B):
    return B.pair("m: relocation target 'foo'",
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'foo', 0]]),
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'sym:fn_m', 0]]))


# the DSP side of loading: fixtures in tests/test_octatrack.py, which has the real payloads
external('Mod2.__init__', '<> <>: <> bytes, not whole 24-bit DSP words', 'tests/test_octatrack.py',
         'not whole 24-bit DSP words')
external('Mod2.__init__', '<>: DSP symbol <> is not at a whole word', 'tests/test_octatrack.py',
         'DSP symbol tst is not at a whole word')
external('Mod2.__init__', '<>: table <> in space <>, which the <> lacks', 'tests/test_octatrack.py',
         'which the Octatrack lacks')
external('Mod2.__init__', '<>: DSP table <> is not whole 24-bit words', 'tests/test_octatrack.py',
         'DSP table t is not whole 24-bit words')


# ---- numbers, parts, sites and resources (elemod.py) ----------------------------------------------

@row('_int', '<>: <> is not a number')
def _(B):
    return B.pair('is not a number', lambda d: d[1]['sites'][0].update(addr='zz'))


@row('_hex', '<>: not hex')
def _(B):
    return B.pair('m .run part: not hex', lambda d: d[1]['sections']['.run'].update(
        parts=[['hex', 'zz4e714e714e714e75']]))


@row('resolve_target', '<>: <>')
def _(B):
    return B.pair('m: made for a firmware elekloader does not know',
                  lambda d: d[1].update(target={'device': 'octatrack', 'syx_sha256': '0' * 64}))


@row('parse_parts', '<>: a part copies 0x<> +<>, outside the image')
def _(B):
    def part(addr):
        return lambda d: d[1]['sections']['.run'].update(
            parts=[['hex', '4e714e71'], ['stock', '0x%08x' % addr, 4]])
    return B.pair('m .run: a part copies 0x%08x +4, outside the image' % (B.dev.main_load - 4),
                  part(B.dev.main_load - 4), part(B.at(0x100)))


@row('parse_parts', '<>: bad part <>')
def _(B):
    return B.pair("m .run: bad part ['nope']", lambda d: d[1]['sections']['.run'].update(parts=[['nope']]))


@row('parse_sites', '<>: "new" is not <> bytes')
def _(B):
    return B.pair('m site 0: "new" is not 6 bytes', lambda d: d[1]['sites'][0].update(new='4ef9'))


@row('parse_sites', '<>: 0x<> +<> is outside the stock image')
def _(B):
    def site(addr):
        return lambda d: d[1].update(sites=[B.site(addr, 4)])
    end = B.dev.main_load + len(B.img)
    return B.pair('is outside the stock image', site(end), site(end - 4))


@row('parse_sites', '<>: kind is "code" or "data"')
def _(B):
    return B.pair('kind is "code" or "data"', lambda d: d[1]['sites'][0].update(kind='both'))


@row('parse_sites', '<>: no stock_sha256')
def _(B):
    return B.pair('no stock_sha256', lambda d: d[1]['sites'][0].pop('stock_sha256'))


@row('parse_sites', '<>: bad relocation <>')
def _(B):
    def reloc(at):
        return lambda d: d[1]['sites'][0].update(relocs=[[at, 'abs32', 'sym:fn_m', 0]])
    return B.pair('bad relocation', reloc(4), reloc(2))


@row('parse_resources', '<>: empty region <>')
def _(B):
    lo = B.dev.areas['ddr'][0]
    return B.pair('m: empty region', edit_region('r', lo, lo), edit_region('r', lo, lo + 16))


@row('parse_resources', '<>: region <> 0x<>-0x<> is outside every free area')
def _(B):
    lo = B.dev.areas['ddr'][0]
    return B.pair('m: region r 0x00001000-0x00001010 is outside every free area',
                  edit_region('r', 0x1000, 0x1010), edit_region('r', lo, lo + 16))


@row('parse_needs_core', '<>: resources.core is the version of core it needs, such as "3.0"')
def _(B):
    return B.pair('resources.core is the version of core it needs',
                  lambda d: d[1].update(resources={'core': 3}),
                  lambda d: d[1].update(resources={'core': '1'}))


@row('read_json', '<>: not JSON (<>)')
def _(B):
    def read(text):
        def f():
            with tempfile.TemporaryDirectory() as tmp:
                p = os.path.join(tmp, 'x.elemod')
                with open(p, 'w') as fh:
                    fh.write(text)
                elemod.read_json(p)
        return f
    return [('x.elemod: not JSON', read('{"elemod": 2,'))], read('{"elemod": 2}')


# ---- the combination (link.check and elemod.common_checks) ---------------------------------------

@row('check', 'exactly one mod must carry .boot (core); got <>')
def _(B):
    def second_core(d):
        c = B.core('core2', collections={}, symbols={}, exports=[])
        d.append(c)
    return B.cases([('core is not enabled, and every other mod builds on it', lambda d: d.pop(0)),
                    ("exactly one mod must carry .boot (core); got ['core 1', 'core2 1']", second_core)])


@row('check', '<> needs core <> or newer, and this build has core <>')
def _(B):
    return B.pair('m 1 needs core 9 or newer, and this build has core 1',
                  lambda d: d[1].update(resources={'core': '9'}),
                  lambda d: d[1].update(resources={'core': '1'}))


@row('check', '<> and <> both export <>')
def _(B):
    def second(name):
        def f(d):
            n = B.other('n', symbols={'fn_n': ['.run', 0], 'fn_m': ['.run', 4]}, exports=[name])
            d[1]['symbols']['fn_m'] = ['.run', 0]
            d[1]['exports'] = ['fn_m']
            d.append(n)
        return f
    return B.pair('m 1 and n 1 both export fn_m', second('fn_m'), second('fn_n'))


@row('check', '<> and <> both declare the table <>')
def _(B):
    return B.pair('core 1 and m 1 both declare the table ev_x',
                  lambda d: d[1].update(collections={'ev_x': {'entry': 4}}),
                  lambda d: d[1].update(collections={'ev_y': {'entry': 4}}))


@row('check', 'table <> has the name of a symbol <> exports')
def _(B):
    def export(name):
        def f(d):
            d[1]['symbols'][name] = ['.run', 4]
            d[1]['exports'] = [name]
        return f
    return B.pair('table ev_x has the name of a symbol m 1 exports', export('ev_x'), export('ev_y'))


@row('check', '<> imports <>, which no given mod exports')
def _(B):
    return B.pair('m 1 imports ghost, which no given mod exports',
                  lambda d: d[1].update(imports=['core_fn', 'ghost']),
                  lambda d: d[1].update(imports=['core_fn', 'ghost'], weak=['ghost']))


@row('check', '<> adds to table <>, which no given mod declares')
def _(B):
    return B.pair('m 1 adds to table ev_nope, which no given mod declares',
                  lambda d: d[1]['contribute'][0].update(to='ev_nope'))


@row('check', '<>: an entry for <> is not <> bytes')
def _(B):
    return B.pair('m 1: an entry for ev_x is not 4 bytes',
                  lambda d: d[1]['contribute'][0].update(data='000000000000', relocs=[]),
                  lambda d: d[1]['contribute'][0].update(data='0000000000000000', relocs=[]))


@row('check', '<>: an entry for <> has a <> relocation at +<>; <>')
def _(B):
    return B.pair('m 1: an entry for ev_x has a dsp24 relocation at +0; dsp24 is for DSP tables',
                  lambda d: d[1]['contribute'][0].update(relocs=[[0, 'dsp24', 'sym:fn_m', 0]]))


@row('check', '<> has .fast code, which needs the mod that declares <>', device='digitakt-mk1')
def _(B):
    def fast(d):
        d[1]['sections']['.fast'] = {'align': 4, 'len': 4, 'parts': [['hex', '4e714e75']]}

    def declared(d):
        fast(d)
        d[0]['collections']['fa_copies'] = {'entry': 16}
    return B.pair('m 1 has .fast code, which needs the mod that declares fa_copies', fast, declared)


@row('check', '<> holds at most <> entries on <> <>; <> add<> <>', device='digitakt-mk1')
def _(B):
    def machines(n_m, n_n):
        def f(d):
            d[0]['collections']['sdk_alpha_machines'] = {'entry': 4}
            d[1]['contribute'].append({'to': 'sdk_alpha_machines', 'data': '00000000' * n_m, 'relocs': []})
            d.append(B.other('n'))
            d[2]['contribute'] = [{'to': 'sdk_alpha_machines', 'data': '00000000' * n_n, 'relocs': []}]
        return f
    return B.pair('sdk_alpha_machines holds at most 32 entries on digitakt-mk1 SYN; m 1 and n 1 add 33',
                  machines(16, 17), machines(16, 16))


external('check', '<> has DSP code for payload <>, which needs a mod that frees DSP memory there '
         '(one that claims <>)', 'tests/test_octatrack.py', 'which needs a mod that frees DSP memory')


@row('check', '<> <> and <> <> overlap')
def _(B):
    lo = B.dev.areas['ddr'][0]

    def regions(a2):
        def f(d):
            edit_region('ra', lo, lo + 0x100)(d)
            d.append(B.other('n', resources={'regions': [{'name': 'rb', 'lo': a2, 'hi': a2 + 0x100}]}))
        return f
    return B.pair('m 1 region ra and n 1 region rb overlap', regions(lo + 0x80), regions(lo + 0x100))


@row('common_checks', 'mod <> is given <> times')
def _(B):
    return B.pair('mod m is given 2 times', lambda d: d.append(copy.deepcopy(d[1])))


@row('common_checks', '<> requires <>')
def _(B):
    return B.pair('m 1 requires nothing', lambda d: d[1].update(requires=['core', 'nothing']))


@row('common_checks', '<> conflicts with <>')
def _(B):
    return B.pair('m 1 conflicts with core', lambda d: d[1].update(conflicts=['core']),
                  lambda d: d[1].update(conflicts=['nobody']))


@row('common_checks', '<> <> and <> <> overlap (0x<>-0x<>)')
def _(B):
    def other(at):
        return lambda d: d.append(B.other('n', sites=[B.site(at, 4)]))
    return B.pair('m 1 site 0x%08x and n 1 site 0x%08x overlap (0x%08x-0x%08x)'
                  % (B.at(0x1000), B.at(0x1004), B.at(0x1004), B.at(0x1006)),
                  other(B.at(0x1004)), other(B.at(0x1006)))


@row('common_checks', '<> and <> both claim <>')
def _(B):
    def claims(a, b):
        def f(d):
            d[1].update(resources={'names': [a]})
            d.append(B.other('n', resources={'names': [b]}))
        return f
    return B.pair('m 1 and n 1 both claim x', claims('x', 'x'), claims('x', 'y'))


@row('common_checks', '<> site 0x<> is inside 0x<>-0x<>, <>: protected on the <>')
def _(B):
    lo, hi, _why = B.dev.protected[0]

    def site(at):
        return lambda d: d[1].update(sites=[B.site(at, 4)])
    return B.pair('m 1 site 0x%08x is inside 0x%08x-0x%08x' % (lo, lo, hi), site(lo), site(lo - 4))


@row('common_checks', '<> site 0x<> writes into 0x<>-0x<>, which stays stock on the <>: <>')
def _(B):
    lo, hi, _why = B.dev.keep[0]

    def site(at):
        return lambda d: d[1].update(sites=[B.site(at, 4)])
    return B.pair('m 1 site 0x%08x writes into 0x%08x-0x%08x, which stays stock on the Octatrack'
                  % (lo, lo, hi), site(lo), site(hi))


@row('common_checks', '<> site 0x<>: the stock bytes are not the ones it expects')
def _(B):
    return B.pair('m 1 site 0x%08x: the stock bytes are not the ones it expects' % B.at(0x1000),
                  lambda d: d[1]['sites'][0].update(stock_sha256=sha(b'not stock')))


@row('common_checks', '<> site 0x<>: <>')
def _(B):
    def code(n, new):
        return lambda d: d[1].update(sites=[B.site(B.at(0x1000), n, 'code', new)])
    return B.pair('m 1 site 0x%08x: ends mid-instruction' % B.at(0x1000), code(3, '4e71ff'),
                  code(4, '4e714e71'))


# ---- the copied-block rules --------------------------------------------------------------------------

def copied_block(B, site):
    at = B.at(0x2000)

    def f(d):
        d[1]['copied'] = [{'lo': '0x%08x' % at, 'hi': '0x%08x' % (at + 0x40), 'to': '0x%08x' % B.at(0x3000)}]
        d.append(B.other('n', sites=[site]))
    return f


@row('_copied_rules', "<> site 0x<>: data inside <>'s copied block")
def _(B):
    return B.pair("n 1 site 0x%08x: data inside m 1's copied block" % B.at(0x2000),
                  copied_block(B, B.site(B.at(0x2000), 4)), copied_block(B, B.site(B.at(0x2100), 4)))


@row('_copied_rules', "<> site 0x<>: <> at 0x<> is PC-relative, inside <>'s copied block")
def _(B):
    at = B.at(0x2000)
    return B.pair("is PC-relative, inside m 1's copied block",
                  copied_block(B, B.site(at, 4, 'code', '60000010')),
                  copied_block(B, B.site(at, 4, 'code', '4e714e71')))


@row('_copied_rules', '<> site 0x<>: its new bytes are not whole instructions')
def _(B):
    at = B.at(0x2000)
    return B.pair('n 1 site 0x%08x: its new bytes are not whole instructions' % at,
                  copied_block(B, B.site(at, 4, 'code', '4ef90000')),
                  copied_block(B, B.site(at, 4, 'code', '4e714e71')))


# ---- the link's own: the layout, the budgets, the relocations ---------------------------------------------

@row('link', 'no mods')
def _(B):
    return [('no mods', lambda: link.link([], B.img))], B.plain()


@row('link', 'the stock main OS is not <> <>')
def _(B):
    def other(img):
        return lambda: link.link([link.Mod2(d, d['id']) for d in B.plain()], img)
    return [('the stock main OS is not Octatrack SYN', other(B.img[:-2] + b'\x00\x00'))], B.plain()


@row('link', 'the mods do not combine:\n  <>')
def _(B):
    return B.pair('the mods do not combine:\n  m 1 imports ghost, which no given mod exports',
                  lambda d: d[1].update(imports=['core_fn', 'ghost']))


@row('link', 'the mods need RAM to 0x<>, past 0x<> (<> bytes over)<>')
def _(B):
    room = B.dev.ddr[1] - B.dev.ddr[0]
    return B.pair(', past 0x%08x (' % B.dev.ddr[1],
                  lambda d: d[1]['sections']['.bss'].update(size=room + 16),
                  lambda d: d[1]['sections']['.bss'].update(size=room - 4096))


def payload(n, fill='a5'):
    return {'.payload': {'align': 4, 'len': n, 'parts': [['hex', fill * n]]}}


def keeps_payloads(d):
    d[0]['collections']['core_payloads'] = {'entry': 12}


@row('link', '<> carr<> a payload, which <> does not keep (no table <> of <>-byte entries)')
def _(B):
    def bad(d):
        d[1]['sections'].update(payload(64))

    def good(d):
        bad(d)
        keeps_payloads(d)
    return B.pair('m carries a payload, which core 1 does not keep (no table core_payloads of 12-byte entries)',
                  bad, good)


@row('link', 'the payloads (<> bytes) and the mods need RAM to 0x<>, past 0x<> (<> bytes over): the area has '
             'room for <> bytes of payload')
def _(B):
    room = B.dev.ddr[1] - B.dev.ddr[0]

    def size(n):
        def f(d):
            keeps_payloads(d)
            d[1]['sections'].update(payload(n, '00'))
        return f
    return B.pair('the payloads (%d bytes) and the mods need RAM to ' % (room + 16), size(room + 16), size(room - 4096))


@row('link', 'the payloads of <> and <> have the same id (0x<>)')
def _(B):
    def two(second):            # "porbxw" and "puscra": the same FNV-1a hash
        def f(d):
            keeps_payloads(d)
            d[1]['sections'].update(payload(64))
            d.append(B.other(second, sections=dict(B.other(second)['sections'], **payload(32))))
            d[1]['id'] = 'porbxw'
            d[1]['symbols'] = {'fn_porbxw': ['.run', 0]}
            d[1]['contribute'][0]['relocs'][0][2] = 'sym:fn_porbxw'
            d[1]['sites'][0]['relocs'][0][2] = 'sym:fn_porbxw'
        return f
    return B.pair('the payloads of porbxw and puscra have the same id (0x', two('puscra'), two('puscrb'))


@row('link', '.fast code needs SRAM to 0x<>, past 0x<>', device='digitakt-mk1')
def _(B):
    room = B.dev.sram_code[1] - B.dev.sram_code[0]

    def fast(n):
        def f(d):
            d[0]['collections']['fa_copies'] = {'entry': 16}
            d[1]['sections']['.fast'] = {'align': 4, 'len': n, 'parts': [['hex', '4e71' * (n // 2)]]}
        return f
    return B.pair('.fast code needs SRAM to 0x%08x, past 0x%08x' % (B.dev.sram_code[0] + room + 4,
                                                                       B.dev.sram_code[1]),
                  fast(room + 4), fast(room))


external('link', 'the DSP code for payload <> needs P words to 0x<>, past the <> words <> frees (<> over)',
         'tests/test_octatrack.py', 'the DSP code for payload A needs P words to 0x00bd9')


@row('link.resolve', '<>: <> refers to its empty section <>')
def _(B):
    return B.pair('m 1: .run+0x0 refers to its empty section .fast',
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'sec:.fast', 0]]),
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'sec:.run', 0]]))


@row('link.resolve', '<>: <> refers to <>, which nothing defines')
def _(B):
    return B.pair('m 1: .run+0x0 refers to ghost, which nothing defines',
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'sym:ghost', 0]]),
                  lambda d: d[1].update(relocs=[['.run', 0, 'abs32', 'sym:fn_m', 0]]))


external('link.place', '<>: <> is a <> reference to a <> address', 'tests/test_octatrack.py',
         ('is a DSP reference to a ', 'is a ColdFire reference to a DSP address'))
external('link.put', '<>: a DSP address out of range (0x<>)', 'tests/test_octatrack.py',
         'a DSP address out of range (0x1000000)')


@row('link.put', '<>: a 16-bit PC-relative reference spans <> bytes')
def _(B):
    def far(at):
        def f(d):
            d[1]['sections']['.run'] = {'align': 4, 'len': 40000, 'parts': [['hex', '4e71' * 20000]]}
            d[1]['symbols']['far'] = ['.run', at]
            d[1]['relocs'] = [['.run', 0, 'pc16', 'sym:far', 0]]
        return f
    return B.pair('.run+0x0: a 16-bit PC-relative reference spans 39996 bytes', far(39996), far(30000))


@row('link', '<> site 0x<> after relocation: <>')
def _(B):
    def new(hexbytes):
        return lambda d: d[1].update(sites=[B.site(B.at(0x1000), 4, 'code', hexbytes)])
    return B.pair('m 1 site 0x%08x after relocation: 0x%08x does not decode' % (B.at(0x1000), B.at(0x1000)),
                  new('ffffffff'), new('4e714e71'))


@row('link', 'the linked mods break a rule:\n  <>')
def _(B):
    def new(hexbytes):
        return lambda d: d[1].update(sites=[B.site(B.at(0x1000), 4, 'code', hexbytes)])
    return B.pair('the linked mods break a rule:\n  m 1 site', new('ffffffff'), new('4e714e71'))


# ---- mod storage (the Analog Rytm MKII keeps it) ----------------------------------------------------------

def keeper(B, ids=True):
    c = B.core(imports=['__store_gen', '__store_gen2'])
    if ids:
        c['collections']['core_store_ids'] = {'entry': 4}
    return c


def storing(B, sym='st_a', ident='m'):
    return B.other(ident, storage=[{'sym': sym, 'scope': 'project', 'bits': 4}])


@row('link', '<> declare<> mod storage, which <> does not keep', device='analog-rytm-mk2')
def _(B):
    return [('m declares mod storage, which core 1 does not keep', [B.core(), storing(B)])], \
        [keeper(B), storing(B)]


@row('link', '<> declare<> lockable parameters, which <> does not keep (no table <>)', device='analog-rytm-mk2')
def _(B):
    locking = B.other('m', storage=[{'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'lock': True}])
    codes = keeper(B)
    codes['collections']['core_lock_codes'] = {'entry': 8}
    return [('m declares lockable parameters, which core 1 does not keep (no table core_lock_codes)',
             [keeper(B), locking])], [codes, locking]


@row('link', "<> declare<> lockable kit fields (the FX track's), which <> does not keep", device='analog-rytm-mk2')
def _(B):
    # a lockable kit field needs a core that keeps the FX track's parameters (it imports core_fx_lfo_dst)
    kit = B.other('m', storage=[{'sym': 'st_k', 'scope': 'kit', 'bits': 4, 'lock': True}])
    codes = keeper(B)
    codes['collections']['core_lock_codes'] = {'entry': 8}
    fx = keeper(B)
    fx['imports'] = fx['imports'] + ['core_fx_lfo_dst']
    fx['collections']['core_lock_codes'] = {'entry': 8}
    return [("m declares lockable kit fields (the FX track's), which core 1 does not keep", [codes, kit])], \
        [fx, kit]


@row('link', '<> give<> lockable parameters CCs, which <> does not keep (no table <>)', device='analog-rytm-mk2')
def _(B):
    cc = B.other('m', storage=[{'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'lock': True, 'cc': True}])
    codes = keeper(B)
    codes['collections']['core_lock_codes'] = {'entry': 8}
    both = keeper(B)
    both['collections'].update({'core_lock_codes': {'entry': 8}, 'core_lock_ccs': {'entry': 8}})
    return [('m gives lockable parameters CCs, which core 1 does not keep (no table core_lock_ccs)',
             [codes, cc])], [both, cc]


@row('link', '<> give<> storage fields an init, which <> does not keep (no table <>)', device='analog-rytm-mk2')
def _(B):
    inited = B.other('m', storage=[{'sym': 'st_a', 'scope': 'kit', 'bits': 7, 'init': 100}])
    inits = keeper(B)
    inits['collections']['core_field_inits'] = {'entry': 4}
    return [('m gives storage fields an init, which core 1 does not keep (no table core_field_inits)',
             [keeper(B), inited])], [inits, inited]


@row('link', '<> mark<> storage fields as handles, which <> does not keep (no table <>)', device='analog-rytm-mk2')
def _(B):
    handled = B.other('m', storage=[{'sym': 'st_h', 'scope': 'project', 'bits': 16, 'handle': True}])
    handles = keeper(B)
    handles['collections']['core_handles'] = {'entry': 4}
    return [('m marks storage fields as handles, which core 1 does not keep (no table core_handles)',
             [keeper(B), handled])], [handles, handled]


@row('link', '<> imports <> but declares no table <>', device='analog-rytm-mk2')
def _(B):
    # check() refuses a core that imports the generation symbols without the table, unless a mod
    # exports them (which the next fixture refuses): this is the linker's own account of it
    thief = B.other('n', symbols={'__store_gen': ['.run', 0], '__store_gen2': ['.run', 4]},
                    exports=['__store_gen', '__store_gen2'])
    return [('core 1 imports __store_gen but declares no table core_store_ids',
             [keeper(B, ids=False), storing(B), thief])], [keeper(B), storing(B)]


@row('check', '<> and <> both declare the storage field <>', device='analog-rytm-mk2')
def _(B):
    return [('m 1 and n 1 both declare the storage field st_a',
             [keeper(B), storing(B), storing(B, ident='n')])], \
        [keeper(B), storing(B), storing(B, 'st_b', 'n')]


@row('check', '<>: the storage field <> has the name of a symbol or table', device='analog-rytm-mk2')
def _(B):
    clash = storing(B, sym='fn_m')
    clash.update(exports=['fn_m'])
    return [('m 1: the storage field fn_m has the name of a symbol or table', [keeper(B), clash])], \
        [keeper(B), storing(B)]


@row('link', 'the storage field <> has the name of a symbol a mod exports', device='analog-rytm-mk2')
def _(B):
    thief = B.other('n', symbols={'__store_gen2': ['.run', 0]}, exports=['__store_gen2'])
    return [('the storage field __store_gen2 has the name of a symbol a mod exports',
             [keeper(B), storing(B), thief])], [keeper(B), storing(B)]


# ---- knob ids (the Analog Rytm MKII's core_params and core_param_locks, the profile's param_ids) ----------

def knobs(B):
    c = B.core()
    c['collections'].update({'core_params': {'entry': 4}, 'core_param_locks': {'entry': 4}})
    return c


def answering(B, first, last, table='core_params', ident='m'):
    """A mod whose one entry in `table` answers for ids first..last."""
    d = B.other(ident)
    d['sections']['.run'] = {'align': 4, 'len': 8, 'parts': [['hex', '%08x%08x' % (first, last)]]}
    d['symbols'] = {'e_' + ident: ['.run', 0]}
    d['contribute'] = [{'to': table, 'data': '00000000', 'relocs': [[0, 'abs32', 'sym:e_' + ident, 0]]}]
    return d


@row('id_problems', "<>: its <> entry takes ids <>..<>, and <>..<> are <>; a mod's own ids start at <>",
     device='analog-rytm-mk2')
def _(B):
    return [("m 1: its core_params entry takes ids 470..483, and 469..554 are core's lockable "
             "parameters' ids; a mod's own ids start at 555", [knobs(B), answering(B, 470, 483)])], \
        [knobs(B), answering(B, 560, 573)]


@row('id_problems', "<>: its <> entry takes ids <>..<>; that table's ids start at <>", device='analog-rytm-mk2')
def _(B):
    return [("m 1: its core_param_locks entry takes ids 1..4; that table's ids start at 555",
             [knobs(B), answering(B, 1, 4, 'core_param_locks')])], \
        [knobs(B), answering(B, 1, 4)]


@row('id_problems', '<> and <> both take ids <>..<> (<>, <>)', device='analog-rytm-mk2')
def _(B):
    return [('m 1 and n 1 both take ids 570..573 (core_params, core_param_locks)',
             [knobs(B), answering(B, 560, 573), answering(B, 570, 580, 'core_param_locks', 'n')])], \
        [knobs(B), both(B, 560, 573), answering(B, 574, 580, 'core_param_locks', 'n')]


def both(B, first, last):
    """One mod's core_params and core_param_locks entries for the same ids: allowed."""
    d = answering(B, first, last)
    d['contribute'].append(dict(d['contribute'][0], to='core_param_locks'))
    return d


def knobbed(B, count=4, ident='k', add=(0, 3), typ='abs32', short_add=1):
    """A mod with "knobs": count, whose core_param_locks entry answers for __knobs+add[0]..+add[1]
    and whose .run holds a 16-bit knob list entry, __knobs+short_add, at +8."""
    d = B.other(ident)
    d['knobs'] = count
    d['sections']['.run'] = {'align': 4, 'len': 12, 'parts': [['hex', '00' * 12]]}
    d['symbols'] = {'e_' + ident: ['.run', 0]}
    d['imports'] = ['core_fn', '__knobs']
    d['relocs'] = [['.run', 0, typ, 'sym:__knobs', add[0]], ['.run', 4, 'abs32', 'sym:__knobs', add[1]],
                   ['.run', 8, 'abs16', 'sym:__knobs', short_add]]
    d['contribute'] = [{'to': 'core_param_locks', 'data': '00000000',
                        'relocs': [[0, 'abs32', 'sym:e_' + ident, 0]]}]
    return d


@row('Mod2.__init__', '<>: "knobs" is a count of knob ids, 0..<>', device='analog-rytm-mk2')
def _(B):
    def count(n):
        d = knobbed(B, 4)
        d['knobs'] = n
        return [knobs(B), d]
    return [('k: "knobs" is a count of knob ids, 0..1024', count(1025)),
            ('k: "knobs" is a count of knob ids, 0..1024', count('4'))], count(1024)


@row('Mod2.__init__', "<>: <> is the linker's (its first knob id), not a symbol of a mod", device='analog-rytm-mk2')
def _(B):
    bad = knobbed(B)
    bad['symbols']['__knobs'] = ['abs', 600]
    return [("k: __knobs is the linker's (its first knob id), not a symbol of a mod", [knobs(B), bad])], \
        [knobs(B), knobbed(B)]


@row('check', '<> asks for knob ids ("knobs"), which the <> does not give mods')
def _(B):
    return B.pair('m 1 asks for knob ids ("knobs"), which the Octatrack does not give mods',
                  lambda d: d[1].update(knobs=2), lambda d: d[1].update(knobs=0))


@row('check', '<>: <> is a <> reference to <>+<>; one takes abs32 or abs16 with an addend below its "knobs" (<>)',
     device='analog-rytm-mk2')
def _(B):
    return [('k 1: .run+0x0 is a abs32 reference to __knobs+4; one takes abs32 or abs16 with an addend below '
             'its "knobs" (4)', [knobs(B), knobbed(B, 4, add=(4, 3))]),
            ('k 1: .run+0x0 is a pc32 reference to __knobs+0; one takes abs32 or abs16 with an addend below '
             'its "knobs" (4)', [knobs(B), knobbed(B, 4, typ='pc32')]),
            ('k 1: .run+0x8 is a abs16 reference to __knobs+-1; one takes abs32 or abs16 with an addend below '
             'its "knobs" (4)', [knobs(B), knobbed(B, 4, short_add=-1)])], \
        [knobs(B), knobbed(B, 5, add=(4, 4))]


@row('link.put', '<>: a 16-bit value out of range (<>)', device='analog-rytm-mk2')
def _(B):
    def short(tgt, add):
        d = knobbed(B)
        d['relocs'][2] = ['.run', 8, 'abs16', tgt, add]
        return [knobs(B), d]
    return [('.run+0x8: a 16-bit value out of range (%d)' % (0x47be0000), short('abs', 0x47be0000))], \
        short('abs', 0xFFFF)


# ---- the finished image against the stock one (link.image_problems) -------------------------------------------

def stray(B, at, n=1, spacing=1):
    """A copy of the stock image with `n` bytes inverted from `at`, `spacing` apart, and a tail."""
    img = bytearray(B.img)
    for i in range(n):
        img[at - B.dev.main_load + spacing * i] ^= 0xFF
    return bytes(img) + b'tail'


def refusal(B, img, declared=(), tail_at=None):
    """A function that raises image_problems' lines as one ModError, or returns."""
    def f():
        end = B.dev.main_load + len(B.img)
        bad = link.image_problems(B.dev, B.img, img, list(declared), end if tail_at is None else tail_at)
        if bad:
            raise ModError('\n'.join(bad))
    return f


@row('image_problems', 'the appended code starts at 0x<>, not at the end of the stock image, 0x<>')
def _(B):
    end = B.dev.main_load + len(B.img)
    return [('the appended code starts at 0x%08x, not at the end of the stock image, 0x%08x'
             % (end + 4, end), refusal(B, B.img + b'tail', tail_at=end + 4))], \
        refusal(B, B.img + b'tail', tail_at=end)


@row('image_problems', 'the linked image (<> bytes) is shorter than the stock main OS (<> bytes)')
def _(B):
    n = len(B.img)
    return [('the linked image (%d bytes) is shorter than the stock main OS (%d bytes)' % (n - 4, n),
             refusal(B, B.img[:-4]))], refusal(B, B.img + b'x')


@row('image_problems', '<> lies outside the stock image')
def _(B):
    end = B.dev.main_load + len(B.img)
    return [('a site lies outside the stock image', refusal(B, B.img, [(end - 8, end + 4, 'a site')]))], \
        refusal(B, B.img, [(end - 8, end, 'a site')])


@row('image_problems', '0x<>-0x<> differs from stock, and no site, DSP section or table of the mods declares it')
def _(B):
    at = B.at(0x4000)
    tail = ', and no site, DSP section or table of the mods declares it'
    return [('0x%08x-0x%08x differs from stock%s' % (at, at + 6, tail), refusal(B, stray(B, at, 6))),
            ('0x%08x-0x%08x differs from stock%s' % (at + 4, at + 6, tail),
             refusal(B, stray(B, at, 6), [(at, at + 4, 'a site')]))], \
        refusal(B, stray(B, at, 6), [(at - 2, at + 8, 'a site')])


@row('image_problems', '<> more runs of bytes differ from stock outside every declaration')
def _(B):
    return [('2 more runs of bytes differ from stock outside every declaration',
             refusal(B, stray(B, B.at(0x4000), 10, 16)))], refusal(B, B.img)


@row('image_problems', '0x<>-0x<> (<>) is not stock in the linked image')
def _(B):
    plo, phi, pwhy = B.dev.protected[0]
    klo, khi, kwhy = B.dev.keep[0]
    # the bytes are declared, as a site's would be, and still may not be a protected or kept range's
    return [('0x%08x-0x%08x (%s) is not stock in the linked image' % (plo, phi, pwhy),
             refusal(B, stray(B, plo + 8, 2), [(plo + 8, plo + 10, 'a site')])),
            ('0x%08x-0x%08x (%s) is not stock in the linked image' % (klo, khi, kwhy),
             refusal(B, stray(B, klo + 4, 2), [(klo + 4, klo + 6, 'a site')]))], \
        refusal(B, stray(B, plo - 16, 2), [(plo - 16, plo - 14, 'a site')])


# ---- the fixtures run ----------------------------------------------------------------------------------------

def run_rows(device_key):
    n, failed = 0, []
    with Bench(device_key) as B:
        for (fn, template), (key, make) in sorted(ROWS.items()):
            if key != device_key:
                continue
            cases, good = make(B)
            assert cases, (fn, template)
            for expect, bad in cases:
                got = B.run(bad)
                if got is None:
                    failed.append('%s %r: %r was accepted' % (fn, template, expect))
                elif expect not in got:
                    failed.append('%s %r: expected %r in:\n    %s' % (fn, template, expect, got))
            got = B.run(good)
            if got is not None:
                failed.append('%s %r: the must-pass fixture was refused:\n    %s' % (fn, template, got))
            n += 1
    assert not failed, '\n'.join(failed)
    return n


def test_octatrack_fixtures():
    assert run_rows('octatrack') > 40


def test_digitakt_fixtures():
    assert run_rows('digitakt-mk1') == 3


def test_analog_rytm_fixtures():
    assert run_rows('analog-rytm-mk2') == 17


def test_the_lfo_destination_field_is_placed_only_with_an_lfo_field():
    """core_lfo_dst (core-ar2 0.8): a core that imports it gets a 7-bit sound field of its own in a
    build with a field declared "lfo": true, and the symbol 0 (no field) in a build whose fields are
    only lockable; an "lfo" field's code carries LFO_FLAG in core_lock_codes, a lockable one's not.
    "lfo" without "lock" is refused."""
    with Bench('analog-rytm-mk2') as B:
        core = keeper(B)
        core['imports'] = core['imports'] + ['core_lfo_dst']
        core['collections']['core_lock_codes'] = {'entry': 8}
        for lfo in (True, False):
            mod = B.other('m', storage=[{'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'lock': True, 'lfo': lfo},
                                        {'sym': 'st_b', 'scope': 'sound', 'bits': 4, 'lock': True}])
            out = link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
            placed = [f for f in out.store['fields'] if f['sym'] == 'core_lfo_dst']
            codes = {(c['mod'], c['sym']): c['code'] for c in out.store['codes']}
            entries = [int.from_bytes(e[4:], 'big') for e in store.code_entries(out.store, [('m', 'st_a')] if lfo else [])]
            if lfo:
                assert placed == [{'mod': 'core', 'sym': 'core_lfo_dst', 'scope': 'sound',
                                   'offset': 0, 'bits': 7}], out.store['fields']
                assert sorted(entries) == sorted([codes[('m', 'st_a')] | store.LFO_FLAG, codes[('m', 'st_b')]])
            else:
                assert placed == [], out.store['fields']
                assert sorted(entries) == sorted(codes.values())
        mod = B.other('m', storage=[{'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'lfo': True}])
        try:
            link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
        except ModError as e:
            assert 'only a lockable field ("lock": true) can be an LFO destination' in str(e), e
        else:
            raise AssertionError('"lfo" without "lock" was accepted')


def test_a_field_declared_smooth_false_gets_smooth_off_in_its_code():
    """"smooth": false (core-ar2 0.9): the field's code carries SMOOTH_OFF in core_lock_codes, beside
    LFO_FLAG; "smooth": true and no "smooth" leave it out. The flag is not stored data, so the layout is
    the same either way. "smooth" without "lock" is refused."""
    with Bench('analog-rytm-mk2') as B:
        core = keeper(B)
        core['collections']['core_lock_codes'] = {'entry': 8}
        layouts = set()
        for smooth in (False, True, None):
            a = {'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'lock': True, 'lfo': True}
            if smooth is not None:
                a['smooth'] = smooth
            mod = B.other('m', storage=[a, {'sym': 'st_b', 'scope': 'sound', 'bits': 4, 'lock': True}])
            m = link.Mod2(mod, 'm')
            out = link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
            codes = {(c['mod'], c['sym']): c['code'] for c in out.store['codes']}
            entries = sorted(int.from_bytes(e[4:], 'big')
                             for e in store.code_entries(out.store, [('m', 'st_a')], [('m', x) for x in m.rough]))
            want = codes[('m', 'st_a')] | store.LFO_FLAG | (store.SMOOTH_OFF if smooth is False else 0)
            assert entries == sorted([want, codes[('m', 'st_b')]]), (smooth, entries)
            layouts.add(store.layout_id(out.store))
        assert len(layouts) == 1, layouts
        mod = B.other('m', storage=[{'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'smooth': False}])
        try:
            link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
        except ModError as e:
            assert 'only a lockable field ("lock": true) is smoothed' in str(e), e
        else:
            raise AssertionError('"smooth" without "lock" was accepted')


def test_a_field_declared_mod_gets_route_flag_in_its_code():
    """"mod": true (core-ar2 0.9): the field is a velocity and aftertouch destination, and its code
    carries ROUTE_FLAG in core_lock_codes, beside LFO_FLAG; "mod": false and no "mod" leave it out. The
    flag is not stored data, so the layout is the same either way. "mod" without "lock" is refused."""
    with Bench('analog-rytm-mk2') as B:
        core = keeper(B)
        core['collections']['core_lock_codes'] = {'entry': 8}
        layouts = set()
        for routed in (True, False, None):
            a = {'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'lock': True, 'lfo': True}
            if routed is not None:
                a['mod'] = routed
            mod = B.other('m', storage=[a, {'sym': 'st_b', 'scope': 'sound', 'bits': 4, 'lock': True}])
            m = link.Mod2(mod, 'm')
            out = link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
            codes = {(c['mod'], c['sym']): c['code'] for c in out.store['codes']}
            entries = sorted(int.from_bytes(e[4:], 'big')
                             for e in store.code_entries(out.store, [('m', 'st_a')], (), [('m', x) for x in m.routes]))
            want = codes[('m', 'st_a')] | store.LFO_FLAG | (store.ROUTE_FLAG if routed else 0)
            assert entries == sorted([want, codes[('m', 'st_b')]]), (routed, entries)
            layouts.add(store.layout_id(out.store))
        assert len(layouts) == 1, layouts
        mod = B.other('m', storage=[{'sym': 'st_a', 'scope': 'sound', 'bits': 4, 'mod': True}])
        try:
            link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
        except ModError as e:
            assert 'only a lockable field ("lock": true) can be a velocity or aftertouch destination' in str(e), e
        else:
            raise AssertionError('"mod" without "lock" was accepted')


def test_a_lockable_kit_field_is_the_fx_tracks():
    """A lockable kit field (core-ar2 0.9) is the FX track's: with "lfo" the core's kit field
    core_fx_lfo_dst is placed (and core_lfo_dst is not, with no sound "lfo" field); its CC is never one
    stock uses on the FX channel, and a lockfile that gives it one is refused; "mod" is refused (the FX
    track has no velocity or aftertouch routes)."""
    with Bench('analog-rytm-mk2') as B:
        core = keeper(B)
        core['imports'] = core['imports'] + ['core_fx_lfo_dst', 'core_lfo_dst']
        core['collections'].update({'core_lock_codes': {'entry': 8}, 'core_lock_ccs': {'entry': 8}})
        k = {'sym': 'st_k', 'scope': 'kit', 'bits': 7, 'lock': True, 'cc': True, 'lfo': True}
        # 23 sound fields with CCs take the pool up to 69, so 84 and 85 come next: the kit field skips them
        sounds = [{'sym': 'st_%d' % i, 'scope': 'sound', 'bits': 1, 'lock': True, 'cc': True} for i in range(23)]
        mod = B.other('m', storage=sounds + [k])
        out = link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
        placed = {(f['mod'], f['sym']) for f in out.store['fields']}
        assert ('core', 'core_fx_lfo_dst') in placed and ('core', 'core_lfo_dst') not in placed, placed
        taken = store._spec('analog-rytm-mk2')['fx_ccs_taken']
        cc = {(c['mod'], c['sym']): c['cc'] for c in out.store['ccs']}[('m', 'st_k')]
        assert cc not in taken and cc == 110, cc
        bad = copy.deepcopy(out.store)
        for c in bad['ccs']:
            if (c['mod'], c['sym']) == ('m', 'st_k'):
                c['cc'] = taken[0]
        try:
            store.check_lock(bad, 'analog-rytm-mk2')
        except ModError as e:
            assert 'a kit field with a CC stock uses on the FX channel' in str(e), e
        else:
            raise AssertionError('a kit field with CC %d was accepted' % taken[0])
        mod = B.other('m', storage=[dict(k, mod=True)])
        try:
            link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img)
        except ModError as e:
            assert 'cannot be a velocity or aftertouch destination: the FX track has no routes' in str(e), e
        else:
            raise AssertionError('"mod" on a kit field was accepted')


def test_a_field_with_an_init_gets_its_slot_and_a_new_place_when_the_init_changes():
    """"init" (core-ar2 0.10): a field with an init has its slot in core_field_inits in its
    descriptor's bits 16..21 and its init in the table, by slot; fields without one keep the
    descriptor they had. The lockfile keeps the init ("init", format 2), and a field whose init
    changes is a new field: new bits, a new layout ID, and a note that its data is dropped. An init
    the field's bits cannot hold, or more than 63 of them, is refused."""
    with Bench('analog-rytm-mk2') as B:
        core = keeper(B)
        core['collections'].update({'core_lock_codes': {'entry': 8}, 'core_field_inits': {'entry': 4}})

        def build(storage, lock=None):
            mod = B.other('m', storage=storage)
            return link.link([link.Mod2(d, d['id']) for d in (copy.deepcopy(core), mod)], B.img, lock=lock)
        plain = [{'sym': 'st_p', 'scope': 'sound', 'bits': 3}]
        a = {'sym': 'st_a', 'scope': 'sound', 'bits': 7, 'lock': True, 'init': 100}
        k = {'sym': 'st_k', 'scope': 'kit', 'bits': 7, 'init': 127}
        before = build(plain).store
        out = build(plain + [a, k], lock=before)
        assert (out.store['elekloader_store'], before['elekloader_store']) == (2, 1), (out.store, before)
        fields = {f['sym']: f for f in out.store['fields']}
        assert fields['st_a']['init'] == 100 and fields['st_k']['init'] == 127 and 'init' not in fields['st_p']
        desc = store.descriptors(out.store)
        assert desc[('m', 'st_a')] >> 16 & 63 == 1 and desc[('m', 'st_k')] >> 16 & 63 == 2, desc
        assert desc[('m', 'st_p')] == store.descriptors(before)[('m', 'st_p')]
        assert store.init_entries(out.store) == [(100).to_bytes(4, 'big'), (127).to_bytes(4, 'big')]
        assert any(int.from_bytes(e[:4], 'big') == desc[('m', 'st_a')] for e in store.code_entries(out.store))
        again, notes = store.allocate(out.store, [('m', 'st_p', 'sound', 3), ('m', 'st_a', 'sound', 7),
                                                  ('m', 'st_k', 'kit', 7)], 'analog-rytm-mk2', None,
                                      [('m', 'st_a')], (), {('m', 'st_a'): 100, ('m', 'st_k'): 127})
        assert again['id'] == out.store['id'], notes
        moved = build(plain + [dict(a, init=64), k], lock=out.store).store
        f = {g['sym']: g for g in moved['fields']}['st_a']
        assert f['init'] == 64 and f['offset'] != fields['st_a']['offset'], f
        assert moved['id'] != out.store['id'] and out.store['id'] in moved['ancestors']
        assert any(g['sym'] == 'st_a' and g['init'] == 100 for g in moved['freed']), moved['freed']
        _l, notes = store.allocate(out.store, [('m', 'st_p', 'sound', 3), ('m', 'st_a', 'sound', 7),
                                               ('m', 'st_k', 'kit', 7)], 'analog-rytm-mk2', None,
                                   [('m', 'st_a')], (), {('m', 'st_a'): 64, ('m', 'st_k'): 127})
        assert any('st_a (sound): changed its init' in n for n in notes), notes
        for bad, said in (([dict(k, init=128)], 'storage field st_k: init 128 (0 to 127'),
                          ([dict(k, init=-1)], 'storage field st_k: init -1'),
                          ([{'sym': 'g%d' % i, 'scope': 'project', 'bits': 1, 'init': 1} for i in range(64)],
                           'the mods give 64 storage fields an init; the core keeps at most 63')):
            try:
                build(bad)
            except ModError as e:
                assert said in str(e), e
            else:
                raise AssertionError('accepted: %s' % said)
        assert build([{'sym': 'g%d' % i, 'scope': 'project', 'bits': 1, 'init': 1} for i in range(63)])
        assert build([dict(k, init=0)]).store['elekloader_store'] == 1   # init 0 is no init


def test_the_image_check_refuses_a_protected_or_kept_byte_the_other_checks_let_through():
    """The checks that refuse a write to a protected or kept range run before the link. With them
    made to say nothing (as a bug in them would), the image check refuses the same site, so it
    cannot reach the file."""
    with Bench('octatrack') as B:
        plo = B.dev.protected[0][0]
        klo, khi, _w = B.dev.keep[0]
        for at in (plo + 8, klo, khi - 4):
            docs = B.plain()
            docs[1]['sites'] = [B.site(at, 4)]
            assert B.run(docs) is not None
            real_check, link.check = link.check, (lambda mods, image: [])
            try:
                got = B.run(docs)
            finally:
                link.check = real_check
            assert got is not None and 'is not stock in the linked image' in got, (hex(at), got)
        docs = B.plain()
        docs[1]['sites'] = [B.site(klo - 4, 4)]
        assert B.run(docs) is None


# ---- the list -----------------------------------------------------------------------------------------------

LOADER = {'_int', '_hex', 'resolve_target', 'parse_parts', 'parse_sites', 'parse_resources',
          'parse_needs_core', 'read_json', 'common_checks'}
FORMAT1 = {'Mod.__init__', 'check', 'blob_bytes', 'apply'}   # whole builds (format 1): test_patcher, test_octatrack


def _template(node):
    """A message's text with each field blanked, as the syntax gives it."""
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mod):
        return re.sub(r'%[-#0 +]*[0-9.]*[sdrxXa]', '<>', _template(node.left))
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return _template(node.left) + _template(node.right)
    if isinstance(node, ast.JoinedStr):
        return ''.join(_template(v) if isinstance(v, ast.Constant) else '<>' for v in node.values)
    if isinstance(node, ast.IfExp):
        return _template(node.body) + '|' + _template(node.orelse)
    return '<>'


def messages(path):
    """-> [(function, template)] for every ModError(...) and bad.append/insert(...) in a file."""
    out, stack = [], []

    class V(ast.NodeVisitor):
        def visit_FunctionDef(self, n):
            stack.append(n.name)
            self.generic_visit(n)
            stack.pop()

        def visit_ClassDef(self, n):
            stack.append(n.name)
            self.generic_visit(n)
            stack.pop()

        def visit_Call(self, n):
            f = n.func
            name = f.id if isinstance(f, ast.Name) else (f.attr if isinstance(f, ast.Attribute) else '')
            owner = f.value.id if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) else ''
            if (name == 'ModError' or (owner == 'bad' and name in ('append', 'insert'))) and n.args:
                arg = n.args[-1] if name == 'insert' else n.args[0]
                out.append(('.'.join(stack), _template(arg)))
            self.generic_visit(n)
    with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
        V().visit(ast.parse(fh.read()))
    return out


def test_knob_ids_are_given_from_555_past_fixed_ids_and_written_as_abs32_and_abs16():
    import struct
    with Bench('analog-rytm-mk2') as B:
        fixed = answering(B, 555, 560, 'core_param_locks', 'f')     # a mod with fixed ids 555..560
        mods = [knobs(B), fixed, knobbed(B, 4, 'k', (0, 3), short_add=2), knobbed(B, 2, 'j', (0, 1))]
        out = link.link([link.Mod2(d, d['id']) for d in mods], B.img)
        assert out.knobs == {'j': [561, 2], 'k': [563, 4]}         # by mod id, past 555..560
        assert out.knob_notes == ['j: knob ids 561..562', 'k: knob ids 563..566']
        assert out.map['k:__knobs'] == 563 and out.store is None
        run = out.layout['sections']['k .run'] - out.layout['ddr'][0]
        at = out.layout['run_load'] - B.dev.main_load + run
        assert struct.unpack('>IIH', out.image[at:at + 10]) == (563, 566, 565)
        # the linked mods' ids are checked again: a run on another mod's ids is refused
        k = link.Mod2(mods[2], 'k')
        assert link.id_problems([k, link.Mod2(fixed, 'f')], B.img, {'k': 558}) == \
            ['k 1 and f 1 both take ids 558..560 (core_param_locks, core_param_locks)']


def test_a_core_with_a_lockfile_keeps_the_knob_ids_from_build_to_build():
    with Bench('analog-rytm-mk2') as B:
        core = keeper(B)
        core['collections'].update(knobs(B)['collections'])

        def build(lock, *mods):
            return link.link([link.Mod2(copy.deepcopy(d), d['id']) for d in (core,) + mods], B.img, lock)
        one = build(None, knobbed(B, 4, 'k'), knobbed(B, 2, 'j', (0, 1)))
        assert one.knobs == {'j': [555, 2], 'k': [557, 4]}
        assert one.store['knobs'] == [{'mod': 'j', 'first': 555, 'count': 2}, {'mod': 'k', 'first': 557, 'count': 4}]
        two = build(one.store, knobbed(B, 4, 'k'), knobbed(B, 3, 'a', (0, 2)))      # j out, a in: k keeps its ids
        assert two.knobs == {'a': [561, 3], 'k': [557, 4]}
        assert two.store['id'] == one.store['id'] and store.from_image(two.image)['knobs'] == two.store['knobs']
        three = build(two.store, knobbed(B, 4, 'k'), knobbed(B, 2, 'j', (0, 1)), knobbed(B, 3, 'a', (0, 2)))
        assert three.knobs == {'a': [561, 3], 'j': [555, 2], 'k': [557, 4]}
        assert three.knob_notes == ['j: back with knob ids 555..556']


def test_every_refusal_has_a_fixture():
    found = set(messages('elekloader/link.py'))
    loader = [(fn, t) for fn, t in messages('elekloader/elemod.py')]
    found |= {(fn, t) for fn, t in loader if fn in LOADER}
    unknown = {fn for fn, _t in loader} - LOADER - FORMAT1
    assert not unknown, 'a refusal in %s: add it to LOADER, or to FORMAT1 if it is a whole build\'s' % sorted(unknown)
    named = set(ROWS) | set(EXTERNAL)
    assert not found - named, 'no fixtures for:\n  %s' % '\n  '.join(map(repr, sorted(found - named)))
    assert not named - found, 'fixtures for a message that is gone:\n  %s' % '\n  '.join(map(repr, sorted(named - found)))
    for key, (path, text) in EXTERNAL.items():
        with open(os.path.join(ROOT, path), encoding='utf-8') as fh:
            held = fh.read()
        for t in ([text] if isinstance(text, str) else text):
            assert t in held, '%s no longer holds %r (%r)' % (path, t, key)


if __name__ == '__main__':
    import traceback
    ok = failed = 0
    for n in sorted(k for k in globals() if k.startswith('test_')):
        try:
            globals()[n]()
            print('ok      %s' % n)
            ok += 1
        except Exception:
            print('FAIL    %s' % n)
            traceback.print_exc()
            failed += 1
    print('%d passed, %d failed' % (ok, failed))
    sys.exit(1 if failed else 0)
