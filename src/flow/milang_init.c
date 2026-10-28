#include "gxfp/flow/milang_init.h"

#include "gxfp/cmd/device_recovery_cmd.h"
#include "gxfp/cmd/mode_cmd.h"
#include "gxfp/cmd/sensor_cfg_cmd.h"

#include <errno.h>
#include <unistd.h>

#define GXFP_MILANG_RESET_SETTLE_US 1000
#define GXFP_MILANG_IMAGE_EVENT_TIMEOUT_MS 500

int gxfp_milang_prepare_config(struct gxfp_dev *dev, uint16_t dac)
{
    int r;

    if (!dev)
        return -EINVAL;

    r = gxfp_cmd_reset_device(dev, 0x01);
    if (r < 0)
        return r;
    usleep(GXFP_MILANG_RESET_SETTLE_US);

    r = gxfp_cmd_set_dac(dev, dac);
    if (r < 0)
        return r;

    r = gxfp_cmd_reset_device(dev, 0x01);
    if (r < 0)
        return r;
    usleep(GXFP_MILANG_RESET_SETTLE_US);

    r = gxfp_cmd_image_mode_send(dev);
    if (r < 0)
        return r;

    r = gxfp_cmd_image_mode_wait(dev, GXFP_MILANG_IMAGE_EVENT_TIMEOUT_MS);
    if (r == -ETIMEDOUT) {
        return 0;
    }
    return r;
}
