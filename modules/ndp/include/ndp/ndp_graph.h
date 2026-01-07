/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#include <ndp/ndp_stage.h>
#include <ndp/ndp_channel.h>

#ifndef CONFIG_NDP_MAX_STAGES
#define CONFIG_NDP_MAX_STAGES 16
#endif
#ifndef CONFIG_NDP_MAX_CHANNELS
#define CONFIG_NDP_MAX_CHANNELS 32
#endif
#ifndef CONFIG_NDP_MAX_PORTS
#define CONFIG_NDP_MAX_PORTS 4
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t ndp_stage_id_t;
typedef uint8_t ndp_channel_id_t;

struct ndp_graph {
	struct ndp_stage stages[CONFIG_NDP_MAX_STAGES];
	struct ndp_channel channels[CONFIG_NDP_MAX_CHANNELS];
	uint8_t stage_count;
	uint8_t channel_count;
};

void ndp_graph_init(struct ndp_graph *g);

int ndp_graph_add_stage(struct ndp_graph *g, const struct ndp_stage_desc *desc, ndp_stage_id_t *out_id);
int ndp_graph_add_channel(struct ndp_graph *g, const struct ndp_channel_cfg *cfg, ndp_channel_id_t *out_id);

int ndp_graph_connect(struct ndp_graph *g,
			ndp_stage_id_t src, uint8_t src_out_port,
			ndp_stage_id_t dst, uint8_t dst_in_port,
			ndp_channel_id_t ch);

int ndp_graph_start(struct ndp_graph *g);
int ndp_graph_stop(struct ndp_graph *g);

#ifdef __cplusplus
}
#endif
