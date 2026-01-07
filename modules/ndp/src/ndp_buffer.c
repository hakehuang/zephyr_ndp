/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ndp/ndp_buffer.h>

#include <zephyr/sys/__assert.h>

int ndp_buf_pool_init(struct ndp_buf_pool *pool, struct k_mem_slab *slab, uint32_t block_size)
{
	if (pool == NULL || slab == NULL || block_size == 0U) {
		return -EINVAL;
	}

	pool->slab = slab;
	pool->block_size = block_size;
	return 0;
}

int ndp_buf_alloc(struct ndp_buf_pool *pool, struct ndp_buf *out, k_timeout_t timeout)
{
	void *mem = NULL;

	if (pool == NULL || out == NULL || pool->slab == NULL) {
		return -EINVAL;
	}

	int rc = k_mem_slab_alloc(pool->slab, &mem, timeout);
	if (rc != 0) {
		return rc;
	}

	out->payload.handle = (uintptr_t)mem;
	out->len = pool->block_size;
	return 0;
}

int ndp_buf_free(struct ndp_buf_pool *pool, const struct ndp_buf *buf)
{
	if (pool == NULL || buf == NULL || pool->slab == NULL) {
		return -EINVAL;
	}

	void *mem = (void *)buf->payload.handle;
	if (mem == NULL) {
		return -EINVAL;
	}

	k_mem_slab_free(pool->slab, mem);
	return 0;
}
