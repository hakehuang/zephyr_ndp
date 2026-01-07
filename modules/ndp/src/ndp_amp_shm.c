/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ndp/ndp_amp_shm.h>

#include <zephyr/kernel.h>
#include <string.h>

int ndp_amp_shm_ring_push(struct ndp_amp_shm_ring *r, const struct ndp_msg *m)
{
	if (r == NULL || m == NULL || r->depth == 0u) {
		return -EINVAL;
	}

	if (ndp_amp_shm_ring_full(r)) {
		return -EAGAIN;
	}

	uint32_t slot = r->write_idx % r->depth;

	/* Write payload then publish index. */
	r->msgs[slot] = *m;
	barrier_dmem_fence_full();
	r->write_idx++;
	barrier_dmem_fence_full();

	return 0;
}

int ndp_amp_shm_ring_pop(struct ndp_amp_shm_ring *r, struct ndp_msg *m_out)
{
	if (r == NULL || m_out == NULL || r->depth == 0u) {
		return -EINVAL;
	}

	if (ndp_amp_shm_ring_empty(r)) {
		return -EAGAIN;
	}

	uint32_t slot = r->read_idx % r->depth;
	barrier_dmem_fence_full();
	*m_out = r->msgs[slot];
	barrier_dmem_fence_full();
	r->read_idx++;
	barrier_dmem_fence_full();

	return 0;
}
