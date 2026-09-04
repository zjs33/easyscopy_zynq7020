/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define AC880_DMA_PERIOD_BYTES (1024U * 1024U)
#define AC880_DMA_BUFFER_BYTES (4U * AC880_DMA_PERIOD_BYTES)
#define AC880_DMA_IOC_MAGIC 'A'
struct ac880_dma_info {
	uint32_t version, period_bytes, num_periods, buffer_bytes;
	uint32_t sample_bytes, running, completed_index, reserved;
	uint64_t period_seq;
};
struct ac880_dma_wait { uint64_t last_seq; struct ac880_dma_info info; };
#define AC880_DMA_IOC_GET_INFO _IOR(AC880_DMA_IOC_MAGIC, 3, struct ac880_dma_info)
#define AC880_DMA_IOC_WAIT_SEQ _IOWR(AC880_DMA_IOC_MAGIC, 4, struct ac880_dma_wait)

int main(void)
{
	int fd = open("/dev/ac880_capture_dma", O_RDONLY);
	if (fd < 0) { perror("open"); return 1; }
	struct ac880_dma_info info;
	if (ioctl(fd, AC880_DMA_IOC_GET_INFO, &info) < 0) { perror("GET_INFO"); return 1; }
	void *map = mmap(NULL, AC880_DMA_BUFFER_BYTES, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) { perror("mmap"); return 1; }
	struct ac880_dma_wait wait = { .last_seq = info.period_seq };
	if (ioctl(fd, AC880_DMA_IOC_WAIT_SEQ, &wait) < 0) { perror("WAIT_SEQ"); return 1; }
	uint8_t *p = (uint8_t *)map + (size_t)wait.info.completed_index * wait.info.period_bytes;
	printf("version=%u period=%u periods=%u buffer=%u sample_bytes=%u seq=%llu index=%u first=%02x,%02x,%02x,%02x\n",
	       wait.info.version, wait.info.period_bytes, wait.info.num_periods,
	       wait.info.buffer_bytes, wait.info.sample_bytes,
	       (unsigned long long)wait.info.period_seq, wait.info.completed_index,
	       p[0], p[1], p[2], p[3]);
	munmap(map, AC880_DMA_BUFFER_BYTES);
	close(fd);
	return 0;
}
