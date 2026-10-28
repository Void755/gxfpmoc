#include "gxfp_tool.h"

#include "gxfp/algo/common.h"
#include "gxfp/algo/image/fpn.h"
#include "gxfp/algo/image/plain.h"
#include "gxfp/algo/sensor_cfg.h"
#include "gxfp/cmd/device_recovery_cmd.h"
#include "gxfp/cmd/nav_cmd.h"
#include "gxfp/cmd/register_cmd.h"
#include "gxfp/cmd/sensor_cfg_cmd.h"
#include "gxfp/cmd/mode_cmd.h"
#include "gxfp/cmd/tls_cmd.h"
#include "gxfp/flow/fdt.h"
#include "gxfp/flow/milang_init.h"
#include "gxfp/io/dev.h"
#include "gxfp/proto/goodix_constants.h"
#include "gxfp/proto/mp_proto.h"
#include "gxfp/tls/tls_constants.h"
#include "gxfp/tls/tls_session.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GXFP_TOOL_POLL_MS 50
#define GXFP_TOOL_MILANG_CHIP_ID 0x2208

struct gxfp_tool_impl {
	struct gxfp_dev dev;
	struct gxfp_tls_session tls;
	struct gxfp_plain_sink plain;
	struct gxfp_fdt_flow fdt;

	uint8_t *psk;
	size_t psk_len;
	uint8_t *tap;
	size_t tap_cap;

	struct gxfp_fpn_dark dark;
	int dark_loaded;
};

static int tool_store_dark(struct gxfp_tool_impl *t,
			   const struct gxfp_decoded_image *img)
{
	uint16_t *pixels;
	size_t count;

	if (!img || !img->pixels || img->rows <= 0 || img->cols <= 0)
		return -EINVAL;

	count = (size_t)img->rows * (size_t)img->cols;
	pixels = (uint16_t *)malloc(count * sizeof(uint16_t));
	if (!pixels)
		return -ENOMEM;

	memcpy(pixels, img->pixels, count * sizeof(uint16_t));
	if (t->dark_loaded)
		gxfp_fpn_free(&t->dark);
	t->dark.pixels = pixels;
	t->dark.rows = img->rows;
	t->dark.cols = img->cols;
	t->dark_loaded = 1;
	return 0;
}

static int tool_poll(struct gxfp_tool_impl *t, int timeout_ms,
		     uint32_t *out_events)
{
	uint32_t events = GXFP_FDT_EVENT_NONE;

	if (out_events)
		*out_events = GXFP_FDT_EVENT_NONE;

	for (;;) {
		struct gxfp_tap_hdr hdr;
		const uint8_t *payload = NULL;
		size_t payload_len = 0;
		ssize_t n;
		int r;

		r = gxfp_dev_poll_readable(&t->dev, timeout_ms);
		if (r == -EAGAIN)
			return 0;
		if (r < 0)
			return r;

		n = gxfp_dev_read_record(&t->dev, t->tap, t->tap_cap,
					 &hdr, &payload, &payload_len);
		if (n == -EAGAIN)
			return 0;
		if (n < 0)
			return (int)n;

		r = gxfp_fdt_flow_feed_record(&t->fdt, &t->dev, &hdr,
					      payload, payload_len, &events);
		if (r < 0 && r != -EAGAIN)
			return r;

		if (hdr.type == GOODIX_MP_TYPE_TLS) {
			r = gxfp_tls_session_feed_cipher(&t->tls,
							 payload, payload_len);
			if (r < 0)
				return r;
		}

		if (out_events)
			*out_events |= events;

		timeout_ms = 0;
	}
}

static int tool_pull_plain(struct gxfp_tool_impl *t)
{
	for (;;) {
		uint8_t plain[TLS_REC_MAX_PLAIN];
		size_t plain_len = 0;
		int r;

		r = gxfp_tls_session_pull_plain(&t->tls, plain, sizeof(plain),
						&plain_len);
		if (r == 1) {
			gxfp_plain_scan_chunks(plain, plain_len, &t->plain);
			continue;
		}
		if (r == -EAGAIN || r == 0)
			return 0;
		return r;
	}
}

static int tool_apply_milang_cfg(struct gxfp_tool_impl *t)
{
	uint8_t otp[256];
	uint16_t otp_len = 0;
	struct gxfp_milang_otp_cfg otp_cfg;
	const uint8_t *template_data = NULL;
	size_t template_len = 0;
	uint8_t cfg_blob[GXFP_MILANG_CFG_LEN];
	uint8_t ack_status = 0xff;
	int cfg_blob_len;
	int r;

	r = gxfp_cmd_read_otp(&t->dev, otp, (uint16_t)sizeof(otp), &otp_len);
	if (r < 0)
		return r;

	r = gxfp_milang_parse_otp(otp, (size_t)otp_len, &otp_cfg);
	if (r < 0)
		return r;

	r = gxfp_milang_get_default_cfg_template(&template_data, &template_len);
	if (r < 0)
		return r;

	cfg_blob_len = gxfp_milang_prepare_cfg_blob(template_data, template_len,
						    cfg_blob, sizeof(cfg_blob));
	if (cfg_blob_len < 0)
		return cfg_blob_len;

	r = gxfp_milang_apply_otp_patch(cfg_blob, (size_t)cfg_blob_len, &otp_cfg);
	if (r < 0)
		return r;

	gxfp_cmd_fdt_state_set_runtime(&t->fdt.cmd, 0, 0, 0,
				       otp_cfg.has_tcode_delta ?
				       (int16_t)otp_cfg.fdt_delta : 0);

	r = gxfp_milang_prepare_config(&t->dev, otp_cfg.dac);
	if (r < 0)
		return r;

	r = gxfp_cmd_upload_config_mcu(&t->dev, cfg_blob,
				       (uint16_t)cfg_blob_len, &ack_status);
	if (r < 0)
		return r;

	r = gxfp_cmd_verify_sensor_cfg(&t->dev, otp_cfg.dac, otp_cfg.tcode);
	if (r < 0)
		return r;

	return 0;
}

static int tool_tls_init(struct gxfp_tool_impl *t)
{
	int r;

	r = gxfp_tls_session_init(&t->tls, &t->dev,
				  t->psk, t->psk_len);
	if (r < 0)
		return r;

	return gxfp_cmd_tls_server_init(&t->dev);
}

static void tool_impl_free(struct gxfp_tool_impl *t)
{
	if (!t)
		return;

	gxfp_tls_session_free(&t->tls);
	gxfp_dev_close(&t->dev);
	gxfp_plain_sink_reset(&t->plain);
	if (t->dark_loaded)
		gxfp_fpn_free(&t->dark);
	free(t->tap);
	free(t->psk);
	free(t);
}

int gxfp_tool_open(struct gxfp_tool *tool,
		   const char *path,
		   const uint8_t *psk,
		   size_t psk_len)
{
	struct gxfp_tool_impl *t;
	uint16_t chip_id = 0;
	int r;

	if (!tool || !path)
		return -EINVAL;
	if (psk_len > 0 && !psk)
		return -EINVAL;
	if (tool->impl)
		return -EALREADY;

	t = (struct gxfp_tool_impl *)calloc(1, sizeof(*t));
	if (!t)
		return -ENOMEM;

	t->dev.fd = -1;
	if (psk_len > 0) {
		t->psk = (uint8_t *)malloc(psk_len);
		if (!t->psk) {
			free(t);
			return -ENOMEM;
		}
		memcpy(t->psk, psk, psk_len);
		t->psk_len = psk_len;
	}

	r = gxfp_dev_open(&t->dev, path, O_RDWR | O_NONBLOCK);
	if (r < 0)
		goto fail;

	t->tap_cap = sizeof(struct gxfp_tap_hdr) + (size_t)GXFP_MP_FRAME_MAX;
	t->tap = (uint8_t *)malloc(t->tap_cap);
	if (!t->tap) {
		r = -ENOMEM;
		goto fail;
	}

	gxfp_fdt_flow_init(&t->fdt);
	gxfp_plain_sink_reset(&t->plain);

	(void)gxfp_cmd_reset_device(&t->dev, 0x01);
	(void)gxfp_cmd_get_image(&t->dev);
	{
		struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000L };
		(void)nanosleep(&ts, NULL);
	}
	(void)gxfp_dev_flush_rxq(&t->dev);

	r = gxfp_cmd_read_chip_id(&t->dev, &chip_id);
	if (r < 0)
		fprintf(stderr, "gxfp: read chip id failed: %s\n", strerror(-r));
	else if (chip_id == GXFP_TOOL_MILANG_CHIP_ID) {
		r = tool_apply_milang_cfg(t);
		if (r < 0)
			goto fail;
	}

	if (t->psk) {
		r = tool_tls_init(t);
		if (r < 0)
			goto fail;
	}

	tool->impl = t;
	return 0;

fail:
	tool_impl_free(t);
	return r;
}

void gxfp_tool_close(struct gxfp_tool *tool)
{
	struct gxfp_tool_impl *t;

	if (!tool)
		return;
	t = (struct gxfp_tool_impl *)tool->impl;
	if (!t)
		return;

	tool_impl_free(t);
	tool->impl = NULL;
}

static int tool_rearm_tls(struct gxfp_tool_impl *t)
{
	enum gxfp_tls_hs_state state;

	state = gxfp_tls_session_state(&t->tls);
	if (state == GXFP_TLS_HS_CONNECTING ||
	    state == GXFP_TLS_HS_CONNECTED)
		return 0;

	gxfp_tls_session_free(&t->tls);
	(void)gxfp_dev_flush_rxq(&t->dev);
	return tool_tls_init(t);
}

int gxfp_tool_connect(struct gxfp_tool *tool, int timeout_ms)
{
	struct gxfp_tool_impl *t;
	int64_t deadline;
	int r;

	if (!tool || !tool->impl)
		return -EINVAL;
	t = (struct gxfp_tool_impl *)tool->impl;

	r = tool_rearm_tls(t);
	if (r < 0)
		return r;

	deadline = timeout_ms > 0 ? gxfp_monotonic_ms() + timeout_ms : 0;

	for (;;) {
		enum gxfp_tls_hs_state state = gxfp_tls_session_state(&t->tls);
		int64_t remain;
		int wait_ms;

		if (state == GXFP_TLS_HS_CONNECTED)
			return 0;
		if (state == GXFP_TLS_HS_ERROR)
			return gxfp_tls_session_error(&t->tls);

		remain = deadline ? deadline - gxfp_monotonic_ms() : GXFP_TOOL_POLL_MS;
		if (remain <= 0)
			return -ETIMEDOUT;
		wait_ms = remain < GXFP_TOOL_POLL_MS ? (int)remain : GXFP_TOOL_POLL_MS;

		r = tool_poll(t, wait_ms, NULL);
		if (r < 0)
			return r;

		r = gxfp_tls_session_handshake_step(&t->tls);
		if (r == 1)
			return 0;
		if (r != -EAGAIN)
			return r;
	}
}

int gxfp_tool_wait_finger(struct gxfp_tool *tool, int present, int timeout_ms)
{
	struct gxfp_tool_impl *t;
	enum gxfp_fdt_mode mode;
	uint32_t wanted;
	int64_t deadline;
	int r;

	if (!tool || !tool->impl)
		return -EINVAL;
	t = (struct gxfp_tool_impl *)tool->impl;

	mode = present ? GXFP_FDT_MODE_WAIT_DOWN : GXFP_FDT_MODE_WAIT_UP;
	wanted = present ? GXFP_FDT_EVENT_DOWN :
			   (GXFP_FDT_EVENT_UP | GXFP_FDT_EVENT_REVERSE);

	t->fdt.mode = GXFP_FDT_MODE_IDLE;
	r = gxfp_fdt_flow_set_mode(&t->fdt, &t->dev, mode);
	if (r < 0)
		return r;

	deadline = timeout_ms > 0 ? gxfp_monotonic_ms() + timeout_ms : 0;
	for (;;) {
		uint32_t events = GXFP_FDT_EVENT_NONE;
		int64_t remain;
		int wait_ms;

		remain = deadline ? deadline - gxfp_monotonic_ms() : GXFP_TOOL_POLL_MS;
		if (remain <= 0)
			return -ETIMEDOUT;
		wait_ms = remain < GXFP_TOOL_POLL_MS ? (int)remain : GXFP_TOOL_POLL_MS;

		r = tool_poll(t, wait_ms, &events);
		if (r < 0)
			return r;
		if (events & wanted)
			return 0;
	}
}

static int tool_store_raw_image(struct gxfp_tool_impl *t, int reference,
				struct gxfp_decoded_image *out)
{
	struct gxfp_decoded_image img;
	uint8_t hdr[5];
	uint8_t *body = NULL;
	size_t body_len = 0;
	int r;

	memset(&img, 0, sizeof(img));
	r = gxfp_plain_sink_extract_normal(&t->plain, hdr, &body, &body_len);
	if (r <= 0)
		return r < 0 ? r : -EIO;

	r = gxfp_decode_image(body, body_len, GXFP_DECODE_AUTO, &img);
	free(body);
	if (r < 0)
		return r;

	if (reference) {
		r = tool_store_dark(t, &img);
		gxfp_decoded_image_free(&img);
		return r;
	}

	if (t->dark_loaded) {
		r = gxfp_fpn_correct(img.pixels, img.rows, img.cols, &t->dark);
		if (r < 0) {
			gxfp_decoded_image_free(&img);
			return r;
		}
	}

	if (!out) {
		gxfp_decoded_image_free(&img);
		return -EINVAL;
	}

	*out = img;
	return 0;
}

static int tool_capture_internal(struct gxfp_tool_impl *t, int reference,
				 int timeout_ms,
				 struct gxfp_decoded_image *out)
{
	int64_t deadline;
	int r;

	gxfp_plain_sink_reset_normal(&t->plain);
	r = gxfp_cmd_get_image(&t->dev);
	if (r < 0)
		return r;

	deadline = timeout_ms > 0 ? gxfp_monotonic_ms() + timeout_ms : 0;
	for (;;) {
		int64_t remain;
		int wait_ms;

		remain = deadline ? deadline - gxfp_monotonic_ms() : GXFP_TOOL_POLL_MS;
		if (remain <= 0)
			return -ETIMEDOUT;
		wait_ms = remain < GXFP_TOOL_POLL_MS ? (int)remain : GXFP_TOOL_POLL_MS;

		r = tool_poll(t, wait_ms, NULL);
		if (r < 0)
			return r;

		r = tool_pull_plain(t);
		if (r < 0)
			return r;
		if (t->plain.have_normal)
			break;
	}

	return tool_store_raw_image(t, reference, out);
}

int gxfp_tool_activate(struct gxfp_tool *tool, int timeout_ms)
{
	int r;

	r = gxfp_tool_connect(tool, timeout_ms);
	if (r < 0)
		return r;

	r = gxfp_tool_wait_finger(tool, 0, timeout_ms);
	if (r < 0)
		return r;

	return tool_capture_internal(
		(struct gxfp_tool_impl *)tool->impl, 1, timeout_ms, NULL);
}

int gxfp_tool_capture(struct gxfp_tool *tool,
		      int timeout_ms,
		      struct gxfp_decoded_image *out)
{
	if (!tool || !tool->impl || !out)
		return -EINVAL;
	return tool_capture_internal((struct gxfp_tool_impl *)tool->impl,
				     0, timeout_ms, out);
}

int gxfp_tool_nav_frame(struct gxfp_tool *tool, int first, int timeout_ms,
			 struct gxfp_decoded_image *out)
{
	struct gxfp_tool_impl *t;
	uint8_t frame[GXFP_MP_FRAME_MAX];
	size_t frame_len = 0;
	struct gxfp_decode_auto_result detected;
	int r;

	if (!tool || !tool->impl || !out)
		return -EINVAL;
	t = (struct gxfp_tool_impl *)tool->impl;

	memset(out, 0, sizeof(*out));
	r = gxfp_cmd_nav_frame(&t->dev, first, timeout_ms,
			       frame, sizeof(frame), &frame_len);
	if (r < 0)
		return r;

	r = gxfp_decode_image(frame, frame_len, GXFP_DECODE_AUTO, out);
	if (r < 0 && gxfp_decode_auto_detect(frame, frame_len, &detected) == 0) {
		r = gxfp_decode_image(detected.payload, detected.payload_len,
				      detected.mode, out);
	}
	return r;
}

int gxfp_tool_pov_capture(struct gxfp_tool *tool, int timeout_ms)
{
	struct gxfp_tool_impl *t;
	int64_t deadline;
	int r;

	if (!tool || !tool->impl)
		return -EINVAL;
	t = (struct gxfp_tool_impl *)tool->impl;

	gxfp_plain_sink_reset_pov(&t->plain);
	r = gxfp_cmd_sleep_cleanup(&t->dev);
	if (r < 0)
		return r;

	deadline = gxfp_monotonic_ms() + (timeout_ms > 0 ? timeout_ms : 3000);
	for (;;) {
		int64_t remain;
		int wait_ms;

		if (t->plain.pov_count >= GXFP_PLAIN_MAX_POV_FRAMES)
			return 0;
		if (gxfp_monotonic_ms() >= deadline)
			return 0;

		remain = deadline - gxfp_monotonic_ms();
		wait_ms = remain < GXFP_TOOL_POLL_MS ? (int)remain : GXFP_TOOL_POLL_MS;
		if (wait_ms <= 0)
			return 0;

		r = tool_poll(t, wait_ms, NULL);
		if (r < 0)
			return r;

		r = tool_pull_plain(t);
		if (r < 0)
			return r;
	}
}

int gxfp_tool_pov_count(const struct gxfp_tool *tool)
{
	if (!tool || !tool->impl)
		return -EINVAL;
	return ((const struct gxfp_tool_impl *)tool->impl)->plain.pov_count;
}

int gxfp_tool_pov_image(const struct gxfp_tool *tool, int index,
			struct gxfp_decoded_image *out)
{
	const struct gxfp_tool_impl *t;
	const uint8_t *body = NULL;
	size_t body_len = 0;
	uint8_t hdr[5];
	int r;

	if (!tool || !tool->impl || !out)
		return -EINVAL;
	t = (const struct gxfp_tool_impl *)tool->impl;

	r = gxfp_plain_sink_get_pov(&t->plain, index, hdr, &body, &body_len);
	if (r <= 0)
		return r < 0 ? r : -ENOENT;

	memset(out, 0, sizeof(*out));
	return gxfp_decode_image(body, body_len, GXFP_DECODE_AUTO, out);
}
