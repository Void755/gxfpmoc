#define _GNU_SOURCE

#include "gxfp_tool.h"
#include "gxfp/algo/common.h"

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gxfp_write_pgm_u16(const char *path, int cols, int rows,
		       const uint16_t *pixels, int maxval)
{
	FILE *f;

	if (!path || !pixels || rows <= 0 || cols <= 0)
		return -EINVAL;
	if (maxval <= 0 || maxval > 65535)
		return -EINVAL;

	f = fopen(path, "wb");
	if (!f)
		return -errno;

	if (fprintf(f, "P5\n%d %d\n%d\n", cols, rows, maxval) < 0) {
		fclose(f);
		return -EIO;
	}

	if (maxval <= 255) {
		uint8_t *row = (uint8_t *)malloc((size_t)cols);
		if (!row) {
			fclose(f);
			return -ENOMEM;
		}
		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++)
				row[c] = (uint8_t)(pixels[r * cols + c] & 0xFF);
			if (fwrite(row, 1, (size_t)cols, f) != (size_t)cols) {
				free(row);
				fclose(f);
				return -EIO;
			}
		}
		free(row);
	} else {
		uint8_t *row = (uint8_t *)malloc((size_t)cols * 2);
		if (!row) {
			fclose(f);
			return -ENOMEM;
		}
		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++) {
				uint16_t v = pixels[r * cols + c];
				row[c * 2]     = (uint8_t)((v >> 8) & 0xFF);
				row[c * 2 + 1] = (uint8_t)(v & 0xFF);
			}
			if (fwrite(row, 1, (size_t)cols * 2, f) != (size_t)cols * 2) {
				free(row);
				fclose(f);
				return -EIO;
			}
		}
		free(row);
	}

	fclose(f);
	return 0;
}

static int save_image_outputs(const struct gxfp_decoded_image *img)
{
	int rc;

	if (!img)
		return -EINVAL;

	rc = gxfp_write_pgm_u16("finger.pgm", img->cols, img->rows, img->pixels, 4095);
	if (rc < 0)
		return rc;

	fprintf(stderr, "capture ok -> finger.pgm (%dx%d)\n", img->cols, img->rows);
	return 0;
}

static int run_pov_dump(struct gxfp_tool *tool)
{
	int count;
	int saved = 0;

	count = gxfp_tool_pov_count(tool);
	if (count < 0) {
		fprintf(stderr, "pov: count failed: %s\n", strerror(-count));
		return 1;
	}

	fprintf(stderr, "pov: count=%d\n", count);

	if (count == 0) {
		fprintf(stderr, "pov: no frames (MCU has no valid boot capture)\n");
		return 3;
	}

	for (int i = 0; i < count; i++) {
		struct gxfp_decoded_image img;
		char path[64];
		int rc;

		rc = gxfp_tool_pov_image(tool, i, &img);
		if (rc < 0) {
			fprintf(stderr, "pov[%d]: decode failed: %s\n",
				i, strerror(-rc));
			continue;
		}

		snprintf(path, sizeof(path), "pov-%d.pgm", i);
		rc = gxfp_write_pgm_u16(path, img.cols, img.rows,
					img.pixels, 4095);
		if (rc < 0) {
			fprintf(stderr, "pov[%d]: save %s failed: %s\n",
				i, path, strerror(-rc));
			gxfp_decoded_image_free(&img);
			continue;
		}

		fprintf(stderr, "pov[%d]: saved %s (%dx%d)\n",
			i, path, img.cols, img.rows);
		gxfp_decoded_image_free(&img);
		saved++;
	}

	return saved > 0 ? 0 : 3;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s --psk-raw32 <path> [--pov]\n"
		"\n"
		"Options:\n"
		"  --psk-raw32 <path>   path to 32-byte raw PSK binary file (required)\n"
		"  --pov                capture boot POV frames only\n"
		"  -h, --help           show this help\n"
		"\n"
		"Output:\n"
		"  finger.pgm           16-bit PGM fingerprint image\n"
		"  pov-<n>.pgm          16-bit PGM POV image (POV mode)\n",
		argv0);
}

int main(int argc, char **argv)
{
	const char *psk_raw32_file = NULL;
	uint8_t *psk_raw = NULL;
	size_t psk_raw_len = 0;
	uint32_t timeout_ms = 20000;
	int pov_mode = 0;
	struct gxfp_tool tool = { 0 };
	int rc;

	enum {
		OPT_POV = 1000,
	};

	struct option long_opts[] = {
		{"psk-raw32", required_argument, NULL, 'p'},
		{"pov",        no_argument,       NULL, OPT_POV},
		{"help",      no_argument,       NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "p:h", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'p':
			psk_raw32_file = optarg;
			break;
		case OPT_POV:
			pov_mode = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	if (!psk_raw32_file) {
		fprintf(stderr, "Missing required option: --psk-raw32\n");
		usage(argv[0]);
		return 2;
	}

	rc = gxfp_read_file_all(psk_raw32_file, &psk_raw, &psk_raw_len);
	if (rc < 0) {
		fprintf(stderr, "read PSK '%s' failed: %s\n", psk_raw32_file, strerror(-rc));
		return 1;
	}

	rc = gxfp_tool_open(&tool, "/dev/gxfp", psk_raw, psk_raw_len);
	free(psk_raw);
	if (rc < 0) {
		fprintf(stderr, "open /dev/gxfp failed: %s\n", strerror(-rc));
		return 1;
	}

	if (pov_mode) {
		rc = gxfp_tool_connect(&tool, (int)timeout_ms);
		if (rc == 0)
			rc = gxfp_tool_pov_capture(&tool, 3000);
		if (rc < 0)
			fprintf(stderr, "pov capture failed: %s\n", strerror(-rc));
		else
			rc = run_pov_dump(&tool);
		gxfp_tool_close(&tool);
		return rc < 0 ? 1 : rc;
	}

	rc = gxfp_tool_activate(&tool, (int)timeout_ms);
	if (rc < 0) {
		fprintf(stderr, "activate failed: %s\n", strerror(-rc));
		gxfp_tool_close(&tool);
		return 1;
	}

	for (;;) {
		struct gxfp_decoded_image img;

		fprintf(stderr, "Press Enter to capture.\n");
		if (getchar() != '\n')
			continue;

		fprintf(stderr, "FDT armed, waiting finger down...\n");
		rc = gxfp_tool_wait_finger(&tool, 1, 0);
		if (rc < 0) {
			fprintf(stderr, "wait finger down: %s\n", strerror(-rc));
			break;
		}
		fprintf(stderr, "FDT down detected. capture starts now.\n");
		rc = gxfp_tool_capture(&tool, (int)timeout_ms, &img);
		if (rc < 0) {
			fprintf(stderr, "capture: %s\n", strerror(-rc));
			break;
		}

		rc = save_image_outputs(&img);
		gxfp_decoded_image_free(&img);
		if (rc < 0) {
			fprintf(stderr, "save_image: %s\n", strerror(-rc));
			break;
		}

		fprintf(stderr, "waiting finger up after capture...\n");
		rc = gxfp_tool_wait_finger(&tool, 0, 0);
		if (rc < 0) {
			fprintf(stderr, "wait finger up: %s\n", strerror(-rc));
			break;
		}

		fprintf(stderr, "FDT up detected, ready for next capture.\n\n");
	}

	gxfp_tool_close(&tool);
	return rc < 0 ? 1 : 0;
}
