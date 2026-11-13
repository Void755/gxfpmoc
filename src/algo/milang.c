#include "gxfp/algo/milang.h"

#include "gxfp/algo/common.h"

#include <errno.h>
#include <string.h>

enum {
	GXFP_MILANG_OTP_IDX_TCODE = 0x16,
	GXFP_MILANG_OTP_IDX_TCODE_NEG = 0x17,
	GXFP_MILANG_OTP_IDX_TCODE_ALT = 0x19,
	GXFP_MILANG_OTP_IDX_DAC = 0x1a,
	GXFP_MILANG_OTP_IDX_DAC_BAK = 0x1b,
	GXFP_MILANG_OTP_IDX_DAC_NEG = 0x1f,
	GXFP_MILANG_OTP_IDX_FT_CRC = 0x1e,
	GXFP_MILANG_OTP_MIN_LEN = 0x20,
};

int
gxfp_milang_parse_otp(const uint8_t *otp, size_t otp_len,
		 struct gxfp_sensor_cfg *out)
{
	uint8_t ft_buf[0x16];
	uint8_t cp_sum = 0;
	uint8_t tcode_raw;
	uint8_t tcode_bak;
	uint8_t tcode_alt;
	uint8_t dac_raw;
	uint8_t dac_bak;
	uint8_t dac_neg;
	uint8_t selected_tcode = 0;
	uint8_t selected_dac = 0;
	uint16_t tcode;
	uint16_t fdt_delta;
	size_t i;

	if (!otp || !out)
		return -EINVAL;
	if (otp_len < GXFP_MILANG_OTP_MIN_LEN)
		return -EBADMSG;

	for (i = 0; i < 8; i++)
		cp_sum = (uint8_t)(cp_sum + otp[i]);
	cp_sum = (uint8_t)(cp_sum + otp[0x12] + otp[0x13] + otp[0x1d]);
	if (cp_sum != 0)
		return -EBADMSG;

	memcpy(ft_buf, otp, 0x11);
	ft_buf[0x11] = otp[0x12];
	ft_buf[0x12] = otp[0x13];
	ft_buf[0x13] = otp[0x1c];
	ft_buf[0x14] = otp[0x1d];
	ft_buf[0x15] = otp[0x1f];
	if (gxfp_crc8(ft_buf, sizeof(ft_buf)) !=
	    otp[GXFP_MILANG_OTP_IDX_FT_CRC])
		return -EBADMSG;

	tcode_raw = otp[GXFP_MILANG_OTP_IDX_TCODE];
	tcode_bak = otp[GXFP_MILANG_OTP_IDX_TCODE_NEG];
	tcode_alt = otp[GXFP_MILANG_OTP_IDX_TCODE_ALT];
	if (tcode_raw != 0 && tcode_raw == (uint8_t)~tcode_bak)
		selected_tcode = tcode_raw;
	else if (tcode_raw != 0 && tcode_raw == tcode_alt)
		selected_tcode = tcode_raw;
	else if (tcode_alt != 0 && tcode_alt == (uint8_t)~tcode_bak)
		selected_tcode = tcode_alt;
	else
		return -EBADMSG;

	dac_raw = otp[GXFP_MILANG_OTP_IDX_DAC];
	dac_bak = otp[GXFP_MILANG_OTP_IDX_DAC_BAK];
	dac_neg = otp[GXFP_MILANG_OTP_IDX_DAC_NEG];
	if (dac_neg != 0 && dac_neg == (uint8_t)~dac_bak)
		selected_dac = dac_neg;
	else if (dac_neg != 0 && dac_neg == dac_raw)
		selected_dac = dac_neg;
	else if (dac_raw != 0 && dac_raw == (uint8_t)~dac_bak)
		selected_dac = dac_raw;
	else
		return -EBADMSG;

	memset(out, 0, sizeof(*out));
	tcode = (uint16_t)((((uint16_t)(selected_tcode >> 4)) + 1u) * 0x10u);
	if (!tcode)
		return -EBADMSG;

	fdt_delta = (uint16_t)((((uint32_t)(selected_tcode & 0x0fu) + 2u) *
				0x6400u) / (uint32_t)tcode);
	fdt_delta = (uint16_t)(((uint16_t)fdt_delta / 3u) >> 4);

	out->tcode = tcode;
	out->fdt_delta = fdt_delta;
	out->dac[0] = (uint16_t)(((uint16_t)selected_dac << 4) | 0x0008u);
	out->dac_count = 1;
	out->has_dac = 1;
	out->has_tcode_delta = 1;
	return 0;
}
