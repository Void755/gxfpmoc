#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXFP_PLAIN_MAX_POV_FRAMES 5

struct gxfp_plain_frame {
	uint8_t hdr[5];
	uint8_t *body;
	size_t body_len;
};

struct gxfp_plain_sink {
	struct gxfp_plain_frame normal;
	int have_normal;

	struct gxfp_plain_frame pov[GXFP_PLAIN_MAX_POV_FRAMES];
	int pov_count;
	int pov_crc_fail;
};

void gxfp_plain_sink_reset(struct gxfp_plain_sink *s);
void gxfp_plain_sink_reset_normal(struct gxfp_plain_sink *s);
void gxfp_plain_sink_reset_pov(struct gxfp_plain_sink *s);

int gxfp_plain_sink_append_normal(struct gxfp_plain_sink *s,
				  const uint8_t *payload,
				  size_t payload_len);

int gxfp_plain_sink_append_pov(struct gxfp_plain_sink *s,
			       const uint8_t *payload,
			       size_t payload_len);

int gxfp_plain_sink_extract_normal(struct gxfp_plain_sink *s,
				   uint8_t out_hdr[5],
				   uint8_t **out_body,
				   size_t *out_body_len);

int gxfp_plain_sink_get_pov(const struct gxfp_plain_sink *s,
			    int index,
			    uint8_t out_hdr[5],
			    const uint8_t **out_body,
			    size_t *out_body_len);

void gxfp_plain_scan_chunks(const uint8_t *buf,
			    size_t len,
			    struct gxfp_plain_sink *sink);

#ifdef __cplusplus
}
#endif
