# SPDX-License-Identifier: GPL-2.0-or-later
"""Mod storage: the lockfile and the allocator (docs/ADAPTING.md, "Mod storage").

A core that keeps mod storage (core-ar2 0.2) imports `__store_gen`. Mods
declare fields in their mod.json:

    "storage": [{"sym": "my_accent", "scope": "track", "bits": 4}]

and the linker gives each field's symbol a descriptor: its scope in bits
28..31, its width in bits 22..27 and its bit offset in bits 0..21. Where a
field goes is decided here, from the build's lockfile, which keeps every
field where it was from one build to the next:

- a field keeps its place while its mod declares it with the same scope and
  width;
- a field no mod declares any more is freed: no other field is given its
  bits, so its values stay in the projects saved with it;
- a freed field back in the build with the same scope and width takes its
  old place again, with the values it had;
- any other new field, or one whose scope or width changed, takes bits no
  field of this lockfile has ever used, so it reads zero (its default)
  everywhere;
- when a scope's never-used bits run out, its generation goes up and its
  fields are packed again from bit 0: projects and library sounds from older
  generations load that scope at defaults;
- a scope's room can depend on the core's version (core-ar2 0.3 keeps 56
  bits a sound, 0.2 24; 0.3 is for OS 1.74 only, so a 1.73 build has 0.2's
  room): bits a newer core added come after the older
  core's, so its placements keep their meaning; a field that lies past what
  the build's core keeps repacks its scope;
- the tagged store's split (bytes per pattern track and per pattern) grows
  when its fields need it, which moves the store's regions after it, so the
  generations of the scopes that move go up (the patterns and the project;
  the pattern tracks too when their own size changes).

A layout's ID is a hash of the layout (layout_id): two lockfiles share an ID
only when they place every field and freed field alike, so the same mods
built from no lockfile always get the same layout and ID. Any change gives
the lockfile its new layout's ID and keeps the old one as an ancestor (the
newest 15). The core reads data written under its own ID or an ancestor's,
so a build that adds a mod still reads the projects of the build before it.

The lockfile is JSON:

    {"elekloader_store": 1, "device": "analog-rytm-mk2", "id": "0x5a17c0de",
     "ancestors": ["0x..."], "split": {"track": 2, "pattern": 4},
     "generation": {"sound": 1, ..., "project": 1},
     "next": {"sound": 0, ...},          the first never-used bit of each scope
     "fields": [{"mod", "sym", "scope", "offset", "bits"}],
     "freed": [{"mod", "sym", "scope", "offset", "bits"}]}

Nothing here is random: the same lockfile (or none) and mods always link to
the same bytes.
"""
import copy
import hashlib
import json
import re

from .elemod import ModError, _int, version_key

FORMAT = 1
SCOPES = ('sound', 'kit', 'global', 'track', 'pattern', 'project')
SCOPE_NUM = {s: i + 1 for i, s in enumerate(SCOPES)}
MAX_ANCESTORS = 15
MAX_BITS = 32
GEN_SYMS = ('__store_gen', '__store_gen2')
IDS_TABLE = 'core_store_ids'

# Per device: each scope's bits in its live home, and the tagged store.
DEVICES = {
    'analog-rytm-mk2': {
        'fixed': {'sound': 24, 'kit': 64, 'global': 40},
        'core_fixed': (('0.3', {'sound': 56}),),     # from that core's version on (0.3: OS 1.74 only)
        'live': {'track': 512, 'pattern': 512},     # the most the split can give an item
        'store': 4352 - 10,                          # bytes after the header
        'items': {'track': 129 * 13, 'pattern': 129},
        'split': {'track': 2, 'pattern': 4},         # a new lockfile's
    },
}


def supports(dev_key):
    return dev_key in DEVICES


def _spec(dev_key):
    if dev_key not in DEVICES:
        raise ModError('the %s has no mod storage' % dev_key)
    return DEVICES[dev_key]


def _fixed(sp, core):
    fixed = dict(sp['fixed'])
    for v, f in sp.get('core_fixed', ()):
        if core is None or version_key(core) >= version_key(v):
            fixed.update(f)
    return fixed


def capacity(dev_key, split, core=None):
    """-> {scope: bits} for a split, with a core of that version (None: the
    newest core's room)."""
    sp = _spec(dev_key)
    cap = _fixed(sp, core)
    cap['track'] = 8 * split['track']
    cap['pattern'] = 8 * split['pattern']
    left = sp['store'] - sp['items']['track'] * split['track'] - sp['items']['pattern'] * split['pattern']
    cap['project'] = 8 * left
    return cap


def _room_core(dev_key, split, scope, core):
    """The oldest core after `core` that gives `scope` the newest core's room,
    for a message: a version that exists, so the message stays true on an OS
    that core is not made for (whose refusal then says so)."""
    most = capacity(dev_key, split)[scope]
    for v, _f in sorted(_spec(dev_key).get('core_fixed', ()), key=lambda c: version_key(c[0])):
        if version_key(v) > version_key(core) and capacity(dev_key, split, v)[scope] == most:
            return v
    return None


def _idstr(v):
    return '0x%08x' % v


def _idval(s, what):
    v = _int(s, 'lockfile: ' + what)
    if not 0 < v <= 0xFFFFFFFF:
        raise ModError('lockfile: %s %r is not a 32-bit ID other than 0' % (what, s))
    return v


def parse_decls(m):
    """A mod document's "storage" -> [(sym, scope, bits)]. Raises ModError."""
    out, seen = [], set()
    st = m.get('storage', [])
    if not isinstance(st, list):
        raise ModError('"storage" is a list of fields')
    for f in st:
        if not isinstance(f, dict) or set(f) - {'sym', 'scope', 'bits'}:
            raise ModError('a storage field is {"sym", "scope", "bits"}')
        sym, scope, bits = f.get('sym'), f.get('scope'), f.get('bits')
        if not isinstance(sym, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', sym):
            raise ModError('storage field %r: "sym" is a C name' % (sym,))
        if scope not in SCOPES:
            raise ModError('storage field %s: scope %r (%s)' % (sym, scope, ', '.join(SCOPES)))
        if not _whole(bits) or not 1 <= bits <= MAX_BITS:
            raise ModError('storage field %s: bits %r (1 to %d)' % (sym, bits, MAX_BITS))
        if sym in seen:
            raise ModError('storage field %s is declared twice' % sym)
        seen.add(sym)
        out.append((sym, scope, bits))
    return out


def _field_key(f):
    return (SCOPE_NUM[f['scope']], f['offset'], f['bits'], f['mod'], f['sym'])


def layout_id(lock):
    """A layout's ID: the first 4 bytes of a SHA-256 of everything that says
    where data sits (not the ID or the ancestors), other than 0. Freed fields
    count: their bits still hold their values, which a field coming back
    reads."""
    what = [lock['elekloader_store'], lock['device'], [lock['split']['track'], lock['split']['pattern']],
            [lock['generation'][s] for s in SCOPES], [lock['next'][s] for s in SCOPES]]
    for k in ('fields', 'freed'):
        what.append([[f['scope'], f['offset'], f['bits'], f['mod'], f['sym']]
                     for f in sorted(lock[k], key=_field_key)])
    return int(hashlib.sha256(json.dumps(what).encode('ascii')).hexdigest()[:8], 16) or 1


def fresh(dev_key):
    sp = _spec(dev_key)
    lock = {'elekloader_store': FORMAT, 'device': dev_key, 'id': None,
            'ancestors': [], 'split': dict(sp['split']),
            'generation': {s: 1 for s in SCOPES}, 'next': {s: 0 for s in SCOPES},
            'fields': [], 'freed': []}
    lock['id'] = _idstr(layout_id(lock))
    return lock


def check_lock(lock, dev_key):
    """Validate a lockfile's shape. Raises ModError."""
    if not isinstance(lock, dict):
        raise ModError('lockfile: not a JSON object')
    v = lock.get('elekloader_store')
    if not _whole(v):
        raise ModError('lockfile: not an elekloader storage lockfile')
    if v > FORMAT:
        raise ModError('lockfile: format %d is newer than this elekloader (%d): update elekloader'
                       % (v, FORMAT))
    if v < 1:
        raise ModError('lockfile: format %r' % v)
    if lock.get('device') != dev_key:
        raise ModError('lockfile: made for the %s, not the %s' % (lock.get('device'), dev_key))
    _idval(lock.get('id'), 'id')
    anc = lock.get('ancestors', [])
    if not isinstance(anc, list):
        raise ModError('lockfile: "ancestors" is a list')
    for a in anc:
        _idval(a, 'ancestor')
    sp = lock.get('split')
    if not isinstance(sp, dict) or set(sp) != {'track', 'pattern'} or \
            not all(_whole(sp[k]) and sp[k] >= 0 for k in sp):
        raise ModError('lockfile: "split" gives bytes per track and per pattern')
    cap = capacity(dev_key, sp)
    if cap['project'] < 0 or 8 * sp['track'] > _spec(dev_key)['live']['track'] or \
            8 * sp['pattern'] > _spec(dev_key)['live']['pattern']:
        raise ModError('lockfile: the split %d/%d does not fit the store' % (sp['track'], sp['pattern']))
    for k in ('generation', 'next'):
        d = lock.get(k)
        if not isinstance(d, dict) or set(d) != set(SCOPES) or \
                not all(_whole(d[s]) for s in SCOPES):
            raise ModError('lockfile: "%s" gives a number per scope' % k)
    for s in SCOPES:
        if not 1 <= lock['generation'][s] <= 255:
            raise ModError('lockfile: generation %r of %s' % (lock['generation'][s], s))
        if not 0 <= lock['next'][s] <= cap[s]:
            raise ModError('lockfile: next bit %r of %s' % (lock['next'][s], s))
    for k in ('fields', 'freed'):
        fs = lock.get(k)
        if not isinstance(fs, list):
            raise ModError('lockfile: "%s" is a list' % k)
        for f in fs:
            if not isinstance(f, dict) or set(f) != {'mod', 'sym', 'scope', 'offset', 'bits'} \
                    or not isinstance(f['mod'], str) or not isinstance(f['sym'], str) \
                    or f['scope'] not in SCOPES or not _whole(f['offset']) or f['offset'] < 0 \
                    or not _whole(f['bits']) or not 1 <= f['bits'] <= MAX_BITS:
                raise ModError('lockfile: a bad entry in "%s"' % k)
            if k == 'fields' and not 0 <= f['offset'] <= lock['next'][f['scope']] - f['bits']:
                raise ModError('lockfile: field %s of %s lies past its scope\'s used bits'
                               % (f['sym'], f['mod']))


def _whole(v):
    return isinstance(v, int) and not isinstance(v, bool)


def _bump(g):
    return 1 if g >= 255 else g + 1


def allocate(lock, decls, dev_key, core=None):
    """lock: a lockfile (dict) or None for a new one; decls: [(mod id, sym,
    scope, bits)]; core: the build's core's version (None: the newest core's
    room). -> (the new lockfile, [notes]). The lockfile given is not
    changed."""
    notes = []
    if lock is None:
        lock = fresh(dev_key)
        new = True
    else:
        check_lock(lock, dev_key)
        lock = copy.deepcopy(lock)
        new = False
    want = {}
    for mid, sym, scope, bits in decls:
        if any(k[1] == sym for k in want):
            raise ModError('the storage field %s is declared by two mods' % sym)
        want[(mid, sym)] = (scope, bits)
    kept, gone = [], []
    for f in lock['fields']:
        if want.get((f['mod'], f['sym'])) == (f['scope'], f['bits']):
            kept.append(f)
        else:
            gone.append(f)
    have = {(f['mod'], f['sym']) for f in kept}
    added = [{'mod': mid, 'sym': sym, 'scope': want[(mid, sym)][0], 'offset': None,
              'bits': want[(mid, sym)][1]} for mid, sym in sorted(want) if (mid, sym) not in have]
    # a freed field back with the same scope and width takes its old place: no
    # field was given those bits since, so its values are still there
    back = set()
    for f in added:
        same = [g for g in lock['freed'] if (g['mod'], g['sym'], g['scope'], g['bits'])
                == (f['mod'], f['sym'], f['scope'], f['bits'])]
        if same:
            f['offset'] = same[-1]['offset']
            lock['freed'].remove(same[-1])
            back.add((f['mod'], f['sym']))
    for f in gone:
        lock['freed'].append(dict(f))
    fields = kept + added
    cap = capacity(dev_key, lock['split'], core)
    repacked = set()

    def repack(scope):
        repacked.add(scope)
        lock['generation'][scope] = _bump(lock['generation'][scope])
        lock['freed'] = [f for f in lock['freed'] if f['scope'] != scope]
        at = 0
        for f in sorted((f for f in fields if f['scope'] == scope),
                        key=lambda f: (f['mod'], f['sym'])):
            f['offset'] = at
            at += f['bits']
        lock['next'][scope] = at
        notes.append('%s storage repacked (generation %d): older projects and sounds load it '
                     'at defaults' % (scope, lock['generation'][scope]))

    def need(scope):
        return sum(f['bits'] for f in fields if f['scope'] == scope)

    def pending(scope):
        return [f for f in added if f['scope'] == scope and f['offset'] is None]

    # the split first: if the tracks or the patterns can't fit even packed, grow it
    sp = _spec(dev_key)
    split = dict(lock['split'])
    for k in ('track', 'pattern'):
        n = need(k)
        if n > sp['live'][k]:
            raise ModError('the mods need %d bits per %s; the %s keeps at most %d'
                           % (n, k, dev_key, sp['live'][k]))
        fits = lock['next'][k] + sum(f['bits'] for f in pending(k)) <= 8 * split[k]
        if not fits and n > 8 * split[k]:
            split[k] = -(-n // 8)
    if split != lock['split']:
        # the pattern tracks come first in the store: they move only when their own size changes
        moved = ('track', 'pattern', 'project') if split['track'] != lock['split']['track'] \
            else ('pattern', 'project')
        lock['split'] = split
        cap = capacity(dev_key, split, core)
        if cap['project'] < need('project'):
            raise ModError('the mods need %d bytes of pattern-track, pattern and project storage; '
                           'the %s has %d' % ((sp['items']['track'] * split['track']
                                              + sp['items']['pattern'] * split['pattern']
                                              + -(-need('project') // 8)), dev_key, sp['store']))
        notes.append('the tagged store now keeps %d B per pattern track and %d B per pattern'
                     % (split['track'], split['pattern']))
        for k in moved:
            repack(k)
    for s in SCOPES:
        if need(s) > cap[s]:
            more = capacity(dev_key, lock['split'])[s]
            raise ModError('the mods need %d bits of %s storage; the %s has %d%s'
                           % (need(s), s, dev_key, cap[s],
                              ' with core %s (%d with core %s)'
                              % (core, more, _room_core(dev_key, lock['split'], s, core))
                              if more > cap[s] else ''))
        if any(f['offset'] is not None and f['offset'] + f['bits'] > cap[s]
               for f in fields if f['scope'] == s):
            repack(s)                   # placed by a newer core, past what this one keeps
            continue
        pend = pending(s)
        if not pend:
            continue
        if lock['next'][s] + sum(f['bits'] for f in pend) <= cap[s]:
            for f in pend:
                f['offset'] = lock['next'][s]
                lock['next'][s] += f['bits']
        else:
            repack(s)
    for f in gone:
        if (f['mod'], f['sym']) in want or f['scope'] in repacked:
            notes.append('%s %s (%s): %s, its data is dropped'
                         % (f['mod'], f['sym'], f['scope'],
                            'changed size' if (f['mod'], f['sym']) in want else 'no longer in the build'))
        else:
            notes.append('%s %s (%s): no longer in the build; its bits are kept, so it gets its '
                         'values back if it returns' % (f['mod'], f['sym'], f['scope']))
    for f in added:
        if (f['mod'], f['sym']) in back and f['scope'] not in repacked:
            notes.append('%s %s: back at bit %d of %s storage, with the values it had'
                         % (f['mod'], f['sym'], f['offset'], f['scope']))
        else:
            notes.append('%s %s: %d bits of %s storage at bit %d'
                         % (f['mod'], f['sym'], f['bits'], f['scope'], f['offset']))
    lock['fields'] = sorted(fields, key=lambda f: (SCOPE_NUM[f['scope']], f['offset']))
    if new:
        lock['id'] = _idstr(layout_id(lock))
        notes.insert(0, 'a new lockfile, layout ID %s: projects and sounds saved by builds with '
                        'other layouts load their mod data at defaults' % lock['id'])
    elif gone or added or repacked:     # a repack alone (a field past an older core's room) moves fields too
        old = lock['id']
        lock['id'] = _idstr(layout_id(lock))
        seen, anc = {_idval(lock['id'], 'id')}, []
        for a in [old] + lock['ancestors']:        # a layout can come back: no ID twice
            v = _idval(a, 'ancestor')
            if v not in seen:
                seen.add(v)
                anc.append(_idstr(v))
        lock['ancestors'] = anc[:MAX_ANCESTORS]
        notes.append('layout ID %s (from %s, whose projects it still reads)' % (lock['id'], old))
    return lock, notes


def symbols(lock, dev_key):
    """-> ({name: value}, [table entries as bytes]) for the linker."""
    g, sp = lock['generation'], lock['split']
    out = {
        '__store_gen': g['sound'] << 24 | g['kit'] << 16 | g['global'] << 8 | g['track'],
        '__store_gen2': g['pattern'] << 24 | g['project'] << 16 | sp['track'] << 8 | sp['pattern'],
    }
    for f in lock['fields']:
        out[f['sym']] = SCOPE_NUM[f['scope']] << 28 | f['bits'] << 22 | f['offset']
    ids = [_idval(lock['id'], 'id')] + [_idval(a, 'ancestor') for a in lock['ancestors']]
    return out, [i.to_bytes(4, 'big') for i in ids]


def dumps(lock):
    return json.dumps(lock, indent=1) + '\n'


TRAILER = b'ELKSTORE'


def trailer(lock):
    """The lockfile as the build carries it, after the mods' image: TRAILER, its
    length (4 bytes, big-endian), then dumps(lock). Nothing reads it on the unit:
    it is there so the lockfile can be taken back out of the build (from_image)."""
    text = dumps(lock).encode('ascii')
    return TRAILER + len(text).to_bytes(4, 'big') + text


def from_image(image):
    """A built main OS image -> the lockfile it carries (a dict), or None."""
    at = bytes(image).rfind(TRAILER)
    if at < 0:
        return None
    n = int.from_bytes(image[at + 8:at + 12], 'big')
    try:
        lock = json.loads(bytes(image[at + 12:at + 12 + n]).decode('ascii'))
    except (ValueError, UnicodeDecodeError):
        raise ModError('the build\'s lockfile cannot be read')
    check_lock(lock, lock.get('device') if isinstance(lock, dict) else None)
    return lock
