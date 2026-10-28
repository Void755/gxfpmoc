#include "gxfp/cmd/mode_cmd.h"

#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/proto/goodix_cmd.h"
#include "gxfp/proto/goodix_proto.h"
#include "gxfp/proto/mp_proto.h"

#include <errno.h>

int gxfp_cmd_image_mode_send(struct gxfp_dev *dev)
{
    const uint8_t payload[2] = { 0x01, 0x00 };

    if (!dev)
        return -EINVAL;

    return gxfp_goodix_send_async(dev, GXFP_CMD_GET_IMAGE,
                                  payload, (uint16_t)sizeof(payload));
}

int gxfp_cmd_image_mode_wait(struct gxfp_dev *dev, int timeout_ms)
{
    uint8_t rx[GXFP_MP_FRAME_MAX];
    uint32_t rx_len = 0;
    struct gxfp_frame_parsed frame;
    int r;

    if (!dev)
        return -EINVAL;

    r = gxfp_goodix_wait_selected(dev,
                                  GXFP_CMD_GET_IMAGE,
                                  rx,
                                  (uint32_t)sizeof(rx),
                                  &rx_len,
                                  timeout_ms > 0 ? timeout_ms : 500,
                                  NULL,
                                  NULL);
    if (r < 0)
        return r;
    if (!gxfp_parse_goodix_body(rx, rx_len, &frame) ||
        frame.cmd != GXFP_CMD_GET_IMAGE)
        return -EBADMSG;
    return 0;
}

int gxfp_cmd_image_mode_sync(struct gxfp_dev *dev, int timeout_ms)
{
    int r;

    r = gxfp_cmd_image_mode_send(dev);
    if (r < 0)
        return r;
    return gxfp_cmd_image_mode_wait(dev, timeout_ms);
}

int gxfp_cmd_get_image(struct gxfp_dev *dev)
{
    return gxfp_cmd_image_mode_send(dev);
}

int gxfp_cmd_set_idle(struct gxfp_dev *dev)
{
    const uint8_t payload[2] = { 0x14, 0x00 };
    uint8_t rx[GXFP_MP_FRAME_MAX];
    uint32_t rx_len = 0;
    struct gxfp_frame_parsed frame;
    int r;

    if (!dev)
        return -EINVAL;

    r = gxfp_goodix_request_selected(dev,
                                     GXFP_CMD_SET_IDLE,
                                     GXFP_CMD_ACK,
                                     payload,
                                     (uint16_t)sizeof(payload),
                                     rx,
                                     (uint32_t)sizeof(rx),
                                     &rx_len,
                                     1,
                                     200,
                                     NULL,
                                     NULL);
    if (r < 0)
        return r;
    if (!gxfp_parse_goodix_body(rx, rx_len, &frame) ||
        frame.cmd != GXFP_CMD_ACK)
        return -EBADMSG;
    return 0;
}
