/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

#include <ndp/ndp_stage.h>

#ifdef __cplusplus
extern "C" {
#endif

int ndp_sched_start_stage(struct ndp_stage *st);
int ndp_sched_stop_stage(struct ndp_stage *st);

#ifdef __cplusplus
}
#endif
