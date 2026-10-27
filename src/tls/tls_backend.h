#pragma once

#include <stddef.h>
#include <stdint.h>

struct gxfp_tls_backend;

typedef int (*gxfp_tls_backend_send_fn)(void *ctx, const uint8_t *rec, size_t rec_len);

int gxfp_tls_backend_create(struct gxfp_tls_backend **out,
			   const uint8_t *psk,
			   size_t psk_len,
			   gxfp_tls_backend_send_fn send_fn,
			   void *send_ctx);

void gxfp_tls_backend_free(struct gxfp_tls_backend *e);

int gxfp_tls_backend_feed_cipher_record(struct gxfp_tls_backend *e,
				       const uint8_t *rec,
				       size_t rec_len);

int gxfp_tls_backend_handshake_step(struct gxfp_tls_backend *e);

int gxfp_tls_backend_pull_plain(struct gxfp_tls_backend *e,
			      uint8_t *out,
			      size_t out_cap,
			      size_t *out_len);

const char *gxfp_tls_backend_strerror(int err, char *buf, size_t buf_len);
