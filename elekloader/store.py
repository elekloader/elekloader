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

Lockable parameters (core-ar2 0.8). A sound-scope field declared with
`"lock": true` also gets a lock code, 42..127, the number stock-style lock
rows, the LFO destination list and NRPN MSB 4 know the parameter by. Codes
follow the fields: a field keeps its code while it keeps its place; a freed
field's code is kept for it (and comes back with it) until the never-used
codes run out, and only then given to another field; at most 86 fields can
be lockable. The lockfile keeps them in three more keys, written only once a
build has a lockable field (a lockfile without them has the same layout ID
as before):

     "codes": [{"mod", "sym", "code"}], "freed_codes": [{"mod", "sym", "code"}],
     "next_code": 42

The core reads them from the table core_lock_codes: {field descriptor, code}
for each lockable field.

MIDI CCs (core-ar2 0.8). A lockable field declared with `"cc": true` also
gets a CC from the device's pool (the CCs stock leaves unused on a track's
channel), which it keeps from one build to the next whatever happens to its
place or code: a CC is not stored data, so it is not part of the layout ID.
A field out of the build keeps its CC for when it comes back until the
never-used CCs run out; then the CC freed longest ago is given to another
field; when none is left a field gets no CC (a note says so) and is on
MIDI by its NRPN only. The lockfile keeps them in two more keys, written once
a build has such a field:

     "ccs": [{"mod", "sym", "cc"}], "freed_ccs": [{"mod", "sym", "cc"}]

The core reads them from the table core_lock_ccs: {field descriptor, CC}.

The LFO destination (core-ar2 0.8). A lockable field declared with
`"lfo": true` is also an LFO destination. Stock saves a sound's LFO
destination as an index into a table of its own parameters, which has no
entry for a lock code, so a core that keeps a mod parameter there imports
the field core_lfo_dst: in a build with an `"lfo"` field the linker places
it, as the core's own sound field of the device's `lock_dst` bits, and the
core keeps the code in it; in a build without one it is not placed (its
symbol is 0) and the mods have the whole room. The core reads the flag from
core_lock_codes: an `"lfo"` field's code has LFO_FLAG set. The flag is not
stored data, so it is not part of the layout ID.

The smoothed value (core-ar2 0.9). The core smooths a lockable parameter's
value as stock smooths its own; a lockable field declared `"smooth": false`
jumps (a type, a mode). Its code has SMOOTH_OFF set in core_lock_codes. Like
LFO_FLAG it is not stored data, so it is not part of the layout ID.

Velocity and aftertouch destinations (core-ar2 0.9). A lockable field
declared `"mod": true` is also a destination in each sound's VELOCITY MOD and
AFTERTOUCH lists (one flag for both: stock builds them with one routine). A
route keeps its destination as a code in its own byte, which the core's
code-to-file hook already passes, so the field needs no room. Its code has
ROUTE_FLAG set in core_lock_codes; not stored data either.

The FX track (core-ar2 0.9). A kit-scope field can be lockable too: it is a
parameter of the FX track (track 12), which the kit holds, with a lock code
from the same 42..127. It can have a CC, from the same pool less the CCs
stock maps on the FX channel (`fx_ccs_taken`), and be an LFO destination of
the FX LFO, but not a velocity or aftertouch destination: the FX track has
no routes. The FX LFO keeps its destination in a kit byte through a table
of the FX parameters, with no entry for a code either, so a core that keeps
a kit-scope "lfo" field there imports core_fx_lfo_dst: the linker places it,
as the core's own kit field of the device's `fx_lock_dst` bits, in a build
with a kit-scope "lfo" field (0 otherwise).

Field defaults (core-ar2 0.10). A field declared with `"init": N` (0 < N <
2**bits) reads N where its bits are zero: in a new project, after its scope
was reset, and in data saved by another layout. The core keeps a field's
value XOR its init (store bits = value ^ init), so every value round-trips
and everything that goes through core_store_get and core_store_set (p-locks,
scenes, MIDI, the smoothed value, RANDOMIZE and RELOAD PAGE) works on the
value. A field's init is part of where its data means what it says, so it is
kept with the field in the lockfile ("init", written only when not 0) and is
part of the layout ID: a field whose init changes is a new field, like one
whose width changed. The descriptor of a field with an init carries its slot
in core_field_inits (1..63, bits 16..21; 0 for none): no scope keeps more
than 2**16 bits, so the offset never needs those bits. A lockfile with an
init is format 2, which an older elekloader refuses instead of misreading.

Handles (core-ar2 0.11). A field declared `"handle": true` holds a mod's
handle on something kept elsewhere (for the Sidecar mods, a record on the
Pi), 0 for none. Nothing about its bits changes: it is placed, kept and read
like any field, it is not in the lockfile and not part of the layout ID, and
the same field with or without the mark links to the same bytes. The linker
gives the core the table core_handles, each handle field's descriptor in the
lockfile's order, which core_handle_fields walks over every item of the
project, so a mod can list every handle a project uses. A handle is not a
parameter (no "lock") and has no init (0 is none).

Nothing here is random: the same lockfile (or none) and mods always link to
the same bytes.
"""
import copy
import hashlib
import json
import re

from .elemod import ModError, _int, version_key

FORMAT = 2                                      # 2 only for a lockfile with a field's init (core-ar2 0.10)
SCOPES = ('sound', 'kit', 'global', 'track', 'pattern', 'project')
SCOPE_NUM = {s: i + 1 for i, s in enumerate(SCOPES)}
MAX_ANCESTORS = 15
MAX_BITS = 32
GEN_SYMS = ('__store_gen', '__store_gen2')
IDS_TABLE = 'core_store_ids'
CODES_TABLE = 'core_lock_codes'
CCS_TABLE = 'core_lock_ccs'
LOCK_DST = 'core_lfo_dst'
FX_LOCK_DST = 'core_fx_lfo_dst'                 # core-ar2 0.9: the FX LFO's code, a kit field
LFO_FLAG = 0x100                                # in core_lock_codes: the field is an LFO destination
SMOOTH_OFF = 0x200                              # in core_lock_codes: the field's value jumps (core-ar2 0.9)
ROUTE_FLAG = 0x400                              # in core_lock_codes: a velocity/aftertouch destination (core-ar2 0.9)
FIRST_CODE, LAST_CODE = 42, 127
INITS_TABLE = 'core_field_inits'                # core-ar2 0.10: the fields' inits, by slot
HANDLES_TABLE = 'core_handles'                  # core-ar2 0.11: the handle fields' descriptors
MAX_INITS = 63                                  # slots 1..63, in the descriptor's bits 16..21
LOCK_SCOPES = ('sound', 'kit')

# Per device: each scope's bits in its live home, and the tagged store.
DEVICES = {
    'analog-rytm-mk2': {
        'fixed': {'sound': 24, 'kit': 64, 'global': 40},
        'core_fixed': (('0.3', {'sound': 56}),),     # from that core's version on (0.3: OS 1.74 only)
        'live': {'track': 512, 'pattern': 512},     # the most the split can give an item
        'store': 4352 - 10,                          # bytes after the header
        'items': {'track': 129 * 13, 'pattern': 129},
        'split': {'track': 2, 'pattern': 4},         # a new lockfile's
        # the CCs no table of stock's maps on a track's channel and that are not
        # the performances' (0x401de1f4), in the order fields get them
        'ccs': (8, 9) + tuple(range(48, 64)) + tuple(range(65, 70)) + (84, 85) + tuple(range(110, 117)) + (119,),
        'lock_dst': 7,                              # core_lfo_dst: a code - 41, 0 for none
        'fx_lock_dst': 7,                           # core_fx_lfo_dst (core-ar2 0.9): the same, for the FX LFO
        # of the pool, the CCs stock maps on the FX channel (0x413dcb14): a kit field (the FX track's) gets none of them
        'fx_ccs_taken': (84, 85),
    },
    'analog-four-mk2': {                            # core-a4 (mods/core-a4/store.c)
        'fixed': {'sound': 32, 'kit': 128, 'global': 64},
        'live': {'track': 512, 'pattern': 512},
        'store': 4352 - 10,
        'items': {'track': 129 * 6, 'pattern': 129},
        'split': {'track': 2, 'pattern': 4},
    },
}


def supports(dev_key):
    return dev_key in DEVICES


def lock_dst_bits(dev_key):
    """The bits of core_lfo_dst on the device, 0 where the core keeps none."""
    return _spec(dev_key).get('lock_dst', 0)


def fx_lock_dst_bits(dev_key):
    """The bits of core_fx_lfo_dst on the device, 0 where the core keeps none."""
    return _spec(dev_key).get('fx_lock_dst', 0)


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


def parse_locks(m):
    """A mod document's "storage" -> the syms of its lockable fields, in order.
    parse_decls checks the rest."""
    return [f['sym'] for f in m.get('storage', []) if isinstance(f, dict) and f.get('lock') is True]


def parse_lfos(m):
    """A mod document's "storage" -> the syms of its fields that are LFO destinations, in order."""
    return [f['sym'] for f in m.get('storage', []) if isinstance(f, dict) and f.get('lfo') is True]


def parse_rough(m):
    """A mod document's "storage" -> the syms of its fields declared "smooth": false, in order."""
    return [f['sym'] for f in m.get('storage', []) if isinstance(f, dict) and f.get('smooth') is False]


def parse_routes(m):
    """A mod document's "storage" -> the syms of its fields declared "mod": true (velocity and aftertouch
    destinations), in order."""
    return [f['sym'] for f in m.get('storage', []) if isinstance(f, dict) and f.get('mod') is True]


def parse_ccs(m):
    """A mod document's "storage" -> the syms of its fields with a CC, in order."""
    return [f['sym'] for f in m.get('storage', []) if isinstance(f, dict) and f.get('cc') is True]


def parse_inits(m):
    """A mod document's "storage" -> {sym: init} of its fields with an init other than 0.
    parse_decls checks the rest."""
    return {f['sym']: f['init'] for f in m.get('storage', []) if isinstance(f, dict) and f.get('init')}


def parse_handles(m):
    """A mod document's "storage" -> the syms of its fields declared "handle": true, in order."""
    return [f['sym'] for f in m.get('storage', []) if isinstance(f, dict) and f.get('handle') is True]


def parse_decls(m):
    """A mod document's "storage" -> [(sym, scope, bits)]. Raises ModError."""
    out, seen = [], set()
    st = m.get('storage', [])
    if not isinstance(st, list):
        raise ModError('"storage" is a list of fields')
    for f in st:
        if not isinstance(f, dict) or set(f) - {'sym', 'scope', 'bits', 'lock', 'cc', 'lfo', 'smooth', 'mod', 'init',
                                                'handle'}:
            raise ModError('a storage field is {"sym", "scope", "bits"}, and "lock" for a lockable one')
        for k in ('lock', 'cc', 'lfo', 'smooth', 'mod', 'handle'):
            if k in f and not isinstance(f[k], bool):
                raise ModError('storage field %r: "%s" is true or false' % (f.get('sym'), k))
        if f.get('cc') and not f.get('lock'):
            raise ModError('storage field %r: only a lockable field ("lock": true) can have a CC'
                           % (f.get('sym'),))
        if f.get('lfo') and not f.get('lock'):
            raise ModError('storage field %r: only a lockable field ("lock": true) can be an LFO destination'
                           % (f.get('sym'),))
        if f.get('mod') and not f.get('lock'):
            raise ModError('storage field %r: only a lockable field ("lock": true) can be a velocity or aftertouch '
                           'destination' % (f.get('sym'),))
        if 'smooth' in f and not f.get('lock'):
            raise ModError('storage field %r: only a lockable field ("lock": true) is smoothed'
                           % (f.get('sym'),))
        if f.get('handle') and f.get('lock'):
            raise ModError('storage field %r: a handle ("handle": true) is not a parameter: it cannot be lockable'
                           % (f.get('sym'),))
        if f.get('handle') and f.get('init'):
            raise ModError('storage field %r: a handle ("handle": true) has no init: 0 is none'
                           % (f.get('sym'),))
        if f.get('lock') and f.get('scope') not in LOCK_SCOPES:
            raise ModError('storage field %r: only %s fields can be lockable'
                           % (f.get('sym'), ' and '.join(LOCK_SCOPES)))
        if f.get('mod') and f.get('scope') == 'kit':
            raise ModError('storage field %r: a kit field (the FX track\'s) cannot be a velocity or aftertouch '
                           'destination: the FX track has no routes' % (f.get('sym'),))
        sym, scope, bits = f.get('sym'), f.get('scope'), f.get('bits')
        if not isinstance(sym, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', sym):
            raise ModError('storage field %r: "sym" is a C name' % (sym,))
        if scope not in SCOPES:
            raise ModError('storage field %s: scope %r (%s)' % (sym, scope, ', '.join(SCOPES)))
        if not _whole(bits) or not 1 <= bits <= MAX_BITS:
            raise ModError('storage field %s: bits %r (1 to %d)' % (sym, bits, MAX_BITS))
        if 'init' in f and (not _whole(f['init']) or not 0 <= f['init'] < 1 << bits):
            raise ModError('storage field %s: init %r (0 to %d, what its %d bits hold)'
                           % (sym, f['init'], (1 << bits) - 1, bits))
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
    for k in ('fields', 'freed'):       # a field's init counts only where there is one: older IDs stay
        what.append([[f['scope'], f['offset'], f['bits'], f['mod'], f['sym']] + ([f['init']] if f.get('init') else [])
                     for f in sorted(lock[k], key=_field_key)])
    if lock.get('codes') or lock.get('freed_codes'):   # a lockfile with no codes keeps its ID
        what.append([lock['next_code']] + [[c['code'], c['mod'], c['sym']] for k in ('codes', 'freed_codes')
                                           for c in sorted(lock[k], key=lambda c: c['code'])])
    return int(hashlib.sha256(json.dumps(what).encode('ascii')).hexdigest()[:8], 16) or 1


def fresh(dev_key):
    sp = _spec(dev_key)
    lock = {'elekloader_store': 1, 'device': dev_key, 'id': None,
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
            if not isinstance(f, dict) or set(f) - {'init'} != {'mod', 'sym', 'scope', 'offset', 'bits'} \
                    or not isinstance(f['mod'], str) or not isinstance(f['sym'], str) \
                    or f['scope'] not in SCOPES or not _whole(f['offset']) or f['offset'] < 0 \
                    or not _whole(f['bits']) or not 1 <= f['bits'] <= MAX_BITS \
                    or 'init' in f and (v < 2 or not _whole(f['init']) or not 0 < f['init'] < 1 << f['bits']):
                raise ModError('lockfile: a bad entry in "%s"' % k)
            if k == 'fields' and not 0 <= f['offset'] <= lock['next'][f['scope']] - f['bits']:
                raise ModError('lockfile: field %s of %s lies past its scope\'s used bits'
                               % (f['sym'], f['mod']))
    if any(k in lock for k in ('codes', 'freed_codes', 'next_code')):
        nc = lock.get('next_code')
        if not _whole(nc) or not FIRST_CODE <= nc <= LAST_CODE + 1:
            raise ModError('lockfile: "next_code" %r' % (nc,))
        seen = set()
        for k in ('codes', 'freed_codes'):
            cs = lock.get(k)
            if not isinstance(cs, list):
                raise ModError('lockfile: "%s" is a list' % k)
            for c in cs:
                if not isinstance(c, dict) or set(c) != {'mod', 'sym', 'code'} \
                        or not isinstance(c['mod'], str) or not isinstance(c['sym'], str) \
                        or not _whole(c['code']) or not FIRST_CODE <= c['code'] < nc or c['code'] in seen:
                    raise ModError('lockfile: a bad entry in "%s"' % k)
                seen.add(c['code'])
        placed = {(f['mod'], f['sym']) for f in lock['fields'] if f['scope'] in LOCK_SCOPES}
        if any((c['mod'], c['sym']) not in placed for c in lock['codes']):
            raise ModError('lockfile: a code for a field that is not a placed sound or kit field')
    if 'ccs' in lock or 'freed_ccs' in lock:
        pool, seen = _spec(dev_key).get('ccs', ()), set()
        for k in ('ccs', 'freed_ccs'):
            cs = lock.get(k)
            if not isinstance(cs, list):
                raise ModError('lockfile: "%s" is a list' % k)
            for c in cs:
                if not isinstance(c, dict) or set(c) != {'mod', 'sym', 'cc'} \
                        or not isinstance(c['mod'], str) or not isinstance(c['sym'], str) \
                        or not _whole(c['cc']) or c['cc'] not in pool or c['cc'] in seen:
                    raise ModError('lockfile: a bad entry in "%s"' % k)
                seen.add(c['cc'])
        kits = {(f['mod'], f['sym']) for f in lock['fields'] if f['scope'] == 'kit'}
        if any((c['mod'], c['sym']) in kits and c['cc'] in _spec(dev_key).get('fx_ccs_taken', ())
               for c in lock['ccs']):
            raise ModError('lockfile: a kit field with a CC stock uses on the FX channel')
        coded = {(c['mod'], c['sym']) for c in lock.get('codes', [])}
        if any((c['mod'], c['sym']) not in coded for c in lock['ccs']):
            raise ModError('lockfile: a CC for a field that has no lock code')


def _whole(v):
    return isinstance(v, int) and not isinstance(v, bool)


def _bump(g):
    return 1 if g >= 255 else g + 1


def allocate(lock, decls, dev_key, core=None, locks=(), ccs=(), inits=None):
    """lock: a lockfile (dict) or None for a new one; decls: [(mod id, sym,
    scope, bits)]; core: the build's core's version (None: the newest core's
    room); locks: [(mod id, sym)] of the lockable fields; ccs: those of them
    with a CC; inits: {(mod id, sym): init} of the fields with an init other
    than 0. -> (the new lockfile, [notes]). The lockfile given is not
    changed."""
    inits = {k: v for k, v in (inits or {}).items() if v}
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
        want[(mid, sym)] = (scope, bits, inits.get((mid, sym), 0))
    kept, gone = [], []
    for f in lock['fields']:
        if want.get((f['mod'], f['sym'])) == (f['scope'], f['bits'], f.get('init', 0)):
            kept.append(f)
        else:
            gone.append(f)
    have = {(f['mod'], f['sym']) for f in kept}
    added = [dict({'mod': mid, 'sym': sym, 'scope': want[(mid, sym)][0], 'offset': None,
                   'bits': want[(mid, sym)][1]}, **({'init': want[(mid, sym)][2]} if want[(mid, sym)][2] else {}))
             for mid, sym in sorted(want) if (mid, sym) not in have]
    # a freed field back with the same scope and width takes its old place: no
    # field was given those bits since, so its values are still there
    back = set()
    for f in added:
        same = [g for g in lock['freed'] if (g['mod'], g['sym'], g['scope'], g['bits'], g.get('init', 0))
                == (f['mod'], f['sym'], f['scope'], f['bits'], f.get('init', 0))]
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
            w = want.get((f['mod'], f['sym']))
            notes.append('%s %s (%s): %s, its data is dropped'
                         % (f['mod'], f['sym'], f['scope'],
                            'no longer in the build' if w is None
                            else 'changed its init' if w[:2] == (f['scope'], f['bits']) else 'changed size'))
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
    # format 2 from the first init: an older elekloader refuses it rather than reading the inits' fields as 0
    if any(f.get('init') for k in ('fields', 'freed') for f in lock[k]):
        lock['elekloader_store'] = 2
    recoded = _codes(lock, set(locks), {(f['mod'], f['sym']) for f in kept},
                     {(f['mod'], f['sym']) for f in added if (f['mod'], f['sym']) in back}, notes)
    _ccs(lock, set(ccs), _spec(dev_key).get('ccs', ()), notes,
         {(f['mod'], f['sym']) for f in fields if f['scope'] == 'kit'}, _spec(dev_key).get('fx_ccs_taken', ()))
    if new:
        lock['id'] = _idstr(layout_id(lock))
        notes.insert(0, 'a new lockfile, layout ID %s: projects and sounds saved by builds with '
                        'other layouts load their mod data at defaults' % lock['id'])
    elif gone or added or repacked or recoded:  # a repack alone (a field past an older core's room) moves fields too
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


def _codes(lock, want, kept, back, notes):
    """Give each lockable field (mod, sym) in want its lock code, in lock
    (changed in place). kept: the fields that kept their place; back: freed
    fields that came back to theirs. -> whether any code changed."""
    had = lock.get('codes', []), lock.get('freed_codes', [])
    if not want and not had[0] and not had[1]:
        return False                    # a lockfile that never had a lockable field stays as it was
    codes, freed = [], [dict(c) for c in had[1]]
    nxt = lock.get('next_code', FIRST_CODE)
    changed = False
    for c in had[0]:
        k = (c['mod'], c['sym'])
        if k in want and k in kept:
            codes.append(dict(c))
        else:
            freed.append(dict(c))
            changed = True
            notes.append('%s %s: lock code %d %s' % (c['mod'], c['sym'], c['code'],
                         'kept for it while it is out of the build' if k not in want or k not in kept
                         else 'freed'))
    have = {(c['mod'], c['sym']) for c in codes}
    for k in sorted(want - have):
        same = [c for c in freed if (c['mod'], c['sym']) == k]
        if same and k in back:          # back in its old place: its old code, with its rows
            c = same[-1]
            freed.remove(c)
            notes.append('%s %s: back with lock code %d' % (k[0], k[1], c['code']))
        elif nxt <= LAST_CODE:
            c = {'mod': k[0], 'sym': k[1], 'code': nxt}
            nxt += 1
            notes.append('%s %s: lock code %d' % (k[0], k[1], c['code']))
        elif freed:                     # the never-used codes are gone: the oldest freed one
            old = freed.pop(0)
            c = {'mod': k[0], 'sym': k[1], 'code': old['code']}
            notes.append('%s %s: lock code %d (was %s %s\'s)' % (k[0], k[1], c['code'], old['mod'], old['sym']))
        else:
            raise ModError('the mods have more than %d lockable parameters' % (LAST_CODE - FIRST_CODE + 1))
        for d in [d for d in freed if (d['mod'], d['sym']) == k]:
            freed.remove(d)             # a field has one code
        codes.append(c)
        changed = True
    lock['codes'] = sorted(codes, key=lambda c: c['code'])
    lock['freed_codes'] = freed
    lock['next_code'] = nxt
    return changed


def _ccs(lock, want, pool, notes, kits=(), fx_taken=()):
    """Give each field (mod, sym) in want its CC from pool, in lock (changed
    in place); a field in kits (the FX track's) none in fx_taken. Not part of
    the layout ID: a CC says nothing of where data is."""
    had = lock.get('ccs', []), lock.get('freed_ccs', [])
    if not want and not had[0] and not had[1]:
        return
    ccs, freed = [], [dict(c) for c in had[1]]
    for c in had[0]:
        if (c['mod'], c['sym']) in want and not ((c['mod'], c['sym']) in kits and c['cc'] in fx_taken):
            ccs.append(dict(c))
        else:
            freed.append(dict(c))
            notes.append('%s %s: CC %d freed, and kept for it until the unused CCs run out'
                         % (c['mod'], c['sym'], c['cc']))
    used = {c['cc'] for c in ccs} | {c['cc'] for c in freed}
    unused = [cc for cc in pool if cc not in used]
    have = {(c['mod'], c['sym']) for c in ccs}
    for k in sorted(want - have):
        ok = (lambda cc: cc not in fx_taken) if k in kits else (lambda cc: True)
        same = [c for c in freed if (c['mod'], c['sym']) == k and ok(c['cc'])]
        new = [cc for cc in unused if ok(cc)]
        old = [c for c in freed if ok(c['cc'])]
        if same:
            c = same[-1]
            freed.remove(c)
            notes.append('%s %s: back with CC %d' % (k[0], k[1], c['cc']))
        elif new:
            unused.remove(new[0])
            c = {'mod': k[0], 'sym': k[1], 'cc': new[0]}
            notes.append('%s %s: CC %d' % (k[0], k[1], c['cc']))
        elif old:                       # the never-used CCs are gone: the one freed longest ago
            old = old[0]
            freed.remove(old)
            c = {'mod': k[0], 'sym': k[1], 'cc': old['cc']}
            notes.append('%s %s: CC %d (was %s %s\'s)' % (k[0], k[1], c['cc'], old['mod'], old['sym']))
        else:
            notes.append('%s %s: no CC left (all %d are taken); it is on MIDI by its NRPN only'
                         % (k[0], k[1], len(pool)))
            continue
        ccs.append(c)
    lock['ccs'] = sorted(ccs, key=lambda c: c['cc'])
    lock['freed_ccs'] = freed


def _inited(lock):
    """-> the placed fields with an init, in their slots' order (slot 1 first). Raises ModError."""
    out = [f for f in lock['fields'] if f.get('init')]
    if len(out) > MAX_INITS:
        raise ModError('the mods give %d storage fields an init; the core keeps at most %d'
                       % (len(out), MAX_INITS))
    return out


def descriptors(lock):
    """-> {(mod, sym): descriptor} of the placed fields: scope in bits 28..31,
    width in 22..27, the init's slot in 16..21 (0: none), the offset in
    0..15."""
    slot = {(f['mod'], f['sym']): i + 1 for i, f in enumerate(_inited(lock))}
    return {(f['mod'], f['sym']): SCOPE_NUM[f['scope']] << 28 | f['bits'] << 22
            | slot.get((f['mod'], f['sym']), 0) << 16 | f['offset'] for f in lock['fields']}


def init_entries(lock):
    """-> the table core_field_inits' entries: each init, 4 bytes, by slot."""
    return [f['init'].to_bytes(4, 'big') for f in _inited(lock)]


def handle_entries(lock, handles=()):
    """-> the table core_handles' entries: the descriptor of each placed field in handles
    ((mod, sym)), 4 bytes each, in the lockfile's order."""
    desc, handles = descriptors(lock), set(handles)
    return [desc[(f['mod'], f['sym'])].to_bytes(4, 'big') for f in lock['fields']
            if (f['mod'], f['sym']) in handles]


def symbols(lock, dev_key):
    """-> ({name: value}, [table entries as bytes]) for the linker."""
    g, sp = lock['generation'], lock['split']
    out = {
        '__store_gen': g['sound'] << 24 | g['kit'] << 16 | g['global'] << 8 | g['track'],
        '__store_gen2': g['pattern'] << 24 | g['project'] << 16 | sp['track'] << 8 | sp['pattern'],
    }
    for (mod, sym), d in descriptors(lock).items():
        out[sym] = d
    ids = [_idval(lock['id'], 'id')] + [_idval(a, 'ancestor') for a in lock['ancestors']]
    return out, [i.to_bytes(4, 'big') for i in ids]


def code_entries(lock, lfos=(), rough=(), routes=()):
    """-> the table core_lock_codes' entries: {field descriptor, code}, 8 bytes
    each, by code; the code of a field in lfos ((mod, sym)) has LFO_FLAG set,
    of one in rough SMOOTH_OFF, of one in routes ROUTE_FLAG."""
    desc = descriptors(lock)
    lfos, rough, routes = set(lfos), set(rough), set(routes)
    return [desc[(c['mod'], c['sym'])].to_bytes(4, 'big')
            + (c['code'] | (LFO_FLAG if (c['mod'], c['sym']) in lfos else 0)
               | (SMOOTH_OFF if (c['mod'], c['sym']) in rough else 0)
               | (ROUTE_FLAG if (c['mod'], c['sym']) in routes else 0)).to_bytes(4, 'big')
            for c in lock.get('codes', [])]


def cc_entries(lock):
    """-> the table core_lock_ccs' entries: {field descriptor, CC}, 8 bytes
    each, by CC."""
    desc = descriptors(lock)
    return [desc[(c['mod'], c['sym'])].to_bytes(4, 'big') + c['cc'].to_bytes(4, 'big')
            for c in lock.get('ccs', [])]


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
