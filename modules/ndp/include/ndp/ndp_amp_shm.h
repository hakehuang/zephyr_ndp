/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/barrier.h>

#include <ndp/ndp_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal shared-memory ring buffer for AMP channels.
 *
 * IMPORTANT:
 * - The shared memory backing these structs must be visible to both domains.
 * - Prefer mapping it as non-cacheable or perform explicit cache maintenance.
 * - Indices are single-producer/single-consumer.
 */

#define NDP_AMP_SHM_MAGIC 0x4E445053u /* 'NDPS' */
#define NDP_AMP_SHM_VERSION 1u

struct ndp_amp_shm_ring {
	uint32_t depth; /* number of message slots */
	volatile uint32_t write_idx;
	volatile uint32_t read_idx;
	struct ndp_msg msgs[]; /* depth elements */
};

static inline uint32_t ndp_amp_shm_ring_count(const struct ndp_amp_shm_ring *r)
{
	uint32_t w = r->write_idx;
	uint32_t rd = r->read_idx;
	return (w - rd);
}

static inline bool ndp_amp_shm_ring_empty(const struct ndp_amp_shm_ring *r)
{
	return ndp_amp_shm_ring_count(r) == 0u;
}

static inline bool ndp_amp_shm_ring_full(const struct ndp_amp_shm_ring *r)
{
	return ndp_amp_shm_ring_count(r) >= r->depth;
}

/* Returns 0 on success; -EAGAIN if full */
int ndp_amp_shm_ring_push(struct ndp_amp_shm_ring *r, const struct ndp_msg *m);

/* Returns 0 on success; -EAGAIN if empty */
int ndp_amp_shm_ring_pop(struct ndp_amp_shm_ring *r, struct ndp_msg *m_out);

#ifdef __cplusplus
}
#endif
