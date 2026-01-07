/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t ndp_type_id_t;

enum ndp_drop_policy {
	NDP_DROP_NEW = 0,
	NDP_DROP_OLD = 1,
	NDP_BLOCK = 2,
};

struct ndp_roi {
	bool valid;
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	uint16_t tile_id;
	uint16_t tile_count;
};

struct ndp_payload_plane {
	uint32_t offset;
	uint32_t size;
	uint32_t stride_bytes;
};

struct ndp_image_meta {
	uint16_t width;
	uint16_t height;
	uint16_t pixel_format;
	uint16_t plane_count;
	struct ndp_payload_plane planes[3];
	struct ndp_roi roi;
};

struct ndp_payload_ref {
	uintptr_t handle;
};

struct ndp_msg {
	ndp_type_id_t type_id;
	uint16_t schema_version;
	uint16_t flags;

	uint64_t timestamp_produced_cycles;
	uint32_t sequence;

	uint16_t stream_id;
	uint16_t _rsv0;
	uint32_t frame_id;
	uint16_t roi_id;
	uint16_t tile_id;
	uint16_t tile_count;
	uint16_t _rsv1;

	struct ndp_payload_ref payload;

	union {
		struct ndp_image_meta image;
		uint8_t raw[64];
	} meta;
};

#ifdef __cplusplus
}
#endif
