/* The /mods route's names, apart from route.c so that a host test can build them
 * (tests/drive_route_names_host.c). route.c includes this after its helpers len, copy, same and
 * ends, and fs.h. */

/* one folder or file name in a path */
static int seg_ok(const char *s)
{
    unsigned n = len(s), i;
    if (!n || n > DL_SEG_MAX || s[0] == '.')
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\' || (unsigned char)s[i] < 0x20)
            return 0;
    return 1;
}

/* a path's only segment: a plain name, or one of the route's own files (read only: dl_entry gives
 * them P_READ), which start with "." */
static int name_ok(const char *s)
{
    if (same(s, ".build") || same(s, ".status"))
        return 1;
#ifdef DL_TEST_ARMED
    if (same(s, ".armed"))               /* test builds only: plant the crash flag */
        return 1;
#endif
    return seg_ok(s);
}

/* An empty folder: an upload to "FOLDER/.dir" (DL_MKDIR) asks the route to make FOLDER, and its
 * missing parents, up to DL_DEPTH below DL_DIR (the route's deepest entry pattern is a third-level
 * folder's ".dir"). The Data router has no command for a folder, so the upload is the request; it
 * writes no file. -> FOLDER's length, or 0 when `s` is not such a request. */
static unsigned mkdir_of(const char *s)
{
    unsigned n = len(s), k = sizeof DL_MKDIR - 1;
    return n > k + 1 && s[n - k - 1] == '/' && same(s + n - k, DL_MKDIR) ? n - k - 1 : 0;
}

/* a path below DL_DIR that may be uploaded: "active" at the top, a .elemod at any depth, or a
 * folder's DL_MKDIR */
static int writable(const char *s)
{
    if (mkdir_of(s))
        return 1;
#ifdef DL_TEST_ARMED
    if (same(s, ".armed"))
        return 1;
#endif
    return same(s, "active") || (ends(s, ".elemod") && len(s) > 7);
}

/* The route's captures (a vector of std::string, one pointer to the chars each: one a `*` of the
 * pattern, so one a folder or file name) joined into a path below DL_DIR. -> its folders (the
 * captures less one), or -1 for a name that isn't one (the first may be .build, .status, .armed:
 * name_ok; the others must be plain names, but for a last DL_MKDIR after a folder) or a path past
 * DL_NAME_MAX. */
static int join_caps(char *out, void **caps)
{
    const char *const *v = (const char *const *)caps[0], *const *e = (const char *const *)caps[1];
    unsigned at = 0, k;
    int n = (int)(e - v), i;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        const char *s = v[i];
        if (n == 1 ? !name_ok(s) : !seg_ok(s) && !(i == n - 1 && same(s, DL_MKDIR)))
            return -1;
        k = len(s);
        if (at + (i ? 1 : 0) + k > DL_NAME_MAX)
            return -1;
        if (i)
            out[at++] = '/';
        copy(out + at, s, k + 1);
        at += k;
    }
    return n - 1;
}

/* A Clear of a folder (`elekloader mods rm FOLDER`): stock's rmdir removes only an empty one, and
 * the entry's after-change call can't report a failure, so a folder with `entries` (its files and
 * folders, without . and ..) is refused when it resolves. -> 0, or the refusal. */
static const char *dir_clear_why(int entries)
{
    return entries > 0 ? "Folder not empty" : 0;
}
