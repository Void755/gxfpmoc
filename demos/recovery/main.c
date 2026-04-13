#include "gxfp/flow/device_recovery.h"
#include "gxfp/io/dev.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage:\n"
		"  %s [--no-unstick] [--reset-mcu] [--reset-sensor]\n",
		argv0);
}

int main(int argc, char **argv)
{
	struct gxfp_dev dev;
	int unstick_tls = 1;
	int reset_mcu = 0;
	int reset_sensor = 0;
	int r;
	int i;

	if (argc < 2) {
		usage(argv[0]);
		return 2;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--no-unstick") == 0) {
			unstick_tls = 0;
			continue;
		}
		if (strcmp(argv[i], "--reset-mcu") == 0) {
			reset_mcu = 1;
			continue;
		}
		if (strcmp(argv[i], "--reset-sensor") == 0) {
			reset_sensor = 1;
			continue;
		}
		usage(argv[0]);
		return 2;
	}

	memset(&dev, 0, sizeof(dev));
	r = gxfp_dev_open(&dev, "/dev/gxfp", O_RDWR | O_NONBLOCK);
	if (r < 0) {
		fprintf(stderr, "open(/dev/gxfp) failed: %s\n", strerror(-r));
		return 1;
	}

	r = gxfp_device_recovery(&dev, unstick_tls, reset_mcu, reset_sensor);
	if (r < 0) {
		fprintf(stderr, "recovery failed: %s\n", strerror(-r));
		gxfp_dev_close(&dev);
		return 1;
	}

	fprintf(stderr,
		"recovery complete (unstick=%d, reset_mcu=%d, reset_sensor=%d)\n",
		unstick_tls,
		reset_mcu,
		reset_sensor);
	gxfp_dev_close(&dev);
	return 0;
}
