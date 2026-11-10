/* SPDX-License-Identifier: GPL-2.0-or-later */
/* look-check: its pages (generated with looks.h) */
#include <stdint.h>
#include "analog-rytm-mk2/pages.h"
#include "looks.h"
CORE_KNOBS;
static const int16_t l[] = {LOOK_IDS, UI_LOOK, 94};
const struct core_page lk_page_0 = {60, "LOOK CHECK 1", {l[0], CORE_KNOB(0), l[1], CORE_KNOB(1), l[2], CORE_KNOB(2), l[3], CORE_KNOB(3)}};
const struct core_view_page lk_view_0 = {CORE_VIEW_LFO, 60, 0};
const struct core_page lk_page_1 = {61, "LOOK CHECK 2", {l[4], CORE_KNOB(4), l[5], CORE_KNOB(5), l[6], CORE_KNOB(6), l[7], CORE_KNOB(7)}};
const struct core_view_page lk_view_1 = {CORE_VIEW_LFO, 61, 0};
const struct core_page lk_page_2 = {62, "LOOK CHECK 3", {l[8], CORE_KNOB(8), l[9], CORE_KNOB(9), l[10], CORE_KNOB(10), l[11], CORE_KNOB(11)}};
const struct core_view_page lk_view_2 = {CORE_VIEW_LFO, 62, 0};
const struct core_page lk_page_3 = {63, "LOOK CHECK 4", {l[12], CORE_KNOB(12), l[13], CORE_KNOB(13), l[14], CORE_KNOB(14), l[15], CORE_KNOB(15)}};
const struct core_view_page lk_view_3 = {CORE_VIEW_LFO, 63, 0};
const struct core_page lk_page_4 = {64, "LOOK CHECK 5", {l[16], CORE_KNOB(16), l[17], CORE_KNOB(17), l[18], CORE_KNOB(18), l[19], CORE_KNOB(19)}};
const struct core_view_page lk_view_4 = {CORE_VIEW_LFO, 64, 0};
const struct core_page lk_page_5 = {65, "LOOK CHECK 6", {l[20], CORE_KNOB(20), l[21], CORE_KNOB(21), l[22], CORE_KNOB(22), l[23], CORE_KNOB(23)}};
const struct core_view_page lk_view_5 = {CORE_VIEW_LFO, 65, 0};
const struct core_page lk_page_6 = {66, "LOOK CHECK 7", {l[24], CORE_KNOB(24), l[25], CORE_KNOB(25), l[26], CORE_KNOB(26), l[27], CORE_KNOB(27)}};
const struct core_view_page lk_view_6 = {CORE_VIEW_LFO, 66, 0};
const struct core_page lk_page_7 = {67, "LOOK CHECK 8", {l[28], CORE_KNOB(28), l[29], CORE_KNOB(29), l[30], CORE_KNOB(30), l[31], CORE_KNOB(31)}};
const struct core_view_page lk_view_7 = {CORE_VIEW_LFO, 67, 0};
const struct core_page lk_page_8 = {68, "LOOK CHECK 9", {l[32], CORE_KNOB(32), l[33], CORE_KNOB(33), l[34], CORE_KNOB(34), l[35], CORE_KNOB(35)}};
const struct core_view_page lk_view_8 = {CORE_VIEW_LFO, 68, 0};
const struct core_page lk_page_9 = {69, "LOOK CHECK 10", {l[36], CORE_KNOB(36), l[37], CORE_KNOB(37), l[38], CORE_KNOB(38), l[39], CORE_KNOB(39)}};
const struct core_view_page lk_view_9 = {CORE_VIEW_LFO, 69, 0};
const struct core_page lk_page_10 = {70, "LOOK CHECK 11", {l[40], CORE_KNOB(40), l[41], CORE_KNOB(41), l[42], CORE_KNOB(42), l[43], CORE_KNOB(43)}};
const struct core_view_page lk_view_10 = {CORE_VIEW_LFO, 70, 0};
const struct core_page lk_page_11 = {71, "LOOK CHECK 12", {l[44], CORE_KNOB(44), l[45], CORE_KNOB(45), l[46], CORE_KNOB(46), l[47], CORE_KNOB(47)}};
const struct core_view_page lk_view_11 = {CORE_VIEW_LFO, 71, 0};
const struct core_page lk_page_12 = {72, "LOOK CHECK 13", {l[48], CORE_KNOB(48), l[49], CORE_KNOB(49), l[50], CORE_KNOB(50), l[51], CORE_KNOB(51)}};
const struct core_view_page lk_view_12 = {CORE_VIEW_AMP, 72, 0};
const struct core_page lk_page_13 = {73, "LOOK CHECK 14", {l[52], CORE_KNOB(52), l[53], CORE_KNOB(53), l[54], CORE_KNOB(54), l[55], CORE_KNOB(55)}};
const struct core_view_page lk_view_13 = {CORE_VIEW_AMP, 73, 0};
const struct core_page lk_page_14 = {74, "LOOK CHECK 15", {l[56], CORE_KNOB(56), l[57], CORE_KNOB(57), l[58], CORE_KNOB(58), l[59], CORE_KNOB(59)}};
const struct core_view_page lk_view_14 = {CORE_VIEW_AMP, 74, 0};
const struct core_page lk_page_15 = {75, "LOOK CHECK 16", {l[60], CORE_KNOB(60), l[61], CORE_KNOB(61), l[62], CORE_KNOB(62), l[63], CORE_KNOB(63)}};
const struct core_view_page lk_view_15 = {CORE_VIEW_AMP, 75, 0};
const struct core_page lk_page_16 = {76, "LOOK CHECK 17", {l[64], CORE_KNOB(64), l[65], CORE_KNOB(65), l[66], CORE_KNOB(66), l[67], CORE_KNOB(67)}};
const struct core_view_page lk_view_16 = {CORE_VIEW_AMP, 76, 0};
const struct core_page lk_page_17 = {77, "LOOK CHECK 18", {l[68], CORE_KNOB(68), l[69], CORE_KNOB(69), l[70], CORE_KNOB(70), l[71], CORE_KNOB(71)}};
const struct core_view_page lk_view_17 = {CORE_VIEW_AMP, 77, 0};
const struct core_page lk_page_18 = {78, "LOOK CHECK 19", {l[72], CORE_KNOB(72), l[73], CORE_KNOB(73), l[74], CORE_KNOB(74), l[75], CORE_KNOB(75)}};
const struct core_view_page lk_view_18 = {CORE_VIEW_AMP, 78, 0};
const struct core_page lk_page_19 = {79, "LOOK CHECK 20", {l[76], CORE_KNOB(76), l[77], CORE_KNOB(77), l[78], CORE_KNOB(78), l[79], CORE_KNOB(79)}};
const struct core_view_page lk_view_19 = {CORE_VIEW_AMP, 79, 0};
const struct core_page lk_page_20 = {80, "LOOK CHECK 21", {l[80], CORE_KNOB(80), l[81], CORE_KNOB(81), l[82], CORE_KNOB(82), l[83], CORE_KNOB(83)}};
const struct core_view_page lk_view_20 = {CORE_VIEW_AMP, 80, 0};
const struct core_page lk_page_21 = {81, "LOOK CHECK 22", {l[84], CORE_KNOB(84), l[85], CORE_KNOB(85), l[86], CORE_KNOB(86), l[87], CORE_KNOB(87)}};
const struct core_view_page lk_view_21 = {CORE_VIEW_AMP, 81, 0};
const struct core_page lk_page_22 = {82, "LOOK CHECK 23", {l[88], CORE_KNOB(88), 0, 0, 0, 0, 0, 0}};
const struct core_view_page lk_view_22 = {CORE_VIEW_AMP, 82, 0};
