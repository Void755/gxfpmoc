#pragma once

#include "gxfp/proto/sensor_variant.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXFP_MILANG_CFG_LEN GXFP_SENSOR_CFG_LEN

struct gxfp_sensor_cfg {
	uint16_t tcode;
	uint16_t fdt_delta;
	uint16_t dac[4];
	uint8_t dac_count;

	uint8_t has_dac;
	uint8_t has_tcode_delta;
};

int gxfp_sensor_parse_otp(enum gxfp_sensor_variant variant,
			  const uint8_t *otp,
			  size_t otp_len,
			  struct gxfp_sensor_cfg *out);

int gxfp_sensor_get_cfg_template(enum gxfp_sensor_variant variant,
				 const uint8_t **out_template,
				 size_t *out_len);

int gxfp_sensor_prepare_cfg_blob(const uint8_t *template_data,
				 size_t template_len,
				 uint8_t *out_cfg,
				 size_t out_cfg_cap);

int gxfp_sensor_apply_otp_patch(enum gxfp_sensor_variant variant,
				uint8_t *cfg,
				size_t cfg_len,
				const struct gxfp_sensor_cfg *otp_cfg);

int gxfp_sensor_apply_default_patch(enum gxfp_sensor_variant variant,
				    uint8_t *cfg,
				    size_t cfg_len);

#ifdef __cplusplus
}
#endif
