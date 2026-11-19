/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The MODS page's build list label clip (mods/drive-loader-ar2/clip.inc) with a made-up
 * proportional font: tests/test_drive_page.py builds it with the host's cc and drives it.
 * Each stdin line is "ROOM PATH"; each output line "WIDTH LABEL" (the fake font's width). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/drive-loader-ar2/clip.inc"

static int fake_width(const char *s)          /* 2 for . and space, 6 for M W ~ m w, else 4 */
{
    int w = 0;
    for (; *s; s++)
        w += strchr(". ", *s) ? 2 : strchr("MW~mw", *s) ? 6 : 4;
    return w;
}

int main(void)
{
    char line[512], label[48];
    while (fgets(line, sizeof line, stdin)) {
        char *path;
        int room = (int)strtol(line, &path, 10);
        path++;
        path[strcspn(path, "\r\n")] = 0;
        clip_tail(label, label + sizeof label, path, room, fake_width);
        printf("%d %s\n", fake_width(label), label);
    }
    return 0;
}
