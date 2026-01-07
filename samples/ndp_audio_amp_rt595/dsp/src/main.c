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

LOG_MODULE_REGISTER(ndp_audio_amp_dsp, LOG_LEVEL_INF);

#define NDP_TYPE_PCM16 0x1001u
#define NDP_TYPE_TEXT  0x1003u

#define SAMPLE_RATE_HZ 16000
#define FRAME_MS 10
#define SAMPLES_PER_FRAME ((SAMPLE_RATE_HZ / 1000) * FRAME_MS)

#define RING_DEPTH 8
#define PCM_BLOCK_BYTES (SAMPLES_PER_FRAME * sizeof(int16_t))

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

static struct ndp_amp_shm_layout *shm_get(void)
{
#if DT_NODE_EXISTS(DT_CHOSEN(ndp_shm))
	return (struct ndp_amp_shm_layout *)DT_REG_ADDR(DT_CHOSEN(ndp_shm));
#else
	return NULL;
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
	/* CM33 -> DSP doorbell: frames available in shm ring */
	k_sem_give(&rx_notify_sem);
}
#endif

static uint32_t rms_energy_u32(const int16_t *pcm)
{
	uint64_t acc = 0;
	for (uint32_t i = 0; i < SAMPLES_PER_FRAME; i++) {
		int32_t s = pcm[i];
		acc += (uint64_t)(s * s);
	}
	return (uint32_t)(acc / SAMPLES_PER_FRAME);
}

static struct ndp_graph g;

K_THREAD_STACK_DEFINE(stack_rec, CONFIG_NDP_STAGE_STACK_SIZE);

struct rec_state {
	struct ndp_amp_shm_layout *shm;
	uint32_t threshold;
	struct ndp_channel *tx; /* DSP -> CM33 */
};

static int rec_process(struct ndp_stage_ctx *ctx, const struct ndp_msg *msg_in, uint8_t in_port)
{
	ARG_UNUSED(in_port);
	if (msg_in->type_id != NDP_TYPE_PCM16) {
		return 0;
	}

	struct rec_state *st = (struct rec_state *)ctx->user;
	uint32_t slot = (uint32_t)msg_in->payload.handle;
	if (slot >= RING_DEPTH) {
		return 0;
	}

	const int16_t *pcm = (const int16_t *)&st->shm->pcm_blocks[slot][0];
	uint32_t e = rms_energy_u32(pcm);

	/* Simple recognizer: energy threshold → token */
	uint32_t token = (e > st->threshold) ? 1u : 0u;

	struct ndp_msg out;
	memset(&out, 0, sizeof(out));
	out.type_id = NDP_TYPE_TEXT;
	out.schema_version = 1;
	out.timestamp_produced_cycles = msg_in->timestamp_produced_cycles;
	out.sequence = msg_in->sequence;
	out.frame_id = msg_in->frame_id;
	out.payload.handle = (uintptr_t)token;

	int rc = ndp_channel_send(st->tx, &out, K_NO_WAIT);
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

	return 0;
}

static const struct ndp_stage_ops rec_ops = {
	.process = rec_process,
};

void main(void)
{
	LOG_INF("NDP AMP DSP start");

	struct ndp_amp_shm_layout *shm = shm_get();
	if (shm == NULL) {
		LOG_ERR("No shared memory chosen node; set chosen ndp,shm");
		return;
	}

	if (shm->magic != NDP_AMP_SHM_MAGIC || shm->version != NDP_AMP_SHM_VERSION) {
		LOG_ERR("Shared memory not initialized by CM33");
		return;
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

	ndp_stage_id_t rec_id;
	ndp_channel_id_t rx_id, tx_id;

	/* RX channel is semaphore-driven (given by mailbox ISR). */
	struct ndp_channel_cfg rx_cfg = {
		.backend = NDP_CH_BACKEND_AMP_SHM_RING,
		.depth = RING_DEPTH,
		.policy = NDP_DROP_OLD,
		.default_timeout = K_NO_WAIT,
		.u.amp.ring = &shm->cm33_to_dsp.ring,
#if IS_ENABLED(CONFIG_MBOX) && DT_NODE_EXISTS(DT_CHOSEN(ndp_mbox_rx))
		.u.amp.notify_sem = &rx_notify_sem,
#else
		.u.amp.notify_sem = NULL,
#endif
	};

	struct ndp_channel_cfg tx_cfg = {
		.backend = NDP_CH_BACKEND_AMP_SHM_RING,
		.depth = RING_DEPTH,
		.policy = NDP_DROP_OLD,
		.default_timeout = K_NO_WAIT,
		.u.amp.ring = &shm->dsp_to_cm33.ring,
		.u.amp.notify_sem = NULL,
	};

	(void)ndp_graph_add_channel(&g, &rx_cfg, &rx_id);
	(void)ndp_graph_add_channel(&g, &tx_cfg, &tx_id);

	struct ndp_channel *tx = &g.channels[tx_id];

	struct rec_state rec_state = {
		.shm = shm,
		.threshold = 200000u,
		.tx = tx,
	};

	struct ndp_stage_desc rec = {
		.name = "DSPRecognizer",
		.ops = &rec_ops,
		.user = &rec_state,
		.num_inputs = 1,
		.num_outputs = 0,
		.prio = INT32_MIN,
		.stack_area = stack_rec,
		.stack_size = K_THREAD_STACK_SIZEOF(stack_rec),
	};

	(void)ndp_graph_add_stage(&g, &rec, &rec_id);

	/* Bind RX ring to recognizer input */
	(void)ndp_stage_bind_input(&g.stages[rec_id], 0, &g.channels[rx_id]);

	int rc = ndp_graph_start(&g);
	if (rc != 0) {
		LOG_ERR("ndp_graph_start failed: %d", rc);
		return;
	}

	LOG_INF("DSP running: waiting for CM33 frames.");
}
