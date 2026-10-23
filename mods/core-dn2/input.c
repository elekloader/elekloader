/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"

extern volatile unsigned char fw_key_state[];
extern volatile unsigned fw_ui_queue[];
extern unsigned core_keys_lock(void);
extern void core_keys_unlock(unsigned saved_sr);
extern void core_stock_buttons(unsigned group, unsigned mask);

static unsigned char physical[CORE_KEYS_GROUPS], synthetic[CORE_KEYS_GROUPS];
static unsigned initialized;

/* Interrupts stay masked through each driver call: its repeat state and
 * record pool are shared with the panel receiver and repeat producer. */
static void initialize(void)
{
    unsigned i;
    if (!initialized) {
        for (i = 0; i < CORE_KEYS_GROUPS; ++i)
            physical[i] = fw_key_state[i];
        initialized = 1;
    }
}

void core_panel_buttons(unsigned group, unsigned mask)
{
    unsigned sr = core_keys_lock();
    initialize();
    if (group < CORE_KEYS_GROUPS) {
        physical[group] = mask;
        mask |= synthetic[group];
    }
    core_stock_buttons(group, mask);
    core_keys_unlock(sr);
}

int core_keys_set(unsigned group, unsigned mask)
{
    unsigned sr, old_mask, new_mask, count, capacity;
    int result = CORE_KEYS_OK;
    if (group >= CORE_KEYS_GROUPS || mask > 255 || (group == 6 && (mask & 0x0c)))
        return CORE_KEYS_INVALID;
    sr = core_keys_lock();
    initialize();
    old_mask = physical[group] | synthetic[group];
    new_mask = physical[group] | mask;
    if (mask & ~synthetic[group] & physical[group]) {
        result = CORE_KEYS_OVERLAP;
    } else if (old_mask != new_mask) {
        count = fw_ui_queue[1];
        capacity = fw_ui_queue[4] + 1;
        /* Reserve eight queue slots and stay below the driver's 128-record
         * key pool: queue_send stores pointers and checks neither limit. */
        if (capacity < 16 || count > capacity - 16 || count > 112)
            result = CORE_KEYS_BUSY;
        else
            core_stock_buttons(group, new_mask);
    }
    if (result == CORE_KEYS_OK)
        synthetic[group] = mask;
    core_keys_unlock(sr);
    return result;
}

int core_keys_clear(void)
{
    unsigned group;
    int result;
    for (group = 0; group < CORE_KEYS_GROUPS; ++group) {
        result = core_keys_set(group, 0);
        if (result != CORE_KEYS_OK)
            return result;
    }
    return CORE_KEYS_OK;
}

extern void fw_panel_encoder(unsigned index, int delta);

/* The stock decoder accumulates detents; its periodic callback queues the
 * event, so it follows the same ev_enc/stock path as a physical turn. */
int core_encoder_turn(unsigned encoder, int delta)
{
    unsigned sr, count, capacity;
    int result = CORE_KEYS_OK;
    if (encoder < 1 || encoder > 9 || !delta || delta < -30 || delta > 30)
        return CORE_KEYS_INVALID;
    sr = core_keys_lock();
    count = fw_ui_queue[1];
    capacity = fw_ui_queue[4] + 1;
    if (capacity < 16 || count > capacity - 16 || count > 48)
        result = CORE_KEYS_BUSY;
    else
        fw_panel_encoder(encoder - 1, delta);
    core_keys_unlock(sr);
    return result;
}
