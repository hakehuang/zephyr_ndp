/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#include <ndp/ndp_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ndp_buf {
	struct ndp_payload_ref payload;
	uint32_t len;
};

struct ndp_buf_pool {
	struct k_mem_slab *slab;
	uint32_t block_size;
};

int ndp_buf_pool_init(struct ndp_buf_pool *pool, struct k_mem_slab *slab, uint32_t block_size);
int ndp_buf_alloc(struct ndp_buf_pool *pool, struct ndp_buf *out, k_timeout_t timeout);
int ndp_buf_free(struct ndp_buf_pool *pool, const struct ndp_buf *buf);

static inline struct ndp_msg ndp_msg_view_roi(const struct ndp_msg *in, const struct ndp_roi *roi)
{
	struct ndp_msg out = *in;
	out.meta.image.roi = *roi;
	return out;
}

#ifdef __cplusplus
}
#endif
