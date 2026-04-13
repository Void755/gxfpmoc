#include "gxfp/cmd/tls_cmd.h"

#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/proto/goodix_cmd.h"

#include <errno.h>

int gxfp_cmd_tls_server_init(struct gxfp_dev *dev)
{
    uint8_t payload[2] = { 0x00, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                      GXFP_CMD_TLS_SERVER_INIT,
                                      payload,
                                      (uint16_t)sizeof(payload));
}

int gxfp_cmd_tls_server_exit(struct gxfp_dev *dev)
{
    uint8_t payload[2] = { 0x00, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                  GXFP_CMD_TLS_SERVER_EXIT,
                                  payload,
                                  (uint16_t)sizeof(payload));
}

int gxfp_cmd_get_image(struct gxfp_dev *dev)
{
    uint8_t payload[2] = { 0x01, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                      GXFP_CMD_GET_IMAGE,
                                      payload,
                                      (uint16_t)sizeof(payload));
}

int gxfp_cmd_tls_unlock(struct gxfp_dev *dev)
{
    uint8_t payload[2] = { 0x00, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev, GXFP_CMD_TLS_UNLOCK, payload, (uint16_t)sizeof(payload));
}

int gxfp_cmd_tls_unlock_force(struct gxfp_dev *dev)
{
    uint8_t payload[2] = { 0x00, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev,
                                  GXFP_CMD_TLS_UNLOCK_FORCE,
                                  payload,
                                  (uint16_t)sizeof(payload));
}
