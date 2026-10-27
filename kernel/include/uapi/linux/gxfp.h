/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_GXFP_H
#define _UAPI_LINUX_GXFP_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Goodix GXFP5130 EC mailbox transport.
 *
 * The kernel owns the ACPI-described mailbox window, the two GpioIo strobe
 * outputs and the GpioInt doorbell.
 *
 *   write(2)  submit one raw TX frame (maximum GXFP_TX_MAX bytes).
 *             The kernel copies the frame into the TX window and pulses
 *             write-done.
 *
 *   read(2)   return one raw RX chunk. The record length is
 *             currently fixed to GXFP_RX_MAX.
 *
 *   poll(2)   report POLLIN while a complete RX record is queued.
 */

#define GXFP_IOCTL_MAGIC	0x3F

#define GXFP_TX_MAX		0x200u
#define GXFP_RX_MAX		0xE00u

/**
 * struct gxfp_rx_record - one opaque RX chunk
 * @len: number of valid bytes in @data (currently GXFP_RX_MAX)
 * @reserved: must be zero
 * @timestamp_ns: CLOCK_MONOTONIC timestamp captured before the RX read
 * @data: raw RX window bytes
 */
struct gxfp_rx_record {
	__u32 len;
	__u32 reserved;
	__u64 timestamp_ns;
	__u8 data[];
};

#define GXFP_RX_RECORD_SIZE \
	(sizeof(struct gxfp_rx_record) + GXFP_RX_MAX)

/* Discard all queued RX records. */
#define GXFP_IOCTL_FLUSH_RX	_IO(GXFP_IOCTL_MAGIC, 0x00)

#endif /* _UAPI_LINUX_GXFP_H */
