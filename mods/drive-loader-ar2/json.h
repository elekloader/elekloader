/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A pull reader for JSON over a buffer: no heap, no copies. Strings come back as
 * slices of the buffer with their escapes still in (js_unescape decodes them). */
#ifndef JSON_H
#define JSON_H
#include "dl.h"

typedef struct {
    const char *p, *end;
    int err;
} js_t;

void js_init(js_t *j, const char *p, u32 n);
/* The next value's kind without taking it: '{' '[' '"' '0' (a number) 't' 'f' 'n', or 0
 * at the end or on an error. */
int js_peek(js_t *j);
/* Take '{' or '['. -> 1, or 0 (and err) when the next value isn't one. */
int js_open(js_t *j, int c);
/* Inside an object or array: -> 1 when another member follows (its comma taken), 0 when the
 * close came (taken). `*first` starts at 1. */
int js_next(js_t *j, int close, int *first);
/* An object's key and its colon. */
int js_key(js_t *j, const char **s, u32 *n);
/* A string value: its raw slice. */
int js_str(js_t *j, const char **s, u32 *n);
/* A number, or a string holding one the way Python's int(v, 0) reads it. */
int js_int(js_t *j, s32 *v);
/* true / false. */
int js_bool(js_t *j, int *v);
/* Skip one value of any kind. */
int js_skip(js_t *j);
/* A string of hex digits -> bytes at dst (at most max). -> 1, with *n the count. dst may be
 * 0 to only count them. */
int js_hex(js_t *j, u8 *dst, u32 max, u32 *n);
/* A raw slice equal to a literal (no escapes in the literal)? */
int js_eq(const char *s, u32 n, const char *lit);
/* Decode a raw slice's escapes into out (NUL-terminated, at most max with the NUL, UTF-8).
 * -> the length, or -1 when it doesn't fit or is malformed. */
s32 js_unescape(const char *s, u32 n, char *out, u32 max);
/* Parse an int the way Python's int(text, 0) does. -> 1 on success. */
int js_parse_int(const char *s, u32 n, s32 *v);

#endif
