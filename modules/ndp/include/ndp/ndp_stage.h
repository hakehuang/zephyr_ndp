/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include <ndp/ndp_types.h>
#include <ndp/ndp_channel.h>

#ifndef CONFIG_NDP_MAX_PORTS
#define CONFIG_NDP_MAX_PORTS 4
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ndp_stage;

struct ndp_stage_ctx {
	struct ndp_stage *stage;
	void *user;
};

struct ndp_stage_ops {
	int (*init)(struct ndp_stage_ctx *ctx);
	int (*start)(struct ndp_stage_ctx *ctx);
	int (*process)(struct ndp_stage_ctx *ctx, const struct ndp_msg *msg_in, uint8_t in_port);
	int (*run)(struct ndp_stage_ctx *ctx); /* for sources with no inputs */
	int (*stop)(struct ndp_stage_ctx *ctx);
	int (*deinit)(struct ndp_stage_ctx *ctx);
};

struct ndp_stage_desc {
	const char *name;
	const struct ndp_stage_ops *ops;
	void *user;

	uint8_t num_inputs;
	uint8_t num_outputs;

	int prio; /* optional; if INT32_MIN, uses CONFIG_NDP_STAGE_PRIORITY */
	k_thread_stack_t *stack_area; /* required for thread-per-stage */
	size_t stack_size;

	k_timeout_t source_period; /* if num_inputs==0 and ops->run!=NULL */
};

struct ndp_stage {
	const char *name;
	const struct ndp_stage_ops *ops;
	struct ndp_stage_ctx ctx;

	uint8_t num_inputs;
	uint8_t num_outputs;

	struct ndp_channel *in[CONFIG_NDP_MAX_PORTS];
	struct ndp_channel *out[CONFIG_NDP_MAX_PORTS];

	struct k_thread thread;
	k_tid_t tid;
	k_thread_stack_t *stack_area;
	size_t stack_size;
	int prio;

	struct k_poll_signal stop_sig;
	atomic_t running;
	k_timeout_t source_period;
};

int ndp_stage_bind_input(struct ndp_stage *st, uint8_t in_port, struct ndp_channel *ch);
int ndp_stage_bind_output(struct ndp_stage *st, uint8_t out_port, struct ndp_channel *ch);

int ndp_stage_send(struct ndp_stage_ctx *ctx, uint8_t out_port, const struct ndp_msg *msg, k_timeout_t timeout);
int ndp_stage_recv(struct ndp_stage_ctx *ctx, uint8_t in_port, struct ndp_msg *msg_out, k_timeout_t timeout);

#ifdef __cplusplus
}
#endif
