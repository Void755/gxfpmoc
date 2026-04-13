#pragma once

#include "gxfp/io/dev.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_cmd_notify_power_state(struct gxfp_dev *dev);

int gxfp_cmd_send_nop(struct gxfp_dev *dev);

int gxfp_cmd_set_sleep_mode(struct gxfp_dev *dev);

int gxfp_cmd_hard_reset_mcu(struct gxfp_dev *dev, uint8_t reset_flag);

#ifdef __cplusplus
}
#endif
