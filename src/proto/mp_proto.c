#include "gxfp/proto/mp_proto.h"

#include "gxfp/proto/goodix_constants.h"

#include <errno.h>
#include <string.h>

static bool gxfp_mp_type_valid(uint8_t type)
{
	return type == GOODIX_MP_TYPE_CMD ||
	       type == GOODIX_MP_TYPE_TLS ||
	       type == GOODIX_MP_TYPE_NOTICE;
}

bool gxfp_mp_check_head(const uint8_t *buf, size_t buf_len,
			uint16_t *out_mp_len)
{
	uint8_t flags;
	uint8_t type;
	uint16_t mp_len;
	uint8_t checksum;

	if (out_mp_len)
		*out_mp_len = 0;
	if (!buf || buf_len < 4)
		return false;

	flags = buf[0];
	type = flags >> 4;
	if (!gxfp_mp_type_valid(type))
		return false;

	mp_len = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
	if (mp_len >= (uint16_t)GXFP_MP_LEN_MAX)
		return false;

	checksum = (uint8_t)(flags + buf[1] + buf[2]);
	if (buf[3] != checksum)
		return false;

	if (out_mp_len)
		*out_mp_len = mp_len;
	return true;
}

int gxfp_mp_build(uint8_t mp_type, const uint8_t *payload, size_t payload_len,
		  uint8_t *out, size_t out_cap)
{
	size_t total;

	if (!out)
		return -EINVAL;
	if (payload_len && !payload)
		return -EINVAL;
	if (payload_len >= GXFP_MP_LEN_MAX)
		return -EOVERFLOW;
	if (!gxfp_mp_type_valid(mp_type))
		return -EINVAL;

	total = 4u + payload_len;
	if (total > out_cap)
		return -EMSGSIZE;

	out[0] = (uint8_t)((mp_type & 0x0F) << 4);
	out[1] = (uint8_t)(payload_len & 0xFF);
	out[2] = (uint8_t)((payload_len >> 8) & 0xFF);
	out[3] = (uint8_t)(out[0] + out[1] + out[2]);
	if (payload_len)
		memcpy(&out[4], payload, payload_len);

	return (int)total;
}

static size_t gxfp_mp_wrapped_len(size_t mp_frame_len)
{
	size_t pad = (GXFP_MP_ALIGN - (mp_frame_len & (GXFP_MP_ALIGN - 1))) &
		     (GXFP_MP_ALIGN - 1);

	return 8u + mp_frame_len + pad;
}

int gxfp_mp_wrap(const uint8_t *mp_frame, size_t mp_frame_len,
		 uint16_t *seq16, uint8_t *out, size_t out_cap)
{
	size_t tx_len;

	if (!mp_frame || !seq16 || !out || mp_frame_len == 0)
		return -EINVAL;

	tx_len = gxfp_mp_wrapped_len(mp_frame_len);
	if (tx_len > GXFP_MP_TX_BUFFER_SIZE)
		return -EOVERFLOW;
	if (tx_len > out_cap)
		return -EMSGSIZE;

	memset(out, 0, tx_len);
	*seq16 = (uint16_t)((*seq16 + 1u) & 0xFFFFu);

	out[0] = GXFP_MP_WRAPPER_MAGIC;
	out[1] = (uint8_t)(mp_frame_len & 0xFF);
	out[2] = (uint8_t)((mp_frame_len >> 8) & 0xFF);
	out[3] = (uint8_t)(out[0] + out[1] + out[2]);
	out[4] = (uint8_t)(*seq16 & 0xFF);
	out[5] = (uint8_t)((*seq16 >> 8) & 0xFF);
	memcpy(&out[8], mp_frame, mp_frame_len);

	return (int)tx_len;
}

size_t gxfp_mp_rx_chunk(size_t remain)
{
	return remain < GXFP_MP_CHUNK_MAX ? remain : GXFP_MP_CHUNK_MAX;
}
