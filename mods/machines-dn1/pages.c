/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native.h"

static struct {
    void *view;
    uint32_t end, cursor;
    int32_t limited;
} syn_views[2];

static const struct dnm_page *page_for(int32_t id)
{
    const struct dnm_page *const *p;
    if (!id)
        return 0;
    for (p = dn_machine_pages; *p; p++)
        if ((*p)->id == id && (*p)->abi == &dnm_ui_v1)
            return *p;
    return 0;
}

void *dnm_syn_view(int32_t syn)
{
    return syn >= 1 && syn <= 2 ? syn_views[syn - 1].view : 0;
}

static void navigation(void)
{
    const struct dnm_page *page = page_for(dnm_machine_id());
    for (int32_t i = 0; i < 2; i++) {
        uint8_t *view = syn_views[i].view;
        int32_t count = page ? page->pages[i] : 0;
        uint32_t begin, end, native_count;
        if (!view)
            continue;
        begin = dnm_read32(view + 124);
        end = syn_views[i].limited ? syn_views[i].end : dnm_read32(view + 128);
        if (!begin || end < begin || (end - begin) % 4)
            continue;
        native_count = (end - begin) / 4;
        if (count >= 1 && count <= 2 && (uint32_t)count < native_count) {
            if (!syn_views[i].limited) {
                syn_views[i].end = end;
                syn_views[i].cursor = dnm_read32(view + 144);
                syn_views[i].limited = 1;
            }
            *(volatile uint32_t *)(view + 128) = begin + 4 * count;
            if (dnm_read32(view + 144) >= (uint32_t)count)
                *(volatile uint32_t *)(view + 144) = 0;
        } else if (syn_views[i].limited) {
            *(volatile uint32_t *)(view + 128) = syn_views[i].end;
            *(volatile uint32_t *)(view + 144) = syn_views[i].cursor;
            syn_views[i].limited = 0;
        }
    }
}

void dnm_pages_tick(void *ctrl)
{
    (void)ctrl;
    navigation();
}

int32_t dnm_pages_input(void *brain, void *event)
{
    (void)brain;
    (void)event;
    navigation();
    return 0;
}

int32_t dnm_syn_page(void *brain)
{
    uint32_t bottom, manager;
    int32_t syn;
    if (!brain)
        return 0;
    bottom = dnm_read32((uint8_t *)brain + 0x54);
    if (!bottom || bottom != dnm_read32((uint8_t *)brain + 0x5c))
        return 0;
    syn = fw_syn_key;
    if (syn != 1 && syn != 2)
        return 0;
    manager = dnm_read32((uint8_t *)dnm_ptr(bottom) + 8);
    return manager && dnm_read32((uint8_t *)dnm_ptr(manager) + 0xc0) ==
        (uint32_t)(uintptr_t)dnm_syn_view(syn) ? syn : 0;
}

int32_t dnm_pages_enc(void *brain, void *event)
{
    int32_t syn, enc, page;
    navigation();
    syn = dnm_syn_page(brain);
    if (!syn || !event)
        return 0;
    page = dnm_read32((uint8_t *)dnm_syn_view(syn) + 144);
    if (page < 0 || page > 1)
        return 0;
    enc = (int32_t)dnm_read32((uint8_t *)event + 12);
    if (enc < 1 || enc > 8 || !dnm_read32((uint8_t *)event + 16))
        return 0;
    return dnm_reset(brain, syn, page * DNM_KNOBS + (syn - 1) * 8 + enc - 1);
}

static void draw(void *view, void *bitmap, int32_t syn)
{
    const struct dnm_page *page;
    if (syn_views[syn - 1].view != view) {
        syn_views[syn - 1].view = view;
        syn_views[syn - 1].limited = 0;
    }
    navigation();
    page = page_for(dnm_machine_id());
    if (page && page->draw && page->draw(view, bitmap, syn, dnm_read32((uint8_t *)view + 144)))
        return;
    if (syn == 1)
        fw_syn1_draw(view, bitmap);
    else
        fw_syn2_draw(view, bitmap);
}

void dnm_syn1_draw(void *view, void *bitmap) { draw(view, bitmap, 1); }
void dnm_syn2_draw(void *view, void *bitmap) { draw(view, bitmap, 2); }
