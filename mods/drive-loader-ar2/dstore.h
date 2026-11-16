/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Drive Loader: +Drive storage, the boot allocator and /mods/.store (docs/DRIVE.md, "+Drive
 * storage"). A port of elekloader/drive_store.py, rule for rule: the same inputs give the same
 * placements, table entries and .store bytes (tests/drive_store_host.c replays
 * js/test/drivestore-vectors.json). Pure: memory only through dl_alloc and dl_free, no files.
 *
 * The notes drive_store.py writes for a person are not made here; .status gives counts. */
#ifndef DSTORE_H
#define DSTORE_H
#include "dl.h"

#define DS_SCOPES 6                      /* sound, kit, global, track, pattern, project (1..6) */
#define DS_FIRST_CODE 42                 /* the Analog Rytm MKII's lock codes, 42..127 */
#define DS_LAST_CODE 127
#define DS_MAX_IDS 15                    /* ancestors kept */
#define DS_MAX_INITS 63                  /* core_field_inits' slots 1..63 */
#define DS_MAX_BITS 32
#define DS_NAME_MAX 255                  /* a mod id or a symbol in .store, in bytes */
/* the loader's own room for a .store it reads (a file past it reads as none: a fresh placement) */
#define DS_MAX_FIELDS 512                /* fields, and again parked fields */
#define DS_MAX_TAGS 256                  /* codes, freed codes, CCs, freed CCs: each */
#define DS_MAX_FILE 0x10000              /* /mods/.store's most bytes */

/* core_lock_codes' flags */
#define DS_LFO_FLAG 0x100
#define DS_SMOOTH_OFF 0x200
#define DS_ROUTE_FLAG 0x400
#define DS_LFO_DST_BITS 7                /* the core's LFO destination fields (store.py lock_dst) */

/* a declared field's flags (mod.json "storage") */
enum { DS_LOCK = 1, DS_CC = 2, DS_LFO = 4, DS_ROUGH = 8, DS_ROUTE = 16, DS_HANDLE = 32 };

/* what fit() finds short, in the order it checks them */
enum { DS_W_SOUND, DS_W_KIT, DS_W_GLOBAL, DS_W_TRACK, DS_W_PATTERN, DS_W_PROJECT, DS_W_CODES,
       DS_W_CODES_TABLE, DS_W_INITS_TABLE, DS_W_HANDLES_TABLE, DS_W_N };
extern const char *const ds_what[DS_W_N];   /* "sound" ... "codes", "core_lock_codes" ... */

/* The flashed build, as the boot sees it (drive_store.Base). */
struct ds_base {
    u32 room[DS_SCOPES], used[DS_SCOPES], gen[DS_SCOPES];   /* the descriptor's storage record */
    u32 flashed_id;                      /* core_store_ids[0] as flashed */
    const u32 *anc;                      /* its ancestors (core_store_ids[1..]) */
    u32 nanc;
    const u8 *codes;                     /* core_lock_codes' flashed entries: {descriptor, code | flags} */
    u32 ncodes;
    const u8 *ccs;                       /* core_lock_ccs' flashed entries: {descriptor, CC} */
    u32 nccs;
    u32 inits;                           /* core_field_inits' flashed entries */
    s32 spare[4];                        /* spare entries: codes, CCs, inits, handles; -1 no table */
    u32 live_dst;                        /* the core keeps its LFO destinations live (core_lfo_dsts) */
    const char *core;                    /* the core's id: its LFO destination fields' mod */
};
enum { DS_T_CODES, DS_T_CCS, DS_T_INITS, DS_T_HANDLES };

/* One declared field of a chosen mod. A set is given in mod id order (byte order), each mod's
 * fields in its declaration order; names are NUL-terminated and live as long as the call. */
struct ds_want {
    const char *mod, *sym;
    u8 scope;                            /* 1..6 */
    u8 bits;                             /* 1..32 */
    u8 flags;                            /* DS_* */
    u8 pad;
    u32 init;
};

/* fit(), one refusal at a time: the first short resource of the set (the entries with off[i]
 * zero; off may be 0 for all), and the last mod in id order that uses it. -> 1 with *s filled,
 * 0 when the set fits. */
struct ds_short {
    u32 victim;                          /* a want index of the mod to refuse */
    u32 what;                            /* DS_W_* */
    s32 need, have;
};
int ds_fit_one(const struct ds_base *b, const struct ds_want *w, u32 n, const u8 *off, struct ds_short *s);

/* /mods/.store, decoded (drive_store's state). */
struct ds_field {
    const char *mod, *sym;
    u8 scope, bits;
    u16 off;
    u32 init;
    u8 placed;                           /* the allocator's own: off holds a place */
};
struct ds_tag {                          /* a code or a CC and its field */
    const char *mod, *sym;
    u8 v;
};
struct ds_state {
    u32 flashed_id, id;
    u8 next_code;
    u8 fgen[DS_SCOPES], gen[DS_SCOPES];
    u32 low[DS_SCOPES];
    u32 nids;
    u32 ids[255];
    u32 nf, npk, ncd, nfcd, ncc, nfcc;
    struct ds_field f[DS_MAX_FIELDS], pk[DS_MAX_FIELDS];
    struct ds_tag cd[DS_MAX_TAGS], fcd[DS_MAX_TAGS], cc[DS_MAX_TAGS], fcc[DS_MAX_TAGS];
    char *names;                         /* the decoded names (dl_alloc), 0 for none */
};

/* .store bytes -> a state from the heap, 0 for a file the allocator does not take (missing, cut
 * short, another format, a wrong CRC, a bad record, past the loader's room) or no memory. */
struct ds_state *ds_decode(const u8 *p, u32 n);
/* A state -> its .store bytes, from the heap (*n their count), 0 when no memory. */
u8 *ds_encode(const struct ds_state *st, u32 *n);
void ds_state_free(struct ds_state *st);
/* FORGET DATA: drop mod's parked fields, freed codes and freed CCs. -> 1 when it changed. */
int ds_forget(struct ds_state *st, const char *mod);

/* allocate()'s result (drive_store.Placed). Every pointer is from the heap: ds_out_free. */
struct ds_out {
    u32 id;
    u32 anc[DS_MAX_IDS];
    u32 nanc;
    u8 gen[DS_SCOPES];
    u32 *desc;                           /* each want entry's field descriptor */
    u8 *codes;                           /* core_lock_codes' new entries, 8 bytes each */
    u32 ncodes;
    u8 *ccs;                             /* core_lock_ccs' */
    u32 nccs;
    u32 *inits;                          /* core_field_inits' */
    u32 ninits;
    u32 *handles;                        /* core_handles' */
    u32 nhandles;
    u32 lfo_dst[2];                      /* the core's LFO destination fields the +Drive placed, by
                                          * scope (sound, kit): core_lfo_dsts' words; 0 none */
    u32 nfields;                         /* placed fields after it */
    u32 nparked;                         /* parked fields after it */
    u8 *bytes;                           /* the new /mods/.store */
    u32 nbytes;
};
/* Place a set that fits (ds_fit_one finds nothing) from the .store bytes prev (0 or a file
 * ds_decode refuses: a fresh placement). -> 0, or -1 no memory, -2 a set that does not fit. */
int ds_allocate(const struct ds_base *b, const u8 *prev, u32 nprev, const struct ds_want *w, u32 n,
                struct ds_out *out);
void ds_out_free(struct ds_out *o);

/* The boot link's +Drive storage (dlink.c): dl_link places the living mods' fields from
 * dl_store_prev (/mods/.store as the boot read it, or 0) and leaves the result in dl_store_out
 * (its bytes 0 when the build keeps no +Drive storage or the heap had no room). */
extern const u8 *dl_store_prev;
extern u32 dl_store_prev_n;
extern struct ds_out dl_store_out;

#endif
