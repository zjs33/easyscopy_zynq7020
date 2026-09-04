/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef AC880_CAPTURE_DMA_UAPI_H
#define AC880_CAPTURE_DMA_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define AC880_DMA_UAPI_VERSION 1U
#define AC880_DMA_NUM_PERIODS  4U
#define AC880_DMA_PERIOD_BYTES (1024U * 1024U)
#define AC880_DMA_BUFFER_BYTES (AC880_DMA_NUM_PERIODS * AC880_DMA_PERIOD_BYTES)

#define AC880_DMA_IOC_MAGIC 'A'
#define AC880_DMA_IOC_START   _IO(AC880_DMA_IOC_MAGIC, 0)
#define AC880_DMA_IOC_STOP    _IO(AC880_DMA_IOC_MAGIC, 1)
#define AC880_DMA_IOC_GET_SEQ _IOR(AC880_DMA_IOC_MAGIC, 2, __u64)

/* Snapshot of the cyclic buffer state.  completed_index identifies the
 * period that was most recently completed by S2MM and is safe to consume
 * until the ring laps it. */
struct ac880_dma_info {
	__u32 version;
	__u32 period_bytes;
	__u32 num_periods;
	__u32 buffer_bytes;
	__u32 sample_bytes;
	__u32 running;
	__u32 completed_index;
	__u32 reserved;
	__u64 period_seq;
};

#define AC880_DMA_IOC_GET_INFO _IOR(AC880_DMA_IOC_MAGIC, 3, struct ac880_dma_info)

/* Block until period_seq differs from the supplied value, then return the
 * corresponding buffer metadata.  This is the preferred mmap consumer API. */
struct ac880_dma_wait {
	__u64 last_seq;
	struct ac880_dma_info info;
};

#define AC880_DMA_IOC_WAIT_SEQ _IOWR(AC880_DMA_IOC_MAGIC, 4, struct ac880_dma_wait)

#endif
