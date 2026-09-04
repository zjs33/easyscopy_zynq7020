/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define REG_ID          0x00
#define REG_VERSION     0x04
#define REG_CONTROL     0x08
#define REG_MODE        0x0c
#define REG_SAMPLE_DIV  0x10
#define REG_TRIG_LEVEL  0x14
#define REG_FRAME       0x18
#define REG_PRETRIG     0x1c
#define REG_BLOCK       0x20
#define REG_AUTO        0x24
#define REG_STATUS      0x28
#define REG_SEQ         0x2c
#define REG_SAMPLE_LO   0x30
#define REG_SAMPLE_HI   0x34
#define REG_OUTPUT_LO   0x38
#define REG_OUTPUT_HI   0x3c
#define REG_TRIGGER     0x40
#define REG_OVERFLOW    0x44

static void *g_map;
static size_t g_map_len;

static volatile uint32_t *map_regs(const char *path, int *fd_out, int *is_mem)
{
    int fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    size_t map_len = 0x1000;
    off_t map_off = 0;
    *is_mem = strcmp(path, "/dev/mem") == 0;
    if (*is_mem) {
        const char *base_env = getenv("AC880_CAPTURE_BASE");
        unsigned long base = base_env ? strtoul(base_env, NULL, 0) : 0x43c20000UL;
        long page = sysconf(_SC_PAGESIZE);
        map_off = (off_t)(base & ~((unsigned long)page - 1));
        map_len = (size_t)page;
    }
    void *p = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_off);
    if (p == MAP_FAILED) {
        fprintf(stderr, "mmap %s: %s\n", path, strerror(errno));
        close(fd);
        return NULL;
    }
    g_map = p;
    g_map_len = map_len;
    *fd_out = fd;
    return (volatile uint32_t *)p + ((*is_mem ?
             ((getenv("AC880_CAPTURE_BASE") ? strtoul(getenv("AC880_CAPTURE_BASE"), NULL, 0) : 0x43c20000UL) &
              (sysconf(_SC_PAGESIZE) - 1)) : 0) / sizeof(uint32_t));
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s <uio-device|/dev/mem> <id|status|start|stop|clear|adc|test|snapshot>\n", prog);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        usage(argv[0]);
        return 2;
    }
    int fd = -1, is_mem = 0;
    volatile uint32_t *r = map_regs(argv[1], &fd, &is_mem);
    if (!r) return 1;

    const char *cmd = argv[2];
    if (strcmp(cmd, "id") == 0) {
        printf("ID=0x%08" PRIx32 " VERSION=0x%08" PRIx32 "\n", r[REG_ID/4], r[REG_VERSION/4]);
    } else if (strcmp(cmd, "status") == 0) {
        uint32_t s = r[REG_STATUS/4];
        printf("STATUS=0x%08" PRIx32 " running=%u armed=%u triggered=%u overflow=%u state=%u commit_busy=%u\n",
               s, s & 1, (s >> 1) & 1, (s >> 2) & 1, (s >> 3) & 1,
               (s >> 4) & 0xf, (s >> 8) & 1);
        printf("samples=%" PRIu64 " output=%" PRIu64 " triggers=%" PRIu32 " overflows=%" PRIu32 " seq=%" PRIu32 "\n",
               ((uint64_t)r[REG_SAMPLE_HI/4] << 32) | r[REG_SAMPLE_LO/4],
               ((uint64_t)r[REG_OUTPUT_HI/4] << 32) | r[REG_OUTPUT_LO/4],
               r[REG_TRIGGER/4], r[REG_OVERFLOW/4], r[REG_SEQ/4]);
    } else if (strcmp(cmd, "start") == 0) {
        /* bit0 keeps enable shadow high; bit1 requests atomic commit. */
        r[REG_CONTROL/4] = 0x3;
    } else if (strcmp(cmd, "adc") == 0) {
        /* MODE: trigger_mode=0, source_select=0 (external ADC),
         * rising/auto trigger flags left enabled as in the default setup. */
        r[REG_MODE/4] = 0x10;
        r[REG_CONTROL/4] = 0x3;
    } else if (strcmp(cmd, "test") == 0) {
        /* MODE source_select=1 selects the internal incrementing test source. */
        r[REG_MODE/4] = 0x12;
        r[REG_CONTROL/4] = 0x3;
    } else if (strcmp(cmd, "stop") == 0) {
        /* bit0=0 disables acquisition; bit1=1 commits the new shadow value. */
        r[REG_CONTROL/4] = 0x2;
    } else if (strcmp(cmd, "clear") == 0) {
        r[REG_CONTROL/4] = 0x4;
    } else if (strcmp(cmd, "snapshot") == 0) {
        r[REG_CONTROL/4] = 0x8;
        usleep(1000);
        printf("SEQ=%" PRIu32 " samples=%" PRIu64 " output=%" PRIu64 "\n", r[REG_SEQ/4],
               ((uint64_t)r[REG_SAMPLE_HI/4] << 32) | r[REG_SAMPLE_LO/4],
               ((uint64_t)r[REG_OUTPUT_HI/4] << 32) | r[REG_OUTPUT_LO/4]);
    } else {
        usage(argv[0]);
        munmap(g_map, g_map_len);
        close(fd);
        return 2;
    }

    munmap(g_map, g_map_len);
    close(fd);
    return 0;
}
