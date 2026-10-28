#include "gxfp/cmd/version_cmd.h"

#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/proto/goodix_cmd.h"
#include "gxfp/proto/goodix_proto.h"
#include "gxfp/proto/mp_proto.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

int gxfp_cmd_read_firmware_version(struct gxfp_dev *dev,
                                  char *version,
                                  size_t version_cap,
                                  size_t *out_len)
{
    const uint8_t req_payload[2] = { 0x00, 0x00 };
    uint8_t rx[GXFP_MP_FRAME_MAX];
    uint32_t rx_len = 0;
    struct gxfp_frame_parsed frame;
    size_t n;
    int rc;

    if (!dev || !version || version_cap == 0)
        return -EINVAL;
    version[0] = '\0';
    if (out_len)
        *out_len = 0;

    rc = gxfp_goodix_request_selected(dev,
                                      GXFP_CMD_FIRMWARE_VERSION,
                                      GXFP_CMD_FIRMWARE_VERSION,
                                      req_payload,
                                      (uint16_t)sizeof(req_payload),
                                      rx,
                                      (uint32_t)sizeof(rx),
                                      &rx_len,
                                      3,
                                      500,
                                      NULL,
                                      NULL);
    if (rc != 0)
        return rc;

    if (!gxfp_parse_goodix_body(rx, rx_len, &frame) || !frame.valid)
        return -EBADMSG;
    if (frame.cmd != GXFP_CMD_FIRMWARE_VERSION)
        return -EBADMSG;
    if (!frame.payload || frame.payload_len == 0)
        return -ENODATA;
    if (frame.payload_len >= version_cap)
        return -EMSGSIZE;

    memcpy(version, frame.payload, frame.payload_len);
    version[frame.payload_len] = '\0';

    n = 0;
    while (n < frame.payload_len && version[n] != '\0')
        n++;
    if (out_len)
        *out_len = n;

    return 0;
}
