#include "gxfp/io/dev.h"

#include "gxfp/io/uapi.h"
#include "gxfp/proto/mp_proto.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* ---- opaque mailbox records --------------------------------------------- */

static int gxfp_read_chunk(struct gxfp_dev *dev,
			   const struct gxfp_rx_record **out)
{
	const struct gxfp_rx_record *rec;
	ssize_t n;

	n = read(dev->fd, dev->rx_chunk, GXFP_RX_RECORD_SIZE);
	if (n < 0)
		return -errno;
	if ((size_t)n != GXFP_RX_RECORD_SIZE)
		return -EIO;

	rec = (const struct gxfp_rx_record *)dev->rx_chunk;
	if (rec->reserved != 0 || rec->len == 0 || rec->len > GXFP_RX_MAX)
		return -EBADMSG;

	*out = rec;
	return 0;
}

static ssize_t gxfp_rx_assemble(struct gxfp_dev *dev, void *buf, size_t buf_cap,
				struct gxfp_tap_hdr *out_hdr,
				const uint8_t **out_payload,
				size_t *out_payload_len)
{
	struct gxfp_tap_hdr hdr;
	const struct gxfp_rx_record *rec;
	uint8_t *frame;
	uint16_t mp_len = 0;
	size_t total;
	size_t copied;
	struct timespec ts;
	int rc;

	if (buf_cap < sizeof(hdr) + GXFP_MP_CHUNK_MAX)
		return -EMSGSIZE;

	frame = (uint8_t *)buf + sizeof(hdr);

	rc = gxfp_read_chunk(dev, &rec);
	if (rc)
		return rc;

	if (!gxfp_mp_check_head(rec->data, rec->len, &mp_len)) {
		return -EAGAIN;
	}

	total = 4u + (size_t)mp_len;
	if (total > buf_cap - sizeof(hdr)) {
		return -EMSGSIZE;
	}

	copied = gxfp_mp_rx_chunk(total);
	if (copied > rec->len)
		copied = rec->len;
	memcpy(frame, rec->data, copied);

	while (copied < total) {
		size_t chunk = gxfp_mp_rx_chunk(total - copied);

		rc = gxfp_dev_poll_readable(dev, (int)dev->irq_timeout_ms);
		if (rc) {
			return rc;
		}

		rc = gxfp_read_chunk(dev, &rec);
		if (rc) {
			return rc;
		}

		if (chunk > rec->len)
			chunk = rec->len;
		memcpy(frame + copied, rec->data, chunk);
		copied += chunk;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.len = mp_len;
	hdr.type = frame[0] >> 4;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	hdr.ts_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
	memcpy(hdr.head16, frame + 4,
	       mp_len < sizeof(hdr.head16) ? mp_len : sizeof(hdr.head16));

	*out_hdr = hdr;
	if (out_payload)
		*out_payload = frame + 4;
	if (out_payload_len)
		*out_payload_len = mp_len;

	return (ssize_t)(sizeof(hdr) + mp_len);
}

/* ---- public API ---------------------------------------------------------- */

int gxfp_dev_open(struct gxfp_dev *dev, const char *path, int flags)
{
	if (!dev || !path)
		return -EINVAL;

	memset(dev, 0, sizeof(*dev));
	dev->fd = -1;
	dev->irq_timeout_ms = 500;

	dev->fd = open(path, flags | O_CLOEXEC);
	if (dev->fd < 0)
		return -errno;

	dev->rx_chunk = malloc(GXFP_RX_RECORD_SIZE);
	if (!dev->rx_chunk) {
		int rc = -ENOMEM;

		close(dev->fd);
		dev->fd = -1;
		return rc;
	}

	dev->seq16 = GXFP_MP_SEQ16_INIT;
	return 0;
}

void gxfp_dev_close(struct gxfp_dev *dev)
{
	if (!dev)
		return;

	if (dev->fd >= 0) {
		close(dev->fd);
		dev->fd = -1;
	}
	free(dev->rx_chunk);
	dev->rx_chunk = NULL;
}

int gxfp_dev_poll_readable(struct gxfp_dev *dev, int timeout_ms)
{
	struct pollfd pfd;
	int rc;
	int wait_ms;

	if (!dev || dev->fd < 0)
		return -EINVAL;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = dev->fd;
	pfd.events = POLLIN;

	wait_ms = timeout_ms > 0 ? timeout_ms : 1;

	rc = poll(&pfd, 1, wait_ms);
	if (rc < 0)
		return -errno;
	if (rc == 0)
		return -EAGAIN;
	if (pfd.revents & POLLIN)
		return 0;
	if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
		return -EIO;
	return -EAGAIN;
}

ssize_t gxfp_dev_read_record(struct gxfp_dev *dev,
			     void *buf,
			     size_t buf_cap,
			     struct gxfp_tap_hdr *out_hdr,
			     const uint8_t **out_payload,
			     size_t *out_payload_len)
{
	int pr;

	if (!dev || dev->fd < 0 || !dev->rx_chunk || !buf || !out_hdr)
		return -EINVAL;

	pr = gxfp_dev_poll_readable(dev, 0);
	if (pr)
		return pr;

	return gxfp_rx_assemble(dev, buf, buf_cap, out_hdr, out_payload,
				out_payload_len);
}

int gxfp_dev_send_packet(struct gxfp_dev *dev,
			 uint8_t mp_type,
			 const void *payload,
			 uint16_t payload_len)
{
	uint8_t mp[4u + GXFP_MP_MAX_PAYLOAD];
	uint8_t tx[GXFP_MP_TX_BUFFER_SIZE];
	int mp_len;
	int tx_len;
	ssize_t wr;

	if (!dev || dev->fd < 0)
		return -EINVAL;
	if (payload_len && !payload)
		return -EINVAL;
	if (payload_len > GXFP_MP_MAX_PAYLOAD)
		return -EMSGSIZE;

	mp_len = gxfp_mp_build(mp_type, (const uint8_t *)payload, payload_len,
			       mp, sizeof(mp));
	if (mp_len < 0)
		return mp_len;

	tx_len = gxfp_mp_wrap(mp, (size_t)mp_len, &dev->seq16, tx, sizeof(tx));
	if (tx_len < 0)
		return tx_len;

	wr = write(dev->fd, tx, (size_t)tx_len);
	if (wr < 0)
		return -errno;
	if ((size_t)wr != (size_t)tx_len)
		return -EIO;
	return 0;
}

int gxfp_dev_flush_rxq(struct gxfp_dev *dev)
{
	if (!dev || dev->fd < 0)
		return -EINVAL;

	if (ioctl(dev->fd, GXFP_IOCTL_FLUSH_RX) != 0)
		return -errno;

	return 0;
}
