#include "gxfp/tls/tls_session.h"

#include "../tls/tls_backend.h"

#include "gxfp/proto/goodix_constants.h"
#include "gxfp/tls/tls_constants.h"

#include <errno.h>
#include <stdlib.h>

struct gxfp_tls_session_impl {
	struct gxfp_dev *dev;
	enum gxfp_tls_hs_state state;
	int err;
	struct gxfp_tls_backend *backend;
};

static void session_impl_free(struct gxfp_tls_session_impl *s)
{
	if (!s)
		return;
	gxfp_tls_backend_free(s->backend);
	free(s);
}

static int send_tls_record_cb(void *ctx, const uint8_t *rec, size_t rec_len)
{
	struct gxfp_tls_session_impl *s = (struct gxfp_tls_session_impl *)ctx;
	if (!s || !s->dev || !rec || rec_len == 0 || rec_len > 0xFFFFu)
		return -EINVAL;
	return gxfp_dev_send_packet(s->dev, GOODIX_MP_TYPE_TLS, rec,
				    (uint16_t)rec_len);
}

int gxfp_tls_session_init(struct gxfp_tls_session *svc,
			  struct gxfp_dev *dev,
			  const uint8_t *psk,
			  size_t psk_len)
{
	struct gxfp_tls_session_impl *s;
	int r;

	if (!svc || !dev || !psk || psk_len != GXFP5130_PSK_LEN)
		return -EINVAL;
	if (svc->impl)
		return -EALREADY;

	s = (struct gxfp_tls_session_impl *)calloc(1, sizeof(*s));
	if (!s)
		return -ENOMEM;

	s->dev = dev;
	s->state = GXFP_TLS_HS_CONNECTING;

	r = gxfp_tls_backend_create(&s->backend, psk, psk_len,
				    send_tls_record_cb, s);
	if (r < 0) {
		session_impl_free(s);
		return r;
	}

	svc->impl = s;
	return 0;
}

void gxfp_tls_session_free(struct gxfp_tls_session *svc)
{
	struct gxfp_tls_session_impl *s;

	if (!svc)
		return;
	s = (struct gxfp_tls_session_impl *)svc->impl;
	if (!s)
		return;

	session_impl_free(s);
	svc->impl = NULL;
}

int gxfp_tls_session_feed_cipher(struct gxfp_tls_session *svc,
				 const uint8_t *rec,
				 size_t rec_len)
{
	struct gxfp_tls_session_impl *s;
	int r;

	if (!svc || !svc->impl || !rec || rec_len == 0)
		return -EINVAL;
	s = (struct gxfp_tls_session_impl *)svc->impl;

	r = gxfp_tls_backend_feed_cipher_record(s->backend, rec, rec_len);
	if (r < 0) {
		s->err = r;
		s->state = GXFP_TLS_HS_ERROR;
	}
	return r;
}

int gxfp_tls_session_handshake_step(struct gxfp_tls_session *svc)
{
	struct gxfp_tls_session_impl *s;
	int r;

	if (!svc || !svc->impl)
		return -EINVAL;
	s = (struct gxfp_tls_session_impl *)svc->impl;

	if (s->state == GXFP_TLS_HS_ERROR)
		return s->err ? s->err : -EIO;
	if (s->state == GXFP_TLS_HS_CONNECTED)
		return 1;

	r = gxfp_tls_backend_handshake_step(s->backend);
	if (r == 1) {
		s->state = GXFP_TLS_HS_CONNECTED;
		return 1;
	}
	if (r == -EAGAIN)
		return -EAGAIN;

	s->err = r;
	s->state = GXFP_TLS_HS_ERROR;
	return r;
}

int gxfp_tls_session_pull_plain(struct gxfp_tls_session *svc,
				uint8_t *out,
				size_t out_cap,
				size_t *out_len)
{
	if (!svc || !svc->impl || !out || out_cap == 0 || !out_len)
		return -EINVAL;
	return gxfp_tls_backend_pull_plain(
		((struct gxfp_tls_session_impl *)svc->impl)->backend,
		out, out_cap, out_len);
}

enum gxfp_tls_hs_state gxfp_tls_session_state(const struct gxfp_tls_session *svc)
{
	const struct gxfp_tls_session_impl *s;
	if (!svc || !svc->impl)
		return GXFP_TLS_HS_INIT;
	s = (const struct gxfp_tls_session_impl *)svc->impl;
	return s->state;
}

int gxfp_tls_session_error(const struct gxfp_tls_session *svc)
{
	const struct gxfp_tls_session_impl *s;
	if (!svc || !svc->impl)
		return -EINVAL;
	s = (const struct gxfp_tls_session_impl *)svc->impl;
	return s->err;
}

const char *gxfp_tls_session_strerror(int err, char *buf, size_t buf_len)
{
	return gxfp_tls_backend_strerror(err, buf, buf_len);
}
