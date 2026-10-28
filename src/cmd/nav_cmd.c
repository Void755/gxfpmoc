#include "gxfp/cmd/nav_cmd.h"

#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/proto/goodix_cmd.h"
#include "gxfp/proto/goodix_proto.h"
#include "gxfp/proto/mp_proto.h"

#include <errno.h>
#include <string.h>

int gxfp_cmd_nav_frame(struct gxfp_dev *dev,
                       int first,
                       int timeout_ms,
                       uint8_t *payload,
                       size_t payload_cap,
                       size_t *payload_len)
{
    const uint8_t req_payload[2] = { 0x01, 0x00 };
    const uint8_t cmd = first ? GXFP_CMD_NAV_FRAME_FIRST :
                                GXFP_CMD_NAV_FRAME_NEXT;
    uint8_t rx[GXFP_MP_FRAME_MAX];
    uint32_t rx_len = 0;
    struct gxfp_frame_parsed frame;
    int rc;

    if (!dev || !payload || payload_cap == 0)
        return -EINVAL;
    if (payload_len)
        *payload_len = 0;

    rc = gxfp_goodix_request_selected(dev,
                                      cmd,
                                      cmd,
                                      req_payload,
                                      (uint16_t)sizeof(req_payload),
                                      rx,
                                      (uint32_t)sizeof(rx),
                                      &rx_len,
                                      1,
                                      timeout_ms > 0 ? timeout_ms : 500,
                                      NULL,
                                      NULL);
    if (rc != 0)
        return rc;

    if (!gxfp_parse_goodix_body(rx, rx_len, &frame) || !frame.valid)
        return -EBADMSG;
    if (frame.cmd != cmd)
        return -EBADMSG;
    if (!frame.payload || frame.payload_len == 0)
        return -ENODATA;
    if (frame.payload_len > payload_cap)
        return -EMSGSIZE;

    memcpy(payload, frame.payload, frame.payload_len);
    if (payload_len)
        *payload_len = frame.payload_len;

    return 0;
}
