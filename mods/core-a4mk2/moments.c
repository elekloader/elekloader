/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: core-ar2 0.11's moments on the Analog Four MKII (README,
 * "Moments"; analog-four-mk2/moments.h). Every address is OS 1.56's.
 *
 *   - core_project_name: the live project's name.
 */

#include <stdint.h>

/* the live project (the project model's vtable +0x28 returns it): its name
 * the first 16 bytes, which the file save (0x4001141e) copies to the
 * image's +8; the working pattern at +0x10, then the 128 patterns, the kits
 * as edited at +0xb95cea and as last saved at +0xbe0cea */
#define LIVE_PROJECT ((unsigned char *)0x40653c4c)
#define PROJECT_NAME ((const char *)LIVE_PROJECT)

/* the live project's name: 16 bytes, not always zero-terminated */
const char *core_project_name(void)
{
    return PROJECT_NAME;
}
