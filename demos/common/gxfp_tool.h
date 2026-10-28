#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gxfp/algo/image/decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gxfp_tool {
	void *impl;
};

int gxfp_tool_open(struct gxfp_tool *tool,
		   const char *path,
		   const uint8_t *psk,
		   size_t psk_len);

void gxfp_tool_close(struct gxfp_tool *tool);

int gxfp_tool_connect(struct gxfp_tool *tool, int timeout_ms);

int gxfp_tool_activate(struct gxfp_tool *tool, int timeout_ms);

int gxfp_tool_wait_finger(struct gxfp_tool *tool, int present, int timeout_ms);

int gxfp_tool_capture(struct gxfp_tool *tool,
		      int timeout_ms,
		      struct gxfp_decoded_image *out);

int gxfp_tool_nav_frame(struct gxfp_tool *tool, int first, int timeout_ms,
			struct gxfp_decoded_image *out);

int gxfp_tool_pov_capture(struct gxfp_tool *tool, int timeout_ms);
int gxfp_tool_pov_count(const struct gxfp_tool *tool);
int gxfp_tool_pov_image(const struct gxfp_tool *tool, int index,
			struct gxfp_decoded_image *out);

#ifdef __cplusplus
}
#endif
