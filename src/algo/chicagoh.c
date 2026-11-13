#include "gxfp/algo/chicagoh.h"

#include "gxfp/algo/common.h"

#include <errno.h>
#include <string.h>

enum {
	GXFP_CHICAGOH_OTP_LEN = 0x40,

	GXFP_CHICAGOH_OTP_MT_DAC_CRC = 0x16,
	GXFP_CHICAGOH_OTP_TCCODE = 0x2a,
	GXFP_CHICAGOH_OTP_TCCODE_NEG = 0x2b,
	GXFP_CHICAGOH_OTP_TCCODE_ALT = 0x2d,
	GXFP_CHICAGOH_OTP_MT_DAC = 0x2e,

	GXFP_CHICAGOH_OTP_FT_DAC = 0x32,
	GXFP_CHICAGOH_OTP_CP_CRC = 0x3c,
	GXFP_CHICAGOH_OTP_FT_CRC = 0x3d,
	GXFP_CHICAGOH_OTP_FT_DAC_CRC = 0x3e,
	GXFP_CHICAGOH_OTP_MT_CRC = 0x3f,

	GXFP_CHICAGOH_DEFAULT_FDT_DELTA = 0x15,
};

static uint8_t
chicagoh_select_tcode(const uint8_t *otp)
{
	uint8_t tcode = otp[GXFP_CHICAGOH_OTP_TCCODE];
	uint8_t bak = otp[GXFP_CHICAGOH_OTP_TCCODE_NEG];
	uint8_t alt = otp[GXFP_CHICAGOH_OTP_TCCODE_ALT];

	if (tcode != 0 && tcode == (uint8_t)~bak)
		return tcode;
	if (tcode != 0 && tcode == alt)
		return tcode;
	if (alt != 0 && alt == (uint8_t)~bak)
		return alt;
	return 0;
}

static int
chicagoh_select_dac(const uint8_t *otp, uint8_t dac[4])
{
	size_t i;

	for (i = 0; i < 4; i++) {
		if (!otp[GXFP_CHICAGOH_OTP_FT_DAC + i])
			break;
	}
	if (i == 4 &&
	    gxfp_crc8(otp + GXFP_CHICAGOH_OTP_FT_DAC, 4) ==
	    otp[GXFP_CHICAGOH_OTP_FT_DAC_CRC]) {
		memcpy(dac, otp + GXFP_CHICAGOH_OTP_FT_DAC, 4);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		if (!otp[GXFP_CHICAGOH_OTP_MT_DAC + i])
			return -EBADMSG;
	}
	if (gxfp_crc8(otp + GXFP_CHICAGOH_OTP_MT_DAC, 4) !=
	    otp[GXFP_CHICAGOH_OTP_MT_DAC_CRC])
		return -EBADMSG;

	memcpy(dac, otp + GXFP_CHICAGOH_OTP_MT_DAC, 4);
	return 0;
}

int gxfp_chicagoh_parse_otp(const uint8_t *otp,
			    size_t otp_len,
			    struct gxfp_sensor_cfg *out)
{
	uint8_t cp[15];
	uint8_t ft[19];
	uint8_t mt[27];
	uint8_t raw_dac[4];
	uint8_t tcode_raw;
	uint16_t tcode;
	uint16_t fdt_delta;
	int r;

	if (!otp || !out)
		return -EINVAL;
	if (otp_len < GXFP_CHICAGOH_OTP_LEN)
		return -EBADMSG;
	memcpy(cp, otp, 11);
	memcpy(cp + 11, otp + 0x24, 4);
	if (gxfp_crc8(cp, sizeof(cp)) != otp[GXFP_CHICAGOH_OTP_CP_CRC])
		return -EBADMSG;

	memcpy(ft, otp + 0x0b, 9);
	ft[9] = otp[0x1c];
	memcpy(ft + 10, otp + 0x32, 4);
	memcpy(ft + 14, otp + 0x38, 4);
	ft[18] = otp[0x3e];
	if (gxfp_crc8(ft, sizeof(ft)) != otp[GXFP_CHICAGOH_OTP_FT_CRC])
		return -EBADMSG;

	memcpy(mt, otp + 0x14, 8);
	memcpy(mt + 8, otp + 0x1d, 7);
	memcpy(mt + 15, otp + 0x28, 10);
	memcpy(mt + 25, otp + 0x36, 2);
	if (gxfp_crc8(mt, sizeof(mt)) != otp[GXFP_CHICAGOH_OTP_MT_CRC])
		return -EBADMSG;

	tcode_raw = chicagoh_select_tcode(otp);
	if (!tcode_raw)
		return -EBADMSG;

	r = chicagoh_select_dac(otp, raw_dac);
	if (r < 0)
		return r;

	memset(out, 0, sizeof(*out));
	out->tcode = (uint16_t)(((uint16_t)(tcode_raw >> 4) + 1u) * 0x10u + 0x40u);
	tcode = out->tcode;
	if (!tcode)
		return -EBADMSG;

	fdt_delta = (uint16_t)((((uint32_t)(tcode_raw & 0x0fu) + 2u) * 100u *
				0x100u) / tcode);
	fdt_delta = (uint16_t)(((uint16_t)fdt_delta / 3u) >> 4);
	if (!fdt_delta)
		fdt_delta = GXFP_CHICAGOH_DEFAULT_FDT_DELTA;

	out->fdt_delta = fdt_delta;
	out->dac[0] = (uint16_t)(((uint16_t)raw_dac[0] << 4) | 0x0008u);
	out->dac[1] = raw_dac[1];
	out->dac[2] = raw_dac[2];
	out->dac[3] = raw_dac[3];
	out->dac_count = 4;
	out->has_dac = 1;
	out->has_tcode_delta = 1;
	return 0;
}
