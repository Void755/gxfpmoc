#include "gxfp/algo/image/plain.h"

#include "gxfp/proto/goodix_proto.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>


static void crc32_mpeg2_init_table(uint32_t table[256])
{
	for (uint32_t i = 0; i < 0x100; i++) {
		uint32_t local_c = i << 24;
		uint32_t local_crc = 0;
		for (uint32_t bit = 0; bit < 8; bit++) {
			if (((local_c ^ local_crc) & 0x80000000u) == 0) {
				local_crc <<= 1;
			} else {
				local_crc = (local_crc << 1) ^ 0x04C11DB7u;
			}
			local_c <<= 1;
		}
		table[i] = local_crc;
	}
}

static uint32_t crc32_mpeg2(const uint8_t *buf, size_t len)
{
	static uint32_t table[256];
	static int table_inited = 0;

	if (!table_inited) {
		crc32_mpeg2_init_table(table);
		table_inited = 1;
	}

	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		uint8_t b = buf[i];
		crc = table[((crc >> 24) ^ (uint32_t)b) & 0xFFu] ^ (crc << 8);
	}
	return crc;
}

static int gxfp_image_crc_check(const uint8_t *buf, size_t len)
{
	if (!buf || len < 4)
		return 0;

	uint32_t crc = crc32_mpeg2(buf, len - 4);
	uint32_t expected = ((uint32_t)buf[len - 2] << 24) |
			    ((uint32_t)buf[len - 1] << 16) |
			    ((uint32_t)buf[len - 4] << 8) |
			    ((uint32_t)buf[len - 3]);
	return (crc == expected) ? 1 : 0;
}

static void plain_frame_reset(struct gxfp_plain_frame *f)
{
	if (!f)
		return;

	if (f->body && f->body_len)
		memset(f->body, 0, f->body_len);
	free(f->body);
	memset(f, 0, sizeof(*f));
}

static int plain_frame_store(struct gxfp_plain_frame *f,
			     const uint8_t *payload,
			     size_t payload_len,
			     size_t body_len)
{
	uint8_t *buf;

	if (!f || !payload)
		return -EINVAL;
	if (payload_len < 5 || body_len == 0)
		return 0;
	if (body_len > payload_len - 5)
		return -EINVAL;

	const uint8_t *body = payload + 5;

	if (!gxfp_image_crc_check(body, body_len))
		return 0;

	buf = (uint8_t *)malloc(body_len);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, body, body_len);
	memcpy(f->hdr, payload, 5);
	f->body = buf;
	f->body_len = body_len;
	return 1;
}

void gxfp_plain_sink_reset_normal(struct gxfp_plain_sink *s)
{
	if (!s)
		return;

	plain_frame_reset(&s->normal);
	s->have_normal = 0;
}

void gxfp_plain_sink_reset_pov(struct gxfp_plain_sink *s)
{
	if (!s)
		return;

	for (int i = 0; i < GXFP_PLAIN_MAX_POV_FRAMES; i++)
		plain_frame_reset(&s->pov[i]);

	s->pov_count = 0;
	s->pov_crc_fail = 0;
}

void gxfp_plain_sink_reset(struct gxfp_plain_sink *s)
{
	if (!s)
		return;

	gxfp_plain_sink_reset_normal(s);
	gxfp_plain_sink_reset_pov(s);
}

int gxfp_plain_sink_append_normal(struct gxfp_plain_sink *s,
				  const uint8_t *payload,
				  size_t payload_len)
{
	int r;

	if (!s || !payload || payload_len < 5)
		return 0;
	if (s->have_normal)
		return 0;

	r = plain_frame_store(&s->normal, payload, payload_len,
			      payload_len - 5);
	if (r == 1)
		s->have_normal = 1;
	return r;
}

int gxfp_plain_sink_append_pov(struct gxfp_plain_sink *s,
			       const uint8_t *payload,
			       size_t payload_len)
{
	int r;

	if (!s || !payload || payload_len < 5)
		return 0;
	if (s->pov_count >= GXFP_PLAIN_MAX_POV_FRAMES)
		return 0;

	r = plain_frame_store(&s->pov[s->pov_count], payload, payload_len,
			      payload_len - 5);
	if (r == 1)
		s->pov_count++;
	else if (r == 0)
		s->pov_crc_fail++;
	return r;
}

int gxfp_plain_sink_extract_normal(struct gxfp_plain_sink *s,
				   uint8_t out_hdr[5],
				   uint8_t **out_body,
				   size_t *out_body_len)
{
	uint8_t *copy;
	size_t len;

	if (!s || !out_body || !out_body_len)
		return -EINVAL;

	*out_body = NULL;
	*out_body_len = 0;
	if (out_hdr)
		memset(out_hdr, 0, 5);

	if (!s->have_normal || !s->normal.body || s->normal.body_len == 0)
		return 0;

	len = s->normal.body_len;
	copy = (uint8_t *)malloc(len);
	if (!copy)
		return -ENOMEM;
	memcpy(copy, s->normal.body, len);
	if (out_hdr)
		memcpy(out_hdr, s->normal.hdr, 5);

	plain_frame_reset(&s->normal);
	s->have_normal = 0;

	*out_body = copy;
	*out_body_len = len;
	return 1;
}

int gxfp_plain_sink_get_pov(const struct gxfp_plain_sink *s,
			    int index,
			    uint8_t out_hdr[5],
			    const uint8_t **out_body,
			    size_t *out_body_len)
{
	if (!s || !out_body || !out_body_len)
		return -EINVAL;
	if (index < 0 || index >= s->pov_count)
		return -ENOENT;

	const struct gxfp_plain_frame *f = &s->pov[index];

	*out_body = f->body;
	*out_body_len = f->body_len;
	if (out_hdr)
		memcpy(out_hdr, f->hdr, 5);
	return 1;
}

void gxfp_plain_scan_chunks(const uint8_t *buf,
			    size_t len,
			    struct gxfp_plain_sink *sink)
{
	size_t off;

	if (!buf || len < 8 || !sink)
		return;

	for (off = 0; off + 4 <= len; off++) {
		struct gxfp_frame_parsed frame;
		size_t total;
		uint8_t cmd0;
		uint8_t cmd1;

		if (!gxfp_parse_goodix_body(buf + off, len - off, &frame) || !frame.valid)
			continue;

		total = (size_t)frame.payload_len + 4u;

		cmd0 = (uint8_t)((frame.cmd >> 4) & 0x0F);
		cmd1 = (uint8_t)((frame.cmd & 0x0F) >> 1);

		if (cmd0 == 0x2 &&
		    frame.payload_len >= 5 &&
		    frame.payload[0] != 0xAA) {
			(void)gxfp_plain_sink_append_normal(sink,
							    frame.payload,
							    frame.payload_len);
		} else if (cmd0 == 0xD && cmd1 == 0x1 &&
			   frame.payload_len >= 5 &&
			   frame.payload[0] == 0xAA) {
			(void)gxfp_plain_sink_append_pov(sink,
							 frame.payload,
							 frame.payload_len);
		}

		if (total > 0 && off + total <= len)
			off += total - 1u;
	}
}
