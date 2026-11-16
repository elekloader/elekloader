# SPDX-License-Identifier: GPL-2.0-or-later
"""+Drive storage: the boot allocator's twin and /mods/.store (docs/DRIVE.md, "+Drive storage").

A +Drive mod's storage fields live in the same bytes as the flashed build's, so a save, write-through,
Total Recall, SysEx and library sounds carry them with nothing new. Only the placement moves from build
time to boot: the Drive Loader places the +Drive fields in the bits each scope of the flashed build
leaves free, and keeps where it put them in /mods/.store. This module is that allocator on the computer,
rule for rule, so the loader (mods/drive-loader-ar2) and the host give the same placements and the same
.store bytes for the same inputs.

What the allocator reads from the flashed build is only what the unit can read at boot (Base): each
scope's room, bits used and generation (the descriptor's storage record); the layout ID and its ancestors
(core_store_ids); the lock codes, CCs and inits the build gives its own fields (core_lock_codes,
core_lock_ccs, core_field_inits). The build's lockfile trailer is not read: nothing on the unit reads it.

The rules mirror elekloader/store.py's, turned to face the flashed build:

- the pool of a scope is its bits from the build's used bits up to its room: bits no flashed field has
  ever used. +Drive fields are placed from the room's top down, so a later flashed build that grows from
  its used bits up meets them as late as it can;
- a field keeps its bits while its mod declares it with the same scope, width and init;
- a field no chosen mod declares any more is parked: it keeps its bits and its values, the core counts
  it as owned (so a save without the mod keeps them), and its mod gets them back when it returns. Only
  FORGET DATA (forget()) drops a parked field;
- any other new field takes never-used bits from the scope's low mark down. When they run out, the
  scope is repacked: its generation goes up, its parked fields are dropped and its fields are packed again
  from the room's top, in (mod id, symbol) order. Projects and sounds saved before then load the scope at
  defaults, the flashed fields' too: the core keeps one generation a scope;
- the same repack happens when the flashed build changes under a field: a new generation of the scope
  (a flashed repack), a field below the build's used bits (the flashed build grew into it) or past its
  room (a core with less room);
- lock codes come from the top of the device's range down (127, 126, ...), past every code the flashed
  build uses; a field keeps its code while it keeps its place, a parked field's code is kept for it, and
  only when the codes above the build's highest are gone is the code freed longest ago given out again.
  A code the flashed build has taken since is given up. Where the device numbers codes per track kind
  (the Analog Four MKII, store.py "Lock codes per track kind"), each kind has its own range and all of
  this happens per kind: a sound field's code is the synth tracks', a kit field's the FX track's, or the
  CV track's when it declares "track": "cv"; a field whose kind changes is a new parameter;
- CCs come from the device's pool from its end, past the build's own; a field keeps its CC wherever it
  is placed, a parked field's CC is kept for it until the never-used CCs run out;
- the core's LFO destination field of a scope (core_lfo_dst, core_fx_lfo_dst) is one more field, of the
  core's id, placed only while a chosen mod has an "lfo" field of that scope, the flashed build places
  none and its core keeps them live (core_lfo_dsts, core-ar2 0.14): it counts in the room and parks like
  a mod's, and the loader writes its descriptor into core_lfo_dsts;
- the live layout ID is the flashed layout's ID while the .store holds nothing and no generation moved;
  otherwise the first 4 bytes of a SHA-256 of the flashed ID, each scope's generation and every field
  (parked ones too) and lock code (layout_id). A park or a return changes no bit, so it keeps the ID. The
  .store keeps the last 15 live IDs before this one: they and the flashed build's lineage are the
  ancestors the core reads data from.

fit() decides which mods the room refuses (the last in id order, again until the set fits), before
allocate() places a set that fits. Nothing here reads a clock or iterates in hash order.

/mods/.store (format 1; big-endian, every record 4-byte aligned):

  +0x00 'DLST'; +0x04 u16 format, u16 0; +0x08 u32 size; +0x0c u32 CRC-32 (zlib's) of the file with
  this word zero; +0x10 u32 the flashed layout ID it was written for, u32 the live layout ID;
  +0x18 u8 next code, u8 ID count, then u16 counts of fields, parked fields, codes, freed codes, CCs and
  freed CCs, u16 0; +0x28 per scope (sound, kit, global, track, pattern, project), 8 bytes: u8 the
  flashed generation it was written for, u8 the live generation, u16 0, u32 the low mark (the lowest bit
  the +Drive has given out since the scope's last repack; its room when none);
  then the IDs (u32 each, newest first); the fields, then the parked fields (by scope, then offset;
  parked in the order they were parked): u8 scope (1..6), u8 bits, u16 offset, u32 init, u8 mod id's
  length, u8 symbol's length, u16 0, the two names' bytes, zeros to 4; the codes (by code), then the
  freed codes (freed longest ago first): u8 code, u8 and u8 the names' lengths, u8 0, the names, zeros to
  4; the CCs (by CC), then the freed CCs, likewise with the CC.

Format 2, the same with lock codes per track kind (written only for such a device): +0x18 the synth
tracks' next code, +0x26 the FX track's and +0x27 the CV track's (format 1's u16 0); each code and freed
code record's fourth byte is its kind (1 synth, 2 FX, 3 CV); CC records as format 1. The codes are by
kind, then code.

Format 3, format 2 with the +Drive mods' knob ids (docs/DRIVE.md, "Knob ids on the +Drive"; written only
when the state holds any): after the CC records, u16 runs, u16 freed runs, u32 the next knob id, then each
run (by first id), then each freed run (freed longest ago first): u32 first id, u16 count, u8 the mod id's
length, u8 0, its bytes, zeros to 4. Not part of the layout ID: the ids are not stored data.

A file that is missing, cut short, of another format (or of the other one of 1 and 2 than the device
takes) or with a wrong CRC reads as none: a fresh placement, and the items then load those scopes at
defaults.
"""
import hashlib
import struct
import zlib

from . import store
from .elemod import ModError

MAGIC = b'DLST'
FORMAT = 1
FORMAT_KINDS = 2                                # lock codes per track kind (the Analog Four MKII)
FORMAT_KNOBS = 3                                # format 2 and the +Drive mods' knob ids
MAX_KNOB_RUNS = 512                             # knob runs, and again freed runs, a .store keeps (dstore.h)
KINDS = ('synth', 'fx', 'cv')                   # format 2's kind numbers 1..3
HEAD = 0x28
SCOPE_REC = 8
MAX_IDS = store.MAX_ANCESTORS                   # the last 15 live IDs before this one
ID_HASH = b'ELKDRIVE'                           # the layout ID's hash starts with these bytes
NAME_MAX = 255                                  # a mod id or a symbol, in bytes


def _b(s):
    return s.encode('utf-8')


def _key(f):
    """A field's (mod, sym) in the boot allocator's order: the UTF-8 bytes."""
    return (_b(f['mod']), _b(f['sym']))


# ---- what the flashed build gives --------------------------------------------------------------

class Base:
    """The flashed build, as the boot allocator sees it.

    scopes: {scope: (room, used, generation)}, every scope of store.SCOPES; flashed_id and ancestors: the
    build's layout ID and its ancestors (core_store_ids); codes: [(field descriptor, code | flags)]
    (core_lock_codes); ccs: [(field descriptor, CC)] (core_lock_ccs); inits: how many entries
    core_field_inits has; room: {table: spare entries} of the four tables the loader adds to (None for a
    table the build lacks). codes and ccs are None where the build's bytes are not known (a descriptor
    alone): fit() still works, from ncodes (core_lock_codes' count), and allocate() does not. live_dst: the
    core keeps its LFO destinations' fields in RAM (core-ar2 0.14's core_lfo_dsts), so the loader can place
    one at boot; core: the core's id, whose fields those are."""

    def __init__(self, device, scopes, flashed_id, ancestors=(), codes=(), ccs=(), inits=0, room=None, ncodes=0,
                 live_dst=False, core='core'):
        self.device = device
        self.live_dst, self.core = live_dst, core
        self.scopes = {s: tuple(scopes[s]) for s in store.SCOPES}
        self.flashed_id, self.ancestors = flashed_id, list(ancestors)
        self.codes = None if codes is None else [tuple(c) for c in codes]
        self.ccs = None if ccs is None else [tuple(c) for c in ccs]
        self.inits = inits
        self.ncodes = len(self.codes) if self.codes is not None else ncodes
        self.room = dict(room) if room is not None else {t: 256 for t in TABLES}
        first, last = store.code_range(device)
        self.first_code, self.last_code = first, last
        self.pool, self.fx_taken = store.cc_pool(device)
        self.kinds = store.kinds(device)        # () where one range serves every track

    def code_set(self):
        return {c & 0xFF for _d, c in self.codes or ()}

    def entry_kind(self, d, c):
        """A flashed core_lock_codes entry's kind (core-a4mk2's lock.c kind_of_entry), None without kinds."""
        if not self.kinds:
            return None
        return 'synth' if d >> 28 == 1 else 'cv' if c & store.CV_FLAG else 'fx'

    def kind_codes(self, kind):
        """The flashed build's codes of a kind (of every kind where codes have none)."""
        return {c & 0xFF for d, c in self.codes or () if self.entry_kind(d, c) == kind}

    def kind_count(self, kind):
        """How many entries of core_lock_codes the flashed build has of a kind (from ncodes without
        kinds or where its entries are not known)."""
        if not self.kinds:
            return self.ncodes
        return sum(1 for d, c in self.codes or () if self.entry_kind(d, c) == kind)

    def cc_set(self):
        return {c for _d, c in self.ccs or ()}

    def lfo_dst(self, scope):
        """Does the build place the core's LFO destination field for this scope (core_lfo_dst for sound,
        core_fx_lfo_dst for kit)? It does when one of its own fields of the scope is an LFO destination."""
        n = store.SCOPE_NUM[scope]
        return any(c & store.LFO_FLAG and d >> 28 == n for d, c in self.codes or ())

    def dst_fields(self, decls, lfos):
        """The core's LFO destination fields the +Drive places for these declarations ([(mod, sym, scope,
        bits)]) and LFO destinations ({(mod, sym)}): for each scope with an "lfo" field, where the core keeps
        the field live and the flashed build does not place it. -> [(core, sym, scope, bits)]."""
        if not self.live_dst:
            return []
        scope_of = {(d[0], d[1]): d[2] for d in decls}
        out = []
        for sym, scope, bits in ((store.LOCK_DST, 'sound', store.lock_dst_bits(self.device)),
                                 (store.FX_LOCK_DST, 'kit', store.fx_lock_dst_bits(self.device))):
            if bits and not self.lfo_dst(scope) and any(scope_of.get(k) == scope for k in lfos):
                out.append((self.core, sym, scope, bits))
        return out

    @classmethod
    def from_build(cls, build):
        """A drive.Build (a descriptor, with or without the image) -> Base."""
        d = build.d
        sc = {n: (r, u, g) for n, r, u, g in d['storage']['scopes']}
        if set(sc) != set(store.SCOPES):
            raise ModError('the build keeps no mod storage: its core has no __store_gen')

        def entries(name, size):
            i = build.table.get(name)
            if i is None:
                return None
            t = build.tables[i]
            if t['count'] == 0:
                return []
            raw = build.ram(t['address'], t['entry'] * t['count'])
            if raw is None:
                return None
            return [struct.unpack_from('>' + 'I' * (t['entry'] // 4), raw, k * t['entry'])[:size]
                    for k in range(t['count'])]
        ids = entries(store.IDS_TABLE, 1)
        codes = entries(store.CODES_TABLE, 2)
        ccs = entries(store.CCS_TABLE, 2)
        room = {}
        for name in TABLES:
            i = build.table.get(name)
            room[name] = None if i is None else build.tables[i]['capacity'] - build.tables[i]['count']
        i = build.table.get(store.INITS_TABLE)
        inits = 0 if i is None else build.tables[i]['count']
        fid = ids[0][0] if ids else None        # None: not known from a descriptor alone
        i = build.table.get(store.CODES_TABLE)
        ncodes = 0 if i is None else build.tables[i]['count']
        core = build.modules[0]['name'] if build.modules else 'core'
        return cls(build.device_key, sc, fid, [a[0] for a in (ids or [])[1:]],
                   [] if codes is None and i is None else codes,
                   [] if ccs is None and build.table.get(store.CCS_TABLE) is None else ccs, inits, room, ncodes,
                   live_dst=LFO_DSTS in build.flashed_exports, core=core)


LFO_DSTS = 'core_lfo_dsts'                      # core-ar2 0.14: the LFO destinations' fields, live (sound, kit)


TABLES = (store.CODES_TABLE, store.CCS_TABLE, store.INITS_TABLE, store.HANDLES_TABLE)


# ---- the chosen mods ---------------------------------------------------------------------------

class Want:
    """The storage the chosen +Drive mods declare, in id order (UTF-8 bytes)."""

    def __init__(self, mods):
        """mods: [mod document (a dict with "id" and "storage")], any order."""
        self.mods = sorted((m['id'] for m in mods), key=_b)
        self.decls, self.locks, self.ccs, self.inits = [], set(), set(), {}
        self.lfos, self.rough, self.routes, self.handles, self.cvs = set(), set(), set(), set(), set()
        for m in sorted(mods, key=lambda m: _b(m['id'])):
            mid = m['id']
            for sym, scope, bits in store.parse_decls(m):
                self.decls.append((mid, sym, scope, bits))
            for name, out in (('locks', self.locks), ('ccs', self.ccs), ('lfos', self.lfos),
                              ('rough', self.rough), ('routes', self.routes), ('handles', self.handles)):
                out.update((mid, s) for s in getattr(store, 'parse_' + name)(m))
            self.inits.update({(mid, s): v for s, v in store.parse_inits(m).items()})
            self.cvs.update((mid, s) for s in store.parse_cvs(m))

    def of(self, mods):
        """The same declarations, of the given mods only."""
        w = Want([])
        keep = set(mods)
        w.mods = [m for m in self.mods if m in keep]
        w.decls = [d for d in self.decls if d[0] in keep]
        for name in ('locks', 'ccs', 'lfos', 'rough', 'routes', 'handles', 'cvs'):
            setattr(w, name, {k for k in getattr(self, name) if k[0] in keep})
        w.inits = {k: v for k, v in self.inits.items() if k[0] in keep}
        return w

    def kind(self, k):
        """A lockable field's track kind ('synth', 'fx' or 'cv'), on a device with kinds."""
        scope = next(d[2] for d in self.decls if (d[0], d[1]) == k)
        return 'synth' if scope == 'sound' else 'cv' if k in self.cvs else 'fx'


def _no_cv(base, want):
    if want.cvs and 'cv' not in base.kinds:
        mid, s = min(want.cvs, key=lambda k: (_b(k[0]), _b(k[1])))
        raise ModError('%s: storage field %r: +Drive storage has no CV track' % (mid, s))


# ---- the room ----------------------------------------------------------------------------------

def _needs(base, w):
    """[(what, needed, there is, uses(field))] in the order fit() checks them."""
    out = []
    extra = base.dst_fields(w.decls, w.lfos)
    for s in store.SCOPES:
        room, used, _g = base.scopes[s]
        mine = {(d[0], d[1]) for d in w.decls if d[2] == s}
        more = sum(d[3] for d in extra if d[2] == s)     # the core's LFO field, for the scope's "lfo" fields
        out.append((s, sum(d[3] for d in w.decls if d[2] == s) + more, room - used, mine.__contains__))
    span = base.last_code - base.first_code + 1
    if base.kinds:                              # one range a track kind: 'codes-synth', 'codes-fx', 'codes-cv'
        for kd in base.kinds:
            mine = {k for k in w.locks if w.kind(k) == kd}
            out.append(('codes-' + kd, len(mine), span - base.kind_count(kd), mine.__contains__))
    else:
        out.append(('codes', len(w.locks), span - base.ncodes, w.locks.__contains__))
    for t, n, uses in ((store.CODES_TABLE, len(w.locks), w.locks.__contains__),
                       (store.INITS_TABLE, len(w.inits), w.inits.__contains__),
                       (store.HANDLES_TABLE, len(w.handles), w.handles.__contains__)):
        have = base.room.get(t)
        have = 0 if have is None else min(have, store.MAX_INITS - base.inits) if t == store.INITS_TABLE else have
        out.append((t, n, have, uses))
    return out


def fit(base, want):
    """Which mods the room refuses: while a scope's pool, the free lock codes or a table's spare entries are
    short (in that order: scopes in store.SCOPES order, codes, then the tables core_lock_codes,
    core_field_inits and core_handles), the last mod in id order that needs them is refused, and the check
    runs again. Parked fields never refuse a mod: a repack drops them. A full core_lock_ccs refuses nothing:
    a field then has no CC. -> [(mod id, what, short by)], in refusal order; what is a scope, 'codes' (on
    a device with kinds 'codes-synth', 'codes-fx' and 'codes-cv', in that order) or a table's name."""
    _no_cv(base, want)
    alive, out = list(want.mods), []
    while True:
        w = want.of(alive)
        short = next(((what, n - have, uses) for what, n, have, uses in _needs(base, w) if n > have), None)
        if short is None:
            return out
        what, by, uses = short
        users = {d[0] for d in w.decls if uses((d[0], d[1]))}
        last = [m for m in alive if m in users][-1]
        alive.remove(last)
        out.append((last, what, by))


# ---- the state: /mods/.store -------------------------------------------------------------------

def fresh(base):
    return {'flashed_id': base.flashed_id, 'id': base.flashed_id, 'ids': [],
            'scopes': {s: {'fgen': base.scopes[s][2], 'gen': base.scopes[s][2], 'low': base.scopes[s][0]}
                       for s in store.SCOPES},
            'next_code': {k: base.last_code for k in base.kinds} if base.kinds else base.last_code,
            'fields': [], 'parked': [], 'codes': [], 'freed_codes': [],
            'ccs': [], 'freed_ccs': []}


def _pad(b):
    return b + bytes(-len(b) % 4)


def _names(mod, sym):
    m, s = _b(mod), _b(sym)
    if not m or not s or len(m) > NAME_MAX or len(s) > NAME_MAX:
        raise ModError('.store: a name of 1 to %d bytes' % NAME_MAX)
    return m, s


def _kinded(st):
    return isinstance(st['next_code'], dict)


def _code_order(c):
    return (KINDS.index(c['kind']) if 'kind' in c else 0, c['code'])


def _has_knobs(st):
    return bool(st.get('knobs') or st.get('freed_knobs') or st.get('next_knob'))


def encode(st):
    """The state -> /mods/.store's bytes (format 2 for a state with codes per track kind)."""
    kinded = _kinded(st)
    body = bytearray()
    for s in store.SCOPES:
        sc = st['scopes'][s]
        body += struct.pack('>BBHI', sc['fgen'], sc['gen'], 0, sc['low'])
    for i in st['ids']:
        body += struct.pack('>I', i)
    for f in st['fields'] + st['parked']:
        m, s = _names(f['mod'], f['sym'])
        body += _pad(struct.pack('>BBHIBBH', store.SCOPE_NUM[f['scope']], f['bits'], f['offset'],
                                 f.get('init', 0), len(m), len(s), 0) + m + s)
    for k, v in (('codes', 'code'), ('freed_codes', 'code'), ('ccs', 'cc'), ('freed_ccs', 'cc')):
        for c in st[k]:
            m, s = _names(c['mod'], c['sym'])
            kn = KINDS.index(c['kind']) + 1 if kinded and v == 'code' else 0
            body += _pad(struct.pack('>BBBB', c[v], len(m), len(s), kn) + m + s)
    knobbed = kinded and _has_knobs(st)
    if knobbed:
        body += struct.pack('>HHI', len(st.get('knobs', [])), len(st.get('freed_knobs', [])), st.get('next_knob', 0))
        for r in st.get('knobs', []) + st.get('freed_knobs', []):
            m = _b(r['mod'])
            if not m or len(m) > NAME_MAX:
                raise ModError('.store: a name of 1 to %d bytes' % NAME_MAX)
            body += _pad(struct.pack('>IHBB', r['first'], r['count'], len(m), 0) + m)
    nc = st['next_code']
    fmt = FORMAT_KNOBS if knobbed else FORMAT_KINDS if kinded else FORMAT
    head = struct.pack('>4sHHII II BB 6H BB', MAGIC, fmt, 0, HEAD + len(body), 0,
                       st['flashed_id'], st['id'], nc['synth'] if kinded else nc, len(st['ids']),
                       len(st['fields']), len(st['parked']), len(st['codes']), len(st['freed_codes']),
                       len(st['ccs']), len(st['freed_ccs']), nc['fx'] if kinded else 0, nc['cv'] if kinded else 0)
    raw = bytearray(head + body)
    struct.pack_into('>I', raw, 0x0c, zlib.crc32(bytes(raw)) & 0xFFFFFFFF)
    return bytes(raw)


def decode(raw):
    """/mods/.store's bytes -> the state, or None for a file the allocator does not take (missing, cut
    short, another format, a wrong CRC, a bad record): the loader then places afresh."""
    if raw is None:
        return None
    raw = bytes(raw)
    try:
        magic, fmt, _z, size, crc = struct.unpack_from('>4sHHII', raw, 0)
        if magic != MAGIC or fmt not in (FORMAT, FORMAT_KINDS, FORMAT_KNOBS) or size != len(raw) \
                or size < HEAD + SCOPE_REC * 6:
            return None
        if zlib.crc32(raw[:0x0c] + bytes(4) + raw[0x10:]) & 0xFFFFFFFF != crc:
            return None
        kinded = fmt in (FORMAT_KINDS, FORMAT_KNOBS)
        fid, lid, nc, nids, nf, npk, ncd, nfc, ncc, nfcc, nfx, ncv = struct.unpack_from('>II BB 6H BB', raw, 0x10)
        at = HEAD
        st = {'flashed_id': fid, 'id': lid, 'next_code': {'synth': nc, 'fx': nfx, 'cv': ncv} if kinded else nc,
              'scopes': {}}
        for s in store.SCOPES:
            fg, g, _z3, low = struct.unpack_from('>BBHI', raw, at)
            st['scopes'][s] = {'fgen': fg, 'gen': g, 'low': low}
            at += SCOPE_REC
        st['ids'] = list(struct.unpack_from('>%dI' % nids, raw, at))
        at += 4 * nids

        def names(at, lm, ls):
            m, s = raw[at:at + lm], raw[at + lm:at + lm + ls]
            if len(m) != lm or len(s) != ls or not lm or not ls:
                raise ValueError
            return m.decode('utf-8'), s.decode('utf-8'), at + -(-(lm + ls) // 4) * 4
        for k, n in (('fields', nf), ('parked', npk)):
            st[k] = []
            for _i in range(n):
                sn, bits, off, init, lm, ls, _z4 = struct.unpack_from('>BBHIBBH', raw, at)
                mod, sym, at = names(at + 12, lm, ls)
                if not 1 <= sn <= len(store.SCOPES) or not 1 <= bits <= store.MAX_BITS or init >= 1 << bits:
                    return None
                f = {'mod': mod, 'sym': sym, 'scope': store.SCOPES[sn - 1], 'offset': off, 'bits': bits}
                if init:
                    f['init'] = init
                st[k].append(f)
        for k, v, n in (('codes', 'code', ncd), ('freed_codes', 'code', nfc), ('ccs', 'cc', ncc),
                        ('freed_ccs', 'cc', nfcc)):
            st[k] = []
            for _i in range(n):
                c, lm, ls, kn = struct.unpack_from('>BBBB', raw, at)
                mod, sym, at = names(at + 4, lm, ls)
                e = {'mod': mod, 'sym': sym, v: c}
                if kinded and v == 'code':
                    if not 1 <= kn <= len(KINDS):
                        return None
                    e['kind'] = KINDS[kn - 1]
                st[k].append(e)
        if fmt == FORMAT_KNOBS:
            nk, nfk, nxt = struct.unpack_from('>HHI', raw, at)
            at += 8
            if nk > MAX_KNOB_RUNS or nfk > MAX_KNOB_RUNS:
                return None
            for k, n in (('knobs', nk), ('freed_knobs', nfk)):
                st[k] = []
                for _i in range(n):
                    first, count, lm, _z5 = struct.unpack_from('>IHBB', raw, at)
                    m = raw[at + 8:at + 8 + lm]
                    if len(m) != lm or not lm or not count or first + count - 1 > store.KNOB_TOP:
                        return None
                    st[k].append({'mod': m.decode('utf-8'), 'first': first, 'count': count})
                    at += 8 + -(-lm // 4) * 4
            if nxt > store.KNOB_TOP + 1:
                return None
            st['next_knob'] = nxt
        if at != size:
            return None
    except (struct.error, ValueError, UnicodeDecodeError):
        return None
    return st


def reserve(st):
    """The state (decode()'s) -> what the +Drive keeps, as a build's lockfile keeps it (store.py, "drive"):
    the live layout ID, each scope's low mark, and the codes and CCs its fields and parked fields hold; the
    codes by track kind ({kind: [code]}) for a state with codes per track kind (format 2)."""
    held = st['codes'] + st['freed_codes']
    if _kinded(st):
        codes = {k: sorted({c['code'] for c in held if c['kind'] == k}) for k in KINDS}
    else:
        codes = sorted({c['code'] for c in held})
    return {'id': '0x%08x' % st['id'], 'low': {s: st['scopes'][s]['low'] for s in store.SCOPES},
            'codes': codes, 'ccs': sorted({c['cc'] for c in st['ccs'] + st['freed_ccs']})}


def forget(st, mod):
    """FORGET DATA: drop a parked mod's fields, codes and CCs (a dict like st; st is not changed). Their bits
    stay below the low mark, given out again only by a repack. -> (the new state, whether it changed)."""
    out = {k: (dict(v) if isinstance(v, dict) else list(v) if isinstance(v, list) else v) for k, v in st.items()}
    out['scopes'] = {s: dict(v) for s, v in st['scopes'].items()}
    out['parked'] = [f for f in st['parked'] if f['mod'] != mod]
    out['freed_codes'] = [c for c in st['freed_codes'] if c['mod'] != mod]
    out['freed_ccs'] = [c for c in st['freed_ccs'] if c['mod'] != mod]
    changed = any(len(out[k]) != len(st[k]) for k in ('parked', 'freed_codes', 'freed_ccs'))
    return out, changed


# ---- the layout ID -----------------------------------------------------------------------------

def layout_id(st, base):
    """The live layout ID: the flashed one while the .store holds nothing and no generation moved;
    otherwise the first 4 bytes of a SHA-256 of ID_HASH, the flashed ID, each scope's live generation, every
    field and parked field (by scope, then offset: u8 scope, u8 bits, u16 offset, u32 init, then the mod id
    and the symbol, each a u8 length and its bytes) and every lock code and freed code (by code: u8 code, the
    two names; by kind, then code, each after its kind's number 1..3, where codes have kinds), other than 0.
    Parked or live is not hashed: a park changes no bit."""
    empty = not (st['fields'] or st['parked'] or st['codes'] or st['freed_codes'])
    if empty and all(st['scopes'][s]['gen'] == base.scopes[s][2] for s in store.SCOPES):
        return base.flashed_id
    h = bytearray(ID_HASH) + struct.pack('>I', base.flashed_id)
    h += bytes(st['scopes'][s]['gen'] for s in store.SCOPES)
    for f in sorted(st['fields'] + st['parked'], key=lambda f: (store.SCOPE_NUM[f['scope']], f['offset'])):
        m, s = _names(f['mod'], f['sym'])
        h += struct.pack('>BBHI', store.SCOPE_NUM[f['scope']], f['bits'], f['offset'], f.get('init', 0))
        h += bytes([len(m)]) + m + bytes([len(s)]) + s
    for c in sorted(st['codes'] + st['freed_codes'], key=_code_order):
        m, s = _names(c['mod'], c['sym'])
        if 'kind' in c:
            h += bytes([KINDS.index(c['kind']) + 1])
        h += bytes([c['code'], len(m)]) + m + bytes([len(s)]) + s
    return int.from_bytes(hashlib.sha256(bytes(h)).digest()[:4], 'big') or 1


# ---- the allocator -----------------------------------------------------------------------------

class Placed:
    """allocate()'s result: the new state and what the loader writes from it."""

    def __init__(self, base, st, notes, want):
        self.state, self.notes, self.core = st, notes, base.core
        self.id = st['id']
        anc, seen = [], {self.id}
        for a in st['ids'] + [base.flashed_id] + base.ancestors:
            if a and a not in seen:
                seen.add(a)
                anc.append(a)
        self.ancestors = anc[:store.MAX_ANCESTORS]
        self.generations = {s: st['scopes'][s]['gen'] for s in store.SCOPES}
        inited = [f for f in st['fields'] if f.get('init')]
        slot = {(f['mod'], f['sym']): base.inits + i + 1 for i, f in enumerate(inited)}

        def dsc(f, live=True):
            return (store.SCOPE_NUM[f['scope']] << 28 | f['bits'] << 22
                    | (slot.get((f['mod'], f['sym']), 0) if live else 0) << 16 | f['offset'])
        self.descriptors = {(f['mod'], f['sym']): dsc(f) for f in st['fields']}
        # parked fields: the core counts them as owned; no slot, as nothing reads their value
        self.parked = [((f['mod'], f['sym']), dsc(f, False)) for f in st['parked']]
        self.init_entries = [f['init'].to_bytes(4, 'big') for f in inited]
        self.handle_entries = [self.descriptors[(f['mod'], f['sym'])].to_bytes(4, 'big') for f in st['fields']
                               if (f['mod'], f['sym']) in want.handles]
        scope_of = {(f['mod'], f['sym']): f['scope'] for f in st['fields']}
        # the core's LFO destination fields the +Drive placed: the loader writes their descriptors into
        # core_lfo_dsts ([0] sound, [1] kit); a scope with none keeps the flashed build's
        self.lfo_dsts = {s: self.descriptors.get((base.core, _dst_sym(s))) for s in ('sound', 'kit')}
        self.lfo_dsts = {s: d for s, d in self.lfo_dsts.items() if d is not None}
        self.code_entries = []
        for c in st['codes']:
            k = (c['mod'], c['sym'])
            lfo = base.lfo_dst(scope_of[k]) or scope_of[k] in self.lfo_dsts
            flags = (store.LFO_FLAG if k in want.lfos and lfo else 0) \
                | (store.SMOOTH_OFF if k in want.rough else 0) | (store.ROUTE_FLAG if k in want.routes else 0) \
                | (store.CV_FLAG if c.get('kind') == 'cv' else 0)
            self.code_entries.append(self.descriptors[k].to_bytes(4, 'big') + (c['code'] | flags).to_bytes(4, 'big'))
        self.cc_entries = [self.descriptors[(c['mod'], c['sym'])].to_bytes(4, 'big') + c['cc'].to_bytes(4, 'big')
                           for c in st['ccs']]
        self.bytes = encode(st)

    def summary(self):
        """A JSON-ready summary (the vectors' and `--json`'s)."""
        return {'id': '0x%08x' % self.id, 'ancestors': ['0x%08x' % a for a in self.ancestors],
                'generations': self.generations,
                'fields': {'%s/%s' % k: '0x%08x' % d for k, d in sorted(self.descriptors.items())
                           if k[0] != self.core or k[1] not in DST_SYMS},
                'parked': ['%s/%s 0x%08x' % (k + (d,)) for k, d in self.parked],
                'lfo_dsts': {s: '0x%08x' % d for s, d in sorted(self.lfo_dsts.items())},
                'codes': [e.hex() for e in self.code_entries], 'ccs': [e.hex() for e in self.cc_entries],
                'inits': [e.hex() for e in self.init_entries], 'handles': [e.hex() for e in self.handle_entries],
                'notes': self.notes, 'store': self.bytes.hex()}


DST_SYMS = (store.LOCK_DST, store.FX_LOCK_DST)


def _dst_sym(scope):
    return store.LOCK_DST if scope == 'sound' else store.FX_LOCK_DST


def _bump(g):
    return store._bump(g)


def _held_ok(held, low, room, used):
    """Do a scope's held fields (live and parked) lie in the pool, at or above the low mark, apart?"""
    spans = sorted((f['offset'], f['offset'] + f['bits']) for f in held)
    return all(used <= a and b <= room and low <= a for a, b in spans) and \
        all(b[0] >= a[1] for a, b in zip(spans, spans[1:]))


def allocate(base, prev, want):
    """Place the +Drive storage of the mods in want (a Want that fits: fit() refused nothing) against the
    flashed build base, from the state prev (decode()'s, or None for a fresh placement).
    -> Placed. prev is not changed. Raises ModError for a set that does not fit."""
    if base.codes is None or base.ccs is None or base.flashed_id is None:
        raise ModError('placing +Drive storage needs the build\'s tables: give a built .syx or main OS image, '
                       'not the descriptor alone')
    short = fit(base, want)
    if short:
        m, what, by = short[0]
        raise ModError('%s does not fit: %s is %d short' % (m, what, by))
    notes = []
    if prev is not None and _kinded(prev) != bool(base.kinds):
        prev = None                             # the other format's file: a fresh placement, as the loader's
    st = fresh(base) if prev is None else decode(encode(prev))     # a copy, through the same checks
    if st is None:
        raise ModError('.store: a bad state')
    old_id = st['id']
    want_f = {}
    for mid, sym, scope, bits in want.decls + base.dst_fields(want.decls, want.lfos):
        if any(k[1] == sym for k in want_f):
            raise ModError('the storage field %s is declared by two mods' % sym)
        want_f[(mid, sym)] = (scope, bits, want.inits.get((mid, sym), 0))
    # what the flashed build changed under the +Drive: a scope it repacked has a new generation already,
    # one it grew into (or a core with less room, or a bad record) needs one
    gen_to, why = {}, {}
    for s in store.SCOPES:
        room, used, gen = base.scopes[s]
        sc = st['scopes'][s]
        held = [f for f in st['fields'] + st['parked'] if f['scope'] == s]
        if sc['fgen'] != gen:
            gen_to[s] = gen if sc['gen'] == sc['fgen'] else _bump(sc['gen'])
            why[s] = 'the flashed build repacked it'
        elif not _held_ok(held, sc['low'], room, used) or not used <= sc['low'] <= room:
            gen_to[s] = _bump(sc['gen'])
            why[s] = 'the flashed build took its bits'
        sc['fgen'] = gen
    kept, gone = [], []
    for f in st['fields']:
        if want_f.get((f['mod'], f['sym'])) == (f['scope'], f['bits'], f.get('init', 0)):
            kept.append(f)
        else:
            gone.append(f)
    have = {(f['mod'], f['sym']) for f in kept}
    added = []
    for mid, sym in sorted((k for k in want_f if k not in have), key=lambda k: (_b(k[0]), _b(k[1]))):
        scope, bits, init = want_f[(mid, sym)]
        f = {'mod': mid, 'sym': sym, 'scope': scope, 'offset': None, 'bits': bits}
        if init:
            f['init'] = init
        added.append(f)
    # a parked field back with the same scope, width and init takes its old place, with its values
    back = set()
    for f in added:
        same = [g for g in st['parked'] if (g['mod'], g['sym'], g['scope'], g['bits'], g.get('init', 0))
                == (f['mod'], f['sym'], f['scope'], f['bits'], f.get('init', 0))]
        if same and f['scope'] not in gen_to:
            f['offset'] = same[-1]['offset']
            st['parked'].remove(same[-1])
            back.add((f['mod'], f['sym']))
    # a field no chosen mod declares as it was is parked: its bits keep their values
    for f in gone:
        st['parked'] = [g for g in st['parked'] if (g['mod'], g['sym']) != (f['mod'], f['sym'])]
        st['parked'].append(dict(f))
    fields = kept + added
    repacked = set()

    def repack(s, gen):
        room = base.scopes[s][0]
        sc = st['scopes'][s]
        sc['gen'] = gen
        st['parked'] = [f for f in st['parked'] if f['scope'] != s]
        at = room
        for f in sorted((f for f in fields if f['scope'] == s), key=_key):
            at -= f['bits']
            f['offset'] = at
        sc['low'] = at
        repacked.add(s)
        if at < room or gen != base.scopes[s][2]:
            notes.append('+Drive %s storage repacked (generation %d), as %s: projects and sounds saved before '
                         'load it at defaults' % (s, gen, why[s]))
    for s in store.SCOPES:
        room, used, _gen = base.scopes[s]
        sc = st['scopes'][s]
        if sum(f['bits'] for f in fields if f['scope'] == s) > room - used:
            raise ModError('the +Drive mods need more %s storage than the build leaves' % s)
        if s in gen_to:
            repack(s, gen_to[s])
            continue
        pend = [f for f in added if f['scope'] == s and f['offset'] is None]
        if not pend:
            continue
        if sc['low'] - sum(f['bits'] for f in pend) >= used:
            for f in pend:
                sc['low'] -= f['bits']
                f['offset'] = sc['low']
        else:
            why[s] = 'its never-used bits ran out'
            repack(s, _bump(sc['gen']))
    back = {(f['mod'], f['sym']) for f in added if (f['mod'], f['sym']) in back and f['scope'] not in repacked}
    parked = {(f['mod'], f['sym']) for f in st['parked']}
    for f in gone:
        k = (f['mod'], f['sym'])
        w = want_f.get(k)
        if k in parked:
            notes.append('%s %s (%s): parked; its bits and values are kept for when it comes back'
                         % (f['mod'], f['sym'], f['scope']))
        else:
            notes.append('%s %s (%s): %s, its data is dropped'
                         % (f['mod'], f['sym'], f['scope'],
                            'parked, and its scope repacked' if w is None
                            else 'changed its init' if w[:2] == (f['scope'], f['bits']) else 'changed size'))
    for f in added:
        if (f['mod'], f['sym']) in back:
            notes.append('%s %s: back at bit %d of %s storage, with the values it had'
                         % (f['mod'], f['sym'], f['offset'], f['scope']))
        else:
            notes.append('%s %s: %d bits of %s storage at bit %d'
                         % (f['mod'], f['sym'], f['bits'], f['scope'], f['offset']))
    st['fields'] = sorted(fields, key=lambda f: (store.SCOPE_NUM[f['scope']], f['offset']))
    if base.kinds:
        _kind_codes(st, base, want, {(f['mod'], f['sym']) for f in kept}, back, notes)
    else:
        _codes(st, base, want.locks, {(f['mod'], f['sym']) for f in kept}, back, notes)
    _ccs(st, base, want.ccs, {(f['mod'], f['sym']) for f in fields if f['scope'] == 'kit'}, notes)
    scope_of = {(f['mod'], f['sym']): f['scope'] for f in fields}
    placed = {f['sym'] for f in fields if f['mod'] == base.core}
    for k in sorted(want.lfos, key=lambda k: (_b(k[0]), _b(k[1]))):
        if not base.lfo_dst(scope_of[k]) and _dst_sym(scope_of[k]) not in placed:
            notes.append('%s %s: not an LFO destination on this build, which places no %s'
                         % (k[0], k[1], store.LOCK_DST if scope_of[k] == 'sound' else store.FX_LOCK_DST))
    st['id'] = layout_id(st, base)
    if st['id'] != old_id:
        ids, seen = [], {st['id']}
        for a in [old_id] + st['ids']:
            if a not in seen:
                seen.add(a)
                ids.append(a)
        st['ids'] = ids[:MAX_IDS]
        notes.append('layout ID 0x%08x (from 0x%08x, whose projects it still reads)' % (st['id'], old_id))
    st['flashed_id'] = base.flashed_id
    return Placed(base, st, notes, want)


def _codes(st, base, want, kept, back, notes):
    """Each lockable field in want ({(mod, sym)}) its code, top down, past the build's codes (st changed in
    place). kept: the fields that kept their place; back: parked fields that came back to theirs."""
    flashed = base.code_set()
    top = max(flashed, default=base.first_code - 1)
    codes, freed = [], [dict(c) for c in st['freed_codes'] if c['code'] not in flashed]
    for c in st['freed_codes']:
        if c['code'] in flashed:
            notes.append('%s %s: lock code %d is the flashed build\'s now' % (c['mod'], c['sym'], c['code']))
    for c in st['codes']:
        k = (c['mod'], c['sym'])
        if c['code'] in flashed:
            notes.append('%s %s: lock code %d is the flashed build\'s now' % (k[0], k[1], c['code']))
        elif k in want and k in kept:
            codes.append(dict(c))
        else:
            freed.append(dict(c))
            notes.append('%s %s: lock code %d kept for it while it is out' % (k[0], k[1], c['code']))
    have = {(c['mod'], c['sym']) for c in codes}
    nxt = st['next_code']
    for k in sorted(set(want) - have, key=lambda k: (_b(k[0]), _b(k[1]))):
        same = [c for c in freed if (c['mod'], c['sym']) == k]
        if same and k in back:
            c = same[-1]
            freed.remove(c)
            notes.append('%s %s: back with lock code %d' % (k[0], k[1], c['code']))
        elif nxt > top:
            c = {'mod': k[0], 'sym': k[1], 'code': nxt}
            nxt -= 1
            notes.append('%s %s: lock code %d' % (k[0], k[1], c['code']))
        elif freed:                             # the never-used codes are gone: the one freed longest ago
            old = freed.pop(0)
            c = {'mod': k[0], 'sym': k[1], 'code': old['code']}
            notes.append('%s %s: lock code %d (was %s %s\'s)' % (k[0], k[1], c['code'], old['mod'], old['sym']))
        else:
            used = flashed | {c['code'] for c in codes}
            free = [x for x in range(base.last_code, base.first_code - 1, -1) if x not in used]
            if not free:
                raise ModError('the +Drive mods have more lockable parameters than the build leaves codes')
            c = {'mod': k[0], 'sym': k[1], 'code': free[0]}
            notes.append('%s %s: lock code %d' % (k[0], k[1], c['code']))
        freed = [d for d in freed if (d['mod'], d['sym']) != k]
        codes.append(c)
    st['codes'] = sorted(codes, key=lambda c: c['code'])
    st['freed_codes'] = freed
    st['next_code'] = max(nxt, base.first_code - 1)


def _kind_codes(st, base, want, kept, back, notes):
    """_codes per track kind (codes numbered per kind: the Analog Four MKII): each lockable field its code
    in its kind, top down, past the build's codes of that kind; a code freed in one kind is given again
    only in that kind; a field whose kind changed is a new parameter, its old code freed."""
    flashed = {kd: base.kind_codes(kd) for kd in base.kinds}
    top = {kd: max(flashed[kd], default=base.first_code - 1) for kd in base.kinds}

    def of(kd):
        return ' (%s)' % store.KIND_NAMES[kd][4:]
    codes, freed = [], [dict(c) for c in st['freed_codes'] if c['code'] not in flashed[c['kind']]]
    for c in st['freed_codes']:
        if c['code'] in flashed[c['kind']]:
            notes.append('%s %s: lock code %d%s is the flashed build\'s now' % (c['mod'], c['sym'], c['code'],
                                                                               of(c['kind'])))
    for c in st['codes']:
        k = (c['mod'], c['sym'])
        if c['code'] in flashed[c['kind']]:
            notes.append('%s %s: lock code %d%s is the flashed build\'s now' % (k[0], k[1], c['code'], of(c['kind'])))
        elif k in want.locks and k in kept and want.kind(k) == c['kind']:
            codes.append(dict(c))
        else:
            freed.append(dict(c))
            notes.append('%s %s: lock code %d%s kept for it while it is out' % (k[0], k[1], c['code'], of(c['kind'])))
    have = {(c['mod'], c['sym']) for c in codes}
    nxt = dict(st['next_code'])
    for k in sorted(set(want.locks) - have, key=lambda k: (_b(k[0]), _b(k[1]))):
        kd = want.kind(k)
        mine = [c for c in freed if c['kind'] == kd]
        same = [c for c in mine if (c['mod'], c['sym']) == k]
        if same and k in back:
            c = same[-1]
            freed.remove(c)
            notes.append('%s %s: back with lock code %d%s' % (k[0], k[1], c['code'], of(kd)))
        elif nxt[kd] > top[kd]:
            c = {'mod': k[0], 'sym': k[1], 'code': nxt[kd], 'kind': kd}
            nxt[kd] -= 1
            notes.append('%s %s: lock code %d%s' % (k[0], k[1], c['code'], of(kd)))
        elif mine:                              # the never-used codes are gone: the one freed longest ago
            old = mine[0]
            freed.remove(old)
            c = {'mod': k[0], 'sym': k[1], 'code': old['code'], 'kind': kd}
            notes.append('%s %s: lock code %d%s (was %s %s\'s)' % (k[0], k[1], c['code'], of(kd), old['mod'],
                                                                  old['sym']))
        else:
            used = flashed[kd] | {c['code'] for c in codes if c['kind'] == kd}
            free = [x for x in range(base.last_code, base.first_code - 1, -1) if x not in used]
            if not free:
                raise ModError('the +Drive mods have more lockable parameters than the build leaves codes on %s'
                               % store.KIND_NAMES[kd])
            c = {'mod': k[0], 'sym': k[1], 'code': free[0], 'kind': kd}
            notes.append('%s %s: lock code %d%s' % (k[0], k[1], c['code'], of(kd)))
        freed = [d for d in freed if (d['mod'], d['sym']) != k]
        codes.append(c)
    st['codes'] = sorted(codes, key=_code_order)
    st['freed_codes'] = freed
    st['next_code'] = {kd: max(nxt[kd], base.first_code - 1) for kd in base.kinds}


def _ccs(st, base, want, kits, notes):
    """Each field in want its CC from the device's pool, from its end, past the build's own (st changed in
    place); a kit field (the FX track's) none that stock maps on the FX channel."""
    flashed = base.cc_set()
    pool = [cc for cc in reversed(base.pool) if cc not in flashed]
    room = base.room.get(store.CCS_TABLE) or 0
    ccs, freed = [], [dict(c) for c in st['freed_ccs'] if c['cc'] not in flashed]
    for c in st['ccs']:
        k = (c['mod'], c['sym'])
        if c['cc'] in flashed:
            notes.append('%s %s: CC %d is the flashed build\'s now' % (k[0], k[1], c['cc']))
        elif k in want and not (k in kits and c['cc'] in base.fx_taken):
            ccs.append(dict(c))
        else:
            freed.append(dict(c))
            notes.append('%s %s: CC %d kept for it until the unused CCs run out' % (k[0], k[1], c['cc']))
    used = {c['cc'] for c in ccs} | {c['cc'] for c in freed}
    unused = [cc for cc in pool if cc not in used]
    have = {(c['mod'], c['sym']) for c in ccs}
    for k in sorted(set(want) - have, key=lambda k: (_b(k[0]), _b(k[1]))):
        ok = (lambda cc: cc not in base.fx_taken) if k in kits else (lambda cc: True)
        same = [c for c in freed if (c['mod'], c['sym']) == k and ok(c['cc'])]
        new = [cc for cc in unused if ok(cc)]
        old = [c for c in freed if ok(c['cc'])]
        if len(ccs) >= room:
            notes.append('%s %s: no CC (the build\'s table %s is full); it is on MIDI by its NRPN only'
                         % (k[0], k[1], store.CCS_TABLE))
            continue
        if same:
            c = same[-1]
            freed.remove(c)
            notes.append('%s %s: back with CC %d' % (k[0], k[1], c['cc']))
        elif new:
            unused.remove(new[0])
            c = {'mod': k[0], 'sym': k[1], 'cc': new[0]}
            notes.append('%s %s: CC %d' % (k[0], k[1], c['cc']))
        elif old:
            old = old[0]
            freed.remove(old)
            c = {'mod': k[0], 'sym': k[1], 'cc': old['cc']}
            notes.append('%s %s: CC %d (was %s %s\'s)' % (k[0], k[1], c['cc'], old['mod'], old['sym']))
        else:
            notes.append('%s %s: no CC left; it is on MIDI by its NRPN only' % k)
            continue
        ccs.append(c)
    st['ccs'] = sorted(ccs, key=lambda c: c['cc'])
    st['freed_ccs'] = freed
