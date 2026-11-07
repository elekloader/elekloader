/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: a lockable parameter's value on the dial's scale (README,
 * "Dial scale (0.11)"), for a parameter narrower than the dial whose knob
 * the mod's core_params get answers on 0..127. Opt-in: an entry in
 * core_param_scales for the knob's id,
 *
 *     {first, last, show, unshow, flags}
 *
 * gives show(id, field value) -> 8.8 on the dial and unshow(id, 8.8) ->
 * field value, or with both 0 core's linear map, round((v - min) * 127 /
 * (max - min)) and its inverse. Without an entry a value is the field's, as
 * before. Core converts where a value meets the page: a held trig's lock
 * (get, set, turn), a held scene's lock, MIDI in and PARAM OUTPUT, and the
 * text; a performance's depth is shown in dial steps. Stock's copy and
 * paste of locks carry field values both ways and need nothing.
 *
 * And core_held_kind(view): what the page's held pad is, for a mod drawing
 * its knob; with a performance's pad held core writes a mod parameter's
 * text as the signed depth unless the entry's flags have
 * CORE_SCALE_DEPTHS. */
#include <stdint.h>

struct core_lock_param;
int core_lock_min(const struct core_lock_param *p);
int core_lock_max(const struct core_lock_param *p);
int core_lock_id(const struct core_lock_param *p);

struct core_param_scale {               /* lock.h */
    int32_t first, last;
    int32_t (*show)(int32_t id, int32_t value);
    int32_t (*unshow)(int32_t id, int32_t dial);
    uint32_t flags;
};
#define CORE_SCALE_DEPTHS 1             /* the mod writes a depth's text itself */

extern const struct core_param_scale *const core_param_scales[]; /* zero-terminated */

static const struct core_param_scale *scale_of(const struct core_lock_param *p)
{
    const struct core_param_scale *const *e;
    int id;
    if (!p || !(id = core_lock_id(p)))
        return 0;
    for (e = core_param_scales; *e; e++)
        if (id >= (*e)->first && id <= (*e)->last)
            return *e;
    return 0;
}

/* the linear map's span, 0 when the field is as wide as the dial or wider */
static int span_of(const struct core_lock_param *p)
{
    int s = core_lock_max(p) - core_lock_min(p);
    return s > 0 && s < 127 ? s : 0;
}

static int div_round(int a, int b)      /* b > 0 */
{
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

int core_scale_has(const struct core_lock_param *p)
{
    return scale_of(p) != 0;
}

/* a field value on the dial, 8.8 */
int core_scale_show(const struct core_lock_param *p, int v)
{
    const struct core_param_scale *e = scale_of(p);
    int s;
    if (!e)
        return v << 8;
    if (e->show)
        return e->show(core_lock_id(p), v);
    if (!(s = span_of(p)))
        return v << 8;
    return div_round((v - core_lock_min(p)) * 127, s) << 8;
}

/* a dial value (8.8) as the field's, not clamped */
int core_scale_unshow(const struct core_lock_param *p, int dial)
{
    const struct core_param_scale *e = scale_of(p);
    int s;
    if (!e)
        return dial >> 8;
    if (e->unshow)
        return e->unshow(core_lock_id(p), dial);
    if (!(s = span_of(p)))
        return dial >> 8;
    return core_lock_min(p) + div_round(dial * s, 127 * 256);
}

/* a performance's depth (field steps) in dial steps, whole */
int core_scale_depth(const struct core_lock_param *p, int d)
{
    const struct core_param_scale *e = scale_of(p);
    int s, lo;
    if (!e)
        return d;
    if (e->show) {
        lo = core_lock_min(p);
        return div_round(e->show(core_lock_id(p), lo + d) - e->show(core_lock_id(p), lo), 256);
    }
    if (!(s = span_of(p)))
        return d;
    return div_round(d * 127, s);
}

int core_scale_depths_own(const struct core_lock_param *p)
{
    const struct core_param_scale *e = scale_of(p);
    return e && (e->flags & CORE_SCALE_DEPTHS);
}
