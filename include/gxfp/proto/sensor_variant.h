#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXFP_SENSOR_CFG_LEN 0xE0u
#define GXFP_SENSOR_CFG_SECTIONS 8u

enum gxfp_sensor_variant {
	GXFP_SENSOR_VARIANT_UNKNOWN = 0,
	GXFP_SENSOR_VARIANT_MILANG = 2,
	GXFP_SENSOR_VARIANT_MILANL = 3,
	GXFP_SENSOR_VARIANT_CHICAGOHS = 0x0c,
	GXFP_SENSOR_VARIANT_CHICAGOT = 0x0e,
};

struct gxfp_sensor_variant_info {
	enum gxfp_sensor_variant variant;
	const char *name;
	uint16_t otp_len;
	uint8_t dac_count;
	uint8_t fdt_table_len;
	const uint8_t *cfg_template;
};

enum gxfp_sensor_variant gxfp_sensor_variant_from_chip_id(uint16_t chip_id);
const char *gxfp_sensor_variant_name(enum gxfp_sensor_variant variant);
const struct gxfp_sensor_variant_info *
gxfp_sensor_variant_info(enum gxfp_sensor_variant variant);
int gxfp_sensor_variant_cfg_template(enum gxfp_sensor_variant variant,
				     const uint8_t **out_template,
				     size_t *out_len);

#ifdef __cplusplus
}
#endif
