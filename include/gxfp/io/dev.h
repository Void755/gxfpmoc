#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct gxfp_tap_hdr {
	uint32_t len;
	uint32_t type;
	uint32_t reserved;
	uint64_t ts_ns;
	uint8_t head16[16];
};

struct gxfp_dev {
	int fd;

	uint16_t seq16;
	unsigned int irq_timeout_ms;
	uint8_t *rx_chunk;
};

int gxfp_dev_open(struct gxfp_dev *dev, const char *path, int flags);

void gxfp_dev_close(struct gxfp_dev *dev);


int gxfp_dev_poll_readable(struct gxfp_dev *dev, int timeout_ms);

ssize_t gxfp_dev_read_record(struct gxfp_dev *dev,
				     void *buf,
				     size_t buf_cap,
				     struct gxfp_tap_hdr *out_hdr,
				     const uint8_t **out_payload,
				     size_t *out_payload_len);

int gxfp_dev_send_packet(struct gxfp_dev *dev,
			 uint8_t mp_type,
			 const void *payload,
			 uint16_t payload_len);

int gxfp_dev_flush_rxq(struct gxfp_dev *dev);

#ifdef __cplusplus
}
#endif
