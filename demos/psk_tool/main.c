#define _GNU_SOURCE

#include "gxfp/io/dev.h"
#include "gxfp/cmd/production_cmd.h"
#include "gxfp/algo/common.h"
#include "gxfp/algo/payload/provision.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage:\n"
		"  %s --build-bb010002 <out.bin> [--psk-raw32 <psk.bin>] [--seed8 <seed8.bin>] [--sealed-psk <blob.bin>] [--out-psk-raw32 <psk_out.bin>] [--no-pad4]\n"
		"  %s --build-bb010002-raw <out.bin> [--psk-raw32 <psk.bin>] [--seed8 <seed8.bin>] [--out-psk-raw32 <psk_out.bin>] [--no-pad4]\n"
		"  %s --dump-bb010002 <out.bin>\n"
		"  %s --dump-bb010003 <out.bin>\n"
		"  %s --upload-bb010002 <in.bin>\n"
		"\n"
		"notes:\n"
		"  - Build mode uses external psk/seed8 when provided; otherwise random values are generated.\n"
		"  - --build-bb010002-raw leaves the sealed-psk region empty.\n",
		argv0, argv0, argv0, argv0, argv0);
}

static int fill_random_bytes(uint8_t *buf, size_t len)
{
	int fd;
	ssize_t n;
	size_t off = 0;

	if (!buf || len == 0)
		return -EINVAL;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return -errno;

	while (off < len) {
		n = read(fd, buf + off, len - off);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return -errno;
		}
		if (n == 0) {
			close(fd);
			return -EIO;
		}
		off += (size_t)n;
	}

	close(fd);
	return 0;
}

static int load_or_generate_bytes(const char *path,
				  size_t expected_len,
				  const char *name,
				  uint8_t **out,
				  size_t *out_len)
{
	int rc;

	if (!out || !out_len || !name || expected_len == 0)
		return -EINVAL;

	if (path) {
		rc = gxfp_read_file_all(path, out, out_len);
		if (rc < 0)
			return rc;
		if (*out_len != expected_len) {
			free(*out);
			*out = NULL;
			*out_len = 0;
			return -EINVAL;
		}
		return 0;
	}

	*out = (uint8_t *)malloc(expected_len);
	if (!*out)
		return -ENOMEM;
	*out_len = expected_len;

	rc = fill_random_bytes(*out, expected_len);
	if (rc < 0) {
		free(*out);
		*out = NULL;
		*out_len = 0;
		return rc;
	}

	fprintf(stderr, "generated random %s (%zu bytes)\n", name, expected_len);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dump_bb010002 = NULL;
	const char *dump_bb010003 = NULL;
	const char *upload_bb010002 = NULL;
	const char *build_bb010002 = NULL;
	const char *build_bb010002_raw = NULL;
	const char *psk_raw32 = NULL;
	const char *out_psk_raw32 = NULL;
	const char *seed8_file = NULL;
	const char *sealed_psk_file = NULL;
	int no_pad4 = 0;
	struct gxfp_dev dev;
	int rc;

	if (argc < 2) {
		usage(argv[0]);
		return 2;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--build-bb010002") == 0 && i + 1 < argc) {
			if (argv[i + 1][0] == '-') {
				usage(argv[0]);
				fprintf(stderr, "missing output file after --build-bb010002\n");
				return 2;
			}
			build_bb010002 = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--build-bb010002-raw") == 0 && i + 1 < argc) {
			if (argv[i + 1][0] == '-') {
				usage(argv[0]);
				fprintf(stderr, "missing output file after --build-bb010002-raw\n");
				return 2;
			}
			build_bb010002_raw = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--psk-raw32") == 0 && i + 1 < argc) {
			psk_raw32 = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--seed8") == 0 && i + 1 < argc) {
			seed8_file = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--out-psk-raw32") == 0 && i + 1 < argc) {
			out_psk_raw32 = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--sealed-psk") == 0 && i + 1 < argc) {
			sealed_psk_file = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--no-pad4") == 0) {
			no_pad4 = 1;
			continue;
		}
		if (strcmp(argv[i], "--dump-bb010002") == 0 &&
		    i + 1 < argc) {
			dump_bb010002 = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--dump-bb010003") == 0 &&
		    i + 1 < argc) {
			dump_bb010003 = argv[++i];
			continue;
		}
		if (strcmp(argv[i], "--upload-bb010002") == 0 && i + 1 < argc) {
			upload_bb010002 = argv[++i];
			continue;
		}
		usage(argv[0]);
		return 2;
	}

	int action_cnt = 0;
	action_cnt += build_bb010002 != NULL;
	action_cnt += build_bb010002_raw != NULL;
	action_cnt += dump_bb010002 != NULL;
	action_cnt += dump_bb010003 != NULL;
	action_cnt += upload_bb010002 != NULL;
	if (action_cnt != 1) {
		fprintf(stderr, "need exactly one action\n");
		return 2;
	}

	if (build_bb010002 || build_bb010002_raw) {
		uint8_t *psk = NULL;
		size_t psk_len = 0;
		FILE *psk_out = NULL;
		uint8_t *seed8 = NULL;
		size_t seed8_len = 0;
		uint8_t *sealed = NULL;
		size_t sealed_len = 0;
		uint8_t *wb = NULL;
		size_t wb_len = 0;
		uint8_t *inner = NULL;
		size_t inner_len = 0;
		uint8_t *bb = NULL;
		size_t bb_len = 0;
		FILE *f;

		rc = load_or_generate_bytes(psk_raw32, 32, "psk_raw32", &psk, &psk_len);
		if (rc < 0) {
			fprintf(stderr, "load/generate psk_raw32 failed: %s\n", strerror(-rc));
			free(psk);
			return 1;
		}
		rc = load_or_generate_bytes(seed8_file, 8, "seed8", &seed8, &seed8_len);
		if (rc < 0) {
			fprintf(stderr, "load/generate seed8 failed: %s\n", strerror(-rc));
			free(psk);
			free(seed8);
			return 1;
		}

		rc = gxfp_payload_build_bb010003(psk,
						 gxfp_payload_seed32_runtime,
						 &wb,
						 &wb_len,
						 &inner,
						 &inner_len);
		free(inner);
		if (rc < 0) {
			fprintf(stderr, "build bb010003 failed: %s\n", strerror(-rc));
			free(psk);
			free(seed8);
			free(wb);
			return 1;
		}

		if (build_bb010002_raw) {
			rc = gxfp_payload_build_bb010002_raw_psk(seed8,
							 wb,
							 wb_len,
							 no_pad4 ? 0 : 1,
							 &bb,
							 &bb_len);
		} else {
			if (sealed_psk_file) {
				rc = gxfp_read_file_all(sealed_psk_file, &sealed, &sealed_len);
				if (rc < 0) {
					fprintf(stderr, "read sealed-psk failed: %s\n", strerror(-rc));
					free(psk);
					free(seed8);
					free(wb);
					return 1;
				}
			} else {
				sealed = psk;
				sealed_len = psk_len;
			}

			rc = gxfp_payload_build_bb010002(sealed,
							 sealed_len,
							 seed8,
							 wb,
							 wb_len,
							 no_pad4 ? 0 : 1,
							 &bb,
							 &bb_len);
		}

		if (rc < 0) {
			fprintf(stderr, "build bb010002 failed: %s\n", strerror(-rc));
			if (sealed && sealed != psk)
				free(sealed);
			free(psk);
			free(seed8);
			free(wb);
			free(bb);
			return 1;
		}

		f = fopen(build_bb010002 ? build_bb010002 : build_bb010002_raw, "wb");
		if (!f || fwrite(bb, 1, bb_len, f) != bb_len) {
			if (f)
				fclose(f);
			fprintf(stderr, "write bb010002 output failed\n");
			if (sealed && sealed != psk)
				free(sealed);
			free(psk);
			free(seed8);
			free(wb);
			free(bb);
			return 1;
		}
		fclose(f);

		fprintf(stderr, "built bb010002 -> %s (len=%zu)\n",
			build_bb010002 ? build_bb010002 : build_bb010002_raw,
			bb_len);

		if (out_psk_raw32) {
			psk_out = fopen(out_psk_raw32, "wb");
			if (!psk_out || fwrite(psk, 1, psk_len, psk_out) != psk_len) {
				if (psk_out)
					fclose(psk_out);
				fprintf(stderr, "write psk raw output failed\n");
				if (sealed && sealed != psk)
					free(sealed);
				free(psk);
				free(seed8);
				free(wb);
				free(bb);
				return 1;
			}
			fclose(psk_out);
		}

		if (sealed && sealed != psk)
			free(sealed);
		free(psk);
		free(seed8);
		free(wb);
		free(bb);
		return 0;
	}

	rc = gxfp_dev_open(&dev, "/dev/gxfp", O_RDWR);
	if (rc < 0) {
		fprintf(stderr, "open(/dev/gxfp): %s\n", strerror(-rc));
		return 1;
	}

	(void)gxfp_dev_flush_rxq(&dev);

	if (upload_bb010002) {
		uint8_t *blob = NULL;
		size_t blob_len = 0;
		uint32_t mcu_ret = 0xffffffffu;

		rc = gxfp_read_file_all(upload_bb010002, &blob, &blob_len);
		if (rc < 0) {
			fprintf(stderr, "read file(%s): %s\n", upload_bb010002, strerror(-rc));
			gxfp_dev_close(&dev);
			return 1;
		}

		rc = gxfp_align_blob(&blob, &blob_len);
		if (rc < 0) {
			fprintf(stderr, "align blob: %s\n", strerror(-rc));
			free(blob);
			gxfp_dev_close(&dev);
			return 1;
		}

		rc = gxfp_cmd_production_write_mcu(&dev, blob, (uint16_t)blob_len, &mcu_ret);
		if (rc < 0) {
			fprintf(stderr, "upload bb010002: %s\n", strerror(-rc));
			free(blob);
			gxfp_dev_close(&dev);
			return 1;
		}
		if (mcu_ret != 0) {
			fprintf(stderr, "upload bb010002: mcu ret 0x%x\n", mcu_ret);
			free(blob);
			gxfp_dev_close(&dev);
			return 1;
		}
		fprintf(stderr, "uploaded bb010002 from %s (size=%zu aligned)\n", upload_bb010002, blob_len);
		free(blob);
		gxfp_dev_close(&dev);
		return 0;
	}

	uint32_t dt = dump_bb010002 ? GXFP_DT_BB010002 : GXFP_DT_BB010003;
	const char *outp = dump_bb010002 ? dump_bb010002 : dump_bb010003;
	{
		uint8_t *buf = (uint8_t *)malloc(0x800u);
		uint32_t data_len = 0;
		FILE *f = NULL;

		if (!buf) {
			fprintf(stderr, "dump 0x%08x: %s\n", dt, strerror(ENOMEM));
			gxfp_dev_close(&dev);
			return 1;
		}

		rc = gxfp_cmd_preset_psk_read(&dev, dt, buf, 0x800u, &data_len);
		if (rc < 0) {
			fprintf(stderr, "dump 0x%08x: %s\n", dt, strerror(-rc));
			free(buf);
			gxfp_dev_close(&dev);
			return 1;
		}

		f = fopen(outp, "wb");
		if (!f) {
			fprintf(stderr, "dump 0x%08x: %s\n", dt, strerror(errno));
			free(buf);
			gxfp_dev_close(&dev);
			return 1;
		}

		if (fwrite(buf, 1, (size_t)data_len, f) != (size_t)data_len) {
			fprintf(stderr, "dump 0x%08x: %s\n", dt, strerror(EIO));
			fclose(f);
			free(buf);
			gxfp_dev_close(&dev);
			return 1;
		}

		fclose(f);
		free(buf);
	}
	fprintf(stderr, "dumped 0x%08x payload to %s\n", dt, outp);

	gxfp_dev_close(&dev);
	return 0;
}
