/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-dt2 1.1 and core-dn2 0.3: value rows, and the UI helpers core offers
 * mods (elekloader/sdk/include/mk2/core.h). A value row is drawn as the stock
 * LED INTENSITY is: the label on the left, the value right-aligned with the
 * menus' value font at the row's x + 19, y + 2. */
#include "mk2/core.h"

extern void core_addrow(void *menu, const void *const row[4], void *payload);
extern void core_value_label(void);

typedef struct {
    const core_value_t *row;
    void *menu;
} cell_t;

static void set(cell_t *c, uint32_t v)
{
    if (v != c->row->get())
        c->row->set(v);
    fw_invalidate((char *)c->menu + 0x38);
}

/* YES does nothing: only LEFT and RIGHT change the value. */
static void select(cell_t **storage)
{
    (void)storage;
}

static void draw(cell_t **storage, void *item, void *bmp, int x, int y)
{
    cell_t *c = *storage;
    uint32_t v = c->row->get();
    (void)item;
    if (v < c->row->count)
        fw_textbox(bmp, fw_font_menu, x + 19, y + 2, 2, 0, 0, c->row->values[v]);
}

static void change(cell_t **storage, void *item, int delta)
{
    cell_t *c = *storage;
    int v = (int)c->row->get() + delta;
    (void)item;
    if (v < 0)
        v = 0;
    if (v >= (int)c->row->count)
        v = (int)c->row->count - 1;
    set(c, (uint32_t)v);
}

static const void *const callbacks[4] = {
    (const void *)core_value_label, (const void *)select, (const void *)draw, (const void *)change,
};

void core_addvalue(void *menu, const core_value_t *row)
{
    cell_t *c = (cell_t *)fw_op_new(sizeof *c);
    if (!c || !row->count)
        return;
    c->row = row;
    c->menu = menu;
    core_addrow(menu, callbacks, c);
}

void core_notice(const char *msg)
{
    fw_toast((void *)fw_ui_states(), "%s", msg);
}

/* The view controller is the Brain's + 0x40; its stack's last node (+ 0x18) is the
 * top, its view at node + 8, the view's vtable its first word. */
uint32_t core_view_top(void)
{
    char *ctrl = (char *)fw_brain() + 0x40;
    char *node = *(char **)(ctrl + 0x18);
    if (node == ctrl + 0x14)
        return 0;                   /* an empty stack */
    return **(uint32_t **)(node + 8);
}
