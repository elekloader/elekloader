/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A probe of core-ot 0.6's store, for tests/probe_octatrack_store.py: a project block, a card block
 * and a CS1 claim, each a counter the probe adds one to at every ev_project; and a record of every
 * lifecycle event. `stp` is the record, at a fixed place for the emulator's dump. */
#include <stdint.h>
#include "octatrack/core.h"

struct rec {
    uint32_t magic;                 /* "STP1" */
    uint32_t project_events, last_how, last_dropped;
    uint32_t proj_loaded_calls, proj_found;
    uint32_t card_loaded_calls, card_found;
    uint32_t proj_count, card_count, cs1_count;     /* the counters after the last ev_project */
    uint32_t banks_load[2], banks_load_mask;        /* [pre], the last mask */
    uint32_t banks_write[2], banks_write_mask;
    uint32_t store[2], reload[2];
    uint32_t cs1_bank[2];
    uint32_t part_saved, part_cleared;
    uint32_t boot_done;
    uint32_t engine_runs, engine_arg;
    uint32_t cs1_addr;
    uint32_t call_fn, call_arg, call_tick;  /* poked: fn(arg) once from the UI task, call_tick ticks after the poke */
    uint32_t ticks, called;
};
volatile struct rec stp = { 0x53545031u };

static uint32_t proj_data[4];       /* [0] the counter */
static uint32_t card_data[2];
static void *cs1;

static void proj_loaded(int32_t found) { ++stp.proj_loaded_calls; stp.proj_found = found; }
static void card_loaded(int32_t found) { ++stp.card_loaded_calls; stp.card_found = found; }

const struct core_projdata stp_proj = { OT_TAG('S', 'T', 'P', 'P'), sizeof proj_data, proj_data, proj_loaded };
const struct core_projdata stp_card = { OT_TAG('S', 'T', 'P', 'C'), sizeof card_data, card_data, card_loaded };
const struct ot_sram stp_sram = { OT_TAG('S', 'T', 'P', 'S'), 32, &cs1 };

static void engine_job(uint32_t arg) { ++stp.engine_runs; stp.engine_arg = arg; }

void stp_project(int32_t how, uint32_t dropped)
{
    ++stp.project_events;
    stp.last_how = how;
    stp.last_dropped = dropped;
    ++proj_data[0];
    ++card_data[0];
    stp.proj_count = proj_data[0];
    stp.card_count = card_data[0];
    if (cs1) {
        ++*(volatile uint32_t *)cs1;
        stp.cs1_count = *(volatile uint32_t *)cs1;
    }
    core_projdata_dirty(stp_proj.tag);
    core_carddata_dirty(stp_card.tag);
    ot_engine_post(engine_job, 0x1234);
}

void stp_banks_load(unsigned pre, unsigned mask) { ++stp.banks_load[pre & 1]; stp.banks_load_mask = mask; }
void stp_banks_write(unsigned pre, unsigned mask) { ++stp.banks_write[pre & 1]; stp.banks_write_mask = mask; }
void stp_store(unsigned pre, unsigned b) { (void)b; ++stp.store[pre & 1]; }
void stp_reload(unsigned pre, unsigned b) { (void)b; ++stp.reload[pre & 1]; }
void stp_cs1_bank(unsigned to, unsigned bank) { (void)bank; ++stp.cs1_bank[to & 1]; }
void stp_part_saved(unsigned part, unsigned b) { (void)part; (void)b; ++stp.part_saved; }
void stp_part_cleared(unsigned part, unsigned b) { (void)part; (void)b; ++stp.part_cleared; }
void stp_boot_done(void) { ++stp.boot_done; stp.cs1_addr = (uint32_t)(uintptr_t)cs1; }

/* ev_tick: a stock routine the test names, from the UI task, as the panel would call it. */
void stp_tick(void)
{
    if (!stp.call_fn || stp.called || ++stp.ticks < stp.call_tick)
        return;
    stp.called = 1;
    ((void (*)(uint32_t))stp.call_fn)(stp.call_arg);
}
