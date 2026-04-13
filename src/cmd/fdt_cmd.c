#include "gxfp/cmd/fdt_cmd.h"

#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/proto/goodix_cmd.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#define GXFP_FDT_BASE_TABLE_LEN 24
#define FDT_CMD_DOWN_MAGIC_0 0x08
#define FDT_CMD_DOWN_MAGIC_1 0x01
#define FDT_CMD_MODE_MAGIC_0 0x09
#define FDT_CMD_MODE_MAGIC_1 0x01
#define FDT_CMD_UP_MAGIC_0 0x0A
#define FDT_CMD_UP_MAGIC_1 0x01

#define FDT_PAYLOAD_SIZE_DOWN (2 + GXFP_FDT_BASE_TABLE_LEN + 2)
#define FDT_PAYLOAD_SIZE_MODE (2 + GXFP_FDT_BASE_TABLE_LEN)
#define FDT_PAYLOAD_SIZE_UP (2 + GXFP_FDT_BASE_TABLE_LEN)

static const uint8_t default_base_table[GXFP_FDT_BASE_TABLE_LEN] = {
    0x80, 0xbb,
    0x80, 0xb4,
    0x80, 0xbf,
    0x80, 0xb4,
    0x80, 0xb6,
    0x80, 0xac,
    0x80, 0xb3,
    0x80, 0xa8,
    0x80, 0xad,
    0x80, 0xa1,
    0x00, 0x00,
    0x00, 0x00,
};

void gxfp_cmd_fdt_state_init(struct gxfp_cmd_fdt_state *state)
{
    if (!state)
        return;

    memset(state, 0, sizeof(*state));
    memcpy(state->base_table_5130, default_base_table, GXFP_FDT_BASE_TABLE_LEN);
    state->base_table_inited = 1;
}

void gxfp_cmd_fdt_state_set_runtime(struct gxfp_cmd_fdt_state *state,
                                    int second_flag,
                                    int need_fdt_up_twice,
                                    int diff_use,
                                    int16_t dac_offset)
{
    if (!state)
        return;

    state->up_second_flag = second_flag ? 1 : 0;
    state->up_need_twice = need_fdt_up_twice ? 1 : 0;
    state->up_diff_use = diff_use ? 1 : 0;
    state->up_dac_offset = dac_offset;
    state->up_round = 0;
}


static uint16_t fdt_timestamp16_ms_of_minute(void)
{
    struct timespec ts;
    int64_t ms;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return 0;

    ms = ((int64_t)(ts.tv_sec % 60) * 1000) + (ts.tv_nsec / 1000000);
    if (ms < 0)
        ms = 0;
    if (ms > 59999)
        ms = 59999;

    return (uint16_t)ms;
}

static void fdt_down_payload_build(const struct gxfp_cmd_fdt_state *state,
                                   uint8_t payload[FDT_PAYLOAD_SIZE_DOWN])
{
    uint16_t ts16;

    if (!state)
        return;

    memcpy(payload + 0, (uint8_t[]){ FDT_CMD_DOWN_MAGIC_0, FDT_CMD_DOWN_MAGIC_1 }, 2);
    memcpy(payload + 2, state->base_table_5130, GXFP_FDT_BASE_TABLE_LEN);

    ts16 = fdt_timestamp16_ms_of_minute();
    payload[2 + GXFP_FDT_BASE_TABLE_LEN + 0] = (uint8_t)(ts16 & 0xff);
    payload[2 + GXFP_FDT_BASE_TABLE_LEN + 1] = (uint8_t)((ts16 >> 8) & 0xff);
}

static void fdt_mode_payload_build(const struct gxfp_cmd_fdt_state *state,
                                   uint8_t payload[FDT_PAYLOAD_SIZE_MODE])
{
    if (!state)
        return;

    memcpy(payload + 0, (uint8_t[]){ FDT_CMD_MODE_MAGIC_0, FDT_CMD_MODE_MAGIC_1 }, 2);
    memcpy(payload + 2, state->base_table_5130, GXFP_FDT_BASE_TABLE_LEN);
}

static void fdt_up_payload_build(const struct gxfp_cmd_fdt_state *state,
                                 uint8_t payload[FDT_PAYLOAD_SIZE_UP])
{
    if (!state)
        return;

    memcpy(payload + 0, (uint8_t[]){ FDT_CMD_UP_MAGIC_0, FDT_CMD_UP_MAGIC_1 }, 2);
    memcpy(payload + 2, state->base_table_5130, GXFP_FDT_BASE_TABLE_LEN);
}

int gxfp_cmd_fdt_set_mode(struct gxfp_dev *dev,
                          const struct gxfp_cmd_fdt_state *state)
{
    uint8_t payload_mode[FDT_PAYLOAD_SIZE_MODE];

    if (!dev || !state)
        return -EINVAL;

    fdt_mode_payload_build(state, payload_mode);
    return gxfp_goodix_send_async(dev,
                                  GXFP_CMD_FDT_MODE,
                                  payload_mode,
                                  (uint16_t)FDT_PAYLOAD_SIZE_MODE);
}

int gxfp_cmd_fdt_send_down(struct gxfp_dev *dev,
                           const struct gxfp_cmd_fdt_state *state)
{
    uint8_t payload_down[FDT_PAYLOAD_SIZE_DOWN];

    if (!dev || !state)
        return -EINVAL;

    fdt_down_payload_build(state, payload_down);
    return gxfp_goodix_send_async(dev,
                                  GXFP_CMD_FDT_DOWN,
                                  payload_down,
                                  (uint16_t)FDT_PAYLOAD_SIZE_DOWN);
}

int gxfp_cmd_fdt_send_up(struct gxfp_dev *dev,
                         const struct gxfp_cmd_fdt_state *state)
{
    uint8_t payload_up[FDT_PAYLOAD_SIZE_UP];

    if (!dev || !state)
        return -EINVAL;

    fdt_up_payload_build(state, payload_up);
    return gxfp_goodix_send_async(dev,
                                  GXFP_CMD_FDT_UP,
                                  payload_up,
                                  (uint16_t)FDT_PAYLOAD_SIZE_UP);
}
