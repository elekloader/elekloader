/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ddr-canary: checks on the Analog Four MKII (OS 1.56) that nothing but
 * mods touches the DDR the profile calls unused:
 *   0x448B6D50-0x44C00000  above the RAM the OS clears at start
 *   0x456472C0-0x47BE0000  above the OS's last fixed buffer
 *   __bss_end-0x47C00000   the mods' area after the mods' own data
 * It fills them with address ^ 0xC0DEF00D (the Rytm's canary), then reads
 * them back for as long as it runs, from ev_tick, 64 KB a tick (about
 * 20 ticks a second: a fill and each later sweep take about 35 s). Both go
 * through the uncached view (address + 0x08000000), so what it reads is
 * what DDR holds. A changed word is counted, never written back.
 *
 * Over the frame (y = 0 is the bottom row):
 *   y 60-61  the sweeps done (mod 120); empty until the fill is done
 *   y 56-57  the changed words the last sweep found (up to 120)
 *   y 52-53  three 3x2 dots: the regions above that ever had a changed word
 *   y 48-49  the first changed word's address, 32 dots of 2x2, the most
 *            significant bit at the left, lit for 1 (empty while none)
 * and in the top-right corner a 3x3 square that blinks while it runs; a
 * second, below it, stays lit once any word has changed.
 */
typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#ifndef FILLRECT_AT
#error "FILLRECT_AT: fill_rect's address (mod.json cflags)"
#endif
#define FILLRECT ((fillrect_t)FILLRECT_AT)

#define UNCACHED  0x08000000u
#define PATTERN   0xC0DEF00Du
#define STEP      (64 * 1024)
#define MODS_END  0x47C00000u

extern char __bss_end[];                  /* the linker's: the mods' data end */

static unsigned cn_lo[3] = {0x448B6D50u, 0x456472C0u, 0};
static const unsigned cn_hi[3] = {0x44C00000u, 0x47BE0000u, MODS_END};

static volatile unsigned cn_ticks, cn_sweeps, cn_bad, cn_last_bad;
static volatile unsigned cn_first, cn_regions, cn_any;
static unsigned cn_region, cn_at, cn_found, cn_filled, cn_started;

static void cn_start(void)
{
    cn_lo[2] = ((unsigned)__bss_end + 15) & ~15u;
    cn_region = 0;
    cn_at = cn_lo[0];
    cn_started = 1;
}

/* One step over [cn_at, cn_at + STEP) of the current region: a fill or a
 * check. Returns 1 when the last region's end was reached. */
static int cn_step(void)
{
    unsigned a = cn_at, end = a + STEP;
    if (end > cn_hi[cn_region] || end < a)
        end = cn_hi[cn_region];
    if (!cn_filled) {
        for (; a < end; a += 4)
            *(volatile unsigned *)(a + UNCACHED) = a ^ PATTERN;
    } else {
        for (; a < end; a += 4) {
            if (*(volatile unsigned *)(a + UNCACHED) != (a ^ PATTERN)) {
                if (!cn_any) {
                    cn_first = a;
                    cn_any = 1;
                }
                cn_regions |= 1u << cn_region;
                cn_found++;
                cn_bad++;
            }
        }
    }
    cn_at = end;
    if (cn_at < cn_hi[cn_region])
        return 0;
    if (++cn_region < 3) {
        cn_at = cn_lo[cn_region];
        return 0;
    }
    cn_region = 0;
    cn_at = cn_lo[0];
    return 1;
}

void cn_tick(void *ctrl)
{
    if (!cn_started)
        cn_start();
    if (cn_step()) {
        if (cn_filled) {
            cn_last_bad = cn_found;
            cn_sweeps++;
        }
        cn_filled = 1;
        cn_found = 0;
    }
    if ((++cn_ticks & 7) == 0)
        *((volatile unsigned char *)ctrl + 0x20) = 1;
}

static void cn_bar(void *bmp, int y, unsigned n)
{
    FILLRECT(bmp, 0, y, 123, y + 1, 0);
    if (n)
        FILLRECT(bmp, 0, y, (int)n - 1, y + 1, 1);
}

void cn_draw(void *bmp, void *ctrl)
{
    unsigned last = cn_last_bad;
    int i;
    (void)ctrl;
    cn_bar(bmp, 60, cn_sweeps % 120);
    cn_bar(bmp, 56, last > 120 ? 120 : last);
    FILLRECT(bmp, 0, 52, 127, 53, 0);
    for (i = 0; i < 3; i++)
        if (cn_regions & (1u << i))
            FILLRECT(bmp, i * 5, 52, i * 5 + 2, 53, 1);
    FILLRECT(bmp, 0, 48, 127, 49, 0);
    if (cn_any)
        for (i = 0; i < 32; i++)
            if (cn_first & (0x80000000u >> i))
                FILLRECT(bmp, i * 4, 48, i * 4 + 1, 49, 1);
    FILLRECT(bmp, 125, 61, 127, 63, (cn_ticks >> 4) & 1);
    FILLRECT(bmp, 125, 56, 127, 58, cn_any ? 1 : 0);
}
