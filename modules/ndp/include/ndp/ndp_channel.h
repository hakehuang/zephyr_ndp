/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include <stdbool.h>

#include <ndp/ndp_types.h>
#include <ndp/ndp_metrics.h>
#include <ndp/ndp_amp_shm.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ndp_channel_backend {
	NDP_CH_BACKEND_SMP_MSGQ = 0,
	NDP_CH_BACKEND_AMP_SHM_RING = 1,
};

struct ndp_channel_cfg {
	enum ndp_channel_backend backend;
	uint16_t depth;
	enum ndp_drop_policy policy;
	k_timeout_t default_timeout;

	union {
		struct {
			void *storage;           /* must be at least depth * sizeof(struct ndp_msg) */
			size_t storage_bytes;
		} smp;
		struct {
			struct ndp_amp_shm_ring *ring; /* shared-memory ring */
			struct k_sem *notify_sem;      /* local wakeup, given by IPC ISR */
		} amp;
	} u;
};

/* Use this in send/recv to request the channel's default timeout. */
#define NDP_TIMEOUT_DEFAULT K_FOREVER

#define NDP_CHANNEL_STORAGE(name, depth) \
	__aligned(CONFIG_NDP_MSGQ_ALIGN) static uint8_t name[(depth) * sizeof(struct ndp_msg)]

#define NDP_CHANNEL_CFG_SMP_MSGQ(depth_, policy_, timeout_, storage_) \
	((struct ndp_channel_cfg){ \
		.backend = NDP_CH_BACKEND_SMP_MSGQ, \
		.depth = (depth_), \
		.policy = (policy_), \
		.default_timeout = (timeout_), \
		.u.smp.storage = (storage_), \
		.u.smp.storage_bytes = sizeof(storage_), \
	})

struct ndp_channel {
	enum ndp_channel_backend backend;
	union {
		struct {
			struct k_msgq msgq;
		} smp;
		struct {
			struct ndp_amp_shm_ring *ring;
			struct k_sem *notify_sem;
		} amp;
	} u;

	struct ndp_chan_metrics metrics;
	enum ndp_drop_policy policy;
	uint16_t depth;
	k_timeout_t default_timeout;
};

int ndp_channel_init(struct ndp_channel *ch, const struct ndp_channel_cfg *cfg);
int ndp_channel_send(struct ndp_channel *ch, const struct ndp_msg *msg, k_timeout_t timeout);
int ndp_channel_recv(struct ndp_channel *ch, struct ndp_msg *msg_out, k_timeout_t timeout);
uint32_t ndp_channel_depth(const struct ndp_channel *ch);

/* Scheduler helper: build a poll event for this channel.
 * For SMP channels, this polls the msgq data-available.
 * For AMP channels, this polls the notify semaphore (must be given by IPC ISR).
 */
int ndp_channel_poll_event(const struct ndp_channel *ch, struct k_poll_event *ev_out);

#ifdef __cplusplus
}
#endif
