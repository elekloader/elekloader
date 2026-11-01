/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.9: velocity and aftertouch routes to a lockable mod parameter
 * (README, "Velocity and aftertouch destinations"). Every address is OS
 * 1.74's.
 *
 * A lockable field declared "mod": true (ROUTE in core_lock_codes) is listed
 * at the end of each sound's VELOCITY MOD and AFTERTOUCH lists (lock.c,
 * lock.s), and a route to it keeps the field's code (42..127) in its own
 * destination byte: the sound's save and load pass it through 0.8's
 * core_code_id and core_id_code, with no room of its own.
 *
 * The engine push's index sites (routes.s) give such a route stock's empty
 * entry and keep its amount and code in core_routes; core_mod_offset hands
 * the mod what stock would have added, g x amount, as the audio interrupt
 * adds a stock route's (0x40120f10..0x40120f72, through 0x4011de2a):
 *
 *   - velocity: g = the track's engine word 39 (0x8000f7f6 + 0x54 t) when
 *     bit 3 of 0x8000e53c + 4 t is set, else 0, plus 0x800067c4[t], in the
 *     high halves, saturated; then the high half;
 *   - aftertouch: g = 0x800067de[t];
 *   - the add: the EMAC in fractional mode (MACSR 0x20 in the interrupt),
 *     2 g amount, then the high half: g x amount >> 15, in 8.8 steps of a
 *     stock 0..127 knob, the unit of core_lfo_offset.
 *
 * On a sound's load (store.c), a route to a code this build has no "mod"
 * parameter for, or any code above 41 in a sound of another layout, becomes
 * a route to code 0 with amount 0, as stock's defaults leave an unused one;
 * the amount's low byte, mod storage on a velocity route (route.s), is kept.
 */

#include <stdint.h>

#define FIRST_CODE 42
#define LAST_CODE 127
#define TRACKS 12                             /* drum tracks: the FX track has no routes */
#define ROUTES 8                              /* 4 velocity, then 4 aftertouch */

#define VEL_FLAGS ((const volatile uint32_t *)0x8000e53c)
#define VEL_WORD39 ((const volatile uint8_t *)0x8000f7f6)  /* + 0x54 t, s16 */
#define VEL_EXTRA ((const volatile uint16_t *)0x800067c4)
#define AT_PRESSURE ((const volatile int16_t *)0x800067de)

#define LIVE_VEL 0x70                         /* a live sound's routes, 3 bytes each */
#define LIVE_AT 0x8c

struct core_lock_param;
int core_lock_code(unsigned field);
const struct core_lock_param *core_lock_route(unsigned code);

/* routes.s writes it: per track, the engine push's 8 entries' mod routes,
 * {s16 amount (the engine's word), s16 code}, 0 for a stock route */
volatile uint32_t core_routes[13 * ROUTES];

static int vel_g(int t)
{
    uint16_t w = *(const volatile uint16_t *)(VEL_WORD39 + 0x54 * t);
    int32_t a = VEL_FLAGS[t] & 8 ? (int32_t)((uint32_t)w << 16) : 0;
    int32_t b = (int32_t)((uint32_t)VEL_EXTRA[t] << 16);
    int32_t s = (int32_t)((uint32_t)a + (uint32_t)b);
    if ((a < 0) == (b < 0) && (s < 0) != (a < 0))
        s = a < 0 ? (int32_t)0x80000000 : 0x7fffffff;   /* satsl */
    return (int16_t)(s >> 16);
}

int core_mod_offset(int track, unsigned field)
{
    unsigned code;
    int i, sum = 0;
    const volatile uint32_t *e;
    if ((unsigned)track >= TRACKS || !(code = core_lock_code(field)) || !core_lock_route(code))
        return 0;
    e = core_routes + ROUTES * track;
    for (i = 0; i < ROUTES; i++) {
        uint32_t v = e[i];
        if ((v & 0xffff) == code)
            sum += ((i < 4 ? vel_g(track) : AT_PRESSURE[track]) * (int16_t)(v >> 16)) >> 15;
    }
    return sum;
}

/* store.c, after a sound's load: ok when the sound is of this build's layout
 * (or an ancestor's) */
void core_routes_load(unsigned char *live, int ok)
{
    int i, code;
    unsigned char *r;
    for (i = 0; i < ROUTES; i++) {
        r = live + (i < 4 ? LIVE_VEL + 3 * i : LIVE_AT + 3 * (i - 4));
        code = (signed char)r[2];
        if (code >= FIRST_CODE && code <= LAST_CODE && !(ok && core_lock_route(code))) {
            r[0] = 0;                         /* the amount's high byte; the low byte is kept */
            r[2] = 0;
        }
    }
}
