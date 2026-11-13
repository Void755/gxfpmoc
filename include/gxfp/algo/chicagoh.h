#pragma once

#include "gxfp/algo/sensor_cfg.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_chicagoh_parse_otp(const uint8_t *otp,
			    size_t otp_len,
			    struct gxfp_sensor_cfg *out);

#ifdef __cplusplus
}
#endif
