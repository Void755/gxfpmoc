#pragma once

#include <stdint.h>
#include <stddef.h>

#include "gxfp/io/dev.h"
#include "gxfp/io/uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GXFP_MCU_STATE_LEN 20

struct gxfp_mcu_state {
	uint8_t version;

	uint8_t b1_raw;

	uint8_t is_pov_image_valid;
	uint8_t is_tls_connected;
	uint8_t is_locked;

	uint8_t captured_num_hi;
	uint8_t captured_num_lo;

	uint8_t raw_3_8[6];

	uint8_t byte9_raw;
	uint8_t flags9_pba;
	uint8_t flags9_tls_session;
	uint8_t flags9_pov;
	uint8_t flags9_low;

	uint8_t raw_10_14[5];

	uint8_t byte15_raw;

	uint8_t raw_16_19[4];
};

int gxfp_mcu_state_parse(const uint8_t *buf, size_t len,
			 struct gxfp_mcu_state *out);

int gxfp_mcu_state_query(struct gxfp_dev *dev,
			      struct gxfp_mcu_state *out_state);

#ifdef __cplusplus
}
#endif
