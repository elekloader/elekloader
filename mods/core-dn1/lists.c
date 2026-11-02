/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-dn1 4.2: a page key's list of pages, replaced for a while (docs/ADAPTING.md, "Mod pages").
 *
 * VIEW_INIT copies each page key's list into its view's own std::vector (+124 its first index, +128 its
 * end, +132 its capacity); the view shows the entry at +144, steps through the list when its key is pressed
 * again, and heads the screen with the shown page's title. pages.s notes each view and its stock list's
 * first page in core_views. core_page_list writes another list into that vector, never past its capacity,
 * and puts the stock list back when asked: the vector stays the firmware's own. */
#include <stdint.h>

#define VIEWS   16              /* pages.s's core_views */
#define LISTMAX 32              /* pages.s's longest list */
#define V_LIST  124
#define V_CUR   144

extern struct { void *view; int32_t first; } core_views[VIEWS];

static int32_t stock[VIEWS][LISTMAX];
static int32_t stock_n[VIEWS];  /* 0: not saved yet */

int32_t core_page_list(int32_t first, const int32_t *list, int32_t n)
{
    int32_t v, i;
    for (v = 0; v < VIEWS && core_views[v].view; v++)
        if (core_views[v].first == first)
            break;
    if (v == VIEWS || !core_views[v].view || n < 0)
        return 0;
    int32_t **vec = (int32_t **)((uint8_t *)core_views[v].view + V_LIST);
    int32_t *cur = (int32_t *)((uint8_t *)core_views[v].view + V_CUR);
    int32_t size = vec[1] - vec[0], capacity = vec[2] - vec[0];
    if (!stock_n[v]) {
        if (size < 1 || size > LISTMAX)
            return 0;
        for (i = 0; i < size; i++)
            stock[v][i] = vec[0][i];
        stock_n[v] = size;
    }
    if (!n) {
        list = stock[v];
        n = stock_n[v];
    }
    if (n > capacity)
        return 0;
    for (i = 0; i < n; i++)
        vec[0][i] = list[i];
    vec[1] = vec[0] + n;
    if (*cur >= n)
        *cur = 0;
    return 1;
}
