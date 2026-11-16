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
import hashlib
import json
import os
import struct
import sys
import zlib

from . import container, desc, devices, drive_store as ds, link as linker, store as mstore
from .elemod import ModError, format_of, version_key
from .link import _align, by_id, entry_key, lay_out, put_reloc

MAX_MODS = 256                 # lines of /mods/active, and status records
ID_MAX = 28                    # a status record's id field (27 bytes and a NUL)
DETAIL = 32                    # a detail string (31 bytes and a NUL)
NAME_MAX = 128                 # a path below /mods in /mods/active (127 bytes, folders included)
RECORD = 32                    # a status record's bytes
# the boot linker's own room (dl.h DL_POOL_*, dlink.c pools()): each pool per started MB of the
# +Drive area (at least 1), the symbols past the flashed build's own names, each capped at
# POOL_CAP; past one a mod, or the whole set, is refused with LIMIT
POOL_PER_MB = {'arena': 12288, 'lists': 1024, 'syms': 1024, 'locals': 1024, 'contribs': 256,
               'edata': 2048, 'erelocs': 256, 'sites': 256, 'sdata': 4096, 'colls': 64}
NAME_N_MAX = 60                # a table's name is shorter, so NAME_n is a global name too
POOL_CAP = 0xFFF0
INIT = 'drive_init'            # each mod's own init: void drive_init(void); never a global name
SITE_MAIN_LO = 0x40000400      # dl.h DL_MAIN_LO: the Rytm MKII's main_load, where a site may start

# states and refusal codes (docs/FORMAT.md, "+Drive refusal codes"): the boot linker's numbers
NONE, LOADED, REFUSED, DROPPED = 0, 1, 2, 3
STATES = {NONE: 'none', LOADED: 'loaded', REFUSED: 'refused', DROPPED: 'dropped'}
CODES = {
    0: ('ok', 'loaded'),
    1: ('missing', 'the file is not in /mods'),
    2: ('changed', 'the file is not the one chosen (its CRC-32 differs)'),
    3: ('too-big', 'the file is larger than the room the boot reads it into'),
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
    20: ('count', 'more than 256 mods are chosen'),
    # TELOS on the Digitone mk1 (a build whose descriptor carries the stock shadow): sites and regions
    21: ('site', 'a site overlaps one of {detail}'),
    22: ('flashed', 'a site at {detail} overlaps what the flashed build writes'),
    24: ('stock', 'the bytes at {detail} are not stock\'s'),
    26: ('busy', 'no quiet moment to write the sites in'),
    27: ('region', 'a region overlaps one of {detail}'),
    28: ('outside', 'a region at {detail} is outside the device\'s free areas'),
    # +Drive storage and the boot-linked sites (a build whose descriptor carries the site map, no shadow)
    29: ('store', 'storage: {what} needs {need} of {have}'),
    30: ('clash', 'site 0x{detail}: {why}'),
    31: ('std', 'subscribes to {detail}, which the core does not map'),
}
# DL_R_CLASH's kind (its argument's high byte; the low byte is the other mod's line for SITE_MOD)
SITE_CORE, SITE_MOD, SITE_PRELINK, SITE_STOCK = 1, 2, 3, 4
CODE = {name: c for c, (name, _m) in CODES.items()}
BOOT_ONLY = ('missing', 'changed', 'busy')   # the host has the files it is given; busy is the unit's moment
SKIPS = {0: 'none', 1: 'key', 2: 'armed', 3: 'drive', 4: 'abi', 5: 'unstable', 6: 'count', 7: 'memory'}
SKIP = {v: k for k, v in SKIPS.items()}
NO_FIELDS = ('sites', 'collections', 'copied', 'storage', 'sharc')   # pass 1 refuses them, in order
# +Drive storage (docs/DRIVE.md, "+Drive storage"): a build keeps it with these, and core 0.14's
# generations (dlink.c store_base); a mod's fields go in the boot's room for them (drive_store.py)
STORE_TABLES = (mstore.IDS_TABLE, mstore.CODES_TABLE, mstore.CCS_TABLE, mstore.INITS_TABLE, mstore.HANDLES_TABLE)
STORE_GENS = 'core_store_gens'
MAX_FIELDS = 512                # dstore.h DS_MAX_FIELDS: the fields of every file pass 1 reads
FIELD_KEYS = ('sym', 'scope', 'bits', 'init', 'lock', 'cc', 'lfo', 'smooth', 'mod', 'handle')
FIELD_FLAGS = (('lock', 1), ('cc', 2), ('lfo', 4), ('mod', 16), ('handle', 32))

SITE_FIELDS = ('sites', 'copied')     # ... except these on a build with a site map (ABI 1.2)


def message(code, detail='', arg=0):
    """A status's line, for a person. A storage refusal's detail is "WHAT NEED HAVE"."""
    if CODES[code][0] == 'store':
        w = (str(detail).split() + ['?', '?', '?'])[:3]
        return CODES[code][1].format(what=w[0], need=w[1], have=w[2])
    if code == CODE['clash']:
        addr, _sp, owner = str(detail).partition(' ')
        kind = arg >> 8
        why = {SITE_CORE: 'taken by %s' % (owner or 'the build'),
               SITE_MOD: 'taken by another +Drive mod (line %d)' % ((arg & 0xFF) + 1),
               SITE_PRELINK: 'in a pre-link range', SITE_STOCK: 'not stock bytes'}.get(kind, 'refused')
        return CODES[code][1].format(detail=addr, why=why)
    return CODES[code][1].format(detail=detail, arg=arg)


def file_crc(raw):
    """A file's CRC-32 in /mods/active: zlib's (the boot linker's dl_crc32). The data container's
    footer hash is another one (container.crc)."""
    return zlib.crc32(bytes(raw)) & 0xFFFFFFFF


# ---- payload skip (skip.c's twin) ---------------------------------------------------------------

STAGE_STEP = 64                         # skip.h DL_STAGE_STEP (the twin stages whole files at once)
_P_NONE, _P_SECT, _P_PAY, _P_PARTS, _P_PART = range(5)
_HEXDIGITS = frozenset(b'0123456789abcdefABCDEF')


def stage(raw):
    """A file as a loader with F_PAYLOAD_SKIP stages it (mods/drive-loader-a4mk2/skip.c,
    dl_stage_file): the hex of a skippable .payload (one ["hex", "..."] part, "len" and a 64-digit
    "sha256" before "parts") left out, so the staged JSON has "" there. -> (staged bytes, None or
    {'off': the hex's offset in the file, 'len': bytes, 'sha256': hex digits}). Followed a byte at a
    time as the loader does, so a file the loader would stage differently stages differently here."""
    raw = bytes(raw)
    depth, types = 0, []
    instr = esc = iskey = keyexp = False
    path, key, skey, elem, nparts = _P_NONE, None, None, 0, 0
    hex_first = innum = have_len = have_sha = done = False
    cap, num, plen, sha = bytearray(), 0, 0, None
    out = bytearray()
    i, n = 0, len(raw)
    pay = None
    while i < n:
        c = raw[i]
        i += 1
        out.append(c)
        if instr:
            if esc:
                esc = False
            elif c == 0x5c:
                esc = True
            elif c == 0x22:
                instr = False
                w = bytes(cap) if len(cap) <= 64 else None
                if iskey:
                    key = None
                    if depth == 1 and w == b'sections':
                        key = 'sections'
                    elif path == _P_SECT and depth == 2 and w == b'.payload':
                        key = 'payload'
                    elif path == _P_PAY and depth == 3 and w in (b'len', b'sha256', b'parts'):
                        key = w.decode()
                elif skey == 'sha256':
                    have_sha = w is not None and len(w) == 64 and all(x in _HEXDIGITS for x in w)
                    sha = w.decode().lower() if have_sha else None
                elif path == _P_PART and depth == 5 and elem == 0:
                    hex_first = w == b'hex'
                continue
            if len(cap) <= 64:
                cap.append(c)
            continue
        if innum:
            if 0x30 <= c <= 0x39:
                num = 0xFFFFFFFF if num > 0x7FFFFFFF // 10 else num * 10 + (c - 0x30)
                continue
            innum = False
            have_len = num <= 0x7FFFFFFF
            plen = num
        if c in b' \t\n\r':
            continue
        top = types[depth - 1] if 0 < depth <= 8 else None
        if c == 0x3a:
            keyexp = False
            continue
        if c == 0x2c:
            keyexp = top == 0x7b
            if path == _P_PART and depth == 5:
                elem += 1
            continue
        if c in b'}]':
            if path == _P_PART and depth == 5:
                path = _P_PARTS
            elif path == _P_PARTS and depth == 4:
                path = _P_PAY
            elif path == _P_PAY and depth == 3:
                path = _P_SECT
            elif path == _P_SECT and depth == 2:
                path = _P_NONE
            if depth:
                depth -= 1
                del types[depth:]
            keyexp, key = False, None
            continue
        if c == 0x22 and keyexp and top == 0x7b:
            instr, iskey, cap = True, True, bytearray()
            continue
        k, key = key, None
        if c == 0x22:
            instr, iskey, cap, skey = True, False, bytearray(), k
            if path == _P_PART and depth == 5 and elem == 1 and hex_first and nparts == 1 \
                    and have_len and have_sha and not done:
                done, skey, cap = True, None, bytearray(65)
                pay = {'off': i, 'len': plen, 'sha256': sha}
                i = min(n, i + 2 * plen)
            continue
        if c in b'{[':
            if c == 0x7b and k == 'sections' and depth == 1:
                path = _P_SECT
            elif c == 0x7b and k == 'payload':
                path, have_len, have_sha, nparts = _P_PAY, False, False, 0
            elif c == 0x5b and k == 'parts' and path == _P_PAY:
                path = _P_PARTS
            elif c == 0x5b and path == _P_PARTS and depth == 4:
                path, nparts, elem, hex_first = _P_PART, nparts + 1, 0, False
            depth = min(depth + 1, 0xFF)
            if depth <= 8:
                types.append(c)
            keyexp = c == 0x7b
            continue
        if k == 'len' and 0x30 <= c <= 0x39:
            innum, num = True, c - 0x30
    return bytes(out), pay


def staged(build, raw):
    """A file's bytes as the build's loader stages them: without a skippable payload's hex when the
    descriptor has F_PAYLOAD_SKIP, else as they are."""
    if raw is None or not getattr(build, 'payload_skip', False):
        return raw
    return stage(raw)[0]


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
        # the core's std map (F_STD_MAP, CLAIM_STD "<event> <table>"): a std mod's subscriptions' tables
        self.std_map = bool(d.get('flags', 0) & desc.F_STD_MAP)
        self.std = dict(nm.split(' ', 1) for kind, _mi, nm in d['claims'] if kind == desc.CLAIM_STD)
        # TELOS (a descriptor with the stock shadow, ABI 1.2): sites and regions are taken, checked
        # as the boot linker checks them (DL_SITES, DL_REGIONS)
        self.shadow = d.get('shadow') or []
        self.telos = bool(self.shadow)
        self.regions = []
        for kind, mi, nm in d['claims']:
            if kind == getattr(desc, 'CLAIM_REGION', 2):
                lo, hi = nm.split(' ')[:2]
                self.regions.append((int(lo, 0), int(hi, 0), d['modules'][mi]['name']))
        self.ram_end, self.lo, self.hi, self.staging, self.staging_size = d['area']
        # top-staging (dlink.c dl_stage_top; the descriptor's F_STAGE_TOP, ABI 1.3): the boot reads
        # each file into the area itself, so a file may be as large as the area, and the layout
        # places the mods by file length with room for each file above the code before it
        self.stage_top = bool(d.get('flags', 0) & desc.F_STAGE_TOP)
        # payload skip (skip.c; F_PAYLOAD_SKIP): the boot stages each file without a skippable
        # .payload's hex, so lengths, CRCs and the layout are the staged file's (stage())
        self.payload_skip = bool(d.get('flags', 0) & desc.F_PAYLOAD_SKIP)
        # abs16 (F_ABS16, the A4's dlink.c): the relocation kinds the boot linker takes, with their sizes
        self.rsize = dict({'abs32': 4, 'pc32': 4, 'pc16': 2},
                          **({'abs16': 2} if d.get('flags', 0) & desc.F_ABS16 else {}))
        # +Drive collections (dlink.c DL_COLLECTIONS; F_COLLECTIONS): a +Drive mod may declare tables,
        # which other +Drive mods add to; each is placed after the .bss, sized to its entries
        self.collections = bool(d.get('flags', 0) & desc.F_COLLECTIONS)
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
        self.pools = pools(self)
        exports = {nm: a for m in self.modules for nm, a in m['exports']}
        scopes = d['storage']['scopes']
        self.store = (all(t in self.table for t in STORE_TABLES) and STORE_GENS in exports
                      and [x[0] for x in scopes] == list(mstore.SCOPES)
                      and self.tables[self.table[mstore.IDS_TABLE]]['count'] >= 1)
        self.gens_at = exports.get(STORE_GENS)
        self.dsts_at = exports.get(ds.LFO_DSTS)          # core-ar2 0.14's core_lfo_dsts, None without
        dev = next((x for x in devices.devices() if x.key == self.device_key), None)
        self.main_load = dev.main_load if dev else None
        self.areas = list(dev.areas.values()) if dev else []   # a region must be inside one (TELOS)
        # the main OS a +Drive site may change: from the device's main_load to the boot copier
        # (the site map's kind 4); 0 on a descriptor without a site map (ABI 1.0 and 1.1), and on a
        # TELOS build, whose sites are TELOS's (a loader built with DL_SITES)
        self.site_map = d.get('sites', []) if tuple(d.get('abi', (1, 0)))[1] >= 2 else []
        self.prelink = d.get('prelink', [])
        boots = [lo for lo, _hi, k, _mi in self.site_map if k == desc.SITE_BOOT]
        self.main_lo = SITE_MAIN_LO
        self.main_hi = min(boots) if boots and min(boots) > SITE_MAIN_LO and not self.telos else 0
        # contributions' claims (F_CLAIMS, the A4's dlink.c): taken where the site map can check them
        self.take_claims = bool(d.get('flags', 0) & desc.F_CLAIMS) and bool(self.main_hi)
        self.applied = []                       # sites a boot applied: [(addr, stock bytes)]
        self.image, self._ram, self.desc_at = image, None, None
        if image is not None:
            self._ram = self._locate(image, at)
        self.base = ds.Base.from_build(self) if self.store else None

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

    def stock_bytes(self, addr, n):
        """Stock's bytes at addr: the running main OS with the shadow (what the flashed build wrote
        there, as stock had it) over it, as TELOS's stock_copy. None without an image."""
        b = self.stock(addr, n)
        if b is None:
            return None
        b = bytearray(b)
        for sa, data in self.shadow:
            lo, hi = max(sa, addr), min(sa + len(data), addr + n)
            if lo < hi:
                b[lo - addr:hi - addr] = data[lo - sa:hi - sa]
        return bytes(b)

    def label(self):
        core = self.modules[0]
        return '%s %s, %s %s, build 0x%08x' % (self.device_key, self.os, core['name'], core['version'],
                                                self.crc)


def pools(build):
    """The boot linker's pools for this build, as dlink.c sizes them: {name: size}, and 'merge'
    and 'mdata', the biggest table +Drive entries can go to (its entries and bytes)."""
    mb = max(1, (build.hi - build.lo + 0xFFFFF) >> 20) if build.hi > build.lo else 1
    out = {k: min(POOL_CAP, v * mb + (build.nsyms if k == 'syms' else 0)) for k, v in POOL_PER_MB.items()}
    more = [t for t in build.tables if t['flags'] & desc.F_MORE and not t['flags'] & desc.F_DSP]
    out['merge'] = max([t['capacity'] + 1 for t in more] or [0])
    out['mdata'] = max([(t['capacity'] + 1) * t['entry'] for t in more] or [0])
    if build.collections:                       # a +Drive table's entries all fit the entry bytes' pool
        out['merge'] = max(out['merge'], out['edata'] // 4 + 1)
    return out


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
        self.tables = []                        # the tables it declares (+Drive collections): [(name, entry)]
        self.fields = []                        # its +Drive storage fields (drive_store's documents)
        self.sites, self.regions = [], []        # TELOS: [lo, hi) each
        self.drive_sites = []                   # [(lo, hi, is_site)]: its sites, then its copied blocks
        self.drive_claims = []                  # [(lo, hi)]: its contributions' claims (F_CLAIMS)
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


def _skipped_payload(d):
    """A .payload the stage skipped, as dlink.c's skipped_payload reads it: "len" and a 64-digit
    "sha256" before "parts", and "parts" one ["hex", ""] part."""
    if not isinstance(d, dict):
        return False
    have = set()
    for k, v in d.items():
        if k == 'align':
            if type(v) is not int or not 1 <= v <= 4096 or v & (v - 1):
                return False
        elif k == 'len':
            if type(v) is not int or not 0 <= v <= 0x7FFFFFFF:
                return False
            have.add(k)
        elif k == 'sha256':
            if not isinstance(v, str) or len(v.encode('utf-8')) != 64:
                return False
            have.add(k)
        elif k == 'parts':
            if have != {'len', 'sha256'} or v != [['hex', '']]:
                return False
            have.add(k)
    return 'parts' in have


def _payload_relocs(v):
    """Relocations that point into a skipped .payload ("sec:.payload"), as dlink.c's
    payload_relocs finds them."""
    return isinstance(v, list) and any(isinstance(r, list) and 'sec:.payload' in r for r in v)


def _is_c_name(s):
    return bool(s) and (s[0].isascii() and (s[0].isalpha() or s[0] == '_')) and \
        all(ch.isascii() and (ch.isalnum() or ch == '_') for ch in s)


def _storage(v, fields, exports, state, build, keep):
    """A mod's "storage", as the boot linker reads it (dlink.c storage()): each field into fields,
    its symbol a global name (in exports). -> None, or (code, note)."""
    if _empty(v):
        return None
    if not isinstance(v, list):
        return 'format', 'storage'
    kinded = bool(mstore.kinds(build.device_key)) if mstore.supports(build.device_key) else False
    for f in v:
        if not isinstance(f, dict):
            return 'format', 'storage'
        sym = scope = bits = None
        init, has_init, has_smooth, flags, cv = 0, False, False, 0, False
        on = {}
        for k, x in f.items():                  # in the file's order, as the boot linker reads it
            if k == 'sym':
                if not isinstance(x, str):
                    return 'format', 'storage'
                if not keep(x):
                    return 'limit', 'storage'
                sym = x
            elif k == 'scope':
                if x not in mstore.SCOPES:
                    return 'format', 'storage'
                scope = mstore.SCOPES.index(x) + 1
            elif k == 'bits':
                if type(x) is not int or not -0x80000000 <= x <= 0x7FFFFFFF:
                    return 'format', 'storage'
                bits = x
            elif k == 'init':
                if type(x) is not int or not 0 <= x <= 0xFFFFFFFF:
                    return 'format', 'storage'
                init, has_init = x, True
            elif k in FIELD_KEYS:
                if type(x) is not bool:
                    return 'format', 'storage'
                on[k] = x
                if k == 'smooth':
                    has_smooth = True
            elif k == 'track' and kinded:       # the CV track's (codes per track kind: the Analog Four MKII)
                if x != 'cv':
                    return 'format', 'storage'
                cv = True
            else:
                return 'format', 'storage'      # "track" too where the device has no CV track
        if sym is None or scope is None or bits is None or not 1 <= bits <= mstore.MAX_BITS \
                or not _is_c_name(sym) or len(sym.encode('utf-8')) > ds.NAME_MAX:
            return 'format', 'storage'
        lock = on.get('lock', False)
        # on a device with kinds (the Analog Four MKII, store.check_flags): no "lfo" (its LFOs take "mod"
        # fields), a kit field can be "mod" (the FX and CV tracks have routes), "track": "cv" a lockable kit field's
        if ((on.get('cc') or on.get('lfo') or on.get('mod') or has_smooth) and not lock) \
                or (on.get('handle') and (lock or init)) or (lock and scope > 2) \
                or (on.get('mod') and scope == 2 and not kinded) or (kinded and on.get('lfo')) \
                or (cv and not (lock and scope == 2)) or (has_init and bits < 32 and init >> bits):
            return 'format', 'storage'
        if any(g['sym'] == sym for g in fields):
            return 'format', 'storage'
        if state['fields'] + len(fields) >= MAX_FIELDS or state['syms'] + len(exports) >= build.pools['syms']:
            return 'limit', 'storage'
        doc = {'sym': sym, 'scope': mstore.SCOPES[scope - 1], 'bits': bits}
        for k, _bit in FIELD_FLAGS:
            if on.get(k):
                doc[k] = True
        if on.get('smooth') is False:
            doc['smooth'] = False
        if cv:
            doc['track'] = 'cv'
        if init:
            doc['init'] = init
        fields.append(doc)
        exports.append(sym)
    return None


def _collections(v, tables, exports, state, build, keep):
    """A mod's "collections", as the boot linker reads them (dlink.c collections()): each table's
    name and entry size into tables; NAME and NAME_n are global names. -> None, or (code, detail,
    arg, note)."""
    if _empty(v):
        return None
    if not isinstance(v, dict):
        return 'format', '', 0, 'collections'
    for name, d in v.items():                   # in the file's order, as the boot linker reads it
        if not isinstance(d, dict) or not 0 < len(name.encode('utf-8')) < NAME_N_MAX:
            return 'format', '', 0, 'collections'
        entry = None
        for k, x in d.items():
            if k == 'entry':
                if type(x) is not int or not 0 < x <= 0xFFFF or x % 4:
                    return 'format', '', 0, 'collections'
                entry = x
            elif k == 'space':                  # a DSP table: never on the +Drive
                return 'feature', 'collections'
        if entry is None:
            return 'format', '', 0, 'collections'
        if state['colls'] + len(tables) >= build.pools['colls'] \
                or state['syms'] + len(exports) + 2 * len(tables) + 2 > build.pools['syms'] \
                or not keep(name) or not keep(name + '_n'):
            return 'limit', '', 0, 'collections'
        tables.append((name, entry))
    return None


def _declarer(order, name):
    """The living mod that declares the +Drive table name: the first in id order, or None."""
    return next((o for o in order if o.alive and any(t == name for t, _e in o.tables)), None)


def _names(c):
    """The global names a mod defines: its exports (storage fields too), then each table's NAME
    and NAME_n."""
    return c.exports + [x for t, _e in c.tables for x in (t, t + '_n')]


def pass1(build, c, state):
    """Pass 1, one file: what the boot linker's scan tells from the file and the build alone, in
    its order. `state` counts the boot linker's shared room: symbols and contributions, which a
    refused file gives back, and the names' arena and lists, which it does not. -> c.alive."""
    def keep(text):                             # a name into the arena: its bytes and a NUL
        n = len(text.encode('utf-8')) + 1
        arena = build.pools['arena']
        if state['arena'] >= arena or state['arena'] + n > arena:
            return False
        state['arena'] += n
        return True

    def keep_list(v):
        if v is None:
            return True
        if not isinstance(v, list):
            return False
        for x in v:
            if not isinstance(x, str) or not keep(x) or state['lists'] >= build.pools['lists']:
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
        if f == 'sites' and build.telos:
            continue
        if not (f == 'storage' and build.store) and not (build.main_hi and f in SITE_FIELDS) \
                and not (f == 'collections' and build.collections) and not _empty(doc.get(f)):
            return c.refuse('feature', f)
    secs = doc.get('sections')
    if not isinstance(secs, dict):
        return c.refuse('format', note='its sections')
    for s in secs:
        if s == '.payload' and build.payload_skip:
            if not _skipped_payload(secs[s]):       # bytes the stage didn't skip
                return c.refuse('feature', s)
        elif s not in ('.run', '.bss'):
            return c.refuse('feature', s)
    res = doc.get('resources')
    if res is not None and not isinstance(res, dict):
        return c.refuse('format', note='its resources')
    for k, v in (res or {}).items():            # in the file's order, as the boot linker reads it
        if k == 'regions' and not _empty(v):
            if not build.telos:
                return c.refuse('feature', 'regions')
            try:
                c.regions = [(int(str(g['lo']), 0), int(str(g['hi']), 0)) for g in v]
            except (TypeError, ValueError, KeyError):
                return c.refuse('format', note='resources.regions')
            if any(hi <= lo for lo, hi in c.regions):
                return c.refuse('format', note='resources.regions')
            for lo, hi in c.regions:                 # inside a free area, as a flashed mod's (dl_region_ok)
                if not any(a <= lo and hi <= z for a, z in build.areas):
                    return c.refuse('outside', '0x%08x' % lo)
        if k == 'names' and not keep_list(v):
            return c.refuse('limit', note='resources.names')
        if k == 'core' and v is not None and not (isinstance(v, str) and keep(v)):
            return c.refuse('format', note='resources.core')
    if build.telos and not _empty(doc.get('sites')):      # pass 1's checks of each site (dlink.c, scan_sites)
        import hashlib
        for x in doc['sites']:
            try:
                a, n = int(str(x['addr']), 0), int(x['len'])
                want = bytes.fromhex(x['stock_sha256'])
                new = bytes.fromhex(x['new'])
            except (TypeError, ValueError, KeyError):
                return c.refuse('format', note='a site')
            if a <= 0 or not 0 < n <= 256 or len(new) != n or len(want) != 32:
                return c.refuse('format', note='a site')
            where = '0x%08x' % a
            if any(a < sa + len(dt) and sa < a + n for sa, dt in build.shadow):
                return c.refuse('flashed', where)
            if build.image is not None:             # the descriptor alone (a .build file): the unit checks
                got = build.stock_bytes(a, n)
                if got is None or hashlib.sha256(got).digest() != want:
                    return c.refuse('stock', where)
            c.sites.append((a, a + n))
    for k in ('imports', 'weak', 'requires', 'conflicts'):
        if not keep_list(doc.get(k)):
            return c.refuse('limit', note=k)
    if build.payload_skip and _payload_relocs(doc.get('relocs')):   # the payload is never in RAM
        return c.refuse('feature', 'relocs into .payload')
    run_len = _section_len(secs.get('.run'))
    for r in doc.get('relocs') or []:
        if not (isinstance(r, list) and len(r) == 5) or r[0] != '.run' \
                or not isinstance(r[2], str) or r[2] not in build.rsize or not isinstance(r[1], int) \
                or r[1] < 0 or r[1] + build.rsize[r[2]] > run_len:
            return c.refuse('format', note='a relocation %r' % (r,))
    events = doc.get('events')                  # "std1": its "std" contributions are standard events
    if build.std_map and events is not None and events not in linker.STD_SETS:
        return c.refuse('format', note='"events" %r' % (events,))
    contribs = 0
    late = build.std_map or build.collections   # the A4's dlink.c finds the table after the keys
    for x in doc.get('contribute') or []:
        if not isinstance(x, dict):
            return c.refuse('format', note='a contribution')
        to = n = std = None
        order = 50
        for k, v in x.items():                  # in the file's order, as the boot linker reads it
            if k == 'to':
                to = v
                if not late:                    # without the std map (drive-loader-ar2): checked here
                    t = build.table.get(v) if isinstance(v, str) else None
                    if t is None or not build.tables[t]['flags'] & desc.F_MORE \
                            or build.tables[t]['flags'] & desc.F_DSP:
                        return c.refuse('table', v)
            elif k == 'std' and build.std_map:
                if v is not None and v is not True:
                    return c.refuse('format', note='"std": %r' % (v,))
                std = v
            elif k == 'order':
                if type(v) is not int:
                    return c.refuse('format', note='an order of %r' % (v,))
                order = v
            elif k == 'data':
                try:
                    n = len(bytes.fromhex(v))
                except (TypeError, ValueError):
                    return c.refuse('format', note='an entry\'s data')
            elif k == 'claims' and not _empty(v) and not build.take_claims:   # else read after the sites
                return c.refuse('feature', 'claims')
            elif k == 'relocs' and build.payload_skip and _payload_relocs(v):
                return c.refuse('feature', 'relocs into .payload')
        if std and events is None:
            return c.refuse('format', note='a standard event\'s subscription without "events"')
        if std and isinstance(to, str):         # the core's std map (the descriptor's CLAIM_STD)
            if to not in build.std:
                return c.refuse('std', to)
            to = build.std[to]
        if to is not None and late:             # with it, once the subscription is known
            t = build.table.get(to) if isinstance(to, str) else None
            if t is None and isinstance(to, str) and build.collections:
                if not keep(to):                # a +Drive table's: found in the check rounds
                    return c.refuse('limit', note='names')
            elif t is None or not build.tables[t]['flags'] & desc.F_MORE \
                    or build.tables[t]['flags'] & desc.F_DSP:
                return c.refuse('table', to)
        if to is None or n is None:
            return c.refuse('format', note='a contribution without "to" or "data"')
        if not desc.ORDER_MIN <= order < desc.LINKER_ORDER:
            return c.refuse('format', note='an order of %r' % (order,))
        if not n or (to in build.table and n % build.tables[build.table[to]]['entry']):
            return c.refuse('table', to)
        if state['contribs'] + contribs >= build.pools['contribs']:
            return c.refuse('limit', note='contributions')
        contribs += 1
    syms = doc.get('symbols') or {}
    exports = []
    for x in doc.get('exports') or []:
        if not isinstance(x, str) or x not in syms or not _symbol(syms[x]):
            return c.refuse('format', note='exports %s' % x)
        if x == INIT:
            continue
        if state['syms'] + len(exports) >= build.pools['syms'] or not keep(x):
            return c.refuse('limit', note='symbols')
        exports.append(x)
    fields = []
    if build.store:
        why = _storage(doc.get('storage'), fields, exports, state, build, keep)
        if why:
            return c.refuse(why[0], note=why[1])
    tables = []
    if build.collections:
        why = _collections(doc.get('collections'), tables, exports, state, build, keep)
        if why:
            return c.refuse(*why)
        doc = dict(doc, collections={})         # read here; the host's Mod2 below does not size them
    sites = _scan_sites(build, c, doc, state)
    if sites is None:
        return False
    claims = _scan_claims(build, c, doc, state, len(sites))
    if claims is None:
        return False
    if '.payload' in secs and build.payload_skip:   # its bytes stay on the +Drive: nothing places it
        doc = dict(doc, sections={k: v for k, v in secs.items() if k != '.payload'},
                   symbols={k: v for k, v in syms.items() if not (isinstance(v, list) and v[:1] == ['.payload'])})
    try:                                        # the host's own reading of the file, a last check
        m = linker.Mod2(doc, c.name, c.raw)
    except ModError as e:
        return c.refuse('format', note=str(e))
    for x in m.contribute:                      # a standard event's subscription: the table it maps to
        if 'event' in x and build.std_map:
            x['to'] = build.std[x['event']]
    c.mod, c.exports, c.fields, c.drive_sites, c.drive_claims = m, exports, fields, sites, claims
    c.tables = tables
    state['syms'] += len(exports) + 2 * len(tables)
    state['colls'] += len(tables)
    state['fields'] += len(fields)
    state['contribs'] += contribs
    state['sites'] += len(sites) + len(claims)
    state['sdata'] += sum(hi - lo for lo, hi, is_site in sites if is_site)
    c.alive = True
    return True


def _num(v):
    """A number as the boot linker's js_int reads it: a JSON integer, or a string Python's int(v, 0)
    reads, within 32 bits signed. -> int, or None."""
    if type(v) is int:
        n = v
    elif isinstance(v, str):
        try:
            n = int(v, 0)
        except ValueError:
            return None
    else:
        return None
    return n if -0x80000000 <= n < 0x80000000 else None


def _overlap(a, b, lo, hi):
    return a < hi and lo < b


def _site_detail(addr, owner=''):
    """A refused site's detail, as the boot linker writes it: 8 hex digits, then the flashed owner
    (at most 22 bytes of its name)."""
    d = '%08x' % addr
    if owner:
        d += ' ' + owner.encode('utf-8')[:22].decode('utf-8', 'ignore')
    return d


def _stock_bytes(build, addr, n):
    """A site's bytes in the running main OS before any +Drive site, or None without an image."""
    b = build.stock(addr, n)
    if b is None:
        return None
    b = bytearray(b)
    for at, orig in build.applied:
        lo, hi = max(at, addr), min(at + len(orig), addr + n)
        if lo < hi:
            b[lo - addr:hi - addr] = orig[lo - at:hi - at]
    return bytes(b)


def _scan_sites(build, c, doc, state):
    """Pass 1's part for "sites" and "copied", after the exports, as dlink.c's scan_sites: each
    site's and copied block's form inside the main OS, the loader's room for them, then each site
    against the flashed build's site map, the pre-link ranges and its stock bytes (not checked
    without a built image), and each copied block against the flashed sites. -> [(lo, hi, is_site)]
    (sites first, then copied blocks), or None after refusing c."""
    out, sdata = [], 0
    if not build.main_hi:
        return out
    for copied, key in ((False, 'sites'), (True, 'copied')):
        v = doc.get(key)
        if _empty(v):
            continue
        if not isinstance(v, list):
            return c.refuse('format', note=key) or None
        for x in v:
            if not isinstance(x, dict):
                return c.refuse('format', note=key) or None
            if not copied:
                addr, n = _num(x.get('addr')), _num(x.get('len'))
                try:
                    new = bytes.fromhex(x['new']) if isinstance(x.get('new'), str) else None
                except ValueError:
                    new = None
                sha = x.get('stock_sha256')
                ok = (addr is not None and n is not None and new is not None and x.get('kind') in ('code', 'data')
                      and isinstance(sha, str) and len(sha) == 64 and all(ch in '0123456789abcdefABCDEF' for ch in sha)
                      and n > 0 and len(new) == n and build.main_lo <= addr < build.main_hi
                      and n <= build.main_hi - addr)
                if 'new' in x and not isinstance(x['new'], str):
                    ok = False
                rel = x.get('relocs')
                if ok and 'relocs' in x:
                    if not isinstance(rel, list):
                        ok = False
                    for r in rel if isinstance(rel, list) else []:
                        size = build.rsize.get(r[1]) if isinstance(r, list) and len(r) == 4 \
                            and isinstance(r[1], str) else None
                        off = _num(r[0]) if size else None
                        if size is None or off is None or off < 0 or off + size > n or not isinstance(r[2], str) \
                                or _num(r[3]) is None:
                            ok = False
                            break
                if not ok:
                    return c.refuse('format', note='a site %r' % (x.get('addr'),)) or None
                lo, hi = addr, addr + n
            else:
                lo, hi, to = _num(x.get('lo')), _num(x.get('hi')), _num(x.get('to'))
                if lo is None or hi is None or to is None or not lo < hi or lo < build.main_lo \
                        or hi > build.main_hi:
                    return c.refuse('format', note='a copied block') or None
            if state['sites'] + len(out) >= build.pools['sites'] or \
                    (not copied and state['sdata'] + sdata + (hi - lo) > build.pools['sdata']):
                return c.refuse('limit', note='sites') or None
            out.append((lo, hi, not copied))
            if not copied:
                sdata += hi - lo
    names = [m['name'] for m in build.modules]
    for lo, hi, is_site in out:
        if not is_site:
            continue
        for a, b, kind, mi in build.site_map:
            if _overlap(lo, hi, a, b):
                owner = names[mi] if kind in (desc.SITE_SITE, desc.SITE_COPIED, desc.SITE_CLAIM) \
                    and mi < len(names) else ''
                return c.refuse('clash', _site_detail(lo, owner), SITE_CORE << 8) or None
        for a, b, _kind in build.prelink:
            if _overlap(lo, hi, a, b):
                return c.refuse('clash', _site_detail(lo), SITE_PRELINK << 8) or None
    sites = [x for x in doc.get('sites') or []] if build.main_hi else []
    for (lo, hi, is_site), x in zip([o for o in out if o[2]], sites):
        now = _stock_bytes(build, lo, hi - lo)
        if now is not None and hashlib.sha256(now).hexdigest() != x['stock_sha256'].lower():
            return c.refuse('clash', _site_detail(lo), SITE_STOCK << 8) or None
    for lo, hi, is_site in out:
        if is_site:
            continue
        for a, b, kind, mi in build.site_map:
            if kind == desc.SITE_SITE and _overlap(lo, hi, a, b):
                return c.refuse('clash', _site_detail(lo, names[mi] if mi < len(names) else ''),
                                SITE_CORE << 8) or None
    return out


def _scan_claims(build, c, doc, state, nsites):
    """Pass 1's part for the contributions' "claims" (F_CLAIMS), after the sites, as dlink.c's
    scan_claims: each claim's form ([lo, hi], lo < hi, inside the main OS), the loader's room (its
    sites' pool), then each against the flashed build's site map (every kind, the flashed claims
    too) and the pre-link ranges. Nothing is written for a claim. -> [(lo, hi)], or None after
    refusing c."""
    out = []
    if not build.take_claims:
        return out
    for x in doc.get('contribute') or []:
        v = x.get('claims')
        if _empty(v):
            continue
        if not isinstance(v, list):
            return c.refuse('format', note='a contribution\'s claims') or None
        for r in v:
            lo, hi = (_num(r[0]), _num(r[1])) if isinstance(r, list) and len(r) == 2 else (None, None)
            if lo is None or hi is None or lo < build.main_lo or hi > build.main_hi or not lo < hi:
                return c.refuse('format', note='a claim %r' % (r,)) or None
            if state['sites'] + nsites + len(out) >= build.pools['sites']:
                return c.refuse('limit', note='sites') or None
            out.append((lo, hi))
    names = [m['name'] for m in build.modules]
    for lo, hi in out:
        for a, b, kind, mi in build.site_map:
            if _overlap(lo, hi, a, b):
                owner = names[mi] if kind in (desc.SITE_SITE, desc.SITE_COPIED, desc.SITE_CLAIM) \
                    and mi < len(names) else ''
                return c.refuse('clash', _site_detail(lo, owner), SITE_CORE << 8) or None
        for a, b, _kind in build.prelink:
            if _overlap(lo, hi, a, b):
                return c.refuse('clash', _site_detail(lo), SITE_PRELINK << 8) or None
    return out


def _clashes(mine, theirs):
    """Do two overlapping ranges of different mods clash: a site (1) with anything, a claim (2) with a
    claim; a copied block (0) with a copied block or a claim does not."""
    return mine == 1 or theirs == 1 or (mine == 2 and theirs == 2)


def _ranges(c):
    """A mod's ranges in dlink.c's order: its sites, its copied blocks, then its claims, with their kind."""
    return [(lo, hi, 1 if s else 0) for lo, hi, s in c.drive_sites] + [(lo, hi, 2) for lo, hi in c.drive_claims]


def _resolvable(build, alive, x):
    """Is an import a global name: the build's, or a living +Drive mod's export (its own too)."""
    return x in build.globals or any(x in _names(o) for o in alive)


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
        if why is None and c.sites:                 # sites: the later of two that overlap goes
            w = next((o.id for o in earlier for a, b in c.sites for x, y in o.sites if a < y and x < b), None)
            if w is not None:
                why = ('site', w)
        if why is None:
            x = next((x for x in _names(c) if x in build.globals
                      or any(x in _names(o) for o in earlier)), None)
            if x is not None:
                why = ('export', x)
        if why is None:
            x = next((n for n in m.names if n in build.claims or any(n in o.mod.names for o in earlier)), None)
            if x is not None:
                why = ('claim', x)
        if why is None and c.regions:               # regions: the flashed build's, then earlier mods'
            w = next((nm for a, b in c.regions for x, y, nm in build.regions if a < y and x < b), None)
            if w is None:
                w = next((o.id for o in earlier for a, b in c.regions for x, y in o.regions if a < y and x < b), None)
            if w is not None:
                why = ('region', w)
        if why is None:
            x = next((x for x in m.imports if not _resolvable(build, alive, x) and x not in m.weak), None)
            if x is not None:
                why = ('import', x)
        if why is None and build.collections:   # +Drive tables: a living mod declares each, of that entry
            for x in m.contribute:
                if x['to'] not in build.table:
                    o = _declarer(order, x['to'])
                    if o is None or len(x['data']) % dict(o.tables)[x['to']]:
                        why = ('table', x['to'])
                        break
        if why is None:                         # sites and claims against earlier living mods'
            for lo, hi, mine in _ranges(c):     # sites, copied blocks and claims (_clashes)
                o = next((o for o in earlier if any(_clashes(mine, theirs) and _overlap(lo, hi, a, b)
                                                    for a, b, theirs in _ranges(o))), None)
                if o is not None:
                    why = ('clash', _site_detail(lo), SITE_MOD << 8 | o.line)
                    break
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


def _want(order):
    """The living mods' storage, as drive_store takes it."""
    return ds.Want([{'id': c.id, 'storage': c.fields} for c in order if c.alive])


def _fit(build, order):
    """The room (drive_store.fit), one refusal, as the boot linker makes it: the first short resource,
    and the last living mod in id order that uses it. -> 1 when it refused one."""
    if not build.store:
        return 0
    w = _want(order)
    if not w.decls:
        return 0
    for what, n, have, uses in ds._needs(build.base, w):
        if n <= have:
            continue
        users = {d[0] for d in w.decls if uses((d[0], d[1]))}
        last = [c for c in order if c.alive and c.id in users]
        if not last:
            continue
        last[-1].refuse('store', '%s %d %d' % (what, n, max(have, 0)))
        return 1
    return 0


def _settle(build, order):
    """Check rounds and the room, until neither refuses a mod."""
    while True:
        while _round(build, order):
            pass
        if not _fit(build, order):
            return


def _place(build, order, prev):
    """The living mods' fields placed from the .store bytes prev (dlink.c place_store). -> Placed."""
    st = ds.decode(prev) if prev is not None else None
    return ds.allocate(build.base, st, _want(order))


def _store_tables(build, p):
    """What the boot writes from a placement: each loader-only table's bytes (flashed entries, the
    +Drive ones, zeros to its end) and core_store_gens' 6 bytes. -> ({name: table}, gens). The core's LFO
    destination fields the +Drive placed go into core_lfo_dsts too (Placed.lfo_dsts)."""
    out = {}
    adds = {mstore.CODES_TABLE: p.code_entries, mstore.CCS_TABLE: p.cc_entries,
            mstore.INITS_TABLE: p.init_entries, mstore.HANDLES_TABLE: p.handle_entries}
    for name in STORE_TABLES:
        t = build.tables[build.table[name]]
        e, cnt, cap = t['entry'], t['count'], t['capacity']
        if name == mstore.IDS_TABLE:
            ids = [p.id] + p.ancestors[:cap - 1]
            raw, rows = b''.join(struct.pack('>I', i) for i in ids), len(ids)
            entries = [('drive-loader', desc.LINKER_ORDER)] * rows
        else:
            new = adds[name][:cap - cnt]
            flashed = build.ram(t['address'], cnt * e)
            raw = (flashed if flashed is not None else bytes(cnt * e)) + b''.join(new)
            entries = [(build.flashed[mi], o) for o, mi in t['entries']] + \
                [('drive-loader', desc.LINKER_ORDER)] * len(new)
        raw += bytes(e * (cap + 1) - len(raw))
        out[name] = {'address': t['address'], 'entry': e, 'capacity': cap,
                     'bytes': raw if build.image is not None else None, 'entries': entries}
    return out, bytes(p.generations[s] for s in mstore.SCOPES)


def _stage_at(build, c):
    """With the stage at the top: where pass 2 reads c's file (dlink.c stage_at)."""
    n = len(c.raw)
    return build.lo if n > build.hi - build.lo else (build.hi - n) & ~3


def placement(build, order):
    """The living mods in placement order (dlink.c placement): id order, or with the stage at the
    top, the longest file first, ties in id order."""
    live = [c for c in order if c.alive]
    return sorted(live, key=lambda c: -len(c.raw)) if build.stage_top else live


def _drive_tables(build, order, at):
    """The living mods' tables (+Drive collections) from at, in id order and each mod's order:
    its living entries and a zero entry. -> ({name: {'address', 'entry', 'count', 'owner'}}, end)."""
    out = {}
    for c in order:
        if not c.alive:
            continue
        for name, e in c.tables:
            n = sum(len(x['data']) // e for o in order if o.alive for x in o.mod.contribute
                    if x['to'] == name and x['to'] not in build.table)
            out[name] = {'address': at, 'entry': e, 'count': n, 'owner': c}
            at += (n + 1) * e
    return out, at


def _layout(build, order):
    """In placement order from the area's start: every .run at its alignment, then every .bss, then
    the +Drive tables; with the stage at the top, each file above its own .run's end. -> (base, end,
    the +Drive tables), or None after refusing the last living mod in id order for room (the KB the
    worst point is over)."""
    placed = placement(build, order)
    base, at, over = {}, build.lo, 0
    for c in placed:
        at = lay_out([c.mod], '.run', at, base)
        if build.stage_top:
            over = max(over, at - _stage_at(build, c))
    end = _align(lay_out([c.mod for c in placed], '.bss', _align(at, 4), base), 4)
    dtab, end = _drive_tables(build, order, end)
    over = max(over, end - build.hi)
    if over > 0:
        last = [c for c in order if c.alive][-1]
        last.refuse('room', arg=(over + 1023) // 1024)
        return None
    return base, end, dtab


def _globals(build, order, base, dtab):
    """Name -> a function giving its value: the build's names first (NAME_n the count after the
    merge), then the living +Drive mods' exports and tables, in id order."""
    alive = [c for c in order if c.alive]
    added = {}
    for c in alive:
        for x in c.mod.contribute:
            if x['to'] in build.table:
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
        mine = {f['sym'] for f in c.fields}     # storage fields: their descriptors, once placed
        for x in c.exports:
            if x not in glob and x not in mine:
                sec, off = c.mod.symbols[x]
                v = off if sec == 'abs' else base.get((c.id, sec), 0) + off
                glob[x] = (lambda v=v: v)
        for name, _e in c.tables:
            t = dtab[name]
            glob.setdefault(name, lambda v=t['address']: v)
            glob.setdefault(name + '_n', lambda v=t['count']: v)
    return glob


def _pass2(build, c, base, glob, edata):
    """One living mod's .run, relocated, and its entries' bytes and relocations. -> ((run bytes,
    [(contribution, bytes, [(off, type, S + A)])]), None), or (None, (code, detail))."""
    m = c.mod
    own = {}
    if sum(1 for sec, _off in m.symbols.values() if sec in ('abs', '.run', '.bss')) > build.pools['locals']:
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
                b = build.stock_bytes(p[1], p[2])
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
        if edata[0] + len(x['data']) > build.pools['edata']:
            return None, ('limit', '')
        edata[0] += len(x['data'])
        rel = []
        for off, typ, tgt, add in x['relocs']:
            if edata[1] >= build.pools['erelocs']:
                return None, ('limit', '')
            if typ not in build.rsize:          # dlink.c refuses it here, in pass 2
                return None, ('format', '')
            v = value(tgt)
            if v is None:
                return None, ('import', tgt.split(':', 1)[-1])
            edata[1] += 1
            rel.append((off, typ, v + add))
        ents.append((x, bytes(x['data']), rel))
    sites = []                                  # each site's new bytes, relocated at its own address
    for s in m.sites:
        new = bytearray(s['new'])
        for off, typ, tgt, add in s['relocs']:
            v = value(tgt)
            if v is None:
                return None, ('import', tgt.split(':', 1)[-1])
            try:
                put_reloc(new, off, typ, v + add, s['addr'] + off, 'site 0x%08x' % s['addr'])
            except ModError:
                return None, ('range', '')
        sites.append((s['addr'], bytes(new)))
    return (bytes(run), ents, sites), None


def _bind(build, live, placed, dtab):
    """Each table that gets an entry: flashed and +Drive entries sorted by (order, id, place),
    entry relocations at their final address, then zero entries to capacity + 1; and each +Drive
    table (dtab), its entries so, and its zero entry. -> {name: {...}}, or None past the loader's
    room (then the whole set is refused)."""
    names = build.flashed
    out = {}
    for name, d in dtab.items():
        e = d['entry']
        rows = []
        for c, x, data, rel in ((c, x, data, rel) for c in live for x, data, rel in placed[c.id][1]
                                if x['to'] == name):
            for k in range(len(data) // e):
                rows.append((entry_key(x['order'], c.id.encode('utf-8'), x['i'] << 8 | k), c.id, x['order'],
                             data[k * e:(k + 1) * e],
                             [(o - k * e, typ, v) for o, typ, v in rel if k * e <= o < (k + 1) * e]))
        if len(rows) > build.pools['merge']:
            return None
        rows.sort(key=lambda row: row[0])
        raw = bytearray()
        for i, row in enumerate(rows):
            b = bytearray(row[3])
            for o, typ, v in row[4]:
                try:
                    put_reloc(b, o, typ, v, d['address'] + i * e + o, 'an entry of %s' % name)
                except ModError:
                    return None
            raw += b
        raw += bytes(e)
        out[name] = {'address': d['address'], 'entry': e, 'capacity': d['count'], 'bytes': bytes(raw),
                     'entries': [(row[1], row[2]) for row in rows], 'drive': d['owner'].id}
    for t in build.tables:
        e, cnt = t['entry'], t['count']
        new = [(c, x, data, rel) for c in live for x, data, rel in placed[c.id][1] if x['to'] == t['name']]
        if not new:
            continue
        if (cnt + 1) * e > build.pools['mdata']:
            return None
        flashed = build.ram(t['address'], cnt * e)
        rows = []
        for i, (o, mi) in enumerate(t['entries']):
            nm = names[mi]
            rows.append((entry_key(o, nm.encode('utf-8'), i), nm, o,
                         None if flashed is None else flashed[i * e:(i + 1) * e], []))
        for c, x, data, rel in new:
            for k in range(len(data) // e):
                rows.append((entry_key(x['order'], c.id.encode('utf-8'), x['i'] << 8 | k), c.id, x['order'],
                             data[k * e:(k + 1) * e],
                             [(o - k * e, typ, v) for o, typ, v in rel if k * e <= o < (k + 1) * e]))
        if len(rows) > build.pools['merge']:
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


def check(build, files, deep=False, store=None):
    """Pass 1, the check rounds and the layout; with `deep`, pass 2 and the merge too (link()).
    `files`: [(file name, bytes, or None for a missing file)] in /mods/active's order. -> Result:
    .choices (each with its state), .live (the mods that load, in id order), .base, .end, .skip.
    `store`: /mods/.store's bytes as the boot reads them (None: none); a link gives .placed, the
    +Drive storage's placement (drive_store.Placed, None on a build without +Drive storage)."""
    r = Result()
    r.build, r.skip = build, 'none'
    choices = [Choice(i, n, staged(build, raw)) for i, (n, raw) in enumerate(files)]
    r.choices, r.live, r.base, r.end, r.area, r.tables, r.inits, r.glob = \
        choices, [], {}, build.lo, b'', {}, [], {}
    r.placed, r.gens = None, None
    r.drive_tables = {}
    r.sites = []
    if len(choices) > MAX_MODS:                 # the loader reads none of them
        r.skip = 'count'
        r.choices = choices[:MAX_MODS]
        for c in r.choices:
            c.refuse('count', state=NONE)
        return r
    state = {'syms': build.nsyms, 'contribs': 0, 'arena': 1, 'lists': 0, 'fields': 0, 'colls': 0,   # the arena's 0 is ""
             'sites': 0, 'sdata': 0}
    for c in choices:
        if c.raw is None:
            c.refuse('missing', state=DROPPED)
        elif build.stage_top and len(c.raw) > build.hi - build.lo:
            c.refuse('too-big')
        elif not build.stage_top and build.staging_size and len(c.raw) > build.staging_size:
            c.refuse('too-big')
        else:
            pass1(build, c, state)
    order = by_id(c for c in choices if c.alive)          # stable: of one id, the earlier line first
    lay = placed = glob = None
    for _ in range(4 * MAX_MODS):
        _settle(build, order)
        lay = _layout(build, order)
        if lay is None:
            continue
        if not deep:
            break
        if build.store:
            r.placed = _place(build, order, store)
        glob = _globals(build, order, lay[0], lay[2])
        if r.placed is not None:
            for c in order:
                if c.alive:
                    for f in c.fields:
                        glob[f['sym']] = (lambda v=r.placed.descriptors[(c.id, f['sym'])]: v)
        placed, edata, again = {}, [0, 0], False   # entry bytes, entry relocations
        for c in placement(build, order):          # in address order, as the boot reads them
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
    merged = _bind(build, live, placed, lay[2]) if lay is not None and deep else {}
    if lay is None or merged is None:           # never settled, or past the loader's room
        r.skip = 'unstable'
        for c in live:
            c.refuse('limit')
        if deep and build.store:                # nothing loads: every field parks
            r.placed = _place(build, order, store)
            st_tables, r.gens = _store_tables(build, r.placed)
            r.tables = st_tables
        return r
    base, end, dtab = lay
    r.live, r.base, r.end = live, base, end if live else build.lo
    r.drive_tables = {n: {k: v for k, v in t.items() if k != 'owner'} for n, t in dtab.items()}
    if deep:
        area = bytearray(r.end - build.lo)
        for c in live:
            run = placed[c.id][0]
            if run:
                o = base[(c.id, '.run')] - build.lo
                area[o:o + len(run)] = run
        for name in dtab:                       # +Drive tables are in the area: their bytes there too
            o = merged[name]['address'] - build.lo
            area[o:o + len(merged[name]['bytes'])] = merged[name]['bytes']
        r.area, r.tables = bytes(area), merged
        if r.placed is not None:
            st_tables, r.gens = _store_tables(build, r.placed)
            r.tables.update(st_tables)
        r.glob = {x: glob[x]() for c in live for x in c.exports}
        r.sites = [(c.id, c.line, a, new) for c in live for a, new in placed[c.id][2]]
    for c in live:
        c.state, c.code = LOADED, 0
        sec = c.mod.symbols.get(INIT, ('', 0))[0]
        if sec in ('.run', '.bss'):
            r.inits.append((c.id, base[(c.id, sec)] + c.mod.symbols[INIT][1]))
    return r


def link(build, files, store=None):
    """Pass 1 to the merge. -> Result, as check()'s, and .area (bytes from the area's start to
    .end), .tables ({name: {'address', 'entry', 'capacity', 'bytes' (None without an image),
    'entries': [(module, order)]}}), .inits ([(mod id, address)] in id order), .glob, .sites
    ([(mod id, its line, address, new bytes)] in id order: what the boot writes over the main OS)."""
    return check(build, files, deep=True, store=store)


def apply_sites(build, r):
    """After a link: the main OS as the unit runs it, the sites written and their stock bytes kept,
    so a later check of the build (the MODS page's) reads through them as the loader does. A build
    without an image keeps only the sites."""
    build.applied = []
    for _id, _line, a, new in r.sites:
        build.applied.append((a, build.stock(a, len(new)) or bytes(len(new))))
        if build.image is not None:
            build.image = bytearray(build.image)
            o = a - build.main_load
            build.image[o:o + len(new)] = new


def records(r):
    """The status records and detail strings, one each per line of the set (at most MAX_MODS), as
    the boot linker keeps them."""
    n = 0 if r.skip == 'count' else min(len(r.choices), MAX_MODS)   # past the limit: none
    st, dt = bytearray(n * RECORD), bytearray(n * DETAIL)
    for c in r.choices[:n]:
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
        if r.placed is not None:
            p = r.placed
            out['store'] = {'id': '0x%08x' % p.id, 'ancestors': ['0x%08x' % a for a in p.ancestors],
                            'generations': p.generations, 'fields': len(p.state['fields']),
                            'parked': len(p.state['parked']), 'sha256': _sha(p.bytes),
                            'gens_at': '0x%08x' % b.gens_at, 'notes': p.notes,
                            'lfo_dsts': {s: '0x%08x' % d for s, d in sorted(p.lfo_dsts.items())}}
        out['sites'] = [{'mod': i, 'line': ln, 'addr': '0x%08x' % a, 'len': len(new), 'sha256': _sha(new)}
                        for i, ln, a, new in r.sites]
    return out


def _sha(b):
    import hashlib
    return hashlib.sha256(bytes(b)).hexdigest()


def write_layout(r, out_dir):
    """`link --device-layout`'s files: area.bin, tables/<name>.bin (from a built image),
    sites/<address>.bin (each site's patched bytes), status.bin, detail.bin, status.txt and summary.json. -> the paths written, in order."""
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
    for _i, _ln, a, new in r.sites:
        put(os.path.join('sites', '%08x.bin' % a), new)
    st, dt = records(r)
    if r.placed is not None:
        put('store.bin', r.placed.bytes)        # /mods/.store as the boot writes it
        put('store_gens.bin', r.gens)           # core_store_gens
    put('status.bin', st)
    put('detail.bin', dt)
    put('status.txt', status_text(r))
    put('summary.json', json.dumps(summary(r, True), indent=1, sort_keys=True) + '\n')
    return paths


# ---- /mods/active --------------------------------------------------------------------------------

def active_text(entries, build=None):
    """`/mods/active` for [(file name, bytes)]: one line each, the name and its CRC-32 (8 hex
    digits): the CRC of the file as the build's loader stages it (staged())."""
    for n, _raw in entries:
        if not n or len(n.encode('utf-8')) >= NAME_MAX or n != n.strip() or n.startswith('#') \
                or any(ch in n for ch in '\n\r'):
            raise ModError('%r cannot be a line of /mods/active' % n)
    return ''.join('%s %08x\n' % (n, file_crc(staged(build, raw))) for n, raw in entries)


def stage_of(status):
    """`/mods/.status`'s text -> the most bytes one upload or read takes on that boot, or None for
    a loader without the line: the Rytm loader's `stage:` line (its live stage), or the A4 MKII
    loader's `room:` line (`room: N bytes in one run, M free; ...`: the largest free run of its
    sector store, which an upload goes straight into)."""
    for ln in status.split('\n'):
        w = ln.split()
        if len(w) >= 3 and w[0] in ('stage:', 'room:') and w[1].isdigit() and w[2] == 'bytes':
            return int(w[1])
    return None


def upload_room(build, status):
    """The most bytes one upload or read takes on the unit now: the `stage:` line of its .status,
    else what the build says (the staging buffer, which with the stage at the area's top is only
    the floor, so it never allows more than the unit takes)."""
    n = stage_of(status) if status is not None else None
    return n if n is not None else build.staging_size or None


def parse_active(text):
    """`/mods/active` -> [(name, crc)], as the boot linker reads it (blank and # lines skipped, a
    line without a CRC skipped, the name before the last run of spaces). None past MAX_MODS lines."""
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
    p.add_argument('--store', help='/mods/.store as the unit has it (+Drive storage); store.bin is the new one')
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
        prev = None
        if a.store:
            with open(a.store, 'rb') as fh:
                prev = fh.read()
        r = link(build, files, prev)
    except (OSError, ModError) as e:
        print('REFUSED: %s' % e)
        return 2
    print_result(r)
    for p in write_layout(r, a.out):
        print('WROTE %s' % p)
    return 0 if all(c.state == LOADED for c in r.choices) else 1


if __name__ == '__main__':
    sys.exit(main())
