/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ndp/ndp_stage.h>

int ndp_stage_bind_input(struct ndp_stage *st, uint8_t in_port, struct ndp_channel *ch)
{
	if (st == NULL || ch == NULL) {
		return -EINVAL;
	}
	if (in_port >= st->num_inputs || in_port >= CONFIG_NDP_MAX_PORTS) {
		return -ERANGE;
	}
	st->in[in_port] = ch;
	return 0;
}

int ndp_stage_bind_output(struct ndp_stage *st, uint8_t out_port, struct ndp_channel *ch)
{
	if (st == NULL || ch == NULL) {
		return -EINVAL;
	}
	if (out_port >= st->num_outputs || out_port >= CONFIG_NDP_MAX_PORTS) {
		return -ERANGE;
	}
	st->out[out_port] = ch;
	return 0;
}

int ndp_stage_send(struct ndp_stage_ctx *ctx, uint8_t out_port, const struct ndp_msg *msg, k_timeout_t timeout)
{
	if (ctx == NULL || ctx->stage == NULL || msg == NULL) {
		return -EINVAL;
	}
	struct ndp_stage *st = ctx->stage;
	if (out_port >= st->num_outputs || out_port >= CONFIG_NDP_MAX_PORTS) {
		return -ERANGE;
	}
	if (st->out[out_port] == NULL) {
		return -ENOTCONN;
	}
	return ndp_channel_send(st->out[out_port], msg, timeout);
}

int ndp_stage_recv(struct ndp_stage_ctx *ctx, uint8_t in_port, struct ndp_msg *msg_out, k_timeout_t timeout)
{
	if (ctx == NULL || ctx->stage == NULL || msg_out == NULL) {
		return -EINVAL;
	}
	struct ndp_stage *st = ctx->stage;
	if (in_port >= st->num_inputs || in_port >= CONFIG_NDP_MAX_PORTS) {
		return -ERANGE;
	}
	if (st->in[in_port] == NULL) {
		return -ENOTCONN;
	}
	return ndp_channel_recv(st->in[in_port], msg_out, timeout);
}
