/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The MODS page's background check tick by tick (page.c read_folder, check_next, dl_page_tick),
 * built for the computer with a made-up card whose calls take bus time: tests/test_drive_page.py
 * builds it with the host's cc and reads what it prints. T230 (2026-11-09, the unit, 257 tiny
 * files in one folder) found two faults the emulator's fast card hid: opening the folder and the
 * check held the UI task long enough that a remote key was refused, and closing the page partway
 * through a file's read left the staging buffer taken, so the route answered "Busy" until a power
 * cycle.
 * argv: FILES OPEN_US READ_US CHECK_US SHOW_TICKS TICKS KEY_EVERY
 *   FILES       the files in DL_DIR "/~T DL" (each smaller than a stretch: two reads, as on the card)
 *   OPEN_US     bus time of one file open (its header read included)
 *   READ_US     ... of one read call;  CHECK_US ... of one file's check (dl_check_buf)
 *   SHOW_TICKS  the page shows (draws its rows when marked dirty) for this many ticks, then closes
 *   TICKS       ticks run in all
 *   KEY_EVERY   a key (ev_key) every KEY_EVERY ticks while the page shows (0 none), and four at
 *               its close, as T230's DOWN presses and NO x4
 * prints: "OPENS_AT_FOLDER n" (files opened while the folder was read), then at the end
 * "CHECKED n/N", "STAGING owner", "READING r", "WORK_US us" (the background's bus time) and
 * "BUSIEST_US us" (its most in any one second, 20 ticks). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long bus_us;        /* the made-up bus clock, in µs */
static int dirty;
#define DL_BUS_NOW() ((u32)(bus_us * DL_BUS_PER_US))
#define VIEW_DIRTY(v) ((void)(v), dirty = 1)

#include "../mods/drive-loader-ar2/page.c"

static int nfile, open_us, read_us, check_us, opens;
static int owner;

int dl_list_dir(const char *rel, struct dl_ent *out, int max)
{
    int i, k = 0;
    if (!rel[0]) {                       /* DL_DIR: the one folder */
        if (out && max > 0) {
            strcpy(out[0].name, "~T DL");
            out[0].dir = 1;
        }
        return 1;
    }
    for (i = 0; i < nfile; i++) {
        if (out && k < max) {
            snprintf(out[k].name, sizeof out[k].name, "~T DL-%03d.elemod", i + 1);
            out[k].dir = 0;
        }
        k++;
    }
    return out ? (k < max ? k : max) : k;
}

int dl_fs_file_size(const char *path)
{
    (void)path;
    opens++;
    bus_us += open_us;
    return 1000;
}

int dl_read_open(struct dl_reader *rd, const char *name)
{
    (void)name;
    opens++;
    bus_us += open_us;
    memset(rd, 0, sizeof *rd);
    rd->open = 1;
    rd->crc = 0xffffffff;
    return 0;
}

int dl_read_more(struct dl_reader *rd, unsigned char *buf, unsigned max)
{
    (void)buf, (void)max;
    bus_us += read_us;
    if (!rd->got) {                      /* the file's bytes, then the end on the next call */
        rd->got = 1000;
        return 0;
    }
    rd->open = 0;
    return 1;
}

void dl_read_close(struct dl_reader *rd) { rd->open = 0; }

int dl_check_buf(u32 desc, const struct dl_choice *c, s32 len, u8 *buf, u32 cap, struct dl_status *st,
                 char (*det)[32], char (*ver)[DL_VER_MAX])
{
    (void)desc, (void)c, (void)len, (void)buf, (void)cap, (void)det, (void)ver;
    bus_us += check_us;
    memset(st, 0, sizeof *st);
    return -1;
}

int dl_staging_take_idle(unsigned now, unsigned stall)
{
    (void)now, (void)stall;
    if (owner)
        return 0;
    owner = 1;
    return 1;
}

void dl_staging_give(void) { owner = 0; }
static unsigned char stage[0x10000];     /* the live stage (route.c's): the floor */
unsigned char *volatile dl_stage_at = stage;
volatile unsigned dl_stage_len = sizeof stage;
u8 dl_stage_top;
int dl_area(u32 desc, u32 *lo, u32 *hi) { (void)desc; *lo = *hi = 0; return -1; }
void dl_readers_poll(unsigned now) { (void)now; }
void dl_lock(void) {}
void dl_unlock(void) {}
void *dl_alloc(u32 n) { return calloc(1, n ? n : 1); }
void dl_free(void *p) { free(p); }
void dl_memcpy(void *d, const void *s, u32 n) { memmove(d, s, n); }
void dl_memset(void *d, int c, u32 n) { memset(d, c, n); }
int dl_strcmp(const char *a, const char *b) { return strcmp(a, b); }
u32 dl_strlen(const char *s) { return (u32)strlen(s); }
u32 dl_status_n;
struct dl_choice *dl_boot_choice;
struct dl_status *dl_status;
char (*dl_detail)[32];
volatile unsigned dl_fs_gen;
volatile int dl_move_seen = 1;
char dl_move_from[DL_NAME_MAX + 1], dl_move_to[DL_NAME_MAX + 1];

int main(int argc, char **argv)
{
    static char ctrl[0x40], pane[0x100];
    int show, total, keys, t, i, n;
    unsigned long long work = 0, sec = 0, busiest = 0, before;
    if (argc != 8)
        return 2;
    nfile = atoi(argv[1]);
    open_us = atoi(argv[2]);
    read_us = atoi(argv[3]);
    check_us = atoi(argv[4]);
    show = atoi(argv[5]);
    total = atoi(argv[6]);
    keys = atoi(argv[7]);
    files = calloc(1, sizeof *files);
    fcap = 1;
    view = pane;
    copy_str(cwd, "~T DL", sizeof cwd);  /* YES on the folder (go): read_folder */
    depth = 1;
    opens = 0;
    if (read_folder())
        return 3;
    printf("OPENS_AT_FOLDER %d\n", opens);
    drawn_at = ticks;
    for (t = 0; t < total; t++) {
        if ((keys && t < show && t % keys == 0) || (t >= show && t < show + 4))
            dl_key(0, 0);
        before = bus_us;
        dl_page_tick(ctrl);
        work += bus_us - before;
        sec += bus_us - before;
        if (t % 20 == 19) {
            if (sec > busiest)
                busiest = sec;
            sec = 0;
        }
        bus_us += 50000;                 /* the rest of the tick: stock's own work */
        if ((dirty || ctrl[0x20]) && t < show)   /* a page that shows draws its rows */
            drawn_at = ticks;
        dirty = 0;
        ctrl[0x20] = 0;
    }
    for (n = 0, i = 0; i < nlrows; i++)
        n += lrows[i].kind == R_FILE && files[lrows[i].f].checked;
    printf("CHECKED %d/%d\nSTAGING %d\nREADING %d\nWORK_US %llu\nBUSIEST_US %llu\n", n, nfile, owner, reading,
           work, busiest);
    return 0;
}
