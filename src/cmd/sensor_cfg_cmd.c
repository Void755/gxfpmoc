#include "gxfp/cmd/sensor_cfg_cmd.h"

#include "gxfp/cmd/device_recovery_cmd.h"
#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/cmd/mode_cmd.h"
#include "gxfp/cmd/register_cmd.h"
#include "gxfp/proto/goodix_cmd.h"
#include "gxfp/proto/goodix_proto.h"
#include "gxfp/proto/milang_regs.h"
#include "gxfp/proto/mp_proto.h"

#include <errno.h>
#include <unistd.h>

int gxfp_cmd_read_otp(struct gxfp_dev *dev,
                      uint8_t *otp,
                      uint16_t otp_cap,
                      uint16_t *out_otp_len)
{
    uint8_t req_payload[2] = { 0x00, 0x00 };
    uint8_t rx[GXFP_MP_FRAME_MAX];
    uint32_t rx_len = 0;
    struct gxfp_frame_parsed frame;
    int rc;

    if (!dev || !otp || otp_cap == 0)
        return -EINVAL;
    if (out_otp_len)
        *out_otp_len = 0;

    rc = gxfp_goodix_request_selected(dev,
                                      GXFP_CMD_READ_OTP,
                                      GXFP_CMD_READ_OTP,
                                      req_payload,
                                      (uint16_t)sizeof(req_payload),
                                      rx,
                                      (uint32_t)sizeof(rx),
                                      &rx_len,
                                      8,
                                      500,
                                      NULL,
                                      NULL);
    if (rc != 0)
        return rc;

    if (!gxfp_parse_goodix_body(rx, rx_len, &frame))
        return -EBADMSG;
    if (!frame.payload)
        return -EBADMSG;
    if (frame.payload_len > otp_cap)
        return -EMSGSIZE;

    for (uint16_t i = 0; i < frame.payload_len; i++)
        otp[i] = frame.payload[i];

    if (out_otp_len)
        *out_otp_len = frame.payload_len;

    return 0;
}

int gxfp_cmd_upload_config_mcu(struct gxfp_dev *dev,
                               const uint8_t *cfg,
                               uint16_t cfg_len,
                               uint8_t *out_ack_status)
{
    uint8_t rx[GXFP_MP_FRAME_MAX];
    uint32_t rx_len = 0;
    struct gxfp_frame_parsed frame;
    int rc;

    if (!dev || !cfg || cfg_len == 0)
        return -EINVAL;
    if (out_ack_status)
        *out_ack_status = 0xff;

    rc = gxfp_goodix_request_selected(dev,
                                      GXFP_CMD_UPLOAD_CONFIG_MCU,
                                      GXFP_CMD_UPLOAD_CONFIG_MCU,
                                      cfg,
                                      cfg_len,
                                      rx,
                                      (uint32_t)sizeof(rx),
                                      &rx_len,
                                      3,
                                      200,
                                      NULL,
                                      NULL);
    if (rc != 0)
        return rc;

    if (!gxfp_parse_goodix_body(rx, rx_len, &frame))
        return -EBADMSG;

    if (frame.cmd != GXFP_CMD_UPLOAD_CONFIG_MCU)
        return -EBADMSG;

    if (out_ack_status)
        *out_ack_status = (frame.payload && frame.payload_len > 0) ? frame.payload[0] : 0x00;

    return 0;
}


int gxfp_cmd_set_dac_values(struct gxfp_dev *dev,
			    const uint16_t *dacs,
			    size_t dac_count)
{
	uint8_t payload[8] = { 0 };
	uint8_t rx[GXFP_MP_FRAME_MAX];
	uint32_t rx_len = 0;
	struct gxfp_frame_parsed frame;
	size_t i;
	int r;

	if (!dev || !dacs || dac_count == 0 || dac_count > 4)
		return -EINVAL;

	for (i = 0; i < dac_count; i++) {
		payload[i * 2] = (uint8_t)(dacs[i] & 0xff);
		payload[i * 2 + 1] = (uint8_t)((dacs[i] >> 8) & 0xff);
	}

	r = gxfp_goodix_request_selected(dev,
					 GXFP_CMD_SET_DAC,
					 GXFP_CMD_SET_DAC,
					 payload,
					 (uint16_t)sizeof(payload),
					 rx,
					 (uint32_t)sizeof(rx),
					 &rx_len,
					 3,
					 200,
					 NULL,
					 NULL);
	if (r < 0)
		return r;
	if (!gxfp_parse_goodix_body(rx, rx_len, &frame) ||
	    frame.cmd != GXFP_CMD_SET_DAC)
		return -EBADMSG;
	return 0;
}

int gxfp_cmd_verify_sensor_cfg(struct gxfp_dev *dev,
			       const struct gxfp_sensor_cfg *cfg)
{
	static const uint16_t dac_regs[] = { 0x0220, 0x0236, 0x0238, 0x023a };
	int attempt;

	if (!dev || !cfg)
		return -EINVAL;

	for (attempt = 0; attempt < 3; attempt++) {
		uint16_t tcode = 0;
		int ok = 1;
		size_t i;
		int r;

		(void)gxfp_cmd_set_idle(dev);

		for (i = 0; i < cfg->dac_count; i++) {
			uint16_t value = 0;

			r = gxfp_cmd_read_reg_u16(dev, dac_regs[i], &value);
			if (r < 0 || value != cfg->dac[i]) {
				ok = 0;
				break;
			}
		}

		if (!ok)
			continue;
		r = gxfp_cmd_read_reg_u16(dev, GXFP_MILANG_REG_TCODE, &tcode);
		if (r < 0 || tcode != cfg->tcode)
			continue;
		return 0;
	}

	return -EIO;
}
