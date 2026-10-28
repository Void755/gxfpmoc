#define _GNU_SOURCE

#include "gxfp_tool.h"
#include "gxfp/algo/common.h"

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

static int ensure_outdir(const char *outdir)
{
	if (!outdir || outdir[0] == '\0')
		return -EINVAL;
	if (mkdir(outdir, 0755) < 0 && errno != EEXIST)
		return -errno;
	return 0;
}

static int save_image_outputs(const char *outdir,
			      const struct gxfp_decoded_image *img)
{
	char path[512];
	int rc;

	if (!img)
		return -EINVAL;

	snprintf(path, sizeof(path), "%s/finger.pgm", outdir);
	rc = gxfp_write_pgm_u16(path, img->cols, img->rows, img->pixels, 4095);
	if (rc < 0)
		return rc;

	fprintf(stderr, "capture ok -> %s (%dx%d)\n",
		path, img->cols, img->rows);
	return 0;
}

static int run_pov_dump(struct gxfp_tool *tool, const char *outdir)
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
		char path[512];
		int rc;

		rc = gxfp_tool_pov_image(tool, i, &img);
		if (rc < 0) {
			fprintf(stderr, "pov[%d]: decode failed: %s\n",
				i, strerror(-rc));
			continue;
		}

		snprintf(path, sizeof(path), "%s/pov-%d.pgm", outdir, i);
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

static void sleep_ms(long ms)
{
	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = (ms % 1000) * 1000000L,
	};

	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

static int run_nav_capture(struct gxfp_tool *tool, int frames,
			   int interval_ms, const char *outdir)
{
	int saved = 0;

	for (int i = 0; i < frames; i++) {
		struct gxfp_decoded_image img;
		char path[512];
		int rc;

		rc = gxfp_tool_nav_frame(tool, i == 0, 1000, &img);
		if (rc < 0) {
			fprintf(stderr, "nav[%d]: capture failed: %s\n",
				i, strerror(-rc));
			break;
		}

		snprintf(path, sizeof(path), "%s/nav-%03d.pgm", outdir, i);
		rc = gxfp_write_pgm_u16(path, img.cols, img.rows,
					img.pixels, 4095);
		if (rc < 0) {
			fprintf(stderr, "nav[%d]: save %s failed: %s\n",
				i, path, strerror(-rc));
			gxfp_decoded_image_free(&img);
			break;
		}

		fprintf(stderr, "nav[%d]: saved %s (%dx%d)\n",
			i, path, img.cols, img.rows);
		gxfp_decoded_image_free(&img);
		saved++;

		if (i + 1 < frames && interval_ms > 0)
			sleep_ms(interval_ms);
	}

	return saved > 0 ? 0 : 1;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s --psk-raw32 <path> [--pov] [--no-fpn] [--outdir DIR]\n"
		"       %s --nav [--frames N] [--interval-ms N] [--outdir DIR]\n"
		"\n"
		"Capture modes:\n"
		"  default              interactive fingerprint image capture\n"
		"  --pov                capture boot POV frames only\n"
		"  --nav                capture a sequence of NAV frames only\n"
		"\n"
		"Options:\n"
		"  --psk-raw32 <path>   path to 32-byte raw PSK (required except --nav)\n"
		"  --no-fpn             disable dark-frame/FPN correction\n"
		"  --frames N           NAV frame count (default: 10)\n"
		"  --interval-ms N      delay between NAV frames (default: 20)\n"
		"  --outdir DIR         output directory for all modes (default: .)\n"
		"  -h, --help           show this help\n"
		"\n"
		"Output:\n"
		"  finger.pgm           16-bit PGM fingerprint image\n"
		"  pov-<n>.pgm          16-bit PGM POV image (POV mode)\n"
		"  nav-<n>.pgm          16-bit PGM NAV image (NAV mode)\n",
		argv0, argv0);
}

int main(int argc, char **argv)
{
	const char *psk_raw32_file = NULL;
	uint8_t *psk_raw = NULL;
	size_t psk_raw_len = 0;
	uint32_t timeout_ms = 20000;
	int pov_mode = 0;
	int nav_mode = 0;
	int fpn_enabled = 1;
	int nav_frames = 10;
	int nav_interval_ms = 20;
	const char *outdir = ".";
	struct gxfp_tool tool = { 0 };
	int rc;

	enum {
		OPT_POV = 1000,
		OPT_NAV,
		OPT_NO_FPN,
		OPT_FRAMES,
		OPT_INTERVAL,
		OPT_OUTDIR,
	};

	struct option long_opts[] = {
		{"psk-raw32", required_argument, NULL, 'p'},
		{"pov",        no_argument,       NULL, OPT_POV},
		{"nav",        no_argument,       NULL, OPT_NAV},
		{"no-fpn",     no_argument,       NULL, OPT_NO_FPN},
		{"frames",     required_argument, NULL, OPT_FRAMES},
		{"interval-ms",required_argument, NULL, OPT_INTERVAL},
		{"outdir",     required_argument, NULL, OPT_OUTDIR},
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
		case OPT_NAV:
			nav_mode = 1;
			break;
		case OPT_NO_FPN:
			fpn_enabled = 0;
			break;
		case OPT_FRAMES:
			nav_frames = atoi(optarg);
			break;
		case OPT_INTERVAL:
			nav_interval_ms = atoi(optarg);
			break;
		case OPT_OUTDIR:
			outdir = optarg;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	if (!nav_mode && !psk_raw32_file) {
		fprintf(stderr, "Missing required option: --psk-raw32\n");
		usage(argv[0]);
		return 2;
	}
	if (nav_frames <= 0 || nav_interval_ms < 0) {
		fprintf(stderr, "invalid NAV frame count or interval\n");
		usage(argv[0]);
		return 2;
	}

	rc = ensure_outdir(outdir);
	if (rc < 0) {
		fprintf(stderr, "create output directory '%s' failed: %s\n",
			outdir, strerror(-rc));
		return 1;
	}

	if (psk_raw32_file) {
		rc = gxfp_read_file_all(psk_raw32_file, &psk_raw, &psk_raw_len);
		if (rc < 0) {
			fprintf(stderr, "read PSK '%s' failed: %s\n",
				psk_raw32_file, strerror(-rc));
			return 1;
		}
	}

	rc = gxfp_tool_open(&tool, "/dev/gxfp",
			    nav_mode ? NULL : psk_raw,
			    nav_mode ? 0 : psk_raw_len);
	free(psk_raw);
	psk_raw = NULL;
	if (rc < 0) {
		fprintf(stderr, "open /dev/gxfp failed: %s\n", strerror(-rc));
		return 1;
	}

	rc = gxfp_tool_set_fpn_enabled(&tool, fpn_enabled);
	if (rc < 0) {
		fprintf(stderr, "configure FPN failed: %s\n", strerror(-rc));
		gxfp_tool_close(&tool);
		return 1;
	}

	if (nav_mode) {
		rc = run_nav_capture(&tool, nav_frames, nav_interval_ms,
				     outdir);
		gxfp_tool_close(&tool);
		return rc;
	}

	if (pov_mode) {
		rc = gxfp_tool_connect(&tool, (int)timeout_ms);
		if (rc == 0)
			rc = gxfp_tool_pov_capture(&tool, 3000);
		if (rc < 0)
			fprintf(stderr, "pov capture failed: %s\n", strerror(-rc));
		else
			rc = run_pov_dump(&tool, outdir);
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

		rc = save_image_outputs(outdir, &img);
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
