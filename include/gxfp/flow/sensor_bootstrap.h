#pragma once

#include "gxfp/algo/sensor_cfg.h"
#include "gxfp/cmd/fdt_cmd.h"
#include "gxfp/io/dev.h"

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_sensor_bootstrap(struct gxfp_dev *dev,
			  enum gxfp_sensor_variant variant,
			  struct gxfp_cmd_fdt_state *fdt,
			  struct gxfp_sensor_cfg *out_cfg);

#ifdef __cplusplus
}
#endif
