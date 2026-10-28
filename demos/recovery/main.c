#include "gxfp/cmd/device_recovery_cmd.h"
#include "gxfp/cmd/mcu_state_cmd.h"
#include "gxfp/cmd/register_cmd.h"
#include "gxfp/cmd/mode_cmd.h"
#include "gxfp/cmd/tls_cmd.h"
#include "gxfp/cmd/version_cmd.h"
#include "gxfp/flow/device_recovery.h"
#include "gxfp/io/dev.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [info]\n"
		"	   %s reset [options]\n"
		"\n"
		"Commands:\n"
		"  info			print chip id, MCU firmware version and MCU status\n"
		"  reset		   run device recovery\n"
		"\n"
		"Reset options:\n"
		"  --no-unstick    skip TLS un-sticking\n"
		"  --reset-mcu     reset MCU during recovery\n"
		"  --reset-sensor  reset sensor during recovery\n"
		"  -h, --help      show this help\n",
		argv0, argv0);
}

static void print_info_error(const char *what, int rc)
{
	fprintf(stderr, "%s failed: %s\n", what, strerror(-rc));
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

static int read_chip_id_stable(struct gxfp_dev *dev, uint16_t *out_chip_id)
{
	enum { ATTEMPTS = 3, NOPS_PER_FAILURE = 3 };
	int last_error = -ENODATA;
	int attempt;
	int i;
	int r;

	for (attempt = 0; attempt < ATTEMPTS; attempt++) {
		uint16_t chip_id = 0;

		r = gxfp_cmd_reset_device(dev, 0x01);
		if (r < 0) {
			last_error = r;
			goto retry;
		}

		(void)gxfp_cmd_get_image(dev);
		sleep_ms(1);
		(void)gxfp_dev_flush_rxq(dev);

		r = gxfp_cmd_read_chip_id(dev, &chip_id);
		if (r == 0 && chip_id != 0x0000 && chip_id != 0xffff) {
			*out_chip_id = chip_id;
			return 0;
		}
		last_error = (r < 0) ? r : -ENODATA;

retry:
		for (i = 0; i < NOPS_PER_FAILURE; i++)
			(void)gxfp_cmd_send_nop(dev);
	}

	return last_error;
}

static void print_mcu_state(const struct gxfp_mcu_state *state)
{
	printf("mcu state:\n");
	printf("  %-20s%u\n", "version:", state->version);
	printf("  %-20s%u\n", "pov_valid:", state->is_pov_image_valid);
	printf("  %-20s%u\n", "tls_connected:", state->is_tls_connected);
	printf("  %-20s%u\n", "locked:", state->is_locked);
	printf("  %-20s%u(%u)\n", "captured_num:",
	       state->captured_num_hi, state->captured_num_lo);
	printf("  %-20s%u\n", "pov:", state->flags9_pov);
	printf("  %-20s%u\n", "pba:", state->flags9_pba);
	printf("  %-20s%u\n", "tls_session:", state->flags9_tls_session);
	printf("  %-20s0x%02x\n", "byte9_raw:", state->byte9_raw);
	printf("  %-20s0x%02x\n", "byte15_raw:", state->byte15_raw);
	printf("  %-20s%02x %02x %02x %02x %02x %02x\n", "raw[3..8]:",
	       state->raw_3_8[0], state->raw_3_8[1], state->raw_3_8[2],
	       state->raw_3_8[3], state->raw_3_8[4], state->raw_3_8[5]);
	printf("  %-20s%02x %02x %02x %02x %02x\n", "raw[10..14]:",
	       state->raw_10_14[0], state->raw_10_14[1], state->raw_10_14[2],
	       state->raw_10_14[3], state->raw_10_14[4]);
	printf("  %-20s%02x %02x %02x %02x\n", "raw[16..19]:",
	       state->raw_16_19[0], state->raw_16_19[1],
	       state->raw_16_19[2], state->raw_16_19[3]);
}

static int print_device_info(struct gxfp_dev *dev)
{
	struct gxfp_mcu_state state;
	char version[GXFP_EVK_VERSION_MAX + 1];
	uint16_t chip_id = 0;
	size_t version_len = 0;
	int info_rc = 0;
	int r;

	r = read_chip_id_stable(dev, &chip_id);
	if (r < 0) {
		print_info_error("read chip id", r);
		info_rc = 1;
	} else {
		printf("chip id: 0x%04x\n", chip_id);
	}

	r = gxfp_cmd_read_firmware_version(dev, version, sizeof(version),
									   &version_len);
	if (r < 0) {
		print_info_error("read firmware version", r);
		info_rc = 1;
	} else {
		printf("firmware version: %s\n", version);
	}

	r = gxfp_mcu_state_query(dev, &state);
	if (r < 0) {
		print_info_error("query mcu status", r);
		info_rc = 1;
	} else {
		print_mcu_state(&state);
	}

	return info_rc;
}

static int run_info(void)
{
	struct gxfp_dev dev;
	int r;

	memset(&dev, 0, sizeof(dev));
	r = gxfp_dev_open(&dev, "/dev/gxfp", O_RDWR | O_NONBLOCK);
	if (r < 0) {
		fprintf(stderr, "open(/dev/gxfp) failed: %s\n", strerror(-r));
		return 1;
	}

	(void)gxfp_dev_flush_rxq(&dev);
	r = print_device_info(&dev);
	gxfp_dev_close(&dev);
	return r ? 1 : 0;
}

static int run_reset(int unstick_tls, int reset_mcu, int reset_sensor)
{
	struct gxfp_dev dev;
	int r;

	memset(&dev, 0, sizeof(dev));
	r = gxfp_dev_open(&dev, "/dev/gxfp", O_RDWR | O_NONBLOCK);
	if (r < 0) {
		fprintf(stderr, "open(/dev/gxfp) failed: %s\n", strerror(-r));
		return 1;
	}

	r = gxfp_device_recovery(&dev, unstick_tls, reset_mcu, reset_sensor);
	if (r < 0) {
		fprintf(stderr, "reset failed: %s\n", strerror(-r));
		gxfp_dev_close(&dev);
		return 1;
	}

	fprintf(stderr,
		"reset complete (unstick=%d, reset_mcu=%d, reset_sensor=%d)\n",
		unstick_tls, reset_mcu, reset_sensor);
	gxfp_dev_close(&dev);
	return 0;
}

int main(int argc, char **argv)
{
	const char *command = "info";
	int unstick_tls = 1;
	int reset_mcu = 0;
	int reset_sensor = 0;
	int argi = 1;
	int opt;

	struct option long_opts[] = {
		{"no-unstick",   no_argument, NULL, 'U'},
		{"reset-mcu",	no_argument, NULL, 'M'},
		{"reset-sensor", no_argument, NULL, 'S'},
		{"help",		 no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	if (argc > 1 && argv[1][0] != '-') {
		command = argv[1];
		argi = 2;
	}

	if (strcmp(command, "info") != 0 && strcmp(command, "reset") != 0) {
		fprintf(stderr, "unknown command: %s\n", command);
		usage(argv[0]);
		return 2;
	}

	optind = argi;
	while ((opt = getopt_long(argc, argv, "UMS", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'U':
			if (strcmp(command, "reset") != 0)
				goto invalid_option;
			unstick_tls = 0;
			break;
		case 'M':
			if (strcmp(command, "reset") != 0)
				goto invalid_option;
			reset_mcu = 1;
			break;
		case 'S':
			if (strcmp(command, "reset") != 0)
				goto invalid_option;
			reset_sensor = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			goto invalid_option;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "unexpected argument: %s\n", argv[optind]);
		usage(argv[0]);
		return 2;
	}

	if (strcmp(command, "reset") == 0)
		return run_reset(unstick_tls, reset_mcu, reset_sensor);

	return run_info();

invalid_option:
	fprintf(stderr, "invalid option for '%s'\n", command);
	usage(argv[0]);
	return 2;
}
