#pragma once

#include "gxfp/io/dev.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXFP_EVK_VERSION_MAX 64

int gxfp_cmd_read_firmware_version(struct gxfp_dev *dev,
                                  char *version,
                                  size_t version_cap,
                                  size_t *out_len);

#ifdef __cplusplus
}
#endif
