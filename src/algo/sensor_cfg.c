#include "gxfp/algo/sensor_cfg.h"

#include "gxfp/algo/chicagoh.h"
#include "gxfp/algo/milang.h"
#include "gxfp/algo/common.h"
#include "gxfp/proto/milang_regs.h"

#include <errno.h>
#include <string.h>

enum {
	GXFP_MILANG_DEFAULT_TCODE = 0x80,
	GXFP_MILANG_DEFAULT_FDT_DELTA = 0x15,
	GXFP_MILANG_DEFAULT_DAC = 0x0008,

	GXFP_CFG_CHECKSUM_WORDS = 0x6f,
	GXFP_CFG_CHECKSUM_OFF = 0xde,
};

struct cfg_patch {
	uint8_t section;
	uint16_t reg;
	uint16_t value;
	uint8_t mode;
};

static void
gxfp_cfg_recalc_checksum(uint8_t *cfg, size_t cfg_len)
{
	int16_t sum = (int16_t)0xa5a5;
	size_t i;

	if (!cfg || cfg_len != GXFP_SENSOR_CFG_LEN)
		return;

	for (i = 0; i < GXFP_CFG_CHECKSUM_WORDS; i++) {
		int16_t w = (int16_t)gxfp_le16(cfg + i * 2u);
		sum = (int16_t)(sum + w);
	}

	sum = (int16_t)(-sum);
	cfg[GXFP_CFG_CHECKSUM_OFF] = (uint8_t)(sum & 0xff);
	cfg[GXFP_CFG_CHECKSUM_OFF + 1] =
		(uint8_t)(((uint16_t)sum >> 8) & 0xff);
}

static int
gxfp_cfg_patch_reg(uint8_t *cfg, size_t cfg_len, uint16_t reg,
		   uint16_t val, uint8_t section_idx, uint8_t write_mode)
{
	size_t section_hdr;
	uint8_t section_off;
	uint8_t section_len;
	uint8_t step;

	if (!cfg || cfg_len != GXFP_SENSOR_CFG_LEN)
		return -EINVAL;
	if (section_idx >= GXFP_SENSOR_CFG_SECTIONS)
		return -EINVAL;
	if (write_mode > GXFP_MILANG_CFG_WRITE_HIGH)
		return -EINVAL;

	section_hdr = GXFP_MILANG_CFG_SECTION_PTR_OFF +
		      ((size_t)section_idx * GXFP_MILANG_CFG_SECTION_PTR_STRIDE);
	section_off = cfg[section_hdr];
	section_len = cfg[section_hdr + GXFP_MILANG_CFG_SECTION_LEN_OFF];
	if ((size_t)section_off + (size_t)section_len > cfg_len)
		return -EBADMSG;

	for (step = 0; step < section_len;
	     step = (uint8_t)(step + GXFP_MILANG_CFG_ENTRY_SIZE)) {
		size_t off = (size_t)section_off + (size_t)step;

		if (off + GXFP_MILANG_CFG_ENTRY_SIZE > cfg_len)
			return -EBADMSG;
		if (gxfp_le16(cfg + off) != reg)
			continue;

		if (write_mode == GXFP_MILANG_CFG_WRITE_BOTH ||
		    write_mode == GXFP_MILANG_CFG_WRITE_LOW)
			cfg[off + GXFP_MILANG_CFG_ENTRY_VALUE_OFF] =
				(uint8_t)(val & 0xffu);
		if (write_mode == GXFP_MILANG_CFG_WRITE_BOTH ||
		    write_mode == GXFP_MILANG_CFG_WRITE_HIGH)
			cfg[off + GXFP_MILANG_CFG_ENTRY_VALUE_OFF + 1u] =
				(uint8_t)((val >> 8) & 0xffu);

		gxfp_cfg_recalc_checksum(cfg, cfg_len);
		return 0;
	}

	return -ENOENT;
}

int gxfp_sensor_parse_otp(enum gxfp_sensor_variant variant,
			  const uint8_t *otp, size_t otp_len,
			  struct gxfp_sensor_cfg *out)
{
	if (!otp || !out)
		return -EINVAL;

	switch (variant) {
	case GXFP_SENSOR_VARIANT_MILANG:
		return gxfp_milang_parse_otp(otp, otp_len, out);
	case GXFP_SENSOR_VARIANT_CHICAGOHS:
		return gxfp_chicagoh_parse_otp(otp, otp_len, out);
	default:
		return -ENOTSUP;
	}
}

int gxfp_sensor_get_cfg_template(enum gxfp_sensor_variant variant,
				 const uint8_t **out_template,
				 size_t *out_len)
{
	return gxfp_sensor_variant_cfg_template(variant, out_template, out_len);
}

int gxfp_sensor_prepare_cfg_blob(const uint8_t *template_data,
				 size_t template_len, uint8_t *out_cfg,
				 size_t out_cfg_cap)
{
	if (!template_data || !out_cfg)
		return -EINVAL;
	if (template_len != GXFP_SENSOR_CFG_LEN)
		return -EINVAL;
	if (out_cfg_cap < template_len)
		return -EMSGSIZE;

	memcpy(out_cfg, template_data, template_len);
	return (int)template_len;
}

static int
apply_patch_list(uint8_t *cfg, size_t cfg_len,
		 const struct cfg_patch *patches, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		int r = gxfp_cfg_patch_reg(cfg, cfg_len, patches[i].reg,
					   patches[i].value, patches[i].section,
					   patches[i].mode);
		if (r < 0)
			return r;
	}
	return 0;
}

int gxfp_sensor_apply_otp_patch(enum gxfp_sensor_variant variant,
				uint8_t *cfg, size_t cfg_len,
				const struct gxfp_sensor_cfg *otp_cfg)
{
	if (!cfg || !otp_cfg)
		return -EINVAL;
	if (cfg_len != GXFP_SENSOR_CFG_LEN)
		return -EINVAL;

	if (variant == GXFP_SENSOR_VARIANT_MILANG) {
		const struct cfg_patch patches[] = {
			{ GXFP_MILANG_CFG_SECTION_DAC, GXFP_MILANG_REG_DAC,
			  otp_cfg->dac[0], GXFP_MILANG_CFG_WRITE_BOTH },
			{ GXFP_MILANG_CFG_SECTION_TCODE, GXFP_MILANG_REG_TCODE,
			  otp_cfg->tcode, GXFP_MILANG_CFG_WRITE_BOTH },
			{ GXFP_MILANG_CFG_SECTION_FDT, GXFP_MILANG_REG_FDT,
			  (uint16_t)(otp_cfg->fdt_delta << 8),
			  GXFP_MILANG_CFG_WRITE_HIGH },
		};
		if (!otp_cfg->has_tcode_delta)
			return -EINVAL;
		return apply_patch_list(cfg, cfg_len, patches,
					sizeof(patches) / sizeof(patches[0]));
	}

	if (variant == GXFP_SENSOR_VARIANT_CHICAGOHS) {
		const struct cfg_patch patches[] = {
			{ 0, 0x005c, otp_cfg->tcode, GXFP_MILANG_CFG_WRITE_BOTH },
			{ 0, 0x0220, otp_cfg->dac[0], GXFP_MILANG_CFG_WRITE_BOTH },
			{ 0, 0x0236, otp_cfg->dac[1], GXFP_MILANG_CFG_WRITE_BOTH },
			{ 0, 0x0238, otp_cfg->dac[2], GXFP_MILANG_CFG_WRITE_BOTH },
			{ 0, 0x023a, otp_cfg->dac[3], GXFP_MILANG_CFG_WRITE_BOTH },
			{ 2, 0x0082, (uint16_t)(otp_cfg->fdt_delta << 8),
			  GXFP_MILANG_CFG_WRITE_HIGH },
		};
		if (otp_cfg->dac_count != 4 || !otp_cfg->has_tcode_delta)
			return -EINVAL;
		return apply_patch_list(cfg, cfg_len, patches,
					sizeof(patches) / sizeof(patches[0]));
	}

	return -ENOTSUP;
}

int gxfp_sensor_apply_default_patch(enum gxfp_sensor_variant variant,
				    uint8_t *cfg, size_t cfg_len)
{
	struct gxfp_sensor_cfg defaults;

	if (variant != GXFP_SENSOR_VARIANT_MILANG)
		return -ENOTSUP;

	memset(&defaults, 0, sizeof(defaults));
	defaults.tcode = GXFP_MILANG_DEFAULT_TCODE;
	defaults.fdt_delta = GXFP_MILANG_DEFAULT_FDT_DELTA;
	defaults.dac[0] = GXFP_MILANG_DEFAULT_DAC;
	defaults.dac_count = 1;
	defaults.has_dac = 1;
	defaults.has_tcode_delta = 1;
	return gxfp_sensor_apply_otp_patch(variant, cfg, cfg_len, &defaults);
}
