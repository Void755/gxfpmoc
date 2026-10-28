#pragma once

#include "gxfp/io/dev.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_cmd_image_mode_send(struct gxfp_dev *dev);
int gxfp_cmd_image_mode_wait(struct gxfp_dev *dev, int timeout_ms);
int gxfp_cmd_image_mode_sync(struct gxfp_dev *dev, int timeout_ms);

int gxfp_cmd_get_image(struct gxfp_dev *dev);

int gxfp_cmd_set_idle(struct gxfp_dev *dev);

#ifdef __cplusplus
}
#endif
