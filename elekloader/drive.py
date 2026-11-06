# SPDX-License-Identifier: GPL-2.0-or-later
"""+Drive mods on the host: the build-checker and the boot linker's twin (docs/FORMAT.md,
"+Drive mods").

A flashed build that carries the Drive Loader links more mods at boot, from `/mods` on the unit's
+Drive, into the +Drive area its build descriptor (desc.py) names. This module does on the computer
what that boot linker (mods/drive-loader-ar2/dlink.c) does, rule for rule:

- `check(build, files)` is pass 1, the check rounds and the layout: the build-checker
  (`elekloader check`), and what `elekloader mods put` runs before an upload.
- `link(build, files)` goes on with pass 2 (relocation) and the merge of each table's flashed and
  +Drive entries. Its result (the area's bytes, each rewritten table and the status records) is
  what the unit's RAM holds after boot, byte for byte (`elekloader link --device-layout`).

Layout alignment, the id order, the entry sort key and the relocation writer are link.py's own
functions, so this and the flashed linker cannot drift. Nothing here reads a clock or iterates in
hash order: the same inputs give the same bytes anywhere.
"""
import json
import os
import struct
import sys
import zlib

from . import container, desc, devices, link as linker
from .elemod import ModError, format_of, version_key
from .link import _align, by_id, entry_key, lay_out, put_reloc

MAX_MODS = 32                  # lines of /mods/active, and status records
ID_MAX = 28                    # a status record's id field (27 bytes and a NUL)
DETAIL = 32                    # a detail string (31 bytes and a NUL)
NAME_MAX = 64                  # a file name in /mods/active (63 bytes)
RECORD = 32                    # a status record's bytes
# the boot linker's own room (dlink.c): past it a mod, or the whole set, is refused with LIMIT
SYMS, CONTRIBS, EDATA, MERGE, MDATA = 1536, 384, 4096, 256, 4096
ARENA, LISTS, LOCALS, ERELOCS = 12288, 1536, 1024, 384
INIT = 'drive_init'            # each mod's own init: void drive_init(void); never a global name

# states and refusal codes (docs/FORMAT.md, "+Drive refusal codes"): the boot linker's numbers
NONE, LOADED, REFUSED, DROPPED = 0, 1, 2, 3
STATES = {NONE: 'none', LOADED: 'loaded', REFUSED: 'refused', DROPPED: 'dropped'}
CODES = {
    0: ('ok', 'loaded'),
    1: ('missing', 'the file is not in /mods'),
    2: ('changed', 'the file is not the one chosen (its CRC-32 differs)'),
    3: ('too-big', 'the file is larger than the staging buffer'),
    4: ('json', 'not a JSON object'),
    5: ('format', 'not a format-2 mod the loader can read'),
    6: ('target', 'made for another device or OS'),
    7: ('feature', 'uses {detail}, which a +Drive mod cannot'),
    8: ('duplicate', '{detail} is flashed already, or chosen twice'),
    9: ('core', 'needs core {detail} or newer'),
    10: ('requires', 'requires {detail}'),
    11: ('conflicts', 'conflicts with {detail}'),
    12: ('export', 'exports {detail}, which another mod exports'),
    13: ('claim', 'claims {detail}, which another mod claims'),
    14: ('import', 'imports {detail}, which nothing exports'),
    15: ('table', 'adds to the table {detail}, which takes no +Drive entries of that size'),
    16: ('full', 'the table {detail} is full ({arg} spare)'),
    17: ('room', 'the +Drive area is {arg} KB short'),
    18: ('range', 'a 16-bit PC-relative reference is out of range'),
    19: ('limit', 'past the loader\'s own limits'),
    20: ('count', 'more than 32 mods are chosen'),
}
CODE = {name: c for c, (name, _m) in CODES.items()}
BOOT_ONLY = ('missing', 'changed')     # the host has the files it is given
SKIPS = {0: 'none', 1: 'key', 2: 'armed', 3: 'drive', 4: 'abi', 5: 'unstable', 6: 'count'}
SKIP = {v: k for k, v in SKIPS.items()}
NO_FIELDS = ('sites', 'collections', 'copied', 'storage', 'sharc')   # pass 1 refuses them, in order


def message(code, detail='', arg=0):
    """A status's line, for a person."""
    return CODES[code][1].format(detail=detail, arg=arg)


def file_crc(raw):
    """A file's CRC-32 in /mods/active: zlib's (the boot linker's dl_crc32). The data container's
    footer hash is another one (container.crc)."""
    return zlib.crc32(bytes(raw)) & 0xFFFFFFFF


def _cut(s, n):
    """UTF-8 bytes of s, at most n - 1 of them (the boot linker copies bytes)."""
    return str(s).encode('utf-8')[:n - 1]


# ---- the build ---------------------------------------------------------------------------------

class Build:
    """A flashed build, as the boot linker sees it: its descriptor, and (from a built image) the
    main OS and RAM image's bytes, for the tables' flashed entries and the mods' stock parts."""

    def __init__(self, d, image=None, at=None):
        self.d = d
        self.crc, self.device_key, self.os = d['crc'], d['device_key'], d['os']
        self.modules = d['modules']
        self.flashed = [m['name'] for m in self.modules]
        self.flashed_exports = {nm for m in self.modules for nm, _a in m['exports']}
        self.tables = d['tables']                        # in descriptor order (by name)
        self.table = {t['name']: i for i, t in enumerate(self.tables)}
        self.claims = [nm for kind, _mi, nm in d['claims'] if kind == desc.CLAIM_NAME]
        self.ram_end, self.lo, self.hi, self.staging, self.staging_size = d['area']
        # the global names, in the boot linker's order: flashed exports, tables, NAME_n, core_desc
        self.globals = {}
        for m in self.modules:
            for nm, a in m['exports']:
                self.globals.setdefault(nm, ('abs', a))
        for i, t in enumerate(self.tables):
            self.globals.setdefault(t['name'], ('table', i))
            if len(t['name']) < 60:
                self.globals.setdefault(t['name'] + '_n', ('count', i))
        self.globals.setdefault(desc.SYM, ('desc', 0))
        self.nsyms = sum(len(m['exports']) for m in self.modules) + sum(
            2 if len(t['name']) < 60 else 1 for t in self.tables) + 1
        dev = next((x for x in devices.devices() if x.key == self.device_key), None)
        self.main_load = dev.main_load if dev else None
        self.image, self._ram, self.desc_at = image, None, None
        if image is not None:
            self._ram = self._locate(image, at)

    def _locate(self, image, at):
        """The image offset of RAM address 0 (the RAM image is copied whole): the descriptor sits
        right after the last of the tables, so its RAM address is that table's end."""
        ram = [t for t in self.tables if not t['flags'] & desc.F_DSP]
        last = max(ram, key=lambda t: t['address'])
        self.desc_at = _align(last['address'] + last['entry'] * (last['capacity'] + 1), 4)
        base = at - self.desc_at
        for t in ram:                           # each table ends in its zero and spare entries
            o = base + t['address']
            tail = image[o + t['entry'] * t['count']:o + t['entry'] * (t['capacity'] + 1)]
            if o < 0 or tail != bytes(t['entry'] * (t['capacity'] - t['count'] + 1)):
                raise ModError('the build\'s table %s is not where its descriptor says' % t['name'])
        return base

    def ram(self, addr, n):
        """n bytes of the flashed RAM image at addr, or None without an image."""
        if self._ram is None:
            return None
        o = self._ram + addr
        return bytes(self.image[o:o + n])

    def stock(self, addr, n):
        """n bytes of the running main OS at addr (a mod's stock part), or None."""
        if self.image is None or self.main_load is None or addr < self.main_load:
            return None
        o = addr - self.main_load
        b = bytes(self.image[o:o + n])
        return b if len(b) == n else None

    def label(self):
        core = self.modules[0]
        return '%s %s, %s %s, build 0x%08x' % (self.device_key, self.os, core['name'], core['version'],
                                                self.crc)


def load_build(path_or_bytes, device=None):
    """A build from a file: a built .syx or main OS image, a `.build` file (the descriptor alone,
    as `/mods/.build` serves it), or either in a data container. -> Build."""
    raw = path_or_bytes
    if isinstance(raw, str):
        with open(raw, 'rb') as fh:
            raw = fh.read()
    raw = bytes(raw)
    if raw[:4] == struct.pack('>I', container.MAGIC):
        raw = container.parse(raw)[1]
    if raw[:4] == desc.MAGIC:
        return Build(desc.decode(raw))
    img = desc._main_image(raw, device)
    found = desc.find(img)
    if not found:
        raise ModError('the build has no build descriptor: it was linked without the Drive Loader')
    return Build(desc.decode(img, found[0]), img, found[0])


# ---- the chosen files --------------------------------------------------------------------------

class Choice:
    """One line of /mods/active: a file, its state and, once read, its mod."""

    def __init__(self, line, name, raw):
        self.line, self.name = line, name
        self.raw = None if raw is None else bytes(raw)
        self.crc = None if raw is None else file_crc(raw)
        self.state, self.code, self.arg, self.detail, self.note = NONE, 0, 0, '', ''
        self.label = name                       # the record's id: the file name, then the mod's id
        self.mod = self.id = None
        self.exports = []
        self.alive = False

    def refuse(self, code, detail='', arg=0, note='', state=REFUSED):
        self.alive = False
        self.state, self.code, self.arg = state, CODE[code], min(arg, 0xFFFF)
        if detail:
            self.detail = str(detail)
        self.note = note
        return False

    def why(self):
        return message(self.code, self.detail, self.arg) + (' (%s)' % self.note if self.note else '')

    def record(self):
        r = bytearray(RECORD)
        struct.pack_into('>BBH', r, 0, self.state, self.code, self.arg)
        t = _cut(self.label, ID_MAX)
        r[4:4 + len(t)] = t
        return bytes(r)

    def detail_bytes(self):
        t = _cut(self.detail, DETAIL)
        return t + bytes(DETAIL - len(t))


def _empty(v):
    """A field the boot linker counts as absent: missing, null, false, 0, "", [] or {}."""
    return v is None or v is False or (type(v) is int and v == 0) or v == '' or v == [] or v == {}


def _strings(v):
    return isinstance(v, list) and all(isinstance(x, str) for x in v)


def _symbol(v):
    """A symbol the boot linker can name: [".run" | ".bss" | "abs", offset]."""
    return isinstance(v, list) and len(v) == 2 and v[0] in ('.run', '.bss', 'abs') and type(v[1]) is int


def _section_len(d):
    if not isinstance(d, dict):
        return 0
    n = 0
    for p in d.get('parts', []):
        if p and p[0] == 'hex':
            n += len(p[1]) // 2
        elif p and p[0] == 'stock' and len(p) == 3 and isinstance(p[2], int):
            n += p[2]
    return n


def pass1(build, c, state):
    """Pass 1, one file: what the boot linker's scan tells from the file and the build alone, in
    its order. `state` counts the boot linker's shared room: symbols and contributions, which a
    refused file gives back, and the names' arena and lists, which it does not. -> c.alive."""
    def keep(text):                             # a name into the arena: its bytes and a NUL
        n = len(text.encode('utf-8')) + 1
        if state['arena'] >= ARENA or state['arena'] + n > ARENA:
            return False
        state['arena'] += n
        return True

    def keep_list(v):
        if v is None:
            return True
        if not isinstance(v, list):
            return False
        for x in v:
            if not isinstance(x, str) or not keep(x) or state['lists'] >= LISTS:
                return False
            state['lists'] += 1
        return True
    try:
        doc = json.loads(c.raw.decode('utf-8'))
    except (UnicodeDecodeError, ValueError) as e:
        return c.refuse('json', note=str(e))
    if not isinstance(doc, dict):
        return c.refuse('json')
    if doc.get('elemod') != linker.FORMAT2 or format_of(doc) != linker.FORMAT2:
        return c.refuse('format', note='not "elemod": 2')
    ident = doc.get('id')
    if not isinstance(ident, str) or not keep(ident) or not ident or len(ident.encode('utf-8')) >= ID_MAX:
        return c.refuse('format', note='its id')
    c.id = c.label = ident
    if not isinstance(doc.get('version'), str) or not keep(doc['version']):
        return c.refuse('format', note='its version')
    tgt = doc.get('target')
    if not isinstance(tgt, dict) or tgt.get('device') != build.device_key or tgt.get('os') != build.os:
        return c.refuse('target', note='%s %s' % (tgt.get('device'), tgt.get('os'))
                        if isinstance(tgt, dict) else 'no target')
    for f in NO_FIELDS:
        if not _empty(doc.get(f)):
            return c.refuse('feature', f)
    secs = doc.get('sections')
    if not isinstance(secs, dict):
        return c.refuse('format', note='its sections')
    for s in secs:
        if s not in ('.run', '.bss'):
            return c.refuse('feature', s)
    res = doc.get('resources')
    if res is not None and not isinstance(res, dict):
        return c.refuse('format', note='its resources')
    for k, v in (res or {}).items():            # in the file's order, as the boot linker reads it
        if k == 'regions' and not _empty(v):
            return c.refuse('feature', 'regions')
        if k == 'names' and not keep_list(v):
            return c.refuse('limit', note='resources.names')
        if k == 'core' and v is not None and not (isinstance(v, str) and keep(v)):
            return c.refuse('format', note='resources.core')
    for k in ('imports', 'weak', 'requires', 'conflicts'):
        if not keep_list(doc.get(k)):
            return c.refuse('limit', note=k)
    run_len = _section_len(secs.get('.run'))
    for r in doc.get('relocs') or []:
        if not (isinstance(r, list) and len(r) == 5) or r[0] != '.run' \
                or r[2] not in ('abs32', 'pc32', 'pc16') or not isinstance(r[1], int) \
                or r[1] < 0 or r[1] + linker.RSIZE[r[2]] > run_len:
            return c.refuse('format', note='a relocation %r' % (r,))
    contribs = 0
    for x in doc.get('contribute') or []:
        if not isinstance(x, dict):
            return c.refuse('format', note='a contribution')
        to = order = n = None
        order = 50
        for k, v in x.items():                  # in the file's order, as the boot linker reads it
            if k == 'to':
                t = build.table.get(v) if isinstance(v, str) else None
                if t is None or not build.tables[t]['flags'] & desc.F_MORE \
                        or build.tables[t]['flags'] & desc.F_DSP:
                    return c.refuse('table', v)
                to = v
            elif k == 'order':
                if type(v) is not int:
                    return c.refuse('format', note='an order of %r' % (v,))
                order = v
            elif k == 'data':
                try:
                    n = len(bytes.fromhex(v))
                except (TypeError, ValueError):
                    return c.refuse('format', note='an entry\'s data')
            elif k == 'claims' and not _empty(v):
                return c.refuse('feature', 'claims')
        if to is None or n is None:
            return c.refuse('format', note='a contribution without "to" or "data"')
        if not 0 <= order < desc.LINKER_ORDER:
            return c.refuse('format', note='an order of %r' % (order,))
        if not n or n % build.tables[build.table[to]]['entry']:
            return c.refuse('table', to)
        if state['contribs'] + contribs >= CONTRIBS:
            return c.refuse('limit', note='contributions')
        contribs += 1
    syms = doc.get('symbols') or {}
    exports = []
    for x in doc.get('exports') or []:
        if not isinstance(x, str) or x not in syms or not _symbol(syms[x]):
            return c.refuse('format', note='exports %s' % x)
        if x == INIT:
            continue
        if state['syms'] + len(exports) >= SYMS or not keep(x):
            return c.refuse('limit', note='symbols')
        exports.append(x)
    try:                                        # the host's own reading of the file, a last check
        m = linker.Mod2(doc, c.name, c.raw)
    except ModError as e:
        return c.refuse('format', note=str(e))
    c.mod, c.exports = m, exports
    state['syms'] += len(exports)
    state['contribs'] += contribs
    c.alive = True
    return True


def _resolvable(build, alive, x):
    """Is an import a global name: the build's, or a living +Drive mod's export (its own too)."""
    return x in build.globals or any(x in o.exports for o in alive)


def _round(build, order):
    """One check round over the living mods in id order (a mod refused earlier in the round no
    longer counts), then each table's room. -> how many it refused."""
    refused = 0
    core = build.modules[0]['version']
    for i, c in enumerate(order):
        if not c.alive:
            continue
        m = c.mod
        earlier = [o for o in order[:i] if o.alive]
        alive = [o for o in order if o.alive]
        why = None
        if c.id in build.flashed or any(o.id == c.id for o in earlier):
            why = ('duplicate', c.id)
        elif m.needs_core and version_key(core) < version_key(m.needs_core):
            why = ('core', m.needs_core)
        if why is None:
            miss = next((r for r in m.requires if r not in build.flashed
                         and not any(o.id == r and o is not c for o in alive)), None)
            if miss is not None:
                why = ('requires', miss)
        if why is None:
            w = next((x for x in m.conflicts if x in build.flashed), None)
            if w is None:
                w = next((o.id for o in earlier if o.id in m.conflicts or c.id in o.mod.conflicts), None)
            if w is not None:
                why = ('conflicts', w)
        if why is None:
            x = next((x for x in c.exports if x in build.globals
                      or any(x in o.exports for o in earlier)), None)
            if x is not None:
                why = ('export', x)
        if why is None:
            x = next((n for n in m.names if n in build.claims or any(n in o.mod.names for o in earlier)), None)
            if x is not None:
                why = ('claim', x)
        if why is None:
            x = next((x for x in m.imports if not _resolvable(build, alive, x) and x not in m.weak), None)
            if x is not None:
                why = ('import', x)
        if why:
            c.refuse(*why)
            refused += 1
    for t in build.tables:                      # room, table by table: the last adder goes
        while True:
            total, last = t['count'], None
            for c in order:
                if c.alive:
                    for x in c.mod.contribute:
                        if x['to'] == t['name']:
                            total += len(x['data']) // t['entry']
                            last = c
            if total <= t['capacity']:
                break
            last.refuse('full', t['name'], t['capacity'] - t['count'])
            refused += 1
    return refused


def _layout(build, order):
    """In id order from the area's start: every .run at its alignment, then every .bss. -> (base,
    end), or None after refusing the last living mod for room."""
    live = [c.mod for c in order if c.alive]
    base = {}
    at = lay_out(live, '.run', build.lo, base)
    end = _align(lay_out(live, '.bss', _align(at, 4), base), 4)
    if end > build.hi:
        last = [c for c in order if c.alive][-1]
        last.refuse('room', arg=(end - build.hi + 1023) // 1024)
        return None
    return base, end


def _globals(build, order, base):
    """Name -> a function giving its value: the build's names first (NAME_n the count after the
    merge), then the living +Drive mods' exports, in id order."""
    alive = [c for c in order if c.alive]
    added = {}
    for c in alive:
        for x in c.mod.contribute:
            added[x['to']] = added.get(x['to'], 0) + len(x['data']) // build.tables[build.table[x['to']]]['entry']
    glob = {}
    for nm, (kind, v) in build.globals.items():
        if kind == 'abs':
            glob[nm] = (lambda v=v: v)
        elif kind == 'table':
            glob[nm] = (lambda v=v: build.tables[v]['address'])
        elif kind == 'count':
            glob[nm] = (lambda v=v: build.tables[v]['count'] + added.get(build.tables[v]['name'], 0))
        elif build.desc_at is not None:
            glob[nm] = (lambda: build.desc_at)
    for c in alive:
        for x in c.exports:
            if x not in glob:
                sec, off = c.mod.symbols[x]
                v = off if sec == 'abs' else base.get((c.id, sec), 0) + off
                glob[x] = (lambda v=v: v)
    return glob


def _pass2(build, c, base, glob, edata):
    """One living mod's .run, relocated, and its entries' bytes and relocations. -> ((run bytes,
    [(contribution, bytes, [(off, type, S + A)])]), None), or (None, (code, detail))."""
    m = c.mod
    own = {}
    if sum(1 for sec, _off in m.symbols.values() if sec in ('abs', '.run', '.bss')) > LOCALS:
        return None, ('limit', '')
    for nm, (sec, off) in m.symbols.items():
        if sec == 'abs':
            own[nm] = off
        elif sec in ('.run', '.bss'):
            own[nm] = base.get((c.id, sec), 0) + off

    def value(tgt):
        if tgt == 'abs':
            return 0
        kind, nm = tgt.split(':', 1)
        if kind == 'sec':
            return base.get((c.id, nm)) if nm in ('.run', '.bss') else None
        if nm in own:
            return own[nm]
        if nm in glob:
            return glob[nm]()
        if nm in m.weak and 'core_zero' in glob:
            return glob['core_zero']()
        return None
    run = bytearray()
    if m.size('.run'):
        for p in m.sections['.run']['parts']:
            if p[0] == 'hex':
                run += p[1]
            else:
                b = build.stock(p[1], p[2])
                if b is None:
                    raise ModError('%s copies stock bytes at 0x%08x: link it against a built image, '
                                   'not a .build file' % (m.label(), p[1]))
                run += b
        for sec, off, typ, tgt, add in m.relocs:
            v = value(tgt)
            if v is None:
                return None, ('import', tgt.split(':', 1)[-1])
            try:
                put_reloc(run, off, typ, v + add, base[(c.id, '.run')] + off, '%s+0x%x' % (sec, off))
            except ModError:
                return None, ('range', '')
    ents = []
    for x in m.contribute:
        if edata[0] + len(x['data']) > EDATA:
            return None, ('limit', '')
        edata[0] += len(x['data'])
        rel = []
        for off, typ, tgt, add in x['relocs']:
            if edata[1] >= ERELOCS:
                return None, ('limit', '')
            v = value(tgt)
            if v is None:
                return None, ('import', tgt.split(':', 1)[-1])
            edata[1] += 1
            rel.append((off, typ, v + add))
        ents.append((x, bytes(x['data']), rel))
    return (bytes(run), ents), None


def _bind(build, live, placed):
    """Each table that gets an entry: flashed and +Drive entries sorted by (order, id, place),
    entry relocations at their final address, then zero entries to capacity + 1. -> {name: {...}},
    or None past the loader's room (then the whole set is refused)."""
    names = build.flashed
    out = {}
    for t in build.tables:
        e, cnt = t['entry'], t['count']
        new = [(c, x, data, rel) for c in live for x, data, rel in placed[c.id][1] if x['to'] == t['name']]
        if not new:
            continue
        if (cnt + 1) * e > MDATA:
            return None
        flashed = build.ram(t['address'], cnt * e)
        rows = []
        for i, k in enumerate(t['keys']):
            nm = names[k & 0xFFFF]
            rows.append((entry_key(k >> 16, nm.encode('utf-8'), i), nm, k >> 16,
                         None if flashed is None else flashed[i * e:(i + 1) * e], []))
        for c, x, data, rel in new:
            for k in range(len(data) // e):
                rows.append((entry_key(x['order'], c.id.encode('utf-8'), x['i'] << 8 | k), c.id, x['order'],
                             data[k * e:(k + 1) * e],
                             [(o - k * e, typ, v) for o, typ, v in rel if k * e <= o < (k + 1) * e]))
        if len(rows) > MERGE:
            return None
        rows.sort(key=lambda row: row[0])
        raw = bytearray()
        for i, row in enumerate(rows):
            d = bytearray(row[3] if row[3] is not None else bytes(e))
            for o, typ, v in row[4]:
                try:
                    put_reloc(d, o, typ, v, t['address'] + i * e + o, 'an entry of %s' % t['name'])
                except ModError:
                    return None
            raw += d
        raw += bytes(e * (t['capacity'] + 1) - len(raw))
        out[t['name']] = {'address': t['address'], 'entry': e, 'capacity': t['capacity'],
                          'bytes': bytes(raw) if flashed is not None else None,
                          'entries': [(row[1], row[2]) for row in rows]}
    return out


class Result:
    pass


def check(build, files, deep=False):
    """Pass 1, the check rounds and the layout; with `deep`, pass 2 and the merge too (link()).
    `files`: [(file name, bytes, or None for a missing file)] in /mods/active's order. -> Result:
    .choices (each with its state), .live (the mods that load, in id order), .base, .end, .skip."""
    r = Result()
    r.build, r.skip = build, 'none'
    choices = [Choice(i, n, raw) for i, (n, raw) in enumerate(files)]
    r.choices, r.live, r.base, r.end, r.area, r.tables, r.inits, r.glob = \
        choices, [], {}, build.lo, b'', {}, [], {}
    if len(choices) > MAX_MODS:                 # the loader reads none of them
        r.skip = 'count'
        r.choices = choices[:MAX_MODS]
        for c in r.choices:
            c.refuse('count', state=NONE)
        return r
    state = {'syms': build.nsyms, 'contribs': 0, 'arena': 1, 'lists': 0}   # the arena's 0 is ""
    for c in choices:
        if c.raw is None:
            c.refuse('missing', state=DROPPED)
        elif build.staging_size and len(c.raw) > build.staging_size:
            c.refuse('too-big')
        else:
            pass1(build, c, state)
    order = by_id(c for c in choices if c.alive)          # stable: of one id, the earlier line first
    lay = placed = glob = None
    for _ in range(4 * MAX_MODS):
        while _round(build, order):
            pass
        lay = _layout(build, order)
        if lay is None:
            continue
        if not deep:
            break
        glob = _globals(build, order, lay[0])
        placed, edata, again = {}, [0, 0], False   # entry bytes, entry relocations
        for c in order:
            if not c.alive:
                continue
            got, why = _pass2(build, c, lay[0], glob, edata)
            if why:
                c.refuse(why[0], why[1])
                again = True
                break
            placed[c.id] = got
        if not again:
            break
        lay = None
    live = [c for c in order if c.alive]
    merged = _bind(build, live, placed) if lay is not None and deep else {}
    if lay is None or merged is None:           # never settled, or past the loader's room
        r.skip = 'unstable'
        for c in live:
            c.refuse('limit')
        return r
    base, end = lay
    r.live, r.base, r.end = live, base, end if live else build.lo
    if deep:
        area = bytearray(r.end - build.lo)
        for c in live:
            run = placed[c.id][0]
            if run:
                o = base[(c.id, '.run')] - build.lo
                area[o:o + len(run)] = run
        r.area, r.tables = bytes(area), merged
        r.glob = {x: glob[x]() for c in live for x in c.exports}
    for c in live:
        c.state, c.code = LOADED, 0
        sec = c.mod.symbols.get(INIT, ('', 0))[0]
        if sec in ('.run', '.bss'):
            r.inits.append((c.id, base[(c.id, sec)] + c.mod.symbols[INIT][1]))
    return r


def link(build, files):
    """Pass 1 to the merge. -> Result, as check()'s, and .area (bytes from the area's start to
    .end), .tables ({name: {'address', 'entry', 'capacity', 'bytes' (None without an image),
    'entries': [(module, order)]}}), .inits ([(mod id, address)] in id order), .glob."""
    return check(build, files, deep=True)


def records(r):
    """The 32 status records and the 32 detail strings, as the boot linker keeps them."""
    st, dt = bytearray(MAX_MODS * RECORD), bytearray(MAX_MODS * DETAIL)
    for c in r.choices[:MAX_MODS]:
        st[c.line * RECORD:(c.line + 1) * RECORD] = c.record()
        dt[c.line * DETAIL:(c.line + 1) * DETAIL] = c.detail_bytes()
    return bytes(st), bytes(dt)


def status_text(r):
    """One line per chosen file: name, state, code, message (for a person)."""
    out = ''.join('%s %s %d %s\n' % (c.name, STATES[c.state], c.code,
                                     'loaded' if c.state == LOADED else message(c.code, c.detail, c.arg))
                  for c in r.choices)
    return out + ('skipped: %s\n' % r.skip if r.skip != 'none' else '')


def summary(r, linked=False):
    """The JSON summary: everything a gate compares, in a fixed order."""
    b = r.build
    out = {
        'build': {'crc': '0x%08x' % b.crc, 'device': b.device_key, 'os': b.os,
                  'core': '%s %s' % (b.modules[0]['name'], b.modules[0]['version']),
                  'area': ['0x%08x' % b.lo, '0x%08x' % b.hi], 'staging_size': b.staging_size},
        'skip': r.skip,
        'choices': [{'line': c.line, 'file': c.name, 'crc': None if c.crc is None else '0x%08x' % c.crc,
                     'id': c.id, 'state': STATES[c.state], 'code': c.code, 'reason': CODES[c.code][0],
                     'detail': c.detail, 'arg': c.arg,
                     'message': 'loaded' if c.state == LOADED else c.why()} for c in r.choices],
        'loads': [c.id for c in r.live],
        'end': '0x%08x' % r.end, 'used': r.end - b.lo,
    }
    if linked:
        out['area_sha256'] = _sha(r.area)
        out['inits'] = ['%s 0x%08x' % x for x in r.inits]
        out['mods'] = {c.id: {'file': c.name, 'version': c.mod.version,
                              'sections': {s: '0x%08x' % r.base[(c.id, s)] for s in ('.run', '.bss')
                                           if (c.id, s) in r.base},
                              'exports': {x: '0x%08x' % r.glob[x] for x in sorted(c.exports)}}
                       for c in r.live}
        out['tables'] = {n: {'address': '0x%08x' % t['address'], 'entry': t['entry'],
                             'capacity': t['capacity'], 'count': len(t['entries']),
                             'sha256': _sha(t['bytes']) if t['bytes'] is not None else None,
                             'entries': ['%s:%d' % x for x in t['entries']]}
                         for n, t in sorted(r.tables.items())}
        st, dt = records(r)
        out['status_sha256'], out['detail_sha256'] = _sha(st), _sha(dt)
    return out


def _sha(b):
    import hashlib
    return hashlib.sha256(bytes(b)).hexdigest()


def write_layout(r, out_dir):
    """`link --device-layout`'s files: area.bin, tables/<name>.bin (from a built image),
    status.bin, detail.bin, status.txt and summary.json. -> the paths written, in order."""
    os.makedirs(out_dir, exist_ok=True)
    paths = []

    def put(name, data):
        p = os.path.join(out_dir, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as fh:
            fh.write(data if isinstance(data, bytes) else data.encode('utf-8'))
        paths.append(p)
    put('area.bin', r.area)
    for n, t in sorted(r.tables.items()):
        if t['bytes'] is not None:
            put(os.path.join('tables', n + '.bin'), t['bytes'])
    st, dt = records(r)
    put('status.bin', st)
    put('detail.bin', dt)
    put('status.txt', status_text(r))
    put('summary.json', json.dumps(summary(r, True), indent=1, sort_keys=True) + '\n')
    return paths


# ---- /mods/active --------------------------------------------------------------------------------

def active_text(entries):
    """`/mods/active` for [(file name, bytes)]: one line each, the name and its CRC-32 (8 hex
    digits)."""
    for n, _raw in entries:
        if not n or len(n.encode('utf-8')) >= NAME_MAX or n != n.strip() or n.startswith('#') \
                or any(ch in n for ch in '\n\r'):
            raise ModError('%r cannot be a line of /mods/active' % n)
    return ''.join('%s %08x\n' % (n, file_crc(raw)) for n, raw in entries)


def parse_active(text):
    """`/mods/active` -> [(name, crc)], as the boot linker reads it (blank and # lines skipped, a
    line without a CRC skipped, the name before the last run of spaces). None past 32 lines."""
    out = []
    for ln in text.split('\n'):
        ln = ln[:-1] if ln.endswith('\r') else ln
        ln = ln.strip(' \t')
        if not ln or ln.startswith('#'):
            continue
        cut = max(ln.rfind(' '), ln.rfind('\t'))
        if cut < 0:
            continue
        name, h = ln[:cut].rstrip(' \t'), ln[cut + 1:]
        if len(name.encode('utf-8')) >= NAME_MAX or len(h) > 10:
            continue
        try:
            v = int(h[2:] if h[:2].lower() == '0x' else h, 16)
        except ValueError:
            continue
        if v > 0xFFFFFFFF:
            continue
        if len(out) >= MAX_MODS:
            return None
        out.append((name, v))
    return out


# ---- the command line ----------------------------------------------------------------------------

COMMANDS = ('check', 'link', 'mods')


def _files(paths):
    out = []
    for p in paths:
        with open(p, 'rb') as fh:
            out.append((os.path.basename(p), fh.read()))
    return out


def print_result(r, log=print):
    log('build: %s' % r.build.label())
    if r.skip != 'none':
        log('  nothing loads: %s' % r.skip)
    for c in r.choices:
        log('  %-8s %s%s' % (STATES[c.state].upper(), c.name, '' if c.state == LOADED else ': ' + c.why()))
    log('%d of %d load; the +Drive area is used to 0x%08x (%d of %d bytes)'
        % (len(r.live), len(r.choices), r.end, r.end - r.build.lo, r.build.hi - r.build.lo))


def main(argv=None):
    import argparse
    argv = sys.argv[1:] if argv is None else list(argv)
    ap = argparse.ArgumentParser(prog='elekloader', description='+Drive mods: check a set against a '
                                 'flashed build, lay it out as the unit will, and move files over USB.')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('check', help='the build-checker: would these +Drive mods load on this build?')
    p.add_argument('build', help='the flashed build: its .syx, its main OS image, or a .build file')
    p.add_argument('mods', nargs='*', help='the .elemod files, in /mods/active\'s order')
    p.add_argument('--device', help='the device key, for a .syx')
    p.add_argument('--json', action='store_true', help='print the summary as JSON')
    p = sub.add_parser('link', help='the boot linker\'s twin: what the unit\'s RAM holds after boot')
    p.add_argument('--device-layout', action='store_true', required=True,
                   help='link into the +Drive area as the unit does (the only mode)')
    p.add_argument('build')
    p.add_argument('mods', nargs='*')
    p.add_argument('--device')
    p.add_argument('--out', required=True, help='the folder for area.bin, tables/, status.* and summary.json')
    p = sub.add_parser('mods', help='the unit\'s /mods over USB (the Drive Loader\'s Data route)')
    from . import datarpc
    datarpc.add_arguments(p)
    a = ap.parse_args(argv)
    if a.cmd == 'mods':
        return datarpc.main(a)
    try:
        build = load_build(a.build, a.device)
        files = _files(a.mods)
        if a.cmd == 'check':
            r = check(build, files)
            if a.json:
                print(json.dumps(summary(r), indent=1, sort_keys=True))
            else:
                print_result(r)
            return 0 if all(c.state == LOADED for c in r.choices) else 1
        r = link(build, files)
    except (OSError, ModError) as e:
        print('REFUSED: %s' % e)
        return 2
    print_result(r)
    for p in write_layout(r, a.out):
        print('WROTE %s' % p)
    return 0 if all(c.state == LOADED for c in r.choices) else 1


if __name__ == '__main__':
    sys.exit(main())
