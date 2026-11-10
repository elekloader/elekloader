# SPDX-License-Identifier: GPL-2.0-or-later
"""The linker: format-2 mods -> one patched main OS (docs/FORMAT.md).

A format-2 mod is a relocatable object in JSON:
- its sections: .boot (the core mod only), .run, .fast, .bss and .payload (data the
  boot copier places with .run, after the mods' code; the core's table core_payloads
  lists each mod's: its id, address and length);
- the symbols it defines and exports, and what it imports (some weakly);
- its relocations;
- sites in the image, with relocations of their own;
- the tables it declares, and the entries it adds to other mods' tables;
- its resources.

The linker checks the mods, lays them out in the device's areas, resolves
symbols, builds the tables, relocates, and enforces the budgets and the
copied-block rules.

A relocation target is one of:
- "sym:NAME": this mod's own symbol, else another mod's export, else, if
  the import is weak, core_zero;
- "sec:NAME": this mod's section;
- "abs": zero, so the addend is the value.

On a device with DSP code in its main OS (the profile's `dsp_payloads`), a mod may also
carry DSP56300 code, `.dsp.<payload>` sections of 24-bit little-endian words, and declare
DSP tables (a collection with "space": "dsp.<payload>"). They are placed in the P words of
that payload a mod frees by claiming the area's name (the profile's `dsp_areas`); a symbol
there is a DSP word address, and a `dsp24` relocation writes one as three little-endian bytes.

On a device with knob ids for mods (the profile's `param_ids`), a mod may declare `"knobs": n`:
the linker gives it a run of n ids from the profile's first mod id up (store.knobs, kept in the
lockfile), and its own symbol `__knobs` is the run's first id. A relocation to `__knobs` is an
abs32 or an `abs16` (two big-endian bytes, for the 16-bit knob lists of const tables) with an
addend of 0..n-1.
"""
import struct

from . import dsp, sharc, store, syx
from .elemod import (ModError, sha, _int, _hex, common_checks, insn_check, overlaps, summarize,
                    parse_needs_core, parse_parts, parse_resources, parse_sites, parts_bytes,
                    resolve_target, format_of, version_key,
                    COMMON, sharc_site)

FORMAT2 = 2
SECTIONS = ('.boot', '.run', '.fast', '.bss', '.payload')
RTYPES = ('abs32', 'abs16', 'pc32', 'pc16', 'dsp24')
RSIZE = {'abs32': 4, 'abs16': 2, 'pc32': 4, 'pc16': 2, 'dsp24': 3}
KNOBS = '__knobs'                 # a mod's first knob id (its "knobs")
DSP = '.dsp.'                     # a DSP section: '.dsp.' + its payload's tag


def _dsp_tag(dev, sec):
    """The payload tag of a DSP section name, or None."""
    if sec.startswith(DSP) and sec[len(DSP):] in dev.dsp_payloads:
        return sec[len(DSP):]
    return None
FAST_ENTRY = 16                   # a .fast copy entry: src, dst, len, 0
PAYLOADS = 'core_payloads'        # the core's table of the mods' payloads: id, address, length
PAYLOAD_ENTRY = 12
MAX_KNOBS = 1024                  # a mod's "knobs": at most this many ids
MAX_ALIGN = 4096                  # a section's alignment: a power of two up to this
LINKER_SYMS = {'__run_load', '__run_start', '__run_words', '__bss_start', '__bss_end',
               '__bss_words', '__run_hash'}
HASH_SEED, HASH_MUL = 5381, 33    # __run_hash: h = seed; h = h * 33 + word (mod 2^32), per word


def _align(v, a):
    return (v + a - 1) // a * a


def payload_id(mod_id):
    """A mod's payload id in core_payloads: the FNV-1a hash (32 bits) of its id's UTF-8 bytes."""
    h = 0x811C9DC5
    for b in str(mod_id).encode('utf-8'):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def run_hash(ddr):
    """The hash of the RAM image a core copies at boot (`__run_hash`): a rolling x33 over its
    32-bit big-endian words, from the seed 5381. A core hashes the copy it made and halts on a
    mismatch (core-ot's `.boot`). Each word changes the result: the multiplier is odd, so a
    single wrong word never cancels; the seed makes leading zero words count too."""
    h = HASH_SEED
    for (w,) in struct.iter_unpack('>I', bytes(ddr)):
        h = (h * HASH_MUL + w) & 0xFFFFFFFF
    return h


class Mod2:
    """A loaded, validated format-2 mod."""

    def __init__(self, doc, name='<mod>', raw=None):
        self.doc, self.name = doc, name
        self.sha256 = sha(raw) if raw is not None else None
        if format_of(doc) != FORMAT2:
            raise ModError('%s: not a format-2 .elemod file' % name)
        extra = set(doc) - COMMON - {'sections', 'symbols', 'exports', 'imports', 'weak',
                                     'relocs', 'collections', 'contribute', 'copied', 'storage', 'sharc',
                                     'sharc_raw', 'knobs'}
        if extra:
            raise ModError('%s: unknown fields %s' % (name, sorted(extra)))
        for k in ('id', 'version', 'target', 'sections'):
            if k not in doc:
                raise ModError('%s: no "%s"' % (name, k))
        self.id, self.version = str(doc['id']), str(doc['version'])
        self.dev, self.rel = resolve_target(doc, name)
        self.sections = {}
        for sec, d in doc['sections'].items():
            tag = _dsp_tag(self.dev, sec)
            if sec not in SECTIONS and tag is None:
                raise ModError('%s: unknown section %s' % (name, sec))
            if tag is not None:                 # DSP words: no alignment, whole words
                parts = parse_parts(d.get('parts', []), name + ' ' + sec, self.dev, self.rel)
                n = sum(len(p[1]) if p[0] == 'hex' else p[2] for p in parts)
                if n != _int(d.get('len', n), name) or n % 3:
                    raise ModError('%s %s: %d bytes, not whole 24-bit DSP words' % (name, sec, n))
                self.sections[sec] = {'align': 1, 'parts': parts, 'len': n}
                continue
            align = _int(d.get('align', 4), name)
            if align < 1 or align & (align - 1) or align > MAX_ALIGN:
                raise ModError('%s %s: alignment %d (a power of two up to %d)'
                               % (name, sec, align, MAX_ALIGN))
            if sec == '.bss':
                self.sections[sec] = {'align': align, 'size': _int(d['size'], name)}
                continue
            parts = parse_parts(d.get('parts', []), name + ' ' + sec, self.dev, self.rel)
            n = sum(len(p[1]) if p[0] == 'hex' else p[2] for p in parts)
            if n != _int(d.get('len', n), name):
                raise ModError('%s %s: parts make %d bytes, not %s' % (name, sec, n, d.get('len')))
            self.sections[sec] = {'align': align, 'parts': parts, 'len': n}
        if 'sharc' not in doc:
            self.sharc = None
        elif isinstance(doc['sharc'], dict) and doc['sharc'].get('abi') == sharc.KERNEL_ABI:
            self.sharc = sharc.kernel_payload(doc['sharc'], self.rel.sharc)
        else:
            self.sharc = sharc.module_payload(doc['sharc'], self.rel.sharc, '.boot' in self.sections)
        self.sharc_raw = (sharc.raw_payload(doc['sharc_raw'], self.rel.sharc)
                          if 'sharc_raw' in doc else None)
        self.symbols = {}
        for nm, v in doc.get('symbols', {}).items():
            if not isinstance(v, list) or len(v) != 2:
                raise ModError('%s: symbol %s' % (name, nm))
            if v[0] != 'abs' and v[0] not in self.sections:
                raise ModError('%s: symbol %s in a section it lacks (%s)' % (name, nm, v[0]))
            self.symbols[nm] = (v[0], _int(v[1], name))
            if _dsp_tag(self.dev, v[0]) and self.symbols[nm][1] % 3:
                raise ModError('%s: DSP symbol %s is not at a whole word' % (name, nm))
        if KNOBS in self.symbols:
            raise ModError('%s: %s is the linker\'s (its first knob id), not a symbol of a mod' % (name, KNOBS))
        self.knobs = doc.get('knobs', 0)
        if type(self.knobs) is not int or not 0 <= self.knobs <= MAX_KNOBS:
            raise ModError('%s: "knobs" is a count of knob ids, 0..%d' % (name, MAX_KNOBS))
        self.exports = [str(x) for x in doc.get('exports', [])]
        for x in self.exports:
            if x not in self.symbols:
                raise ModError('%s: exports %s, which it does not define' % (name, x))
        self.imports = [str(x) for x in doc.get('imports', [])]
        self.weak = set(str(x) for x in doc.get('weak', []))
        if not self.weak <= set(self.imports):
            raise ModError('%s: a weak name it does not import' % name)
        self.relocs = []
        for r in doc.get('relocs', []):
            sec, off, typ, tgt, add = r
            off = _int(off, name)
            self._reloc_ok(sec, off, typ, tgt)
            self.relocs.append((sec, off, typ, tgt, _int(add, name)))
        self.sites = parse_sites(doc, name, self.dev, self.rel, relocs=True)
        self.collections, self.dsp_tables = {}, {}
        for c, d in doc.get('collections', {}).items():
            e = _int(d.get('entry'), name)
            space = d.get('space')
            if space is None:
                if e <= 0 or e % 4:
                    raise ModError('%s: table %s entry size %d' % (name, c, e))
            else:
                tag = _dsp_tag(self.dev, '.' + str(space))
                if tag is None:
                    raise ModError('%s: table %s in space %r, which the %s lacks'
                                   % (name, c, space, self.dev.name))
                head, end = _hex(d.get('head', ''), name), _hex(d.get('end', ''), name)
                if e <= 0 or e % 3 or len(head) % 3 or len(end) % 3:
                    raise ModError('%s: DSP table %s is not whole 24-bit words' % (name, c))
                self.dsp_tables[c] = (tag, head, end)
            self.collections[c] = e
        self.contribute = []
        for i, c in enumerate(doc.get('contribute', [])):
            what = '%s contribution %d' % (name, i)
            data = _hex(c.get('data'), what)
            rel = []
            for r in c.get('relocs', []):
                off, typ, tgt, add = r
                off = _int(off, what)
                if typ not in RTYPES or off < 0 or off + RSIZE[typ] > len(data):
                    raise ModError('%s: bad relocation %r' % (what, r))
                rel.append((off, typ, tgt, _int(add, what)))
            claims = [(_int(a, what), _int(b, what)) for a, b in c.get('claims', [])]
            self.contribute.append({'to': str(c['to']), 'order': _int(c.get('order', 50), what),
                                    'data': data, 'relocs': rel, 'claims': claims, 'i': i})
        try:
            self.storage = store.parse_decls(doc)
            self.locks = store.parse_locks(doc)
            self.ccs = store.parse_ccs(doc)
            self.lfos = store.parse_lfos(doc)
            self.rough = store.parse_rough(doc)
            self.routes = store.parse_routes(doc)
            self.cvs = store.parse_cvs(doc)
            self.inits = store.parse_inits(doc)
            self.handles = store.parse_handles(doc)
        except ModError as e:
            raise ModError('%s: %s' % (name, e))
        self.copied = [(_int(c['lo'], name), _int(c['hi'], name), _int(c['to'], name))
                       for c in doc.get('copied', [])]
        self.regions, self.names = parse_resources(doc, name, self.dev)
        self.needs_core = parse_needs_core(doc, name)
        self.requires = [str(x) for x in doc.get('requires', [])]
        self.conflicts = [str(x) for x in doc.get('conflicts', [])]
        self.blob = None                       # (format 1 only)

    def implied_names(self, image, only_table=None):
        """The names this mod claims by what it contributes (the profile's slot_claims): a
        contribution to such a table holds four-byte pointers to descriptors whose long at a
        known offset is a sound slot, and the mod claims that slot (digitables' TBL parameter: soundslot:26), so
        two mods that store in one slot never link together, whether their files say so or not.
        `image` is the stock main OS the mod is for: a descriptor's bytes may be stock parts. A
        descriptor this cannot read (outside the mod's own bytes, or relocated there) claims
        nothing. check() adds them to the mod's names."""
        out = []
        for table, at, count, prefix in self.dev.slot_claims:
            if only_table is not None and table != only_table:
                continue
            for c in self.contribute:
                if c['to'] != table:
                    continue
                for off, typ, tgt, add in c['relocs']:
                    if off % 4 or typ != 'abs32':
                        continue
                    if tgt.startswith('sym:') and tgt[4:] in self.symbols:
                        sec, base = self.symbols[tgt[4:]]
                    elif tgt.startswith('sec:'):
                        sec, base = tgt[4:], 0
                    else:
                        continue
                    v = self._long(sec, base + add + at, image)
                    if v is not None and 0 <= v < count and '%s:%d' % (prefix, v) not in out:
                        out.append('%s:%d' % (prefix, v))
        return out

    def id_ranges(self, image, knobs=None):
        """The knob ids this mod's entries answer for, by the profile's param_ids: a
        contribution to such a table holds four-byte pointers to entries whose first two longs are
        a range of ids (core-ar2's core_params, core_param_locks). -> [(table, first, last)]. An
        entry this cannot read (outside the mod's own bytes, or relocated there) answers for none;
        one relocated to __knobs answers for its ids when knobs (the mod's first id) is given."""
        out = []
        for table, _own_only in (self.dev.param_ids[2] if self.dev.param_ids else ()):
            for c in self.contribute:
                if c['to'] != table:
                    continue
                for off, typ, tgt, add in c['relocs']:
                    at = None if off % 4 else self._target(typ, tgt, add)
                    if at is None:
                        continue
                    first, last = self._id(at[0], at[1], image, knobs), self._id(at[0], at[1] + 4, image, knobs)
                    if first is not None and last is not None:
                        first, last = [v - (1 << 32) if v >> 31 else v for v in (first, last)]
                        if first <= last:
                            out.append((table, first, last))
        return out

    def _id(self, sec, off, image, knobs):
        """An id long at `sec`+`off`: its bytes, or __knobs + the addend where an abs32
        relocates it to __knobs and knobs is given; else None."""
        r = next(((t, g, a) for s, o, t, g, a in self.relocs if s == sec and o == off), None)
        if r is None:
            return self._long(sec, off, image)
        if knobs is not None and r[:2] == ('abs32', 'sym:' + KNOBS):
            return (knobs + r[2]) & 0xFFFFFFFF
        return None

    def knob_refs(self):
        """-> [(where, type, addend)] of every relocation to __knobs: sections, sites, entries."""
        out = [('%s+0x%x' % (sec, off), t, a) for sec, off, t, g, a in self.relocs if g == 'sym:' + KNOBS]
        out += [('site 0x%08x' % s['addr'], t, a) for s in self.sites for _o, t, g, a in s['relocs']
                if g == 'sym:' + KNOBS]
        out += [('an entry of %s' % c['to'], t, a) for c in self.contribute for _o, t, g, a in c['relocs']
                if g == 'sym:' + KNOBS]
        return out

    def _long(self, sec, off, image):
        """The big-endian long at `sec`+`off` of this mod's bytes, or None: in a section
        without bytes or under a relocation."""
        raw = self._bytes(sec, off, 4, image)
        return None if raw is None else struct.unpack('>I', raw)[0]

    def _bytes(self, sec, off, size, image):
        """The `size` bytes at `sec`+`off` of this mod's bytes, or None, as _long."""
        d = self.sections.get(sec)
        if not d or 'parts' not in d or off < 0 or off + size > d['len']:
            return None
        if any(s == sec and o < off + size and off < o + RSIZE[t] for s, o, t, _g, _a in self.relocs):
            return None
        raw, at = bytearray(), 0
        for p in d['parts']:
            n = len(p[1]) if p[0] == 'hex' else p[2]
            lo, hi = max(off, at), min(off + size, at + n)
            if lo < hi:
                source, start = (p[1], lo - at) if p[0] == 'hex' else (image, p[1] - self.dev.main_load + lo - at)
                chunk = source[start:start + hi - lo]
                if len(chunk) != hi - lo:
                    return None
                raw += chunk
                if len(raw) == size:
                    return bytes(raw)
            at += n
        return None

    def _target(self, typ, tgt, add):
        """Where an abs32 relocation points in this mod's bytes, as (section, offset), or None."""
        if typ == 'abs32' and tgt.startswith('sym:') and tgt[4:] in self.symbols:
            target, base = self.symbols[tgt[4:]]
            return target, base + add
        if typ == 'abs32' and tgt.startswith('sec:'):
            return tgt[4:], add
        return None

    def _pointer(self, sec, off):
        """Where the four-byte pointer at `sec`+`off` points in this mod's bytes, or None."""
        return next((self._target(t, g, a) for s, o, t, g, a in self.relocs if s == sec and o == off), None)

    def dn2_filter_g_ok(self, contribution, image):
        """A DN2 filter descriptor's optional G (mk2/machines.h): NULL, or an mk2_param in this
        mod's bytes with a label of 1..5 printable ASCII characters and a default of 0..127."""
        at = self.rel.sharc['filter_g_at']
        descriptor = next((self._target(t, g, a) for o, t, g, a in contribution['relocs'] if o == 0), None)
        if descriptor is None:
            return True                      # the registration checks refuse it
        param = self._pointer(descriptor[0], descriptor[1] + at)
        if param is None:
            return self._long(descriptor[0], descriptor[1] + at, image) == 0
        default, label = self._long(param[0], param[1] + 4, image), self._pointer(*param)
        if default is None or default > 127 or label is None:
            return False
        for n in range(6):
            c = self._bytes(label[0], label[1] + n, 1, image)
            if c is None or c[0] and not 32 <= c[0] <= 126:
                return False
            if not c[0]:
                return 1 <= n <= 5
        return False

    def _reloc_ok(self, sec, off, typ, tgt):
        if sec not in self.sections or sec == '.bss':
            raise ModError('%s: relocation in %s' % (self.name, sec))
        if typ not in RTYPES:
            raise ModError('%s: relocation type %s' % (self.name, typ))
        if (typ == 'dsp24') != bool(_dsp_tag(self.dev, sec)) or (typ == 'dsp24' and off % 3):
            raise ModError('%s: a %s relocation in %s' % (self.name, typ, sec))
        if off < 0 or off + RSIZE[typ] > self.sections[sec]['len']:
            raise ModError('%s: relocation at %s+%d is outside it' % (self.name, sec, off))
        if not (tgt == 'abs' or tgt.startswith('sym:') or tgt.startswith('sec:')):
            raise ModError('%s: relocation target %r' % (self.name, tgt))

    def label(self):
        return '%s %s' % (self.id, self.version)

    def size(self, sec):
        d = self.sections.get(sec)
        if not d:
            return 0
        return d['size'] if sec == '.bss' else d['len']

    def ram_bound(self):
        """The most it can take of the linker's area, whatever it is linked with:
        its .run and .bss with their alignment, its table entries (and the zero
        entry of each table it has), its .fast copy and that copy's entry, each
        rounded up to 4. link() lays them out in no more than the sum of the
        mods' bounds, so a builder can tell which core's area holds them
        (gui.with_core)."""
        n = 0
        for sec in ('.run', '.bss', '.payload'):
            if self.size(sec):
                n += _align(self.size(sec), 4) + max(4, self.sections[sec]['align']) - 4
        if self.size('.payload'):
            n += PAYLOAD_ENTRY
        n += sum(_align(len(c['data']), 4) for c in self.contribute)
        n += sum(_align(e, 4) for e in self.collections.values())
        if self.size('.fast'):
            n += _align(self.size('.fast'), 4) + FAST_ENTRY
        return n


# ---- checks ---------------------------------------------------------------------------

def reserve_names(dev, exported):
    """The symbols a core may size its RAM reserve with (the profile's `reserve`): none when a
    mod exports one of them, as a core with a fixed reserve does."""
    names = set(dev.reserve[1]) if dev.reserve else set()
    return set() if names & set(exported) else names


def dsp_areas(mods, image):
    """The DSP areas the mods free, by the name a mod claims: payload tag -> (lo, hi, main
    OS address of P:lo, the mod, the area's name). One area a payload, the first claimed."""
    dev, out = mods[0].dev, {}
    for tag, lo, hi, nm, what in dev.dsp_areas:
        owner = next((m for m in mods if nm in m.names), None)
        if owner is not None and tag not in out:
            out[tag] = (lo, hi, dsp.p_span(image, dev, tag, lo, hi), owner, nm)
    return out


def _dsp_needs(m):
    """The payload tags a mod's DSP sections and DSP tables need an area in."""
    return ({_dsp_tag(m.dev, s) for s in m.sections if _dsp_tag(m.dev, s)}
            | {t for t, _h, _e in m.dsp_tables.values()})


def kernel_payload(m):
    return isinstance(m.sharc, dict) and m.sharc.get('abi') == sharc.KERNEL_ABI


def sharc_split(mods):
    """(the DN2 kernel mods, the other SHARC mods but the core). A dispatcher without kernels is inert."""
    kernel_mods = [m for m in mods if kernel_payload(m)]
    if not any(m.sharc['kernels'] for m in kernel_mods):
        kernel_mods = []
    return kernel_mods, [m for m in mods if m.sharc and '.boot' not in m.sections and not kernel_payload(m)]


def kernel_registrations(m, image):
    """The DN2 machines m registers, by the profile's kernel_registrations: {(kernel kind, id): (table,
    needs its kernel)}."""
    out = {}
    for table, kind, needs in (m.rel.sharc or {}).get('kernel_registrations', ()):
        for nm in m.implied_names(image, table):
            out[kind, int(nm.rsplit(':', 1)[1])] = (table, needs)
    return out


def check(mods, image):
    """Static checks that need no layout. -> [problems]."""
    spans = []
    for m in mods:
        for s in m.sites:
            spans.append((s['addr'], s['addr'] + s['len'], m, 'site 0x%08x' % s['addr']))
        for c in m.contribute:
            for a, b in c['claims']:
                spans.append((a, b, m, '%s entry (claims 0x%08x-0x%08x)' % (c['to'], a, b)))
    areas = {}
    if mods[0].dev.dsp_areas and len({(m.dev.key, m.rel.version) for m in mods}) == 1:
        areas = dsp_areas(mods, image)
        for tag, (lo, hi, at, owner, nm) in sorted(areas.items()):
            spans.append((at, at + 3 * (hi - lo), owner,
                          'DSP area %s (payload %s P:0x%04x-0x%04x)' % (nm, tag, lo, hi - 1)))
    if len({(m.dev.key, m.rel.version) for m in mods}) == 1:
        for m in mods:                          # the sound slots their parameters store in
            for nm in m.implied_names(image):
                if nm not in m.names:
                    m.names.append(nm)
    bad = common_checks(mods, image, spans)
    if bad and bad[0].startswith('the mods are for different firmware'):
        return bad
    dev = mods[0].dev
    if not dev.linkable():
        return ['the %s has no linkable (format-2) mods yet: only whole builds (format 1) '
                'can be used on it' % dev.name]
    ids = [m.id for m in mods]
    cores = [m for m in mods if '.boot' in m.sections]
    if len(cores) != 1:
        bad.insert(0, 'exactly one mod must carry .boot (core); got %s'
                   % ([m.label() for m in cores] or 'none'))
    else:
        for m in mods:
            if m.needs_core and version_key(cores[0].version) < version_key(m.needs_core):
                bad.append('%s needs core %s or newer, and this build has core %s'
                           % (m.label(), m.needs_core, cores[0].version))
    kernel_mods, sharc_mods = sharc_split(mods)
    for m in mods:
        if not sharc_mods and not kernel_mods and any(sharc_site(m.rel, s['addr']) for s in m.sites):
            bad.append('%s: SHARC data site requires a SHARC mod' % m.label())
    if kernel_mods:
        dispatchers = [m.label() for m in kernel_mods if m.sharc['exports']]
        if len(dispatchers) != 1:
            bad.append('DN2 machine kernels need one dispatcher (machines-dn2); this build has %s'
                       % (', '.join(dispatchers) or 'none'))
        if sharc_mods:
            bad.append('DN2 machine kernels do not combine with %s' % ', '.join(m.label() for m in sharc_mods))
        owners = {}
        for m in kernel_mods:
            for k in m.sharc['kernels']:
                key = (sharc.KERNEL_TABLE[k['kind']], k['id'])
                if key in owners:
                    bad.append('%s and %s both have a DN2 %s kernel with id %d'
                               % (owners[key].label(), m.label(), *key))
                owners[key] = m
    if len(sharc_mods) > 1:
        bad.append('a build takes one SHARC mod; this one has %s' % ', '.join(m.label() for m in sharc_mods))
    for m in mods:                          # DN2 machines: each registration and its kernel, in one mod
        regs = kernel_registrations(m, image)
        have = {(k['kind'], k['id']) for k in m.sharc['kernels']} if kernel_payload(m) else set()
        for (kind, mid), (table, needs) in sorted(regs.items()):
            if needs and (kind, mid) not in have:
                bad.append('%s: its %s entry %d needs a SHARC %s kernel with that id' % (m.label(), table, mid, kind))
        for kind, mid in sorted(have - set(regs)):
            bad.append('%s: its SHARC %s kernel %d has no registration with that id in its mod' % (m.label(), kind, mid))
        g_tables = {t for t, kind, _n in (m.rel.sharc or {}).get('kernel_registrations', ()) if kind == 'mk2-filter'}
        if any(c['to'] in g_tables and not m.dn2_filter_g_ok(c, image) for c in m.contribute):
            bad.append('%s: DN2 filter G needs a label of 1..5 printable ASCII characters and '
                       'a default of 0..127' % m.label())
    for app in sharc_mods:
        if app.sharc['entries'] is None:
            continue
        core = cores[0] if len(cores) == 1 else None
        if not core or not core.sharc:
            bad.append('active SHARC mod requires a qualified matching core bridge')
            continue
        profile = app.rel.sharc
        qualified = profile['active_keyed_core']
        keyed = ([(off, kind, sha(data)) for off, kind, data in core.sharc['segments']]
                 == list(qualified['segments'])
                 and core.sharc['zero'] == list(qualified['zero'])
                 and core.sharc['entries'] == qualified['entries']
                 and core.sharc['stack_bytes'] == qualified['stack_bytes'])
        if not keyed:
            bad.append('%s has unqualified active SHARC core bytes or entry' % core.label())
        if core.sharc['stack_bytes'] + app.sharc['stack_bytes'] > profile['stack_max']:
            bad.append('active SHARC stack exceeds the profile limit')
    owner = {}
    for m in mods:
        for x in m.exports:
            if x in owner:
                bad.append('%s and %s both export %s' % (owner[x].label(), m.label(), x))
            owner[x] = m
    tables = {}
    for m in mods:
        for c, e in m.collections.items():
            if c in tables:
                bad.append('%s and %s both declare the table %s'
                           % (tables[c][0].label(), m.label(), c))
            if c in owner:
                bad.append('table %s has the name of a symbol %s exports' % (c, owner[c].label()))
            tables[c] = (m, e)
    if dev.param_ids and len({(m.dev.key, m.rel.version) for m in mods}) == 1:
        bad += id_problems(mods, image)
    for m in mods:
        if m.knobs and not dev.param_ids:
            bad.append('%s asks for knob ids ("knobs"), which the %s does not give mods'
                       % (m.label(), dev.name))
        for where, t, a in m.knob_refs():
            if t not in ('abs32', 'abs16') or not 0 <= a < m.knobs:
                bad.append('%s: %s is a %s reference to %s+%d; one takes abs32 or abs16 with an addend '
                           'below its "knobs" (%d)' % (m.label(), where, t, KNOBS, a, m.knobs))
    sized = reserve_names(dev, owner)
    fields = {}                                 # mod storage: the linker defines these
    for m in mods:
        for sym, _scope, _bits in m.storage:
            if sym in fields:
                bad.append('%s and %s both declare the storage field %s'
                           % (fields[sym].label(), m.label(), sym))
            elif sym in owner or sym in tables or sym in LINKER_SYMS or sym in store.GEN_SYMS:
                bad.append('%s: the storage field %s has the name of a symbol or table'
                           % (m.label(), sym))
            fields[sym] = m
    if store.IDS_TABLE in tables:
        fields.update({x: None for x in store.GEN_SYMS + (store.LOCK_DST, store.FX_LOCK_DST)})
    for m in mods:
        for x in m.imports:
            if x in owner or x in tables or x in LINKER_SYMS or x in sized or x in m.weak \
                    or (x.endswith('_n') and x[:-2] in tables) or x in fields or (x == KNOBS and m.knobs):
                continue
            bad.append('%s imports %s, which no given mod exports' % (m.label(), x))
        for c in m.contribute:
            if c['to'] not in tables:
                bad.append('%s adds to table %s, which no given mod declares' % (m.label(), c['to']))
            elif len(c['data']) % tables[c['to']][1]:
                bad.append('%s: an entry for %s is not %d bytes'
                           % (m.label(), c['to'], tables[c['to']][1]))
            else:
                in_dsp = c['to'] in tables[c['to']][0].dsp_tables
                for o, t, _g, _a in c['relocs']:
                    if (t == 'dsp24') != in_dsp or (in_dsp and o % 3):
                        bad.append('%s: an entry for %s has a %s relocation at +%d; %s'
                                   % (m.label(), c['to'], t, o,
                                      'a DSP table\'s entries take dsp24, at whole words'
                                      if in_dsp else 'dsp24 is for DSP tables'))
        if m.size('.fast') and (not dev.fast_table or dev.fast_table not in tables):
            bad.append('%s has .fast code, which needs the mod that declares %s'
                       % (m.label(), dev.fast_table or 'a .fast copy table'))
        for tag in sorted(_dsp_needs(m) - set(areas)):
            free = [nm for t, _lo, _hi, nm, _w in dev.dsp_areas if t == tag]
            bad.append('%s has DSP code for payload %s, which needs a mod that frees DSP memory '
                       'there (one that claims %s)' % (m.label(), tag, ' or '.join(free) or
                                                       'an area the %s does not have' % dev.name))
    for t, most in sorted(dev.table_limits.items()):     # the OS uses no more (the profile's table_limits)
        adds = [m.label() for m in mods if any(c['to'] == t for c in m.contribute)]
        n = sum(len(c['data']) for m in mods for c in m.contribute if c['to'] == t) // tables[t][1] \
            if t in tables else 0
        if n > most:
            who = ', '.join(adds[:-1]) + ' and ' + adds[-1] if len(adds) > 1 else adds[0]
            bad.append('%s holds at most %d entries on %s %s; %s add%s %d'
                       % (t, most, dev.key, mods[0].rel.version, who, '' if len(adds) > 1 else 's', n))
    regs = [(g['lo'], g['hi'], m, 'region ' + g['name']) for m in mods for g in m.regions]
    if dev.sram_code[1] > dev.sram_code[0]:
        regs.append((dev.sram_code[0], dev.sram_code[1], None, 'the .fast area'))
    for a, b in overlaps(regs):
        la = a[2].label() if a[2] else 'the linker'
        lb = b[2].label() if b[2] else 'the linker'
        bad.append('%s %s and %s %s overlap' % (la, a[3], lb, b[3]))
    return bad


def id_problems(mods, image, knobs=None):
    """The profile's param_ids against the mods' entries: none in the core's ids, none in a table
    whose ids are mods' own below them, and no id from there up taken twice. knobs: {mod id: its
    first knob id} (link), to count the entries relocated to __knobs too. -> [problems]."""
    own, (lo, hi, what), kinds = mods[0].dev.param_ids
    own_only = {t for t, o in kinds if o}
    taken, bad = [], []
    for m in mods:
        for table, a, b in m.id_ranges(image, (knobs or {}).get(m.id)):
            if a <= hi and lo <= b:
                bad.append('%s: its %s entry takes ids %d..%d, and %d..%d are %s; a mod\'s own '
                           'ids start at %d' % (m.label(), table, a, b, lo, hi, what, own))
            elif table in own_only and a < own:
                bad.append('%s: its %s entry takes ids %d..%d; that table\'s ids start at %d'
                           % (m.label(), table, a, b, own))
            elif b >= own:
                a = max(a, own)
                for n, t, x, y in taken:
                    if n is not m and x <= b and a <= y:
                        bad.append('%s and %s both take ids %d..%d (%s, %s)'
                                   % (n.label(), m.label(), max(a, x), min(b, y), t, table))
                taken.append((m, table, a, b))
    return bad


def _copied_rules(mods, patched, dev):
    """Sites of other mods inside a copied block: no PC-relative operand, no
    relative branch, whole instructions in the patched image."""
    isa = dev.decoder()
    bad = []
    read = isa.reader(patched, dev.main_load)
    for owner in mods:
        for lo, hi, _to in owner.copied:
            for m in mods:
                if m is owner:
                    continue
                for s in m.sites:
                    if not (lo <= s['addr'] < hi or lo < s['addr'] + s['len'] <= hi):
                        continue
                    if s['kind'] != 'code':
                        bad.append('%s site 0x%08x: data inside %s\'s copied block'
                                   % (m.label(), s['addr'], owner.label()))
                        continue
                    a = s['addr']
                    while a < s['addr'] + s['len']:
                        ins = isa.decode(read(a), a)
                        if ins.flags & isa.PCREL or ins.flow in (isa.F_BRA, isa.F_BCC, isa.F_BSR):
                            bad.append('%s site 0x%08x: %s at 0x%08x is PC-relative, inside '
                                       '%s\'s copied block' % (m.label(), s['addr'], ins.op, a,
                                                                owner.label()))
                        a += ins.length
                    if a != s['addr'] + s['len']:
                        bad.append('%s site 0x%08x: its new bytes are not whole instructions'
                                   % (m.label(), s['addr']))
    return bad


# ---- the finished image against the stock one -------------------------------------------

def image_problems(dev, stock, img, declared, tail_at):
    """The linker's last check, on the image it built, against the stock one. It trusts none of
    the layout that built the image; it only compares. -> [problems]
    - the appended code (the core's `.boot`, the RAM image and the lockfile's trailer) begins
      where the stock image ends, `tail_at`;
    - every declared span lies inside the stock image;
    - every byte in the stock image's range that differs from the stock image lies in a
      declared span: `declared` is [(lo, hi, what)], the addresses of each mod's sites and each
      DSP section and table the linker placed in a DSP area;
    - the device's protected and kept ranges are stock (formats.verify checks the same on the
      packed file; this is the linker's own account of it)."""
    ml, n = dev.main_load, len(stock)
    bad = []
    if tail_at != ml + n:
        bad.append('the appended code starts at 0x%08x, not at the end of the stock image, 0x%08x'
                   % (tail_at, ml + n))
    if len(img) < n:
        bad.append('the linked image (%d bytes) is shorter than the stock main OS (%d bytes)'
                   % (len(img), n))
        return bad
    probe = bytearray(img[:n])
    for lo, hi, what in declared:
        if lo < ml or hi > ml + n:
            bad.append('%s lies outside the stock image' % what)
            continue
        probe[lo - ml:hi - ml] = stock[lo - ml:hi - ml]
    if probe != stock:
        runs, i = [], 0
        while i < n:
            if probe[i] == stock[i]:
                i += 1
                continue
            j = i
            while j < n and probe[j] != stock[j]:
                j += 1
            runs.append((ml + i, ml + j))
            i = j
        for lo, hi in runs[:8]:
            bad.append('0x%08x-0x%08x differs from stock, and no site, DSP section or table of the '
                       'mods declares it' % (lo, hi))
        if len(runs) > 8:
            bad.append('%d more runs of bytes differ from stock outside every declaration'
                       % (len(runs) - 8))
    for lo, hi, why in tuple(dev.protected) + tuple(dev.keep):
        if bytes(img[lo - ml:hi - ml]) != bytes(stock[lo - ml:hi - ml]):
            bad.append('0x%08x-0x%08x (%s) is not stock in the linked image' % (lo, hi, why))
    return bad


# ---- the link -------------------------------------------------------------------------

class Linked:
    pass


def link(mods, image, lock=None, *, sharc_stream=None):
    """-> Linked: .image (the patched main OS), .map ({name: address}),
    .layout, .tables ({name: (address, count, entry)}), .order, and for a core
    with mod storage .store (the new lockfile) and .store_notes; .knobs ({mod id:
    [first id, count]}) and .knob_notes for the mods' "knobs". lock: the
    build's lockfile, or None for a new one (elekloader/store.py). Raises
    ModError."""
    if not mods:
        raise ModError('no mods')
    dev, rel = mods[0].dev, mods[0].rel
    if sha(image) != rel.main_sha256:
        raise ModError('the stock main OS is not %s %s' % (dev.name, rel.version))
    bad = summarize(check(mods, image), mods)
    if bad:
        raise ModError('the mods do not combine:\n  ' + '\n  '.join(bad))
    image_end = dev.image_end(rel)
    core = [m for m in mods if '.boot' in m.sections][0]
    ddr_base, ddr_end = dev.ddr_for(core.version)
    fast_base, fast_end = dev.sram_code
    order = [core] + sorted((m for m in mods if m is not core), key=lambda m: m.id)
    kernel_mods, sharc_mods = sharc_split(order)
    dsp_stream = dsp_stored = None
    if kernel_mods or sharc_mods:
        if sharc_stream is None:
            raise ModError('SHARC mod needs the stock section-7 stream')
        loads, fills, patches = [], [], []
        if kernel_mods:
            loads, fills, patches, kernel_layout = sharc.place_kernels([(m.id, m.sharc) for m in kernel_mods], rel.sharc)
        for mod in sharc_mods:
            payload = mod.sharc
            base = payload['owner'][0]
            loads += [(base + off, data) for off, _kind, data in payload['segments']]
            fills += [(base + off, count, 0) for off, count in payload['zero']]
            if payload['entries'] is not None:
                bridge = core.sharc
                def pc16(off):
                    return sharc.pc16(rel.sharc, base + off)
                binding = struct.pack('<III', pc16(payload['entries']['init_offset_bytes']),
                                      pc16(payload['entries']['render_offset_bytes']), 0)
                core_base = bridge['owner'][0]
                # The pinned bridge reads its pitch table from the L1 reservation.
                lo, hi = rel.sharc['pitch_range']
                at, table = rel.sharc['keyed_pitch_table']
                table = bytes.fromhex(table)
                loads.append((lo + at, table))
                fills += [(lo, at, 0), (lo + at + len(table), hi - lo - at - len(table), 0)]
                loads = [(core_base, binding)] + [(core_base + off, data) for off, _kind, data in bridge['segments']] + loads
                fills = [(core_base + off, count, 0) for off, count in bridge['zero']] + fills
                site, old, new = rel.sharc['hook']
                patches.append((site, bytes.fromhex(old), bytes.fromhex(new)))
        # core-dn2's DSP peek rides along whenever section 7 changes (mods/core-dn2/peek.s).
        lo, hi = rel.sharc['peek_range']
        code = bytes.fromhex(rel.sharc['peek_code'])
        loads.append((lo, code))
        fills.append((lo + len(code), hi - lo - len(code), 0))
        site, old, new = rel.sharc['peek_hook']
        patches.append((site, bytes.fromhex(old), bytes.fromhex(new)))
        for m in order:
            for s in m.sites:
                qualified = sharc_site(rel, s['addr'])
                if qualified:
                    patches.append((s['addr'], bytes.fromhex(qualified[0]), s['new']))
        dsp_stream = sharc.insert(sharc_stream, rel.sharc,
                                  loads=loads, fills=fills, patches=patches)
        dsp_stored = syx.pack_main(dsp_stream)
        if len(dsp_stored) > rel.sharc['stored_max']:
            raise ModError('SHARC stored section exceeds the profile limit')
    raw_mods = [m for m in order if m.sharc_raw]
    if raw_mods:
        # (a device's profile takes raw payloads or a bridge's modules, never both: raw_payload
        # refuses a profile without 'raw', and module_payload one without the bridge's ABI)
        if sharc_stream is None:
            raise ModError('SHARC mod needs the stock section-7 stream')
        loads, patches, _dsp_syms = sharc.link_raw([(m.id, m.sharc_raw) for m in raw_mods],
                                                   sharc_stream, rel.sharc)
        spans = sorted((a, a + len(o)) for a, o, _n in patches)
        for (a0, a1), (b0, _b1) in zip(spans, spans[1:]):
            if b0 < a1:
                raise ModError('two raw SHARC patches overlap at 0x%x' % b0)
        dsp_stream = sharc.insert(sharc_stream, rel.sharc, loads=loads, patches=patches)
        dsp_stored = syx.pack_main(dsp_stream)
        if len(dsp_stored) > rel.sharc['stored_max']:
            raise ModError('SHARC stored section exceeds the profile limit')
        payload = {'owner': (min(a for a, _d in loads), max(a + len(d) for a, d in loads))
                   if loads else (0, 0), 'entries': None}
        sharc_mods = raw_mods
    tables = {c: (m, e) for m in order for c, e in m.collections.items()}

    entries = {c: [] for c in tables}
    for m in order:
        for c in m.contribute:
            entries[c['to']].append((c['order'], m.id, c['i'], m, c))

    # mod storage: the fields' places from the lockfile, as symbols and the IDs' table
    keeps = store.GEN_SYMS[0] in core.imports
    decls = [(m.id, sym, scope, bits) for m in order for sym, scope, bits in m.storage]
    new_lock, store_notes, store_syms = None, [], {}
    if decls and not keeps:
        users = sorted({d[0] for d in decls})
        raise ModError('%s declare%s mod storage, which %s does not keep'
                       % (', '.join(users), 's' if len(users) == 1 else '', core.label()))
    if keeps:
        if store.IDS_TABLE not in tables:
            raise ModError('%s imports %s but declares no table %s'
                           % (core.label(), store.GEN_SYMS[0], store.IDS_TABLE))
        locks = [(m.id, sym) for m in order for sym in m.locks]
        if locks and store.CODES_TABLE not in tables:
            raise ModError('%s declare%s lockable parameters, which %s does not keep (no table %s)'
                           % (', '.join(sorted({k[0] for k in locks})), 's' if len({k[0] for k in locks}) == 1
                              else '', core.label(), store.CODES_TABLE))
        lfos = [(m.id, sym) for m in order for sym in m.lfos]
        rough = [(m.id, sym) for m in order for sym in m.rough]
        routes = [(m.id, sym) for m in order for sym in m.routes]
        scope_of = {(d[0], d[1]): d[2] for d in decls}
        cvs = [(m.id, sym) for m in order for sym in m.cvs]
        store.check_flags(dev.key, scope_of, lfos, routes, cvs, [(m.id, sym) for m in order for sym in m.ccs])
        fx = sorted({k[0] for k in locks if scope_of.get(k) == 'kit'})
        if fx and not store.kinds(dev.key) and store.FX_LOCK_DST not in core.imports:
            raise ModError('%s declare%s lockable kit fields (the FX track\'s), which %s does not keep'
                           % (', '.join(fx), 's' if len(fx) == 1 else '', core.label()))
        for dst, scope, bits in ((store.LOCK_DST, 'sound', store.lock_dst_bits(dev.key)),
                                 (store.FX_LOCK_DST, 'kit', store.fx_lock_dst_bits(dev.key))):
            if any(scope_of.get(k) == scope for k in lfos) and dst in core.imports and bits:
                # the core keeps a code as an LFO destination: the sound's LFO, the FX LFO
                decls.append((core.id, dst, scope, bits))
        ccs = [(m.id, sym) for m in order for sym in m.ccs]
        if ccs and store.CCS_TABLE not in tables:
            raise ModError('%s give%s lockable parameters CCs, which %s does not keep (no table %s)'
                           % (', '.join(sorted({k[0] for k in ccs})), 's' if len({k[0] for k in ccs}) == 1
                              else '', core.label(), store.CCS_TABLE))
        inits = {(m.id, sym): v for m in order for sym, v in m.inits.items()}
        if inits and store.INITS_TABLE not in tables:
            users = sorted({k[0] for k in inits})
            raise ModError('%s give%s storage fields an init, which %s does not keep (no table %s)'
                           % (', '.join(users), 's' if len(users) == 1 else '', core.label(), store.INITS_TABLE))
        handles = [(m.id, sym) for m in order for sym in m.handles]
        if handles and store.HANDLES_TABLE not in tables:
            users = sorted({k[0] for k in handles})
            raise ModError('%s mark%s storage fields as handles, which %s does not keep (no table %s)'
                           % (', '.join(users), 's' if len(users) == 1 else '', core.label(), store.HANDLES_TABLE))
        new_lock, store_notes = store.allocate(lock, decls, dev.key, core.version, locks, ccs, inits, cvs)
        store_syms, ids = store.symbols(new_lock, dev.key)
        store_syms.setdefault(store.LOCK_DST, 0)    # not placed: no LFO field
        store_syms.setdefault(store.FX_LOCK_DST, 0)
        for table, made in ((store.CODES_TABLE, lambda l: store.code_entries(l, lfos, rough, routes)),
                            (store.CCS_TABLE, store.cc_entries), (store.INITS_TABLE, store.init_entries),
                            (store.HANDLES_TABLE, lambda l: store.handle_entries(l, handles))):
            if table in tables:
                for i, data in enumerate(made(new_lock)):
                    entries[table].append((-1, '', i, core, {'data': data, 'relocs': []}))
        taken = {nm for m in order for nm in m.exports}
        for nm in store_syms:
            if nm in taken or nm in LINKER_SYMS:
                raise ModError('the storage field %s has the name of a symbol a mod exports' % nm)
        for i, data in enumerate(ids):
            entries[store.IDS_TABLE].append((-1, '', i, core, {'data': data, 'relocs': []}))
    # knob ids: each "knobs" mod's run, past the ids mods' entries name themselves
    knob_base, knob_notes = {}, []
    if dev.param_ids and (any(m.knobs for m in order) or (new_lock or {}).get('knobs')
                          or (new_lock or {}).get('freed_knobs')):
        fixed = [(a, b) for m in order for _t, a, b in m.id_ranges(image)]
        held = new_lock if new_lock is not None else {}
        knob_base, knob_notes = store.knobs(held, {m.id: m.knobs for m in order if m.knobs},
                                            dev.param_ids[0], fixed)
        bad = id_problems(order, image, knob_base)
        if bad:                                # store.knobs keeps clear of them: a self-check
            raise ModError('the linked mods break a rule:\n  ' + '\n  '.join(bad))
    fast_mods = [m for m in order if m.size('.fast')]
    pay_mods = [m for m in order if m.size('.payload')]
    if pay_mods:
        if PAYLOADS not in tables or tables[PAYLOADS][1] != PAYLOAD_ENTRY:
            raise ModError('%s carr%s a payload, which %s does not keep (no table %s of %d-byte entries)'
                           % (', '.join(m.id for m in pay_mods), 'ies' if len(pay_mods) == 1 else 'y',
                              core.label(), PAYLOADS, PAYLOAD_ENTRY))
        ids = {}
        for m in pay_mods:
            i = payload_id(m.id)
            if i in ids:
                raise ModError('the payloads of %s and %s have the same id (0x%08x)' % (ids[i], m.id, i))
            ids[i] = m.id
            entries[PAYLOADS].append((-1, m.id, 0, m, {
                'data': struct.pack('>III', i, 0, m.size('.payload')),
                'relocs': [(4, 'abs32', 'sec:.payload', 0)]}))
    for c in entries:
        entries[c].sort(key=lambda e: e[:3])

    def table_len(c):
        n = sum(len(e[4]['data']) for e in entries[c])
        if c in dsp_tables:                    # a DSP table: its head, its entries, its end
            return len(dsp_tables[c][1]) + n + len(dsp_tables[c][2])
        if c == dev.fast_table:
            n += FAST_ENTRY * len(fast_mods)
        return n + tables[c][1]                # the zero entry

    def table_count(c):
        n = sum(len(e[4]['data']) for e in entries[c])
        return n // tables[c][1] if c in dsp_tables else table_len(c) // tables[c][1] - 1

    dsp_tables = {c: t for m in order for c, t in m.dsp_tables.items()}

    # layout
    base = {}                                  # (mod id, section) -> address
    boot_len = core.size('.boot')
    base[(core.id, '.boot')] = image_end
    at = ddr_base
    for m in order:
        if m.size('.run'):
            at = _align(at, max(4, m.sections['.run']['align']))
            base[(m.id, '.run')] = at
            at += m.size('.run')
    tab_at = {}
    for c in sorted(tables):
        if c in dsp_tables:
            continue
        at = _align(at, 4)
        tab_at[c] = at
        at += table_len(c)
    fast_at, fload = fast_base, {}
    for m in fast_mods:
        fast_at = _align(fast_at, max(4, m.sections['.fast']['align']))
        base[(m.id, '.fast')] = fast_at
        at = _align(at, 4)
        fload[m.id] = at
        n = _align(m.size('.fast'), 4)
        fast_at += n
        at += n
    pay_at = at = _align(at, 4)
    for m in pay_mods:                         # after the code: copied with it, then the .bss
        at = _align(at, max(4, m.sections['.payload']['align']))
        base[(m.id, '.payload')] = at
        at += m.size('.payload')
    pay_end = at
    run_end = _align(at, 4)
    at = run_end
    for m in order:
        if m.size('.bss'):
            at = _align(at, max(4, m.sections['.bss']['align']))
            base[(m.id, '.bss')] = at
            at += m.size('.bss')
    bss_end = _align(at, 4)
    if pay_mods and bss_end > ddr_end:
        raise ModError('the payloads (%d bytes) and the mods need RAM to 0x%08x, past 0x%08x (%d bytes over): '
                       'the area has room for %d bytes of payload'
                       % (pay_end - pay_at, bss_end, ddr_end, bss_end - ddr_end,
                          max(0, pay_end - pay_at - (bss_end - ddr_end))))
    if bss_end > ddr_end:
        more = [(v, a) for v, a in dev.core_ddr if a[1] - a[0] >= bss_end - ddr_base]
        raise ModError('the mods need RAM to 0x%08x, past 0x%08x (%d bytes over)%s'
                       % (bss_end, ddr_end, bss_end - ddr_end,
                          '; core %s or newer gives them %d bytes' % (more[0][0], more[0][1][1] - more[0][1][0])
                          if more else ''))
    if fast_at > fast_end:
        raise ModError('.fast code needs SRAM to 0x%08x, past 0x%08x' % (fast_at, fast_end))
    run_load = _align(image_end + boot_len, 16)

    # DSP: each payload's sections, then its tables, in the P words a mod frees (in words)
    areas = dsp_areas(order, image) if dev.dsp_areas else {}
    dsp_at, dsp_secs = {tag: a[0] for tag, a in areas.items()}, {}
    for m in order:
        for sec in m.sections:
            tag = _dsp_tag(dev, sec)
            if tag:
                base[(m.id, sec)] = dsp_at[tag]
                dsp_secs[(m.id, sec)] = tag
                dsp_at[tag] += m.size(sec) // 3
    for c in sorted(dsp_tables):
        tag = dsp_tables[c][0]
        tab_at[c] = dsp_at[tag]
        dsp_at[tag] += table_len(c) // 3
    for tag, (lo, hi, _a, _o, nm) in areas.items():
        if dsp_at[tag] > hi:
            raise ModError('the DSP code for payload %s needs P words to 0x%05x, past the %d '
                           'words %s frees (%d over)' % (tag, dsp_at[tag], hi - lo, nm,
                                                        dsp_at[tag] - hi))

    # symbols (a DSP one is a word address: its section's base + its byte offset / 3)
    addr, glob, dspsym = {}, {}, set()
    for m in order:
        for nm, (sec, off) in m.symbols.items():
            if sec in m.sections and _dsp_tag(dev, sec):
                v = base[(m.id, sec)] + off // 3
                dspsym.add((m.id, nm))
            else:
                v = off if sec == 'abs' else base[(m.id, sec)] + off
            addr[(m.id, nm)] = v
            if nm in m.exports:
                glob[nm] = v
                if (m.id, nm) in dspsym:
                    dspsym.add((None, nm))
    for c in tables:
        glob[c] = tab_at[c]
        glob[c + '_n'] = table_count(c)
        if c in dsp_tables:
            dspsym.add((None, c))
    glob.update(store_syms)
    glob.update({'__run_load': run_load, '__run_start': ddr_base,
                 '__run_words': (run_end - ddr_base) // 4, '__bss_start': run_end,
                 '__bss_end': bss_end, '__bss_words': (bss_end - run_end) // 4})
    # a reserve sized to the mods, for a core that imports its symbols
    sized, units = reserve_names(dev, glob), None
    if any(x in sized for m in order for x in m.imports):
        unit, names = dev.reserve
        units = max(1, -(-(bss_end - ddr_base) // unit))
        for nm in sorted(names):
            a, b = names[nm]
            glob[nm] = (a + b * units) & 0xFFFFFFFF
    zero = glob.get('core_zero')

    def resolve(m, tgt, where):
        if tgt == 'abs':
            return 0
        kind, nm = tgt.split(':', 1)
        if kind == 'sec':
            if (m.id, nm) not in base:
                raise ModError('%s: %s refers to its empty section %s' % (m.label(), where, nm))
            return base[(m.id, nm)]
        if (m.id, nm) in addr and nm in m.symbols:
            return addr[(m.id, nm)]
        if nm == KNOBS and m.id in knob_base:
            return knob_base[m.id]
        if nm in glob:
            return glob[nm]
        if nm in m.weak and zero is not None:
            return zero
        raise ModError('%s: %s refers to %s, which nothing defines' % (m.label(), where, nm))

    def is_dsp(m, tgt):
        """Is a relocation's target a DSP word address? None for "abs"."""
        if tgt == 'abs':
            return None
        kind, nm = tgt.split(':', 1)
        if kind == 'sec':
            return bool(_dsp_tag(dev, nm))
        if (m.id, nm) in addr and nm in m.symbols:
            return (m.id, nm) in dspsym
        return (None, nm) in dspsym

    def place(m, buf, off, typ, tgt, add, p, where):
        d = is_dsp(m, tgt)
        if d is not None and d != (typ == 'dsp24'):
            raise ModError('%s: %s is a %s reference to a %s address' % (
                m.label(), where, 'DSP' if typ == 'dsp24' else 'ColdFire',
                'DSP' if d else 'ColdFire'))
        put(buf, off, typ, resolve(m, tgt, where) + add, p, where)

    def put(buf, off, typ, value, p, where):
        if typ == 'dsp24':                     # a DSP word, little-endian, as the payloads hold it
            if not 0 <= value <= 0xFFFFFF:
                raise ModError('%s: a DSP address out of range (0x%x)' % (where, value))
            buf[off:off + 3] = bytes((value & 0xFF, (value >> 8) & 0xFF, value >> 16))
        elif typ == 'abs32':
            struct.pack_into('>I', buf, off, value & 0xFFFFFFFF)
        elif typ == 'abs16':
            if not -0x8000 <= value <= 0xFFFF:
                raise ModError('%s: a 16-bit value out of range (%d)' % (where, value))
            struct.pack_into('>H', buf, off, value & 0xFFFF)
        elif typ == 'pc32':
            struct.pack_into('>i', buf, off, value - p)
        else:
            d = value - p
            if not -0x8000 <= d < 0x8000:
                raise ModError('%s: a 16-bit PC-relative reference spans %d bytes' % (where, d))
            struct.pack_into('>h', buf, off, d)

    content, boot_relocs = {}, []
    for m in order:
        for sec in m.sections:
            if sec != '.bss' and m.size(sec):
                content[(m.id, sec)] = parts_bytes(m.sections[sec]['parts'], image, dev)
        for sec, off, typ, tgt, add in m.relocs:
            if sec == '.boot':                 # after the RAM image: it may name its hash
                boot_relocs.append((m, off, typ, tgt, add))
                continue
            where = '%s+0x%x' % (sec, off)
            place(m, content[(m.id, sec)], off, typ, tgt, add, base[(m.id, sec)] + off, where)
    tab = {}
    for c in tables:
        buf = bytearray(dsp_tables[c][1] if c in dsp_tables else b'')
        for _o, _id, _i, m, e in entries[c]:
            d = bytearray(e['data'])
            for off, typ, tgt, add in e['relocs']:
                place(m, d, off, typ, tgt, add, tab_at[c] + len(buf) + off, 'an entry of %s' % c)
            buf += d
        if c in dsp_tables:
            tab[c] = buf + dsp_tables[c][2]
            continue
        if c == dev.fast_table:
            for m in fast_mods:
                buf += struct.pack('>IIII', fload[m.id], base[(m.id, '.fast')],
                                   _align(m.size('.fast'), 4), 0)
        buf += bytes(tables[c][1])
        tab[c] = buf
    ddr = bytearray(run_end - ddr_base)
    for m in order:
        if m.size('.run'):
            o = base[(m.id, '.run')] - ddr_base
            ddr[o:o + m.size('.run')] = content[(m.id, '.run')]
        if m.id in fload:
            o = fload[m.id] - ddr_base
            ddr[o:o + m.size('.fast')] = content[(m.id, '.fast')]
        if m.size('.payload'):
            o = base[(m.id, '.payload')] - ddr_base
            ddr[o:o + m.size('.payload')] = content[(m.id, '.payload')]
    for c in tables:
        if c in dsp_tables:
            continue
        o = tab_at[c] - ddr_base
        ddr[o:o + len(tab[c])] = tab[c]
    glob['__run_hash'] = run_hash(ddr)
    for m, off, typ, tgt, add in boot_relocs:
        place(m, content[(m.id, '.boot')], off, typ, tgt, add, base[(m.id, '.boot')] + off,
              '.boot+0x%x' % off)
    blob = bytearray(content[(core.id, '.boot')])
    blob += bytes(run_load - image_end - len(blob))
    blob += ddr
    img = bytearray(image)

    def dsp_put(tag, word, data):              # P words of a payload's area: main OS bytes
        lo, _hi, at, _o, _n = areas[tag]
        o = at + 3 * (word - lo) - dev.main_load
        img[o:o + len(data)] = data
    for key, tag in dsp_secs.items():
        dsp_put(tag, base[key], content.get(key, b''))
    for c in dsp_tables:
        dsp_put(dsp_tables[c][0], tab_at[c], tab[c])
    for m in order:
        for s in m.sites:
            if sharc_site(rel, s['addr']):
                continue
            new = bytearray(s['new'])
            for off, typ, tgt, add in s['relocs']:
                place(m, new, off, typ, tgt, add, s['addr'] + off, 'site 0x%08x' % s['addr'])
            o = s['addr'] - dev.main_load
            img[o:o + s['len']] = new
    img += blob
    if new_lock is not None:
        img += store.trailer(new_lock)
    bad = _copied_rules(order, bytes(img), dev)
    if dev.isa == 'coldfire':
        # ColdFire stops with an address error on an odd jump target (an emulator may not): a jsr or jmp
        # abs.l in the mods' code, in a site or in the RAM image, must name an even address
        code = [(s['addr'], bytes(img[s['addr'] - dev.main_load:s['addr'] - dev.main_load + s['len']]),
                 '%s site 0x%08x' % (m.label(), s['addr'])) for m in order for s in m.sites if s['kind'] == 'code']
        code += [(base[(m.id, '.run')], bytes(ddr[base[(m.id, '.run')] - ddr_base:
                                                  base[(m.id, '.run')] - ddr_base + m.size('.run')]),
                  '%s .run' % m.label()) for m in order if m.size('.run')]
        for at, data, what in code:
            for i in range(0, len(data) - 5, 2):
                if data[i:i + 2] in (b'\x4e\xb9', b'\x4e\xf9'):
                    t = int.from_bytes(data[i + 2:i + 6], 'big')
                    if t % 2 and ddr_base <= t < bss_end:
                        bad.append('%s: a jsr/jmp at 0x%08x goes to the odd address 0x%08x (align the code: '
                                   '.balign 2 at the start of each .run section)' % (what, at + i, t))
    for m in order:
        for s in m.sites:
            if s['kind'] == 'code':
                ok, note = insn_check(bytes(img), s['addr'], s['len'], dev, sweeps=0)
                if not ok:
                    bad.append('%s site 0x%08x after relocation: %s' % (m.label(), s['addr'], note))
    declared = [(s['addr'], s['addr'] + s['len'], '%s site 0x%08x' % (m.label(), s['addr']))
                for m in order for s in m.sites if not sharc_site(rel, s['addr'])]
    for key, tag in sorted(dsp_secs.items(), key=lambda kv: base[kv[0]]):
        words = len(content.get(key, b'')) // 3
        if words:
            at = dsp.p_span(image, dev, tag, base[key], base[key] + words)
            declared.append((at, at + 3 * words, '%s %s' % key))
    for c in sorted(dsp_tables):
        tag, words = dsp_tables[c][0], len(tab[c]) // 3
        at = dsp.p_span(image, dev, tag, tab_at[c], tab_at[c] + words)
        declared.append((at, at + 3 * words, 'the table %s' % c))
    bad += image_problems(dev, image, img, declared, image_end)
    if bad:
        raise ModError('the linked mods break a rule:\n  ' + '\n  '.join(bad))

    out = Linked()
    out.image = bytes(img)
    out.sharc_stream, out.sharc_stored = dsp_stream, dsp_stored
    out.order = [m.label() for m in order]
    out.map = {}
    for (mid, nm), v in addr.items():
        out.map['%s:%s' % (mid, nm)] = v
    out.map.update(glob)
    uniq = {}
    for (mid, nm), v in addr.items():
        uniq.setdefault(nm, set()).add(v)
    for nm, vs in uniq.items():
        if len(vs) == 1 and nm not in out.map:
            out.map[nm] = vs.pop()
    out.tables = {c: (tab_at[c], table_count(c), tables[c][1]) for c in tables}
    out.layout = {
        'boot': [image_end, image_end + boot_len], 'run_load': run_load,
        'ddr': [ddr_base, run_end], 'bss': [run_end, bss_end],
        'fast': [fast_base, fast_at], 'blob_len': len(blob),
        'payload': [pay_at, pay_end] if pay_mods else None,
        'sections': {'%s %s' % k: v for k, v in sorted(base.items(), key=lambda kv: kv[1])
                     if k not in dsp_secs},
        'ddr_spare': ddr_end - bss_end, 'fast_spare': fast_end - fast_at,
        'ddr_size': ddr_end - ddr_base, 'fast_size': fast_end - fast_base,
    }
    if kernel_mods or sharc_mods:
        info = out.layout['sharc'] = {'decoded': len(dsp_stream), 'decoded_sha256': sha(dsp_stream),
                                      'stored': len(dsp_stored), 'stored_sha256': sha(dsp_stored)}
        if kernel_mods:
            info['kernels'] = kernel_layout
        else:
            info.update(owner=list(payload['owner']), app=sharc_mods[0].id)
            if payload['entries'] is not None:
                info.update(core=list(core.sharc['owner']), hook=rel.sharc['hook'][0])
    out.store, out.store_notes = new_lock, store_notes
    out.knobs = {mid: [b, next(m.knobs for m in order if m.id == mid)] for mid, b in sorted(knob_base.items())}
    out.knob_notes = knob_notes
    for mid, b in knob_base.items():
        out.map['%s:%s' % (mid, KNOBS)] = b
    if units is not None:                      # the reserve the core takes: units of dev.reserve
        out.layout['reserve'] = {'units': units, 'end': ddr_base + units * dev.reserve[0]}
    if areas:                                  # words: P addresses of each payload's area
        out.layout['dsp'] = {
            tag: {'area': [lo, hi], 'used': [lo, dsp_at[tag]], 'name': nm,
                  'sections': {'%s %s' % k: base[k] for k, t in sorted(dsp_secs.items())
                               if t == tag},
                  'tables': {c: tab_at[c] for c in sorted(dsp_tables) if dsp_tables[c][0] == tag}}
            for tag, (lo, hi, _a, _o, nm) in sorted(areas.items())}
    return out
