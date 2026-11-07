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

int js_str(js_t *j, const char **s, u32 *n)
{
    const char *q;
    if (js_peek(j) != '"')
        return fail(j);
    q = ++j->p;
    while (q < j->end && *q != '"') {
        if ((u8)*q < 0x20)
            return fail(j);
        if (*q == '\\') {
            if (++q >= j->end)
                return fail(j);
        }
        q++;
    }
    if (q >= j->end)
        return fail(j);
    *s = j->p;
    *n = q - j->p;
    j->p = q + 1;
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

int js_hex(js_t *j, u8 *dst, u32 max, u32 *n)
{
    const char *s, *e;
    u32 len, k = 0;
    int hi = -1, d;
    if (!js_str(j, &s, &len))
        return 0;
    for (e = s + len; s < e; s++) {
        if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            if (hi >= 0)                 /* bytes.fromhex: spaces only between bytes */
                return fail(j);
            continue;
        }
        d = digit(*s, 16);
        if (d < 0)
            return fail(j);
        if (hi < 0) {
            hi = d;
            continue;
        }
        if (dst) {
            if (k >= max)
                return fail(j);
            dst[k] = (u8)(hi << 4 | d);
        }
        k++;
        hi = -1;
    }
    if (hi >= 0)
        return fail(j);
    *n = k;
    return 1;
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
