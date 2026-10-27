#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gxfp/io/dev.h"

#ifdef __cplusplus
extern "C" {
#endif

enum gxfp_tls_hs_state {
	GXFP_TLS_HS_INIT = 0,
	GXFP_TLS_HS_CONNECTING,
	GXFP_TLS_HS_CONNECTED,
	GXFP_TLS_HS_ERROR,
};

struct gxfp_tls_session {
	void *impl;
};

int gxfp_tls_session_init(struct gxfp_tls_session *svc,
			  struct gxfp_dev *dev,
			  const uint8_t *psk,
			  size_t psk_len);

void gxfp_tls_session_free(struct gxfp_tls_session *svc);

int gxfp_tls_session_feed_cipher(struct gxfp_tls_session *svc,
				 const uint8_t *rec,
				 size_t rec_len);

int gxfp_tls_session_handshake_step(struct gxfp_tls_session *svc);

int gxfp_tls_session_pull_plain(struct gxfp_tls_session *svc,
				uint8_t *out,
				size_t out_cap,
				size_t *out_len);

const char *gxfp_tls_session_strerror(int err, char *buf, size_t buf_len);

enum gxfp_tls_hs_state gxfp_tls_session_state(const struct gxfp_tls_session *svc);
int gxfp_tls_session_error(const struct gxfp_tls_session *svc);

#ifdef __cplusplus
}
#endif
