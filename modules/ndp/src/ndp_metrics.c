/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ndp/ndp_metrics.h>

void ndp_metrics_chan_send_inc(struct ndp_chan_metrics *m)
{
	atomic_inc(&m->sends);
}

void ndp_metrics_chan_recv_inc(struct ndp_chan_metrics *m)
{
	atomic_inc(&m->recvs);
}

void ndp_metrics_chan_drop_inc(struct ndp_chan_metrics *m)
{
	atomic_inc(&m->drops);
}

void ndp_metrics_chan_depth_hwm_update(struct ndp_chan_metrics *m, uint32_t depth)
{
	atomic_t cur = atomic_get(&m->depth_hwm);
	while ((uint32_t)cur < depth) {
		if (atomic_cas(&m->depth_hwm, cur, (atomic_t)depth)) {
			break;
		}
		cur = atomic_get(&m->depth_hwm);
	}
}
