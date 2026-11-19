/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.29: rows mods add to the popup lists (docs/ADAPTING.md, "Popup rows on the Octatrack"; the
 * declarations are in elekloader/sdk/include/octatrack/core.h, the site in popup.s).
 */
#include <stdint.h>
#include "octatrack/core.h"

extern void (*const ev_popup[])(struct ot_popup *);

static const char *pop_labels[OT_POPUP_MAX];
static void (*pop_cbs[OT_POPUP_MAX])(void);
static uint32_t pop_args[5];

/* popup.s: the opener's arguments (n, cursor, out, labels, callbacks) and its caller. Returns 0 to open
 * the list as stock would, or new arguments: the rows copied, with the handlers' changes. */
uint32_t *core_popup_rows(const uint32_t *a, uint32_t ret)
{
    uint32_t n = a[0], i;
    const char *const *labels = (const char *const *)a[3];
    void (*const *cbs)(void) = (void (*const *)(void))a[4];
    if (n > OT_POPUP_MAX || !labels)
        return 0;
    for (i = 0; i < n; i++) {
        pop_labels[i] = labels[i];
        pop_cbs[i] = cbs ? cbs[i] : 0;
    }
    struct ot_popup p = {n, (int32_t)a[1], (int32_t *)a[2], pop_labels, pop_cbs, labels, ret};
    for (void (*const *f)(struct ot_popup *) = ev_popup; *f; ++f)
        (*f)(&p);
    if (p.n > OT_POPUP_MAX)
        p.n = OT_POPUP_MAX;
    int same = p.n == n && p.cursor == (int32_t)a[1];
    for (i = 0; same && i < n; i++)
        same = pop_labels[i] == labels[i] && pop_cbs[i] == (cbs ? cbs[i] : 0);
    if (same || p.n == 0)
        return 0;
    pop_args[0] = p.n;
    pop_args[1] = (uint32_t)(p.cursor < 0 ? 0 : (uint32_t)p.cursor >= p.n ? p.n - 1 : (uint32_t)p.cursor);
    pop_args[2] = (uint32_t)p.out;
    pop_args[3] = (uint32_t)pop_labels;
    pop_args[4] = (uint32_t)pop_cbs;
    return pop_args;
}
