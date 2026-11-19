/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.15: a view whose page went away. pages.s gives a page key's
 * view its list (stock's pages, then core_view_pages'), each page with a
 * shown; the key steps over a page while its shown answers 0. A page can be
 * hidden while the view is on it (smpl-machines: a machine with one page
 * hides the SAMPLE view's page 5), so before the view draws (pages.s,
 * core_sample_draw) it moves on to the next page that is shown, as the key
 * would, and the header takes that page's title; not while a stock list or
 * window is over it (the machine list's CONFIRM?), whose header that is. */
#include <stdint.h>

#define VIEW_MAX 16
#define VIEWS 10
#define VIEW_PAGES 124                  /* the view's copy of its list: begin, end */
#define VIEW_SEL 140                    /* the page it is on: an index in it */
#define VIEW_UI 108
#define VT_TITLE 0x4c
#define STR_FROM_CSTR ((void (*)(void *, const char *, void *))0x401bcff0)
#define UI_SET_TITLE ((void (*)(void *, void *))0x40035fe8)
#define STR_DTOR ((void (*)(void *))0x401bb540)
#define VIEW_INVALIDATE ((void (*)(void *))0x40078580)

typedef int32_t (*shown_fn)(void *view);
int core_stock_overlay(void);           /* screen.c */
extern int32_t core_view_buf[VIEWS * VIEW_MAX * 2];  /* the lists, then each page's shown */
extern int32_t core_view_len[VIEWS];

/* the shown functions of the view's list (core's list with the same pages),
 * or 0 */
static shown_fn *shown_of(void *view, int32_t *n)
{
    int32_t *b = *(int32_t **)((char *)view + VIEW_PAGES);
    int32_t *e = *(int32_t **)((char *)view + VIEW_PAGES + 4);
    int k, i;
    *n = (int32_t)(e - b);
    for (k = 0; k < VIEWS; k++) {
        if (core_view_len[k] != *n || *n <= 0)
            continue;
        for (i = 0; i < *n && core_view_buf[k * VIEW_MAX + i] == b[i]; i++)
            ;
        if (i == *n)
            return (shown_fn *)&core_view_buf[VIEWS * VIEW_MAX + k * VIEW_MAX];
    }
    return 0;
}

static void set_title(void *view)
{
    char str[12];                       /* stock's string: 8 bytes, its allocator's byte after */
    /* stock answers the title in %d0, where our compiler reads a pointer from
     * %a0: so an integer */
    uintptr_t (*title)(void *) = (*(uintptr_t (***)(void *))view)[VT_TITLE / 4];
    STR_FROM_CSTR(str, (const char *)title(view), str + 8);
    UI_SET_TITLE(*(void **)((char *)view + VIEW_UI), str);
    STR_DTOR(str);
}

void core_view_settle(void *view)
{
    int32_t n, sel, at, tries;
    shown_fn *sh = shown_of(view, &n);
    if (!sh || core_stock_overlay())
        return;                         /* a list or window over it owns the header: later */
    sel = *(int32_t *)((char *)view + VIEW_SEL);
    if (sel < 0 || sel >= n || !sh[sel] || sh[sel](view))
        return;
    for (at = sel, tries = n; tries > 1; tries--) {
        at = at + 1 < n ? at + 1 : 0;
        if (!sh[at] || sh[at](view))
            break;
    }
    if (at == sel || (sh[at] && !sh[at](view)))
        return;                         /* none is shown: the page stays */
    *(int32_t *)((char *)view + VIEW_SEL) = at;
    VIEW_INVALIDATE(view);
    set_title(view);
}
