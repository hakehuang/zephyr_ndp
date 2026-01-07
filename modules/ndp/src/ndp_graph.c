/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ndp/ndp_graph.h>
#include <ndp/ndp_sched.h>

#include <string.h>

static void ndp_stage_zero(struct ndp_stage *st)
{
	memset(st, 0, sizeof(*st));
}

void ndp_graph_init(struct ndp_graph *g)
{
	if (g == NULL) {
		return;
	}
	memset(g, 0, sizeof(*g));
}

int ndp_graph_add_stage(struct ndp_graph *g, const struct ndp_stage_desc *desc, ndp_stage_id_t *out_id)
{
	if (g == NULL || desc == NULL || desc->ops == NULL) {
		return -EINVAL;
	}
	if (g->stage_count >= CONFIG_NDP_MAX_STAGES) {
		return -ENOMEM;
	}
	if (desc->num_inputs > CONFIG_NDP_MAX_PORTS || desc->num_outputs > CONFIG_NDP_MAX_PORTS) {
		return -ERANGE;
	}

	ndp_stage_id_t id = g->stage_count++;
	struct ndp_stage *st = &g->stages[id];
	ndp_stage_zero(st);

	st->name = desc->name;
	st->ops = desc->ops;
	st->ctx.stage = st;
	st->ctx.user = desc->user;
	st->num_inputs = desc->num_inputs;
	st->num_outputs = desc->num_outputs;
	st->source_period = desc->source_period;

	st->prio = (desc->prio == INT32_MIN) ? CONFIG_NDP_STAGE_PRIORITY : desc->prio;
	st->stack_area = desc->stack_area;
	st->stack_size = desc->stack_size;

	if (st->ops->init != NULL) {
		int rc = st->ops->init(&st->ctx);
		if (rc != 0) {
			return rc;
		}
	}

	if (out_id != NULL) {
		*out_id = id;
	}
	return 0;
}

int ndp_graph_add_channel(struct ndp_graph *g, const struct ndp_channel_cfg *cfg, ndp_channel_id_t *out_id)
{
	if (g == NULL || cfg == NULL) {
		return -EINVAL;
	}
	if (g->channel_count >= CONFIG_NDP_MAX_CHANNELS) {
		return -ENOMEM;
	}

	ndp_channel_id_t id = g->channel_count++;
	struct ndp_channel *ch = &g->channels[id];
	memset(ch, 0, sizeof(*ch));
	int rc = ndp_channel_init(ch, cfg);
	if (rc != 0) {
		return rc;
	}

	if (out_id != NULL) {
		*out_id = id;
	}
	return 0;
}

int ndp_graph_connect(struct ndp_graph *g,
			ndp_stage_id_t src, uint8_t src_out_port,
			ndp_stage_id_t dst, uint8_t dst_in_port,
			ndp_channel_id_t ch_id)
{
	if (g == NULL) {
		return -EINVAL;
	}
	if (src >= g->stage_count || dst >= g->stage_count || ch_id >= g->channel_count) {
		return -ERANGE;
	}

	struct ndp_stage *src_st = &g->stages[src];
	struct ndp_stage *dst_st = &g->stages[dst];
	struct ndp_channel *ch = &g->channels[ch_id];

	int rc = ndp_stage_bind_output(src_st, src_out_port, ch);
	if (rc != 0) {
		return rc;
	}
	return ndp_stage_bind_input(dst_st, dst_in_port, ch);
}

int ndp_graph_start(struct ndp_graph *g)
{
	if (g == NULL) {
		return -EINVAL;
	}

	for (uint8_t i = 0; i < g->stage_count; i++) {
		int rc = ndp_sched_start_stage(&g->stages[i]);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

int ndp_graph_stop(struct ndp_graph *g)
{
	if (g == NULL) {
		return -EINVAL;
	}

	for (uint8_t i = 0; i < g->stage_count; i++) {
		(void)ndp_sched_stop_stage(&g->stages[i]);
		if (g->stages[i].ops != NULL && g->stages[i].ops->deinit != NULL) {
			(void)g->stages[i].ops->deinit(&g->stages[i].ctx);
		}
	}

	return 0;
}
