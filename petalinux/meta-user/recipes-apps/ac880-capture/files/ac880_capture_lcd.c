/* SPDX-License-Identifier: MIT */
/*
 * AC880 capture-to-LCD viewer.
 *
 * The kernel DMA client continuously receives AXI-Stream samples through
 * AXI-DMA S2MM into a coherent DDR buffer.  This program consumes one DMA
 * period at a time and renders the byte samples as an RGB565 waveform through
 * the existing DRM fbdev emulation (/dev/fb0).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PERIOD_BYTES (1024U * 1024U)
#define DMA_IOC_MAGIC 'A'
#define DMA_IOC_STOP _IO(DMA_IOC_MAGIC, 1)

static volatile sig_atomic_t stop_requested;
static int dma_fd = -1;
static int fb_fd = -1;
static void *fb_map;
static size_t fb_map_len;

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
	return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void put_pixel(uint8_t *fb, const struct fb_var_screeninfo *v,
			     const struct fb_fix_screeninfo *f, unsigned x, unsigned y,
			     uint16_t colour)
{
	if (x >= v->xres || y >= v->yres)
		return;
	*(uint16_t *)(fb + (size_t)y * f->line_length + (size_t)x * 2) = colour;
}

static void draw_waveform(uint8_t *fb, const struct fb_var_screeninfo *v,
			 const struct fb_fix_screeninfo *f, const uint8_t *samples,
			 size_t n)
{
	const unsigned w = v->xres < 800 ? v->xres : 800;
	const unsigned h = v->yres < 480 ? v->yres : 480;
	const uint16_t black = rgb565(0, 0, 0);
	const uint16_t grid = rgb565(0, 18, 24);
	const uint16_t trace = rgb565(0, 255, 64);
	const uint16_t peak = rgb565(255, 220, 0);

	/* Clear only the visible frame.  fb0 is commonly 800x960 for double
	 * buffering; writing the first frame leaves the active yoffset unchanged. */
	for (unsigned y = 0; y < h; ++y) {
		uint16_t *row = (uint16_t *)(fb + (size_t)y * f->line_length);
		for (unsigned x = 0; x < w; ++x)
			row[x] = black;
	}
	for (unsigned x = 0; x < w; ++x) {
		if ((x % 100) == 0)
			for (unsigned y = 0; y < h; ++y) put_pixel(fb, v, f, x, y, grid);
	}
	for (unsigned y = 0; y < h; y += 60)
		for (unsigned x = 0; x < w; ++x) put_pixel(fb, v, f, x, y, grid);

	if (!n || !w || !h)
		return;

	/* Auto-scale each period, while retaining a useful range for a constant
	 * or near-constant input. */
	unsigned minv = 255, maxv = 0;
	for (size_t i = 0; i < n; ++i) {
		if (samples[i] < minv) minv = samples[i];
		if (samples[i] > maxv) maxv = samples[i];
	}
	if ((unsigned)(maxv - minv) < 4) {
		minv = 0;
		maxv = 255;
	}

	int previous = -1;
	for (unsigned x = 0; x < w; ++x) {
		size_t first = (size_t)x * n / w;
		size_t last = (size_t)(x + 1) * n / w;
		if (last <= first) last = first + 1;
		if (last > n) last = n;
		unsigned lo = 255, hi = 0, sum = 0, count = 0;
		for (size_t i = first; i < last; ++i) {
			unsigned s = samples[i];
			if (s < lo) lo = s;
			if (s > hi) hi = s;
			sum += s;
			++count;
		}
		unsigned avg = count ? sum / count : samples[first];
		unsigned yavg = (unsigned)((maxv - avg) * (h - 1) / (maxv - minv));
		unsigned ylo = (unsigned)((maxv - lo) * (h - 1) / (maxv - minv));
		unsigned yhi = (unsigned)((maxv - hi) * (h - 1) / (maxv - minv));
		if (previous >= 0) {
			int a = previous < (int)yavg ? previous : (int)yavg;
			int b = previous > (int)yavg ? previous : (int)yavg;
			for (int y = a; y <= b; ++y) put_pixel(fb, v, f, x, (unsigned)y, trace);
		}
		put_pixel(fb, v, f, x, yavg, trace);
		if (ylo != yhi) {
			put_pixel(fb, v, f, x, ylo, peak);
			put_pixel(fb, v, f, x, yhi, peak);
		}
		previous = (int)yavg;
	}
}

static int read_period(uint8_t *buf, size_t n)
{
	size_t done = 0;
	while (done < n && !stop_requested) {
		ssize_t r = read(dma_fd, buf + done, n - done);
		if (r > 0) { done += (size_t)r; continue; }
		if (r < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (r == 0) { errno = EPIPE; return -1; }
		return -1;
	}
	return stop_requested ? 1 : 0;
}

int main(int argc, char **argv)
{
	const char *dma_path = argc > 1 ? argv[1] : "/dev/ac880_capture_dma";
	const char *fb_path = argc > 2 ? argv[2] : "/dev/fb0";
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	uint8_t *period;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	dma_fd = open(dma_path, O_RDONLY);
	if (dma_fd < 0) { fprintf(stderr, "open %s: %s\n", dma_path, strerror(errno)); return 1; }
	fb_fd = open(fb_path, O_RDWR);
	if (fb_fd < 0) { fprintf(stderr, "open %s: %s\n", fb_path, strerror(errno)); close(dma_fd); return 1; }
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fb_fd, FBIOGET_FSCREENINFO, &f) < 0) {
		perror("fb ioctl"); return 1;
	}
	if (v.bits_per_pixel != 16 || v.xres < 800 || v.yres < 480 || f.line_length < v.xres * 2) {
		fprintf(stderr, "unsupported framebuffer: %ux%u virt %ux%u bpp %u stride %u\n",
			v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.bits_per_pixel, f.line_length);
		return 1;
	}
	fb_map_len = f.smem_len;
	fb_map = mmap(NULL, fb_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_map == MAP_FAILED) { perror("mmap fb0"); return 1; }
	period = malloc(PERIOD_BYTES);
	if (!period) { perror("malloc"); return 1; }
	while (!stop_requested) {
		int ret = read_period(period, PERIOD_BYTES);
		if (ret != 0) break;
		draw_waveform((uint8_t *)fb_map, &v, &f, period, PERIOD_BYTES);
	}
	(void)ioctl(dma_fd, DMA_IOC_STOP, 0);
	free(period);
	munmap(fb_map, fb_map_len);
	close(fb_fd);
	close(dma_fd);
	return 0;
}
