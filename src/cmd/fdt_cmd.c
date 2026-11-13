#include "gxfp/cmd/fdt_cmd.h"

#include "gxfp/cmd/goodix_xfer.h"
#include "gxfp/proto/goodix_cmd.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#define GXFP_FDT_TABLE_CAP 24
#define GXFP_FDT_PAYLOAD_CAP 34

#define FDT_CMD_DOWN_MAGIC_0 0x08
#define FDT_CMD_MODE_MAGIC_0 0x09
#define FDT_CMD_UP_MAGIC_0 0x0A
#define FDT_CMD_BASE_TYPE 0x01
#define GXFP_FDT_MILANG_TRAILER_LEN 8

static uint8_t
fdt_payload_build(const struct gxfp_cmd_fdt_state *state,
		  uint8_t command,
		  const uint8_t *table,
		  uint8_t payload[GXFP_FDT_PAYLOAD_CAP],
		  int with_timestamp)
{
	size_t table_len;
	size_t len;

	if (!state || !table)
		return 0;

	table_len = state->table_len ? state->table_len : GXFP_FDT_TABLE_CAP;
	if (table_len > GXFP_FDT_TABLE_CAP)
		table_len = GXFP_FDT_TABLE_CAP;

	memset(payload, 0, GXFP_FDT_PAYLOAD_CAP);
	payload[0] = command;
	payload[1] = FDT_CMD_BASE_TYPE;
	memcpy(payload + 2, table, table_len);

	if (state->variant == GXFP_SENSOR_VARIANT_CHICAGOHS) {
		len = 2 + table_len;
		if (with_timestamp) {
			struct timespec ts;
			struct tm tm;
			uint16_t stamp = 0;

			if (clock_gettime(CLOCK_REALTIME, &ts) == 0 &&
			    localtime_r(&ts.tv_sec, &tm) != NULL)
				stamp = (uint16_t)(tm.tm_sec * 1000 +
						   ts.tv_nsec / 1000000L);
			payload[len++] = (uint8_t)(stamp & 0xff);
			payload[len++] = (uint8_t)((stamp >> 8) & 0xff);
		}
		return (uint8_t)len;
	}

	len = 2 + table_len + GXFP_FDT_MILANG_TRAILER_LEN;
	return (uint8_t)len;
}

void gxfp_cmd_fdt_state_init(struct gxfp_cmd_fdt_state *state)
{
	if (!state)
		return;

	memset(state, 0, sizeof(*state));
	state->variant = GXFP_SENSOR_VARIANT_MILANG;
	state->table_len = GXFP_FDT_TABLE_CAP;
	state->base_table_inited = 1;
}

void gxfp_cmd_fdt_state_set_variant(struct gxfp_cmd_fdt_state *state,
				    enum gxfp_sensor_variant variant)
{
	const struct gxfp_sensor_variant_info *info =
		gxfp_sensor_variant_info(variant);

	if (!state)
		return;

	state->variant = variant;
	state->table_len = info ? info->fdt_table_len : GXFP_FDT_TABLE_CAP;
	if (!state->table_len || state->table_len > GXFP_FDT_TABLE_CAP)
		state->table_len = GXFP_FDT_TABLE_CAP;
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

int gxfp_cmd_fdt_set_mode(struct gxfp_dev *dev,
			  struct gxfp_cmd_fdt_state *state)
{
	uint8_t payload[GXFP_FDT_PAYLOAD_CAP];
	uint8_t len;

	if (!dev || !state)
		return -EINVAL;

	memcpy(state->manual_table_5130, state->down_table_5130,
	       state->table_len);
	len = fdt_payload_build(state, FDT_CMD_MODE_MAGIC_0,
				state->manual_table_5130, payload, 0);
	return gxfp_goodix_send_async(dev, GXFP_CMD_FDT_MODE, payload, len);
}

int gxfp_cmd_fdt_send_down(struct gxfp_dev *dev,
			   const struct gxfp_cmd_fdt_state *state)
{
	uint8_t payload[GXFP_FDT_PAYLOAD_CAP];
	uint8_t len;

	if (!dev || !state)
		return -EINVAL;

	len = fdt_payload_build(state, FDT_CMD_DOWN_MAGIC_0,
				state->down_table_5130, payload, 1);
	return gxfp_goodix_send_async(dev, GXFP_CMD_FDT_DOWN, payload, len);
}

int gxfp_cmd_fdt_send_up(struct gxfp_dev *dev,
			 const struct gxfp_cmd_fdt_state *state)
{
	uint8_t payload[GXFP_FDT_PAYLOAD_CAP];
	uint8_t len;

	if (!dev || !state)
		return -EINVAL;

	len = fdt_payload_build(state, FDT_CMD_UP_MAGIC_0,
				state->up_table_5130, payload, 0);
	return gxfp_goodix_send_async(dev, GXFP_CMD_FDT_UP, payload, len);
}
