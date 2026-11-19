/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2: payload skip while staging (docs/DRIVE.md, "Payloads on the +Drive").
 *
 * A +Drive mod may carry a `.payload` the loader never brings into RAM. The file stays one JSON
 * document; the payload section is skippable when it has "len" and "sha256" before "parts", and
 * "parts" is one ["hex", "..."] part. While a file is staged for the linker, dl_stage_file follows
 * the JSON a byte at a time; at that hex string's opening quote it records where the hex is, seeks
 * past it in the file and goes on with the closing quote, so the staged JSON holds "" there. The
 * linker (dlink.c with DL_PAYLOAD_SKIP) takes a .payload whose one part is that empty hex and
 * refuses any relocation into it. The host twin is elekloader/drive.py's stage(). */
#ifndef DL_SKIP_H
#define DL_SKIP_H

#include "dl.h"
#include "fs.h"

/* the payload a staged file skipped (len 0: none) */
struct dl_pay {
    u32 off;                             /* the hex's first byte in the file */
    u32 len;                             /* the payload's bytes (the hex is twice as long) */
    u8 sha256[32];                       /* as the file gives it */
};

/* the follower's state: one file, from its first byte */
struct dl_skip {
    u8 depth;                            /* containers open */
    u8 type[8];                          /* '{' or '[' for depths 1..8 */
    u8 instr, esc, iskey, keyexp;        /* in a string; after a backslash; it is a key; a key is next */
    u8 path;                             /* P_* below: where in sections/.payload/parts we are */
    u8 key;                              /* K_*: the key whose value comes next */
    u8 elem;                             /* the element of the part array */
    u8 nparts;                           /* parts begun */
    u8 hex_first;                        /* the part's first element was "hex" */
    u8 innum;                            /* in the "len" number */
    u8 have_len, have_sha, done;         /* "len" read; "sha256" read; skipped once already */
    u8 near;                             /* in the .payload object: read in small steps */
    u8 skey;                             /* K_*: the key of the string being read */
    u8 act;                              /* what the last dl_skip_feed stopped for: DL_STAGE_* */
    u8 ncap;                             /* the current string's first bytes (cap), up to 64 */
    char cap[64];
    u32 num;
    struct dl_pay pay;
};

#define DL_STAGE_STEP 64                  /* bytes a read once .payload has opened, so a read that
                                          * reaches the hex brings at most this many of its bytes
                                          * in; a 32 KB read before that may carry some, which the
                                          * next read overwrites: the hex never stays in RAM */

void dl_skip_init(struct dl_skip *s);
/* Follow n more bytes of the file. -> how many of them to keep: all n (s->act DL_STAGE_KEEP), or
 * fewer when the caller is to stop there and read on from the file's next byte after them:
 * DL_STAGE_NEAR, the .payload object has opened (read in DL_STAGE_STEP bytes from here, while
 * s->near), or DL_STAGE_GO, the hex's opening quote was the last byte kept: the next
 * 2 * s->pay.len bytes of the file are the hex, to be skipped. */
enum { DL_STAGE_KEEP, DL_STAGE_NEAR, DL_STAGE_GO };
u32 dl_skip_feed(struct dl_skip *s, const u8 *p, u32 n);

/* Read a file whole into buf (max bytes), skipping its payload's hex. read(ctx, at, dst, n)
 * reads n bytes at the file's offset at: -> bytes read, 0 at its end, < 0 an error. -> the staged
 * length, -2 when the staged file is longer than max, -3 a read error; *pay the payload skipped
 * (pay->len 0 when none). */
typedef s32 (*dl_read_at)(void *ctx, u32 at, u8 *dst, u32 n);
s32 dl_stage_file(dl_read_at read, void *ctx, u8 *buf, u32 max, struct dl_pay *pay);

/* dl_stage_file in calls of at most DL_BREATHE file bytes each (the MODS page's check, one call a
 * tick: boot.c dl_read_more), with the staged bytes' CRC-32 taken as they come, so the page checks
 * and CRCs a file as the boot stages it and as /mods/active names it. A file staged past max is
 * read on to its end over the buffer's start, for its staged length and CRC alone. */
#define DL_STAGE_COST 512                /* what dl_stage_more counts a read below it as: a +Drive
                                          * sector is read whole, so a call in DL_STAGE_STEP reads
                                          * (a payload that isn't skipped) stops at 64 of them */
struct dl_stage_rd {
    struct dl_skip sk;
    u32 at;                              /* the file's next byte to read */
    u32 got;                             /* staged bytes so far */
    u32 crc;                             /* the CRC-32 register; ~crc is the staged file's at the end */
    u8 over;                             /* staged past max */
};
void dl_stage_rd_init(struct dl_stage_rd *s);
/* -> 0 more to read, 1 staged whole (got bytes in buf), -2 staged past max (got and crc are the
 * whole staged file's all the same), -3 a read error. */
s32 dl_stage_more(struct dl_stage_rd *s, dl_read_at read, void *ctx, u8 *buf, u32 max);

/* A file below DL_DIR staged a stretch (32 KB) a call (boot.c, over dl_stage_more), its CRC-32
 * taken as it comes: the MODS page's check. st.got is the staged length, ~st.crc its CRC (the one
 * /mods/active names), size the file's size on the +Drive. The caller holds dl_lock during each
 * call. dl_read_more -> 0 more, 1 read, -2 past max, -3 a read error (closed after all but 0). */
struct dl_reader {
    dl_file f;
    struct dl_stage_rd st;
    u32 size;                            /* f.size at the open */
    u8 open;
};
int dl_read_open(struct dl_reader *rd, const char *name);   /* 0, or -1 missing (closed) */
int dl_read_more(struct dl_reader *rd, u8 *buf, u32 max);
void dl_read_close(struct dl_reader *rd);

/* ---- the Drive payload table (payload.c): what the boot skipped, for sidecar-core ------------ */
/* One loaded mod's skipped payload. sidecar-core looks a payload up by id in core_payloads (a
 * flashed mod's, in RAM) and then here, and reads it with dl_payload_read. */
#define DL_PAYLOADS 64                   /* entries; a boot with more skipped payloads counts the rest
                                          * in drive_payloads_lost */
struct drive_payload {
    u32 id;                              /* FNV-1a of the mod's id, as core_payloads (link.payload_id) */
    u32 len;                             /* the payload's bytes */
    u32 off;                             /* its hex's first byte in the file */
    u32 sector, size;                    /* the file's first sector and size at boot (store.c) */
    u8 sha256[32];                       /* as the file gives it: sidecar-core's cache key */
    char path[144];                      /* the file, as the store names it (fs.h DL_PATH_MAX) */
};
extern struct drive_payload drive_payloads[DL_PAYLOADS];
extern u32 drive_payloads_n, drive_payloads_lost;

/* the boot (boot.c): a staged file's skipped payload noted, then after the link the table kept
 * for the loaded mods only */
void dl_payload_note(const char *path, const struct dl_pay *pay);
void dl_payload_keep(const struct dl_status *st, const struct dl_choice *ch, int n);
/* the entry for a payload id, or 0 */
const struct drive_payload *drive_payload_find(u32 id);
/* Read n bytes of a payload from byte at, decoded from the file's hex. Takes dl_lock: call it from
 * a task, never an interrupt. -> bytes read (fewer than n at the payload's end), -1 bad arguments,
 * -2 the file is not the one the boot linked (replaced, moved or deleted since), -3 a read error or
 * a byte that isn't hex. The caller still checks the sha256 of what it ships. */
s32 dl_payload_read(const struct drive_payload *p, u32 at, u8 *out, u32 n);

#endif
