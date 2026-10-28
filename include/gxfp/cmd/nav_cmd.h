#pragma once

#include "gxfp/io/dev.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_cmd_nav_frame(struct gxfp_dev *dev,
                       int first,
                       int timeout_ms,
                       uint8_t *payload,
                       size_t payload_cap,
                       size_t *payload_len);

#ifdef __cplusplus
}
#endif
