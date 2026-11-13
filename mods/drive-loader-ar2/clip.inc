/* The build list's label: a mod's path below /mods, clipped to the room left of the state column
 * so that it never runs into KEEP / ADD / DROP (decided 2026-11-08: the tail is kept, the file's
 * name being what tells two mods apart, and the cut is marked with CLIP_MARK in front).
 * page.c includes this; tests/drive_page_clip_host.c builds it with a fake font.
 * width(s) is the font's width of s in pixels. -> out, s or CLIP_MARK + its longest tail that fits
 * in room (CLIP_MARK alone if nothing does). out holds at most end - out - 1 characters. */
#define CLIP_MARK "<"

static int clip_len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static char *clip_set(char *out, char *end, const char *mark, const char *tail)
{
    char *p = out;
    while (*mark && p < end - 1)
        *p++ = *mark++;
    while (*tail && p < end - 1)
        *p++ = *tail++;
    *p = 0;
    return out;
}

static char *clip_tail(char *out, char *end, const char *s, int room, int (*width)(const char *))
{
    int n = clip_len(s), k, w;
    if (n < end - out && width(s) <= room)
        return clip_set(out, end, "", s);
    /* the longest tail s + k that fits behind the mark: start from the average glyph's guess,
     * then step (a few measures, not one a character: this runs for each drawn row) */
    w = width(s);
    k = n - (int)((long)(room > 0 ? room : 0) * n / (w > 0 ? w : 1)) + 1;
    if (k < 1)
        k = 1;
    if (n - k > (int)(end - out) - 2)
        k = n - ((int)(end - out) - 2);
    while (k < n && width(clip_set(out, end, CLIP_MARK, s + k)) > room)
        k++;
    while (k > 1 && n - (k - 1) <= (int)(end - out) - 2 &&
           width(clip_set(out, end, CLIP_MARK, s + k - 1)) <= room)
        k--;
    return clip_set(out, end, CLIP_MARK, s + k);
}
