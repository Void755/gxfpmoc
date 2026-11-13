#pragma once

#include "gxfp/algo/sensor_cfg.h"
#include "gxfp/io/dev.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_sensor_prepare_config(struct gxfp_dev *dev,
			       enum gxfp_sensor_variant variant,
			       const struct gxfp_sensor_cfg *cfg);


#ifdef __cplusplus
}
#endif
