#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXFP_MP_CHUNK_MAX	0xE00u
#define GXFP_MP_ALIGN		8u
#define GXFP_MP_TX_BUFFER_SIZE	0x200u

/* eSPI wrapper header */
#define GXFP_MP_WRAPPER_MAGIC	0xF0u
#define GXFP_MP_SEQ16_INIT	0x8881u

#define GXFP_MP_LEN_MAX		0x6C00u
#define GXFP_MP_FRAME_MAX	(4u + GXFP_MP_LEN_MAX)
#define GXFP_MP_MAX_PAYLOAD	(GXFP_MP_TX_BUFFER_SIZE - 8u - 4u)

bool gxfp_mp_check_head(const uint8_t *buf, size_t buf_len,
			uint16_t *out_mp_len);

int gxfp_mp_build(uint8_t mp_type, const uint8_t *payload, size_t payload_len,
		  uint8_t *out, size_t out_cap);

int gxfp_mp_wrap(const uint8_t *mp_frame, size_t mp_frame_len,
		 uint16_t *seq16, uint8_t *out, size_t out_cap);

size_t gxfp_mp_rx_chunk(size_t remain);

#ifdef __cplusplus
}
#endif
