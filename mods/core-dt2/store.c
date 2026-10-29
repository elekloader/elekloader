/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-dt2 1.1 and core-dn2 0.3: the mods' project data and kit data (store.s
 * calls these from the firmware's saves, loads and kit copies; the format:
 * docs/ADAPTING.md, "Frames, LEDs, project and kit data, value rows").
 *
 * Both live in bytes of the saved project the stock OS writes as zeros and
 * never reads, so a project saved with mods loads on the stock OS, which
 * keeps them when it saves a project it loaded. Core writes them only where
 * they hold zeros or its own data, and takes only its own: anything else
 * (another tool's, a later OS's) is left as it is and counted. */
#include "mk2/core.h"

extern const uint32_t core_store_layout[]; /* store.s: kits, kit size, pad, room, pattern object */
extern const core_projdata_t *const core_projdata[];
extern const core_kitdata_t *const core_kitdata[];

volatile uint32_t core_store_conflicts;

#define KITS (core_store_layout[0])
#define KIT_STRIDE (core_store_layout[1])
#define KIT_PAD (core_store_layout[2])
#define KIT_ROOM (core_store_layout[3])
#define PATTERN_OBJ (core_store_layout[4])

/* ---- project data: the header's gap, as on core-dn1: 'ELKP', then per block its
 * tag, its size and its bytes, then a zero tag ---- */
#define GAP 0x1c
#define GAP_END 0x200
#define PROJ_MAGIC 0x454c4b50u /* 'ELKP' */

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static int blank(const uint8_t *p, uint32_t n)
{
    while (n--)
        if (*p++)
            return 0;
    return 1;
}

static void copy(uint8_t *to, const uint8_t *from, uint32_t n)
{
    while (n--)
        *to++ = *from++;
}

static void zero(uint8_t *to, uint32_t n)
{
    while (n--)
        *to++ = 0;
}

void core_projdata_save(uint8_t *file)
{
    uint8_t *p = file + GAP, *end = file + GAP_END;
    if (!core_projdata[0])
        return;                     /* no mod keeps project data: the bytes as the OS left them */
    if (!blank(p, GAP_END - GAP) && get32(p) != PROJ_MAGIC) {
        core_store_conflicts++;
        return;
    }
    zero(p, GAP_END - GAP);
    put32(p, PROJ_MAGIC);
    p += 4;
    for (const core_projdata_t *const *d = core_projdata; *d; d++) {
        uint32_t n = (*d)->size;
        if ((uint32_t)(end - p) < n + 12) /* tag, size, bytes and the zero tag after */
            continue;               /* does not fit: left out */
        copy(p, (const uint8_t *)(*d)->tag, 4);
        put32(p + 4, n);
        copy(p + 8, (*d)->data, n);
        p += 8 + n;
    }
}

void core_projdata_load(const uint8_t *file)
{
    const uint8_t *gap = file + GAP, *end = file + GAP_END;
    int ours = get32(gap) == PROJ_MAGIC;
    if (!ours && !blank(gap, GAP_END - GAP))
        core_store_conflicts++;
    for (const core_projdata_t *const *d = core_projdata; *d; d++) {
        const uint8_t *p = gap + 4;
        int found = 0;
        zero((*d)->data, (*d)->size);
        while (ours && end - p >= 8 && get32(p)) {
            uint32_t n = get32(p + 4);
            if ((uint32_t)(end - p - 8) < n)
                break;              /* runs past the gap: corrupt, stop */
            if (get32(p) == get32((const uint8_t *)(*d)->tag)) {
                copy((*d)->data, p + 8, n < (*d)->size ? n : (*d)->size);
                found = 1;
                break;
            }
            p += 8 + n;
        }
        if ((*d)->loaded)
            (*d)->loaded(found);
    }
}

/* ---- kit data: after each kit record's end mark, a mark, the layout's id and the
 * blocks in the table's order; a block that does not fit is left out ---- */
#define KIT_MARK 0x4b /* 'K' */
#define KIT_HEAD 3
#define SHADOWS 8
#define ROOM_MAX 340

static uint16_t layout_id;
static uint8_t layout_known;

/* A CRC-16 of each block's tag and size, in the table's order: kit data saved by
 * another set of kit-data mods is not taken for this one's. Never 0. */
static uint16_t layout(void)
{
    if (!layout_known) {
        uint16_t c = 0xffff;
        for (const core_kitdata_t *const *d = core_kitdata; *d; d++) {
            uint8_t b[8];
            copy(b, (const uint8_t *)(*d)->tag, 4);
            put32(b + 4, (*d)->size);
            for (int i = 0; i < 8; i++) {
                c ^= (uint16_t)(b[i] << 8);
                for (int k = 0; k < 8; k++)
                    c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
            }
        }
        layout_id = c ? c : 1;
        layout_known = 1;
    }
    return layout_id;
}

uint32_t core_kitdata_room(void)
{
    return KIT_ROOM - KIT_HEAD;
}

int core_kit_index(const void *kit)
{
    uint32_t off = (uint32_t)kit - KITS;
    if ((uint32_t)kit < KITS || off % KIT_STRIDE || off / KIT_STRIDE >= CORE_KITS)
        return -1;
    return (int)(off / KIT_STRIDE);
}

int core_kit_on(void)
{
    uint32_t o = *(volatile uint32_t *)PATTERN_OBJ, n;
    if (!o)
        return -1;
    n = *(volatile uint32_t *)(o + 0x138);
    return n < CORE_KITS ? (int)n : -1;
}

/* kit's blocks into out (KIT_ROOM bytes), or from in; in 0: zeros, found 0 */
static void pack(int kit, uint8_t *out)
{
    uint8_t *p = out + KIT_HEAD, *end = out + KIT_ROOM;
    zero(out, KIT_ROOM);
    out[0] = KIT_MARK;
    out[1] = (uint8_t)(layout() >> 8), out[2] = (uint8_t)layout();
    for (const core_kitdata_t *const *d = core_kitdata; *d; d++) {
        uint32_t n = (*d)->size;
        if ((uint32_t)(end - p) < n)
            break;
        copy(p, (const uint8_t *)(*d)->data + (uint32_t)kit * n, n);
        p += n;
    }
}

static void unpack(int kit, const uint8_t *in)
{
    int ok = in && in[0] == KIT_MARK && (uint16_t)(in[1] << 8 | in[2]) == layout();
    const uint8_t *p = in ? in + KIT_HEAD : 0, *end = in ? in + KIT_ROOM : 0;
    for (const core_kitdata_t *const *d = core_kitdata; *d; d++) {
        uint32_t n = (*d)->size;
        uint8_t *to = (uint8_t *)(*d)->data + (uint32_t)kit * n;
        int found = ok && (uint32_t)(end - p) >= n;
        if (found) {
            copy(to, p, n);
            p += n;
        } else {
            zero(to, n);
        }
        if ((*d)->changed)
            (*d)->changed(kit, found);
    }
}

static int ours(const uint8_t *area)
{
    return area[0] == KIT_MARK;
}

/* Buffers outside the project's kits that hold a kit (the clipboard, the undo copy,
 * a kit loaded from the +Drive before it is copied in): their blocks, packed. */
static struct {
    uint32_t addr;
    uint8_t bytes[ROOM_MAX];
} shadow[SHADOWS];
static uint8_t shadow_next;

static uint8_t *shadow_of(uint32_t addr)
{
    for (int i = 0; i < SHADOWS; i++)
        if (shadow[i].addr == addr)
            return shadow[i].bytes;
    return 0;
}

static uint8_t *shadow_new(uint32_t addr)
{
    uint8_t *s = shadow_of(addr);
    if (!s) {
        shadow[shadow_next].addr = addr;
        s = shadow[shadow_next].bytes;
        shadow_next = (uint8_t)((shadow_next + 1) % SHADOWS);
    }
    return s;
}

static void shadow_drop(uint32_t addr)
{
    for (int i = 0; i < SHADOWS; i++)
        if (shadow[i].addr == addr)
            shadow[i].addr = 0;
}

void core_kitdata_save(uint8_t *record, const uint8_t *kit)
{
    uint8_t *area = record + KIT_PAD;
    int n = core_kit_index(kit);
    const uint8_t *s;
    if (!core_kitdata[0])
        return;
    if (!blank(area, KIT_ROOM) && !ours(area)) {
        core_store_conflicts++;
        return;
    }
    if (n >= 0)
        pack(n, area);
    else if ((s = shadow_of((uint32_t)kit)) != 0)
        copy(area, s, KIT_ROOM);
    else
        zero(area, KIT_ROOM);       /* a kit core knows nothing of */
}

void core_kitdata_load(const uint8_t *kit, const uint8_t *record)
{
    const uint8_t *area = record + KIT_PAD;
    int n = core_kit_index(kit);
    if (!core_kitdata[0])
        return;
    if (!blank(area, KIT_ROOM) && !ours(area))
        core_store_conflicts++;
    if (n >= 0)
        unpack(n, ours(area) ? area : 0);
    else if (ours(area))
        copy(shadow_new((uint32_t)kit), area, KIT_ROOM);
    else
        shadow_drop((uint32_t)kit);
}

void core_kit_copied(uint8_t *dst, const uint8_t *src)
{
    static uint8_t tmp[ROOM_MAX];   /* not on the stack: memcpy runs on small task stacks too */
    int d = core_kit_index(dst), s = core_kit_index(src);
    const uint8_t *from = 0;
    if (!core_kitdata[0] || dst == src)
        return;
    if (s >= 0) {
        pack(s, tmp);
        from = tmp;
    } else {
        from = shadow_of((uint32_t)src);
    }
    if (d >= 0) {
        if (from)
            unpack(d, from);
        return;                     /* from a buffer core never saw filled: the kit's blocks stay */
    }
    if (from)
        copy(shadow_new((uint32_t)dst), from, KIT_ROOM);
    else
        shadow_drop((uint32_t)dst);
}
