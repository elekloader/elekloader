/* SPDX-License-Identifier: GPL-2.0-or-later */
/* smpl-knob-c: knob C's own value for the SMPL machines that ask for one
 * (SMPL_OWN_C, analog-rytm-mk2/smpl.h). It holds the bits and the knob id;
 * smpl-machines, which imports smpl_knob_c weakly, does the rest: the page,
 * the face, the value, the defaults and the engine's word. */
#include <stdint.h>
#include "analog-rytm-mk2/pages.h"
#include "analog-rytm-mk2/store.h"
#include "analog-rytm-mk2/lock.h"
#include "analog-rytm-mk2/smpl.h"

STORE_FIELD(smpl_c);
CORE_KNOBS;

const struct smpl_knob_c smpl_knob_c = {CORE_KNOB(0), STORE(smpl_c)};

const struct core_lock_param smpl_c_param =
    {STORE(smpl_c), CORE_KNOB(0), 0, 127, "SMC", "SMPL Machine C", CORE_LOCK_CATEGORY("SAMP")};

/* smpl-machines' */
const struct core_knob_face *smpl_face(int32_t id, void *sound);
int smpl_text(struct core_text *t);
int smpl_c_get(int32_t id, int32_t *value);
int smpl_c_delta(void *view, int32_t id, int32_t delta, int32_t func);

const struct core_knob_faces smpl_c_faces = {CORE_KNOB(0), CORE_KNOB(0), smpl_face};
const struct core_param smpl_c_params = {CORE_KNOB(0), CORE_KNOB(0), smpl_c_get, smpl_c_delta, smpl_text};
