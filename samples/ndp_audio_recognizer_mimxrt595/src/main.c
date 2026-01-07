/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>

#include <ndp/ndp_graph.h>
#include <ndp/ndp_buffer.h>

LOG_MODULE_REGISTER(ndp_audio_sample, LOG_LEVEL_INF);

/* ---- Message types ---- */
#define NDP_TYPE_PCM16   0x1001u
#define NDP_TYPE_FEATURE 0x1002u
#define NDP_TYPE_TEXT    0x1003u

/* 16 kHz mono, 10 ms frames */
#define SAMPLE_RATE_HZ 16000
#define FRAME_MS       10
#define SAMPLES_PER_FRAME ((SAMPLE_RATE_HZ / 1000) * FRAME_MS)
#define PCM_BYTES_PER_FRAME (SAMPLES_PER_FRAME * sizeof(int16_t))

/* Keep blocks power-of-two-ish for slabs */
#define PCM_BLOCK_SIZE  512
#define FEAT_BLOCK_SIZE 64

K_MEM_SLAB_DEFINE(pcm_slab, PCM_BLOCK_SIZE, 12, 4);
K_MEM_SLAB_DEFINE(feat_slab, FEAT_BLOCK_SIZE, 12, 4);

static struct ndp_buf_pool pcm_pool;
static struct ndp_buf_pool feat_pool;

/* ---- Channels (bounded, deterministic) ---- */
NDP_CHANNEL_STORAGE(ch_mic_to_feat_storage, 8);
NDP_CHANNEL_STORAGE(ch_feat_to_rec_storage, 8);
NDP_CHANNEL_STORAGE(ch_rec_to_sink_storage, 8);

static struct ndp_graph graph;

K_THREAD_STACK_DEFINE(stack_mic, CONFIG_NDP_STAGE_STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_feat, CONFIG_NDP_STAGE_STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_rec, CONFIG_NDP_STAGE_STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_sink, CONFIG_NDP_STAGE_STACK_SIZE);

/* ---- Utilities ---- */
static void synth_mic_fill(int16_t *pcm, size_t samples, uint32_t seq)
{
	/* Simple deterministic signal: low-amplitude noise + occasional "keyword" burst.
	 * This is intentionally cheap and doesn’t need board audio.
	 */
	const bool burst = ((seq / 50u) % 2u) == 1u; /* ~0.5s on/off */
	for (size_t i = 0; i < samples; i++) {
		uint32_t x = (uint32_t)(seq * 1103515245u + (uint32_t)i * 12345u);
		int16_t noise = (int16_t)((x >> 16) & 0x1F) - 16;
		int16_t sig = burst ? (int16_t)(800 + (int16_t)(i & 7)) : 0;
		pcm[i] = (int16_t)(sig + noise);
	}
}

static uint32_t rms_energy_q15(const int16_t *pcm, size_t samples)
{
	/* Fixed-point-ish RMS proxy: mean(square) in 32-bit.
	 * Real DSP path could be CMSIS-DSP (arm_rms_q15 / FFT / MFCC).
	 */
	uint64_t acc = 0;
	for (size_t i = 0; i < samples; i++) {
		int32_t s = pcm[i];
		acc += (uint64_t)(s * s);
	}
	return (uint32_t)(acc / (samples ? samples : 1));
}

/* ---- Stages ---- */

struct mic_state {
	uint32_t seq;
};

static int mic_run(struct ndp_stage_ctx *ctx)
{
	struct mic_state *st = (struct mic_state *)ctx->user;
	struct ndp_buf buf;
	int rc = ndp_buf_alloc(&pcm_pool, &buf, K_NO_WAIT);
	if (rc != 0) {
		return 0; /* overload: drop deterministically */
	}

	int16_t *pcm = (int16_t *)buf.payload.handle;
	synth_mic_fill(pcm, SAMPLES_PER_FRAME, st->seq);

	struct ndp_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type_id = NDP_TYPE_PCM16;
	msg.schema_version = 1;
	msg.timestamp_produced_cycles = ndp_cycles_now();
	msg.sequence = st->seq;
	msg.stream_id = 0;
	msg.frame_id = st->seq;
	msg.payload = buf.payload;
	msg.meta.raw[0] = (uint8_t)FRAME_MS;

	/* Send; if downstream full, DROP_OLD/DROP_NEW behavior is on the channel. */
	rc = ndp_stage_send(ctx, 0, &msg, K_NO_WAIT);
	if (rc != 0) {
		(void)ndp_buf_free(&pcm_pool, &buf);
	}

	st->seq++;
	return 0;
}

static int feat_process(struct ndp_stage_ctx *ctx, const struct ndp_msg *msg_in, uint8_t in_port)
{
	ARG_UNUSED(in_port);
	if (msg_in->type_id != NDP_TYPE_PCM16) {
		return 0;
	}

	const int16_t *pcm = (const int16_t *)msg_in->payload.handle;
	uint32_t e = rms_energy_q15(pcm, SAMPLES_PER_FRAME);

	struct ndp_buf feat;
	int rc = ndp_buf_alloc(&feat_pool, &feat, K_NO_WAIT);
	if (rc != 0) {
		return 0;
	}

	/* Feature payload: [energy_u32] */
	memcpy((void *)feat.payload.handle, &e, sizeof(e));

	struct ndp_msg out;
	memset(&out, 0, sizeof(out));
	out.type_id = NDP_TYPE_FEATURE;
	out.schema_version = 1;
	out.timestamp_produced_cycles = msg_in->timestamp_produced_cycles;
	out.sequence = msg_in->sequence;
	out.stream_id = msg_in->stream_id;
	out.frame_id = msg_in->frame_id;
	out.payload = feat.payload;

	rc = ndp_stage_send(ctx, 0, &out, K_NO_WAIT);
	if (rc != 0) {
		(void)ndp_buf_free(&feat_pool, &feat);
	}

	/* Free PCM buffer here to model ownership transfer completion.
	 * (In a real system you might pass ownership further or use refcounts.)
	 */
	struct ndp_buf pcm_buf = { .payload = msg_in->payload, .len = PCM_BLOCK_SIZE };
	(void)ndp_buf_free(&pcm_pool, &pcm_buf);
	return 0;
}

struct rec_state {
	uint32_t threshold;
};

static int rec_process(struct ndp_stage_ctx *ctx, const struct ndp_msg *msg_in, uint8_t in_port)
{
	ARG_UNUSED(in_port);
	if (msg_in->type_id != NDP_TYPE_FEATURE) {
		return 0;
	}

	struct rec_state *st = (struct rec_state *)ctx->user;
	uint32_t e = 0;
	memcpy(&e, (const void *)msg_in->payload.handle, sizeof(e));

	const char *text = (e > st->threshold) ? "keyword" : "-";

	struct ndp_buf outb;
	int rc = ndp_buf_alloc(&feat_pool, &outb, K_NO_WAIT);
	if (rc != 0) {
		return 0;
	}

	memset((void *)outb.payload.handle, 0, FEAT_BLOCK_SIZE);
	strncpy((char *)outb.payload.handle, text, FEAT_BLOCK_SIZE - 1);

	struct ndp_msg out;
	memset(&out, 0, sizeof(out));
	out.type_id = NDP_TYPE_TEXT;
	out.schema_version = 1;
	out.timestamp_produced_cycles = msg_in->timestamp_produced_cycles;
	out.sequence = msg_in->sequence;
	out.stream_id = msg_in->stream_id;
	out.frame_id = msg_in->frame_id;
	out.payload = outb.payload;

	rc = ndp_stage_send(ctx, 0, &out, K_NO_WAIT);
	if (rc != 0) {
		(void)ndp_buf_free(&feat_pool, &outb);
	}

	/* Free feature buffer */
	struct ndp_buf feat_buf = { .payload = msg_in->payload, .len = FEAT_BLOCK_SIZE };
	(void)ndp_buf_free(&feat_pool, &feat_buf);
	return 0;
}

static int sink_process(struct ndp_stage_ctx *ctx, const struct ndp_msg *msg_in, uint8_t in_port)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(in_port);
	if (msg_in->type_id != NDP_TYPE_TEXT) {
		return 0;
	}

	const char *txt = (const char *)msg_in->payload.handle;
	if (txt[0] != '-') {
		LOG_INF("frame=%u seq=%u recog=%s", msg_in->frame_id, msg_in->sequence, txt);
	}

	struct ndp_buf b = { .payload = msg_in->payload, .len = FEAT_BLOCK_SIZE };
	(void)ndp_buf_free(&feat_pool, &b);
	return 0;
}

static const struct ndp_stage_ops mic_ops = {
	.run = mic_run,
};

static const struct ndp_stage_ops feat_ops = {
	.process = feat_process,
};

static const struct ndp_stage_ops rec_ops = {
	.process = rec_process,
};

static const struct ndp_stage_ops sink_ops = {
	.process = sink_process,
};

void main(void)
{
	LOG_INF("NDP audio recognition sample (CM33) start");

	(void)ndp_buf_pool_init(&pcm_pool, &pcm_slab, PCM_BLOCK_SIZE);
	(void)ndp_buf_pool_init(&feat_pool, &feat_slab, FEAT_BLOCK_SIZE);

	ndp_graph_init(&graph);

	struct mic_state mic_state = { .seq = 0 };
	struct rec_state rec_state = { .threshold = 200000u };

	ndp_stage_id_t mic_id, feat_id, rec_id, sink_id;
	ndp_channel_id_t ch0, ch1, ch2;

	struct ndp_stage_desc mic = {
		.name = "MicSource",
		.ops = &mic_ops,
		.user = &mic_state,
		.num_inputs = 0,
		.num_outputs = 1,
		.prio = INT32_MIN,
		.stack_area = stack_mic,
		.stack_size = K_THREAD_STACK_SIZEOF(stack_mic),
		.source_period = K_MSEC(FRAME_MS),
	};
	struct ndp_stage_desc feat = {
		.name = "FeatureExtract",
		.ops = &feat_ops,
		.user = NULL,
		.num_inputs = 1,
		.num_outputs = 1,
		.prio = INT32_MIN,
		.stack_area = stack_feat,
		.stack_size = K_THREAD_STACK_SIZEOF(stack_feat),
	};
	struct ndp_stage_desc rec = {
		.name = "Recognizer",
		.ops = &rec_ops,
		.user = &rec_state,
		.num_inputs = 1,
		.num_outputs = 1,
		.prio = INT32_MIN,
		.stack_area = stack_rec,
		.stack_size = K_THREAD_STACK_SIZEOF(stack_rec),
	};
	struct ndp_stage_desc sink = {
		.name = "TextSink",
		.ops = &sink_ops,
		.user = NULL,
		.num_inputs = 1,
		.num_outputs = 0,
		.prio = INT32_MIN,
		.stack_area = stack_sink,
		.stack_size = K_THREAD_STACK_SIZEOF(stack_sink),
	};

	struct ndp_channel_cfg c0 = {
		.depth = 8,
		.policy = NDP_DROP_OLD,
		.default_timeout = K_NO_WAIT,
		.storage = ch_mic_to_feat_storage,
		.storage_bytes = sizeof(ch_mic_to_feat_storage),
	};
	struct ndp_channel_cfg c1 = {
		.depth = 8,
		.policy = NDP_DROP_OLD,
		.default_timeout = K_NO_WAIT,
		.storage = ch_feat_to_rec_storage,
		.storage_bytes = sizeof(ch_feat_to_rec_storage),
	};
	struct ndp_channel_cfg c2 = {
		.depth = 8,
		.policy = NDP_DROP_OLD,
		.default_timeout = K_NO_WAIT,
		.storage = ch_rec_to_sink_storage,
		.storage_bytes = sizeof(ch_rec_to_sink_storage),
	};

	(void)ndp_graph_add_stage(&graph, &mic, &mic_id);
	(void)ndp_graph_add_stage(&graph, &feat, &feat_id);
	(void)ndp_graph_add_stage(&graph, &rec, &rec_id);
	(void)ndp_graph_add_stage(&graph, &sink, &sink_id);

	(void)ndp_graph_add_channel(&graph, &c0, &ch0);
	(void)ndp_graph_add_channel(&graph, &c1, &ch1);
	(void)ndp_graph_add_channel(&graph, &c2, &ch2);

	(void)ndp_graph_connect(&graph, mic_id, 0, feat_id, 0, ch0);
	(void)ndp_graph_connect(&graph, feat_id, 0, rec_id, 0, ch1);
	(void)ndp_graph_connect(&graph, rec_id, 0, sink_id, 0, ch2);

	int rc = ndp_graph_start(&graph);
	if (rc != 0) {
		LOG_ERR("ndp_graph_start failed: %d", rc);
		return;
	}

	LOG_INF("Pipeline running; look for keyword prints.");
}
