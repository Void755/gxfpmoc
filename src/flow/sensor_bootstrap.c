#include "gxfp/flow/sensor_bootstrap.h"

#include "gxfp/algo/sensor_cfg.h"
#include "gxfp/cmd/sensor_cfg_cmd.h"
#include "gxfp/flow/milang_init.h"

#include <errno.h>
#include <string.h>

int gxfp_sensor_bootstrap(struct gxfp_dev *dev,
			  enum gxfp_sensor_variant variant,
			  struct gxfp_cmd_fdt_state *fdt,
			  struct gxfp_sensor_cfg *out_cfg)
{
	uint8_t otp[256];
	uint16_t otp_len = 0;
	const uint8_t *template_data = NULL;
	size_t template_len = 0;
	uint8_t cfg_blob[GXFP_SENSOR_CFG_LEN];
	struct gxfp_sensor_cfg cfg;
	uint8_t ack_status = 0xff;
	int cfg_len;
	int r;

	if (!dev || !out_cfg)
		return -EINVAL;

	memset(&cfg, 0, sizeof(cfg));
	r = gxfp_cmd_read_otp(dev, otp, (uint16_t)sizeof(otp), &otp_len);
	if (r < 0)
		return r;

	r = gxfp_sensor_parse_otp(variant, otp, (size_t)otp_len, &cfg);
	if (r < 0)
		return r;

	r = gxfp_sensor_get_cfg_template(variant, &template_data, &template_len);
	if (r < 0)
		return r;

	cfg_len = gxfp_sensor_prepare_cfg_blob(template_data, template_len,
					       cfg_blob, sizeof(cfg_blob));
	if (cfg_len < 0)
		return cfg_len;

	r = gxfp_sensor_apply_otp_patch(variant, cfg_blob, (size_t)cfg_len,
					&cfg);
	if (r < 0)
		return r;

	if (fdt) {
		gxfp_cmd_fdt_state_set_variant(fdt, variant);
		gxfp_cmd_fdt_state_set_runtime(fdt, 0, 0, 0,
					       cfg.has_tcode_delta ?
					       (int16_t)cfg.fdt_delta : 0);
	}

	r = gxfp_sensor_prepare_config(dev, variant, &cfg);
	if (r < 0)
		return r;

	r = gxfp_cmd_upload_config_mcu(dev, cfg_blob, (uint16_t)cfg_len,
				       &ack_status);
	if (r < 0)
		return r;

	r = gxfp_cmd_verify_sensor_cfg(dev, &cfg);
	if (r < 0)
		return r;

	*out_cfg = cfg;
	return 0;
}
