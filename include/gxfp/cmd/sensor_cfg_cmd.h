#pragma once

#include "gxfp/io/dev.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gxfp_cmd_read_otp(struct gxfp_dev *dev,
                      uint8_t *otp,
                      uint16_t otp_cap,
                      uint16_t *out_otp_len);

int gxfp_cmd_upload_config_mcu(struct gxfp_dev *dev,
                               const uint8_t *cfg,
                               uint16_t cfg_len,
                               uint8_t *out_ack_status);

int gxfp_cmd_set_dac(struct gxfp_dev *dev, uint16_t dac);

int gxfp_cmd_verify_sensor_cfg(struct gxfp_dev *dev,
                               uint16_t expected_dac,
                               uint16_t expected_tcode);

#ifdef __cplusplus
}
#endif
