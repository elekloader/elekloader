/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2: payload skip while staging (skip.h). No stock addresses: the host tests
 * (tests/drive_skip_host.c) run it as it is. */
#include "skip.h"

enum { P_NONE, P_SECT, P_PAY, P_PARTS, P_PART };        /* in "sections", ".payload", "parts", a part */
enum { K_NONE, K_SECTIONS, K_PAYLOAD, K_LEN, K_SHA, K_PARTS };

static int is(const struct dl_skip *s, const char *w)
{
    u32 i;
    for (i = 0; w[i]; i++)
        if (i >= s->ncap || s->cap[i] != w[i])
            return 0;
    return i == s->ncap;
}

static int nib(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

void dl_skip_init(struct dl_skip *s)
{
    dl_memset(s, 0, sizeof *s);
}

static u8 top(const struct dl_skip *s)
{
    return s->depth && s->depth <= sizeof s->type ? s->type[s->depth - 1] : 0;
}

/* a key has ended: the value that follows is one the skip follows, or not */
static void key_end(struct dl_skip *s)
{
    s->key = K_NONE;
    if (s->depth == 1 && is(s, "sections"))
        s->key = K_SECTIONS;
    else if (s->path == P_SECT && s->depth == 2 && is(s, ".payload"))
        s->key = K_PAYLOAD;
    else if (s->path == P_PAY && s->depth == 3)
        s->key = is(s, "len") ? K_LEN : is(s, "sha256") ? K_SHA : is(s, "parts") ? K_PARTS : K_NONE;
}

/* a string value has ended */
static void str_end(struct dl_skip *s)
{
    u32 i;
    int a, b;
    if (s->skey == K_SHA) {
        s->have_sha = 0;
        if (s->ncap == 64) {
            for (i = 0; i < 32; i++) {
                a = nib(s->cap[2 * i]);
                b = nib(s->cap[2 * i + 1]);
                if (a < 0 || b < 0)
                    return;
                s->pay.sha256[i] = (u8)(a << 4 | b);
            }
            s->have_sha = 1;
        }
    } else if (s->path == P_PART && s->depth == 5 && s->elem == 0) {
        s->hex_first = is(s, "hex");
    }
}

u32 dl_skip_feed(struct dl_skip *s, const u8 *p, u32 n)
{
    u32 i;
    s->act = DL_STAGE_KEEP;
    for (i = 0; i < n; i++) {
        u8 c = p[i], k;
        if (s->instr) {
            if (s->esc)
                s->esc = 0;
            else if (c == '\\')
                s->esc = 1;
            else if (c == '"') {
                s->instr = 0;
                if (s->iskey)
                    key_end(s);
                else
                    str_end(s);
                continue;
            }
            if (s->ncap < sizeof s->cap)
                s->cap[s->ncap] = (char)c;
            if (s->ncap <= sizeof s->cap)
                s->ncap++;                   /* 65: longer than cap */
            continue;
        }
        if (s->innum) {
            if (c >= '0' && c <= '9') {
                s->num = s->num > 0x7FFFFFFFu / 10 ? 0xFFFFFFFFu : s->num * 10 + (c - '0');
                continue;
            }
            s->innum = 0;
            s->have_len = s->num <= 0x7FFFFFFFu;
            s->pay.len = s->num;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == ':') {
            s->keyexp = 0;
            continue;
        }
        if (c == ',') {
            s->keyexp = top(s) == '{';
            if (s->path == P_PART && s->depth == 5)
                s->elem++;
            continue;
        }
        if (c == '}' || c == ']') {
            if (s->path == P_PART && s->depth == 5)
                s->path = P_PARTS;
            else if (s->path == P_PARTS && s->depth == 4)
                s->path = P_PAY;
            else if (s->path == P_PAY && s->depth == 3) {
                s->path = P_SECT;
                s->near = 0;
            } else if (s->path == P_SECT && s->depth == 2)
                s->path = P_NONE;
            if (s->depth)
                s->depth--;
            s->keyexp = 0;
            s->key = K_NONE;
            continue;
        }
        if (c == '"' && s->keyexp && top(s) == '{') {   /* a key */
            s->instr = 1;
            s->iskey = 1;
            s->ncap = 0;
            continue;
        }
        k = s->key;                          /* a value begins */
        s->key = K_NONE;
        if (c == '"') {
            s->instr = 1;
            s->iskey = 0;
            s->ncap = 0;
            s->skey = k;
            if (s->path == P_PART && s->depth == 5 && s->elem == 1 && s->hex_first && s->nparts == 1
                && s->have_len && s->have_sha && !s->done) {
                s->done = 1;                 /* the hex: the caller skips it, then the closing quote */
                s->skey = K_NONE;
                s->ncap = sizeof s->cap + 1;
                s->act = DL_STAGE_GO;
                return i + 1;
            }
            continue;
        }
        if (c == '{' || c == '[') {
            if (c == '{' && k == K_SECTIONS && s->depth == 1)
                s->path = P_SECT;
            else if (c == '{' && k == K_PAYLOAD) {
                s->path = P_PAY;
                s->have_len = s->have_sha = 0;
                s->nparts = 0;
            } else if (c == '[' && k == K_PARTS && s->path == P_PAY)
                s->path = P_PARTS;
            else if (c == '[' && s->path == P_PARTS && s->depth == 4) {
                s->path = P_PART;
                s->nparts++;
                s->elem = 0;
                s->hex_first = 0;
            }
            if (s->depth < 0xFF)
                s->depth++;
            if (s->depth <= sizeof s->type)
                s->type[s->depth - 1] = c;
            s->keyexp = c == '{';
            if (s->path == P_PAY && s->depth == 3 && !s->near && !s->done) {
                s->near = 1;                 /* small steps from here: the hex may be close */
                s->act = DL_STAGE_NEAR;
                return i + 1;
            }
            continue;
        }
        if (k == K_LEN && c >= '0' && c <= '9') {
            s->innum = 1;
            s->num = c - '0';
        }
    }
    return n;
}

s32 dl_stage_file(dl_read_at read, void *ctx, u8 *buf, u32 max, struct dl_pay *pay)
{
    struct dl_skip s;
    u32 got = 0, at = 0, k, want;
    s32 r;
    u8 more;
    dl_skip_init(&s);
    dl_memset(pay, 0, sizeof *pay);
    while (got < max) {
        want = max - got;
        if (want > (s.near ? DL_STAGE_STEP : DL_BREATHE))
            want = s.near ? DL_STAGE_STEP : DL_BREATHE;
        r = read(ctx, at, buf + got, want);
        if (r < 0)
            return -3;
        if (r == 0)
            break;
        k = dl_skip_feed(&s, buf + got, (u32)r);
        got += k;
        at += k;
        if (s.act == DL_STAGE_GO) {
            dl_memcpy(pay, &s.pay, sizeof *pay);
            pay->off = at;
            at = 2 * s.pay.len > 0xFFFFFFFFu - at ? 0xFFFFFFFFu : at + 2 * s.pay.len;
        }
    }
    if (got == max) {
        r = read(ctx, at, &more, 1);
        if (r < 0)
            return -3;
        if (r > 0)
            return -2;
    }
    return (s32)got;
}

void dl_stage_rd_init(struct dl_stage_rd *s)
{
    dl_memset(s, 0, sizeof *s);
    s->crc = 0xFFFFFFFFu;
}

s32 dl_stage_more(struct dl_stage_rd *s, dl_read_at read, void *ctx, u8 *buf, u32 max)
{
    u32 done = 0, off, room, want, k;
    s32 r;
    if (!max)
        return -2;
    while (done < DL_BREATHE) {
        off = s->got < max ? s->got : 0;     /* past max: on over the buffer's start */
        room = s->got < max ? max - s->got : max;
        want = s->sk.near ? DL_STAGE_STEP : DL_BREATHE - done;
        if (want > room)
            want = room;
        r = read(ctx, s->at, buf + off, want);
        if (r < 0)
            return -3;
        if (r == 0)
            return s->over ? -2 : 1;
        k = dl_skip_feed(&s->sk, buf + off, (u32)r);
        s->crc = dl_crc32_add(s->crc, buf + off, k);
        if (s->got >= max)
            s->over = 1;
        s->got += k;
        s->at += k;
        done += k < DL_STAGE_COST ? DL_STAGE_COST : k;   /* a short read costs a sector */
        if (s->sk.act == DL_STAGE_GO)
            s->at = 2 * s->sk.pay.len > 0xFFFFFFFFu - s->at ? 0xFFFFFFFFu : s->at + 2 * s->sk.pay.len;
    }
    return 0;
}
