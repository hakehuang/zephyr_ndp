/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ndp/ndp_channel.h>

#include <zephyr/sys/__assert.h>

static int ndp_channel_send_drop_old(struct ndp_channel *ch, const struct ndp_msg *msg)
{
	struct ndp_msg tmp;
	(void)k_msgq_get(&ch->u.smp.msgq, &tmp, K_NO_WAIT);
	int rc = k_msgq_put(&ch->u.smp.msgq, msg, K_NO_WAIT);
	return rc;
}

int ndp_channel_init(struct ndp_channel *ch, const struct ndp_channel_cfg *cfg)
{
	if (ch == NULL || cfg == NULL) {
		return -EINVAL;
	}

	ch->backend = cfg->backend;
	ch->policy = cfg->policy;
	ch->depth = cfg->depth;
	ch->default_timeout = cfg->default_timeout;

	switch (cfg->backend) {
	case NDP_CH_BACKEND_SMP_MSGQ:
		if (cfg->depth == 0U || cfg->u.smp.storage == NULL) {
			return -EINVAL;
		}
		if (cfg->u.smp.storage_bytes < (size_t)cfg->depth * sizeof(struct ndp_msg)) {
			return -EINVAL;
		}
		k_msgq_init(&ch->u.smp.msgq, cfg->u.smp.storage, sizeof(struct ndp_msg), cfg->depth);
		break;
	case NDP_CH_BACKEND_AMP_SHM_RING:
		if (cfg->u.amp.ring == NULL) {
			return -EINVAL;
		}
		ch->u.amp.ring = cfg->u.amp.ring;
		ch->u.amp.notify_sem = cfg->u.amp.notify_sem;
		break;
	default:
		return -EINVAL;
	}

	atomic_clear(&ch->metrics.drops);
	atomic_clear(&ch->metrics.sends);
	atomic_clear(&ch->metrics.recvs);
	atomic_clear(&ch->metrics.depth_hwm);

	return 0;
}

uint32_t ndp_channel_depth(const struct ndp_channel *ch)
{
	if (ch == NULL) {
		return 0U;
	}

	switch (ch->backend) {
	case NDP_CH_BACKEND_SMP_MSGQ:
		return (uint32_t)k_msgq_num_used_get(&ch->u.smp.msgq);
	case NDP_CH_BACKEND_AMP_SHM_RING:
		return ndp_amp_shm_ring_count(ch->u.amp.ring);
	default:
		return 0U;
	}
}

int ndp_channel_send(struct ndp_channel *ch, const struct ndp_msg *msg, k_timeout_t timeout)
{
	if (ch == NULL || msg == NULL) {
		return -EINVAL;
	}

	if (K_TIMEOUT_EQ(timeout, NDP_TIMEOUT_DEFAULT)) {
		timeout = ch->default_timeout;
	}

	k_timeout_t t = (K_TIMEOUT_EQ(timeout, K_FOREVER) || K_TIMEOUT_EQ(timeout, K_NO_WAIT)) ?
		timeout : timeout;
	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT) && !K_TIMEOUT_EQ(ch->default_timeout, K_NO_WAIT)) {
		/* caller explicitly asked no-wait; keep it */
	} else if (K_TIMEOUT_EQ(timeout, K_FOREVER) && !K_TIMEOUT_EQ(ch->default_timeout, K_FOREVER)) {
		/* caller explicitly asked forever; keep it */
	} else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT) == false && K_TIMEOUT_EQ(timeout, K_FOREVER) == false) {
		/* keep caller timeout */
	}
	(void)t;

	int rc;

	switch (ch->backend) {
	case NDP_CH_BACKEND_SMP_MSGQ:
		switch (ch->policy) {
		case NDP_BLOCK:
			rc = k_msgq_put(&ch->u.smp.msgq, msg, timeout);
			break;
		case NDP_DROP_OLD:
			rc = k_msgq_put(&ch->u.smp.msgq, msg, K_NO_WAIT);
			if (rc == -EAGAIN) {
				rc = ndp_channel_send_drop_old(ch, msg);
			}
			break;
		case NDP_DROP_NEW:
		default:
			rc = k_msgq_put(&ch->u.smp.msgq, msg, K_NO_WAIT);
			break;
		}
		break;
	case NDP_CH_BACKEND_AMP_SHM_RING:
		/* AMP ring is always non-blocking here; overload => drop policy. */
		rc = ndp_amp_shm_ring_push(ch->u.amp.ring, msg);
		if (rc == -EAGAIN && ch->policy == NDP_DROP_OLD) {
			struct ndp_msg tmp;
			(void)ndp_amp_shm_ring_pop(ch->u.amp.ring, &tmp);
			rc = ndp_amp_shm_ring_push(ch->u.amp.ring, msg);
		}
		break;
	default:
		return -EINVAL;
	}

	if (rc == 0) {
		ndp_metrics_chan_send_inc(&ch->metrics);
		ndp_metrics_chan_depth_hwm_update(&ch->metrics, ndp_channel_depth(ch));
	} else if (rc == -EAGAIN) {
		ndp_metrics_chan_drop_inc(&ch->metrics);
	}

	return rc;
}

int ndp_channel_recv(struct ndp_channel *ch, struct ndp_msg *msg_out, k_timeout_t timeout)
{
	if (ch == NULL || msg_out == NULL) {
		return -EINVAL;
	}

	if (K_TIMEOUT_EQ(timeout, NDP_TIMEOUT_DEFAULT)) {
		timeout = ch->default_timeout;
	}

	int rc;
	switch (ch->backend) {
	case NDP_CH_BACKEND_SMP_MSGQ:
		rc = k_msgq_get(&ch->u.smp.msgq, msg_out, timeout);
		break;
	case NDP_CH_BACKEND_AMP_SHM_RING:
		/* timeout is handled by scheduler waiting on notify_sem; here we pop. */
		rc = ndp_amp_shm_ring_pop(ch->u.amp.ring, msg_out);
		break;
	default:
		return -EINVAL;
	}
	if (rc == 0) {
		ndp_metrics_chan_recv_inc(&ch->metrics);
	}
	return rc;
}

int ndp_channel_poll_event(const struct ndp_channel *ch, struct k_poll_event *ev_out)
{
	if (ch == NULL || ev_out == NULL) {
		return -EINVAL;
	}

	switch (ch->backend) {
	case NDP_CH_BACKEND_SMP_MSGQ:
		*ev_out = (struct k_poll_event)K_POLL_EVENT_INITIALIZER(
			K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, (void *)&ch->u.smp.msgq);
		return 0;
	case NDP_CH_BACKEND_AMP_SHM_RING:
		if (ch->u.amp.notify_sem == NULL) {
			return -EINVAL;
		}
		*ev_out = (struct k_poll_event)K_POLL_EVENT_INITIALIZER(
			K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, (void *)ch->u.amp.notify_sem);
		return 0;
	default:
		return -EINVAL;
	}
}
