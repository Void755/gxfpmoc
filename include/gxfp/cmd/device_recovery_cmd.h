#pragma once

#include "gxfp/io/dev.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_cmd_notify_power_state(struct gxfp_dev *dev);

int gxfp_cmd_send_nop(struct gxfp_dev *dev);

int gxfp_cmd_sleep_cleanup(struct gxfp_dev *dev);

int gxfp_cmd_reset_device(struct gxfp_dev *dev, uint8_t reset_flag);

int gxfp_cmd_d01(struct gxfp_dev *dev);

#ifdef __cplusplus
}
#endif
