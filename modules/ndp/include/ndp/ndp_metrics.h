/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ndp_chan_metrics {
	atomic_t drops;
	atomic_t sends;
	atomic_t recvs;
	atomic_t depth_hwm;
};

static inline uint64_t ndp_cycles_now(void)
{
	return (uint64_t)k_cycle_get_64();
}

void ndp_metrics_chan_send_inc(struct ndp_chan_metrics *m);
void ndp_metrics_chan_recv_inc(struct ndp_chan_metrics *m);
void ndp_metrics_chan_drop_inc(struct ndp_chan_metrics *m);
void ndp_metrics_chan_depth_hwm_update(struct ndp_chan_metrics *m, uint32_t depth);

#ifdef __cplusplus
}
#endif
