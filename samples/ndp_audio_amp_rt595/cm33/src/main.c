/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>

#if IS_ENABLED(CONFIG_IPC_SERVICE)
#include <zephyr/ipc/ipc_service.h>
#endif

#include <string.h>
#include <stdint.h>

#include <ndp/ndp_graph.h>
#include <ndp/ndp_channel.h>
#include <ndp/ndp_amp_shm.h>

LOG_MODULE_REGISTER(ndp_audio_amp_cm33, LOG_LEVEL_INF);

#define NDP_TYPE_PCM16 0x1001u
#define NDP_TYPE_TEXT  0x1003u

#define SAMPLE_RATE_HZ 16000
#define FRAME_MS 10
#define SAMPLES_PER_FRAME ((SAMPLE_RATE_HZ / 1000) * FRAME_MS)
#define PCM_BLOCK_BYTES (SAMPLES_PER_FRAME * sizeof(int16_t))

#define RING_DEPTH 8

struct ndp_amp_shm_ring_fixed {
	struct ndp_amp_shm_ring ring;
	struct ndp_msg msgs[RING_DEPTH];
};

struct ndp_amp_shm_layout {
	uint32_t magic;
	uint32_t version;
	struct ndp_amp_shm_ring_fixed cm33_to_dsp;
	struct ndp_amp_shm_ring_fixed dsp_to_cm33;
	uint8_t pcm_blocks[RING_DEPTH][PCM_BLOCK_BYTES];
};

/*
 * IMPORTANT: you must place this struct into a true shared-memory region.
 * For real hardware, map this via Devicetree reserved-memory and a linker section.
 *
 * This fallback (static RAM) is only a placeholder.
 */
static struct ndp_amp_shm_layout shm_fallback __aligned(32);

static struct ndp_amp_shm_layout *shm_get(void)
{
#if DT_NODE_EXISTS(DT_CHOSEN(ndp_shm))
	return (struct ndp_amp_shm_layout *)DT_REG_ADDR(DT_CHOSEN(ndp_shm));
#else
	return &shm_fallback;
#endif
}

static struct k_sem rx_notify_sem;

#if IS_ENABLED(CONFIG_IPC_SERVICE) && DT_NODE_EXISTS(DT_CHOSEN(ndp_ipc))
static const struct device *ipc_inst = DEVICE_DT_GET(DT_CHOSEN(ndp_ipc));
static struct ipc_ept ipc_ept;
static atomic_t ipc_bound;

static void ipc_bound_cb(void *priv)
{
	ARG_UNUSED(priv);
	atomic_set(&ipc_bound, 1);
}

static void ipc_recv_cb(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(priv);
	/* DSP -> CM33 doorbell: results available in shm ring */
	k_sem_give(&rx_notify_sem);
}
#endif

static struct ndp_graph g;
static struct ndp_channel *ch_tx; /* CM33 -> DSP */
static struct ndp_channel *ch_rx; /* DSP -> CM33 */

K_THREAD_STACK_DEFINE(stack_mic, CONFIG_NDP_STAGE_STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_sink, CONFIG_NDP_STAGE_STACK_SIZE);

static void synth_fill(int16_t *pcm, uint32_t seq)
{
	const bool burst = ((seq / 50u) % 2u) == 1u;
	for (uint32_t i = 0; i < SAMPLES_PER_FRAME; i++) {
		uint32_t x = (uint32_t)(seq * 1103515245u + i * 12345u);
		int16_t noise = (int16_t)((x >> 16) & 0x1F) - 16;
		int16_t sig = burst ? (int16_t)(800 + (int16_t)(i & 7)) : 0;
		pcm[i] = (int16_t)(sig + noise);
	}
}

struct mic_state {
	uint32_t seq;
	struct ndp_amp_shm_layout *shm;
};

static int mic_run(struct ndp_stage_ctx *ctx)
{
	struct mic_state *st = (struct mic_state *)ctx->user;
	struct ndp_amp_shm_layout *shm = st->shm;

	uint32_t slot = st->seq % RING_DEPTH;
	int16_t *pcm = (int16_t *)&shm->pcm_blocks[slot][0];
	synth_fill(pcm, st->seq);

	struct ndp_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type_id = NDP_TYPE_PCM16;
	msg.schema_version = 1;
	msg.timestamp_produced_cycles = ndp_cycles_now();
	msg.sequence = st->seq;
	msg.frame_id = st->seq;

	/* For AMP: interpret payload.handle as an OFFSET (or slot id) in shared memory.
	 * Here we encode the slot number; DSP uses it to locate pcm_blocks[slot].
	 */
	msg.payload.handle = (uintptr_t)slot;

	int rc = ndp_channel_send(ch_tx, &msg, K_NO_WAIT);

	if (rc == 0) {
#if IS_ENABLED(CONFIG_IPC_SERVICE) && DT_NODE_EXISTS(DT_CHOSEN(ndp_ipc))
		if (atomic_get(&ipc_bound) != 0) {
			uint32_t doorbell = 1;
			(void)ipc_service_send(&ipc_ept, &doorbell, sizeof(doorbell));
		}
#else
		/* No notify backend configured */
#endif
	}

	st->seq++;
	return 0;
}

static int sink_process(struct ndp_stage_ctx *ctx, const struct ndp_msg *msg_in, uint8_t in_port)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(in_port);
	if (msg_in->type_id != NDP_TYPE_TEXT) {
		return 0;
	}

	uint32_t token = (uint32_t)msg_in->payload.handle;
	if (token != 0u) {
		LOG_INF("DSP result: token=%u frame=%u seq=%u", token, msg_in->frame_id, msg_in->sequence);
	}
	return 0;
}

static const struct ndp_stage_ops mic_ops = {
	.run = mic_run,
};

static const struct ndp_stage_ops sink_ops = {
	.process = sink_process,
};

void main(void)
{
	LOG_INF("NDP AMP audio CM33 start");

	struct ndp_amp_shm_layout *shm = shm_get();
	if (shm->magic != NDP_AMP_SHM_MAGIC || shm->version != NDP_AMP_SHM_VERSION) {
		memset(shm, 0, sizeof(*shm));
		shm->magic = NDP_AMP_SHM_MAGIC;
		shm->version = NDP_AMP_SHM_VERSION;

		shm->cm33_to_dsp.ring.depth = RING_DEPTH;
		shm->dsp_to_cm33.ring.depth = RING_DEPTH;
	}

	k_sem_init(&rx_notify_sem, 0, K_SEM_MAX_LIMIT);

#if IS_ENABLED(CONFIG_IPC_SERVICE) && DT_NODE_EXISTS(DT_CHOSEN(ndp_ipc))
	if (!device_is_ready(ipc_inst)) {
		LOG_WRN("IPC service instance not ready; no OpenAMP notify");
	} else {
		struct ipc_ept_cfg cfg = {
			.name = "ndp_audio",
			.cb = {
				.bound = ipc_bound_cb,
				.received = ipc_recv_cb,
			},
		};

		atomic_clear(&ipc_bound);
		int irc = ipc_service_open_instance(ipc_inst);
		if (irc == 0) {
			irc = ipc_service_register_endpoint(ipc_inst, &ipc_ept, &cfg);
		}
		if (irc != 0) {
			LOG_WRN("OpenAMP ipc_service init failed: %d", irc);
		}
	}
#endif

	ndp_graph_init(&g);

	struct mic_state mic_state = { .seq = 0, .shm = shm };

	ndp_stage_id_t mic_id, sink_id;
	ndp_channel_id_t tx_id, rx_id;

	struct ndp_stage_desc mic = {
		.name = "MicToShm",
		.ops = &mic_ops,
		.user = &mic_state,
		.num_inputs = 0,
		.num_outputs = 1,
		.prio = INT32_MIN,
		.stack_area = stack_mic,
		.stack_size = K_THREAD_STACK_SIZEOF(stack_mic),
		.source_period = K_MSEC(FRAME_MS),
	};

	struct ndp_stage_desc sink = {
		.name = "ResultSink",
		.ops = &sink_ops,
		.user = NULL,
		.num_inputs = 1,
		.num_outputs = 0,
		.prio = INT32_MIN,
		.stack_area = stack_sink,
		.stack_size = K_THREAD_STACK_SIZEOF(stack_sink),
	};

	struct ndp_channel_cfg tx_cfg = {
		.backend = NDP_CH_BACKEND_AMP_SHM_RING,
		.depth = RING_DEPTH,
		.policy = NDP_DROP_OLD,
		.default_timeout = K_NO_WAIT,
		.u.amp.ring = &shm->cm33_to_dsp.ring,
		.u.amp.notify_sem = NULL,
	};

	struct ndp_channel_cfg rx_cfg = {
		.backend = NDP_CH_BACKEND_AMP_SHM_RING,
		.depth = RING_DEPTH,
		.policy = NDP_DROP_OLD,
		.default_timeout = K_NO_WAIT,
		.u.amp.ring = &shm->dsp_to_cm33.ring,
#if IS_ENABLED(CONFIG_MBOX) && DT_NODE_EXISTS(DT_CHOSEN(ndp_mbox_rx))
		.u.amp.notify_sem = &rx_notify_sem,
#else
		.u.amp.notify_sem = NULL,
#endif
	};

	(void)ndp_graph_add_stage(&g, &mic, &mic_id);
	(void)ndp_graph_add_stage(&g, &sink, &sink_id);

	(void)ndp_graph_add_channel(&g, &tx_cfg, &tx_id);
	(void)ndp_graph_add_channel(&g, &rx_cfg, &rx_id);

	ch_tx = &g.channels[tx_id];
	ch_rx = &g.channels[rx_id];

	(void)ndp_stage_bind_output(&g.stages[mic_id], 0, ch_tx);
	(void)ndp_stage_bind_input(&g.stages[sink_id], 0, ch_rx);

	int rc = ndp_graph_start(&g);
	if (rc != 0) {
		LOG_ERR("ndp_graph_start failed: %d", rc);
		return;
	}

	LOG_INF("CM33 running: producing PCM to shared memory.");
}
