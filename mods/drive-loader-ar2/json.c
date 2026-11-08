/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A pull reader for JSON (json.h). It reads what Python's json module writes: objects,
 * arrays, strings with escapes, integers, true, false, null. A malformed file sets err and
 * every call after it fails, so a caller checks err once at the end. */
#include "json.h"

void js_init(js_t *j, const char *p, u32 n)
{
    j->p = p;
    j->end = p + n;
    j->err = 0;
}

static void ws(js_t *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t'))
        j->p++;
}

static int fail(js_t *j)
{
    j->err = 1;
    j->p = j->end;
    return 0;
}

int js_peek(js_t *j)
{
    if (j->err)
        return 0;
    ws(j);
    if (j->p >= j->end)
        return 0;
    switch (*j->p) {
    case '{': case '[': case '"': case 't': case 'f': case 'n':
        return *j->p;
    case '-':
        return '0';
    default:
        return (*j->p >= '0' && *j->p <= '9') ? '0' : 0;
    }
}

int js_open(js_t *j, int c)
{
    if (js_peek(j) != c)
        return fail(j);
    j->p++;
    return 1;
}

int js_next(js_t *j, int close, int *first)
{
    if (j->err)
        return 0;
    ws(j);
    if (j->p >= j->end)
        return fail(j);
    if (*j->p == close) {
        j->p++;
        return 0;
    }
    if (!*first) {
        if (*j->p != ',')
            return fail(j);
        j->p++;
        ws(j);
        if (j->p < j->end && *j->p == close)  /* a trailing comma: not JSON */
            return fail(j);
    }
    *first = 0;
    return 1;
}

/* The scans of long strings run through tables, 32 KB between breaths: most of a +Drive mod
 * file is one hex string, which the boot reads twice (found 2026-11-08: the per-character
 * tests were most of an 8.7 MB boot link). STR_STOP marks what ends a run of plain string
 * characters; HEX_V is a hex digit's value + 1, HEX_WS a space, 0 anything else. */
static const u8 STR_STOP[256] = { [0 ... 0x1f] = 1, ['"'] = 1, ['\\'] = 1 };
#define HEX_WS 17
static const u8 HEX_V[256] = {
    ['0'] = 1, ['1'] = 2, ['2'] = 3, ['3'] = 4, ['4'] = 5, ['5'] = 6, ['6'] = 7, ['7'] = 8,
    ['8'] = 9, ['9'] = 10, ['a'] = 11, ['b'] = 12, ['c'] = 13, ['d'] = 14, ['e'] = 15, ['f'] = 16,
    ['A'] = 11, ['B'] = 12, ['C'] = 13, ['D'] = 14, ['E'] = 15, ['F'] = 16,
    [' '] = HEX_WS,                      /* a raw tab or line end is a control character: no JSON */
};

/* the end of the next stretch of a long scan: at most DL_BREATHE bytes on */
static const u8 *stretch(const u8 *q, const u8 *e)
{
    return (u32)(e - q) > DL_BREATHE ? q + DL_BREATHE : e;
}

int js_str(js_t *j, const char **s, u32 *n)
{
    const u8 *q, *e, *ce;
    if (js_peek(j) != '"')
        return fail(j);
    q = (const u8 *)++j->p;
    e = (const u8 *)j->end;
    for (;;) {
        ce = stretch(q, e);
        while (q < ce && !STR_STOP[*q])
            q++;
        if (q >= e)
            return fail(j);
        if (q >= ce) {
            dl_breathe();
            continue;
        }
        if (*q == '"')
            break;
        if (*q < 0x20)
            return fail(j);
        if (++q >= e)                    /* a backslash: the next character is escaped */
            return fail(j);
        q++;
    }
    *s = j->p;
    *n = (u32)((const char *)q - j->p);
    j->p = (const char *)q + 1;
    return 1;
}

int js_key(js_t *j, const char **s, u32 *n)
{
    if (!js_str(j, s, n))
        return 0;
    ws(j);
    if (j->p >= j->end || *j->p != ':')
        return fail(j);
    j->p++;
    return 1;
}

static int digit(int c, int base)
{
    int v;
    if (c >= '0' && c <= '9')
        v = c - '0';
    else if (c >= 'a' && c <= 'z')
        v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z')
        v = c - 'A' + 10;
    else
        return -1;
    return v < base ? v : -1;
}

int js_parse_int(const char *s, u32 n, s32 *out)
{
    const char *e = s + n;
    int neg = 0, base = 10, any = 0, last_us = 0, d;
    u32 v = 0;
    while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
        s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
        e--;
    if (s < e && (*s == '+' || *s == '-'))
        neg = *s++ == '-';
    if (e - s >= 2 && s[0] == '0') {
        int c = s[1] | 0x20;
        if (c == 'x' || c == 'o' || c == 'b') {
            base = c == 'x' ? 16 : c == 'o' ? 8 : 2;
            s += 2;
            last_us = 1;                 /* "0x_1f" is allowed: an underscore may follow */
            if (s < e && *s == '_')
                s++;
            last_us = 0;
        }
    }
    if (base == 10 && s < e && *s == '0') {     /* int('010', 0) fails; '0', '00', '0_0' don't */
        const char *q;
        for (q = s; q < e; q++)
            if (*q != '0' && *q != '_')
                return 0;
    }
    for (; s < e; s++) {
        if (*s == '_') {
            if (!any || last_us)
                return 0;
            last_us = 1;
            continue;
        }
        d = digit(*s, base);
        if (d < 0)
            return 0;
        v = v * base + d;
        any = 1;
        last_us = 0;
    }
    if (!any || last_us)
        return 0;
    *out = (s32)(neg ? 0u - v : v);
    return 1;
}

int js_int(js_t *j, s32 *v)
{
    int k = js_peek(j);
    if (k == '"') {
        const char *s;
        u32 n;
        if (!js_str(j, &s, &n))
            return 0;
        return js_parse_int(s, n, v) ? 1 : fail(j);
    }
    if (k == '0') {
        const char *s = j->p;
        if (*j->p == '-')
            j->p++;
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
            j->p++;
        if (j->p < j->end && (*j->p == '.' || *j->p == 'e' || *j->p == 'E'))
            return fail(j);              /* not an integer */
        return js_parse_int(s, j->p - s, v) ? 1 : fail(j);
    }
    return fail(j);
}

static int word(js_t *j, const char *w)
{
    const char *q = j->p;
    while (*w) {
        if (q >= j->end || *q != *w)
            return fail(j);
        q++;
        w++;
    }
    j->p = q;
    return 1;
}

int js_bool(js_t *j, int *v)
{
    int k = js_peek(j);
    if (k == 't' && word(j, "true")) {
        *v = 1;
        return 1;
    }
    if (k == 'f' && word(j, "false")) {
        *v = 0;
        return 1;
    }
    return fail(j);
}

int js_skip(js_t *j)
{
    const char *s;
    u32 n;
    int first, depth = 0;
    switch (js_peek(j)) {
    case '"':
        return js_str(j, &s, &n);
    case 't':
        return word(j, "true");
    case 'f':
        return word(j, "false");
    case 'n':
        return word(j, "null");
    case '0':
        if (*j->p == '-')
            j->p++;
        while (j->p < j->end && ((*j->p >= '0' && *j->p <= '9') || *j->p == '.' || *j->p == 'e'
                                 || *j->p == 'E' || *j->p == '+' || *j->p == '-'))
            j->p++;
        return 1;
    case '{':
        j->p++;
        first = 1;
        while (js_next(j, '}', &first))
            if (!js_key(j, &s, &n) || !js_skip(j))
                return 0;
        return !j->err;
    case '[':
        j->p++;
        first = 1;
        while (js_next(j, ']', &first))
            if (!js_skip(j))
                return 0;
        return !j->err;
    }
    (void)depth;
    return fail(j);
}

/* One walk over the string: a hex string has no escape and no control character, so
 * anything but a digit, whitespace between bytes or the closing quote fails it, as the
 * string read followed by bytes.fromhex would. */
int js_hex(js_t *j, u8 *dst, u32 max, u32 *n)
{
    const u8 *q, *e, *ce;
    u32 k = 0;
    int hi = -1;
    u32 a, b;
    if (js_peek(j) != '"')
        return fail(j);
    q = (const u8 *)++j->p;
    e = (const u8 *)j->end;
    for (;;) {
        ce = stretch(q, e);
        while (q < ce) {
            if (hi < 0) {                /* whole bytes, two digits at a time */
                while (q + 1 < ce && (a = HEX_V[q[0]]) - 1u < 16u && (b = HEX_V[q[1]]) - 1u < 16u) {
                    if (dst) {
                        if (k >= max)
                            return fail(j);
                        dst[k] = (u8)((a - 1) << 4 | (b - 1));
                    }
                    k++;
                    q += 2;
                }
                if (q >= ce)
                    break;
            }
            a = HEX_V[*q];
            if (a - 1u < 16u) {
                if (hi < 0) {
                    hi = (int)a - 1;
                } else {
                    if (dst) {
                        if (k >= max)
                            return fail(j);
                        dst[k] = (u8)(hi << 4 | (a - 1));
                    }
                    k++;
                    hi = -1;
                }
            } else if (a == HEX_WS) {
                if (hi >= 0)             /* bytes.fromhex: spaces only between bytes */
                    return fail(j);
            } else if (*q == '"' && hi < 0) {
                j->p = (const char *)q + 1;
                *n = k;
                return 1;
            } else {
                return fail(j);
            }
            q++;
        }
        if (q >= e)
            return fail(j);
        dl_breathe();
    }
}

int js_eq(const char *s, u32 n, const char *lit)
{
    u32 i;
    for (i = 0; i < n; i++)
        if (!lit[i] || lit[i] != s[i])
            return 0;
    return !lit[n];
}

static s32 hex4(const char *s)
{
    s32 v = 0;
    int i, d;
    for (i = 0; i < 4; i++) {
        d = digit(s[i], 16);
        if (d < 0)
            return -1;
        v = v << 4 | d;
    }
    return v;
}

s32 js_unescape(const char *s, u32 n, char *out, u32 max)
{
    const char *e = s + n;
    u32 k = 0, c;
    while (s < e) {
        if (*s != '\\') {
            c = (u8)*s++;
            if (k + 1 >= max)
                return -1;
            out[k++] = (char)c;
            continue;
        }
        if (++s >= e)
            return -1;
        switch (*s++) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'b': c = 8; break;
        case 'f': c = 12; break;
        case 'n': c = 10; break;
        case 'r': c = 13; break;
        case 't': c = 9; break;
        case 'u': {
            s32 h;
            if (e - s < 4 || (h = hex4(s)) < 0)
                return -1;
            s += 4;
            c = (u32)h;
            if (c >= 0xD800 && c < 0xDC00 && e - s >= 6 && s[0] == '\\' && s[1] == 'u') {
                s32 lo = hex4(s + 2);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + ((u32)lo - 0xDC00);
                    s += 6;
                }
            }
            break;
        }
        default:
            return -1;
        }
        if (c < 0x80) {
            if (k + 1 >= max)
                return -1;
            out[k++] = (char)c;
        } else if (c < 0x800) {
            if (k + 2 >= max)
                return -1;
            out[k++] = (char)(0xC0 | c >> 6);
            out[k++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            if (k + 3 >= max)
                return -1;
            out[k++] = (char)(0xE0 | c >> 12);
            out[k++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[k++] = (char)(0x80 | (c & 0x3F));
        } else {
            if (k + 4 >= max)
                return -1;
            out[k++] = (char)(0xF0 | c >> 18);
            out[k++] = (char)(0x80 | (c >> 12 & 0x3F));
            out[k++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[k++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[k] = 0;
    return (s32)k;
}
