/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
volatile unsigned char fw_key_state[7];
volatile unsigned fw_ui_queue[8];
static unsigned depth, calls, enc_calls, enc_index;
static int enc_delta;
unsigned core_keys_lock(void) { return depth++; }
void core_keys_unlock(unsigned sr) { assert(depth && --depth == sr); }
void core_stock_buttons(unsigned group, unsigned mask) {
    assert(depth); calls++; fw_key_state[group]=mask;
}
void fw_panel_encoder(unsigned index, int delta) {
    assert(depth); enc_calls++; enc_index=index; enc_delta=delta;
}
#include "../mods/core-dn2/input.c"
int main(void) {
    fw_ui_queue[4]=255;
    assert(core_keys_set(7,1)==CORE_KEYS_INVALID);
    assert(core_keys_set(0,256)==CORE_KEYS_INVALID);
    assert(core_keys_set(6,4)==CORE_KEYS_INVALID);
    assert(core_keys_set(6,8)==CORE_KEYS_INVALID);
    core_panel_buttons(2,1);
    assert(core_keys_set(2,1)==CORE_KEYS_OVERLAP);
    assert(core_keys_set(2,2)==CORE_KEYS_OK && fw_key_state[2]==3);
    core_panel_buttons(2,3);
    assert(core_keys_clear()==CORE_KEYS_OK && fw_key_state[2]==3);
    core_panel_buttons(2,0);
    assert(core_keys_set(2,2)==CORE_KEYS_OK);
    fw_ui_queue[1]=113;
    assert(core_keys_clear()==CORE_KEYS_BUSY && fw_key_state[2]==2);
    fw_ui_queue[1]=0;
    assert(core_keys_clear()==CORE_KEYS_OK && fw_key_state[2]==0);
    assert(core_encoder_turn(0,1)==CORE_KEYS_INVALID);
    assert(core_encoder_turn(10,1)==CORE_KEYS_INVALID);
    assert(core_encoder_turn(1,0)==CORE_KEYS_INVALID);
    assert(core_encoder_turn(1,31)==CORE_KEYS_INVALID);
    assert(core_encoder_turn(1,-31)==CORE_KEYS_INVALID);
    assert(core_encoder_turn(9,-30)==CORE_KEYS_OK);
    assert(enc_calls==1 && enc_index==8 && enc_delta==-30);
    fw_ui_queue[1]=49;
    assert(core_encoder_turn(1,1)==CORE_KEYS_BUSY && enc_calls==1);
    fw_ui_queue[1]=0;
    fw_ui_queue[4]=7;
    assert(core_encoder_turn(1,1)==CORE_KEYS_BUSY && enc_calls==1);
    assert(!depth && calls>0);
}
