#pragma once

#include "gxfp/io/dev.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_milang_prepare_config(struct gxfp_dev *dev, uint16_t dac);

#ifdef __cplusplus
}
#endif
