#include "gxfp/cmd/device_recovery_cmd.h"

#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/proto/goodix_cmd.h"

#include <errno.h>

#define GXFP_RESET_DELAY_MAGIC 0x14

int gxfp_cmd_notify_power_state(struct gxfp_dev *dev)
{
    const uint8_t power_state = 0x01;
    uint8_t payload[1] = { power_state };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                      GXFP_CMD_NOTIFY_POWER_STATE,
                                      payload,
                                      (uint16_t)sizeof(payload));
}

int gxfp_cmd_send_nop(struct gxfp_dev *dev)
{
    uint8_t payload[4] = { 0x00, 0x00, 0x00, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                      GXFP_CMD_SEND_NOP,
                                      payload,
                                      (uint16_t)sizeof(payload));
}

int gxfp_cmd_set_sleep_mode(struct gxfp_dev *dev)
{
    uint8_t payload[2] = { 0x00, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                      GXFP_CMD_SET_SLEEP_MODE,
                                      payload,
                                      (uint16_t)sizeof(payload));
}

int gxfp_cmd_hard_reset_mcu(struct gxfp_dev *dev, uint8_t reset_flag)
{
    uint8_t payload[2] = { reset_flag, GXFP_RESET_DELAY_MAGIC };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                      GXFP_CMD_HARD_RESET_MCU,
                                      payload,
                                      (uint16_t)sizeof(payload));
}
