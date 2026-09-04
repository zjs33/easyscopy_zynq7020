/*
 * AC880 DRM/KMS smoke test.
 *
 * This is a userspace DRM dumb-buffer test, not a framebuffer driver.  It
 * creates one 800x480 RGB565 buffer, programs the Xilinx CRTC with the LCD
 * timing, and leaves the file descriptor open so the image remains visible.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#define WIDTH 800U
#define HEIGHT 480U
#define CLOCK_KHZ 33264U

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
        (void)sig;
        stop = 1;
}

static int drm_ioctl(int fd, unsigned long request, void *arg, const char *name)
{
        if (ioctl(fd, request, arg) < 0) {
                fprintf(stderr, "%s: %s\n", name, strerror(errno));
                return -1;
        }
        return 0;
}

static void fill_bars(uint16_t *pixels, uint32_t pitch)
{
        static const uint16_t colors[] = {
                0xf800, /* red */
                0x07e0, /* green */
                0x001f, /* blue */
                0xffe0, /* yellow */
                0xf81f, /* magenta */
                0x07ff, /* cyan */
                0xffff, /* white */
                0x0000, /* black */
        };
        unsigned int y, x;

        for (y = 0; y < HEIGHT; ++y) {
                uint16_t *row = (uint16_t *)((uint8_t *)pixels + y * pitch);
                for (x = 0; x < WIDTH; ++x)
                        row[x] = colors[(x * 8U) / WIDTH];
        }
}

int main(int argc, char **argv)
{
        const char *path = argc > 1 ? argv[1] : "/dev/dri/card0";
        struct drm_mode_card_res res = { 0 };
        struct drm_mode_create_dumb create = { 0 };
        struct drm_mode_map_dumb map = { 0 };
        struct drm_mode_fb_cmd2 fb = { 0 };
        struct drm_mode_crtc crtc = { 0 };
        struct drm_mode_modeinfo mode = { 0 };
        uint32_t connectors[16] = { 0 };
        uint32_t encoders[16] = { 0 };
        uint32_t fbs[16] = { 0 };
        uint32_t crtcs_storage[16] = { 0 };
        uint32_t *crtcs = NULL;
        int fd = -1;
        void *map_addr = MAP_FAILED;
        int ret = EXIT_FAILURE;

        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);

        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
                fprintf(stderr, "open %s: %s\n", path, strerror(errno));
                return EXIT_FAILURE;
        }

        /* The kernel copies IDs during the first GETRESOURCES call when any
         * connector exists, so provide storage for every ID list up front. */
        res.fb_id_ptr = (uintptr_t)fbs;
        res.crtc_id_ptr = (uintptr_t)crtcs_storage;
        res.connector_id_ptr = (uintptr_t)connectors;
        res.encoder_id_ptr = (uintptr_t)encoders;
        res.count_fbs = 16;
        res.count_crtcs = 16;
        res.count_connectors = 16;
        res.count_encoders = 16;

        if (drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res,
                      "GETRESOURCES(size)") < 0 || !res.count_crtcs) {
                fprintf(stderr, "DRM card has no CRTC\n");
                goto out;
        }
        crtcs = calloc(res.count_crtcs, sizeof(*crtcs));
        if (!crtcs)
                goto out;
        res.crtc_id_ptr = (uintptr_t)crtcs;
        if (drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res,
                      "GETRESOURCES") < 0)
                goto out;
        fprintf(stdout, "Using CRTC %u (connectors reported: %u)\n",
                crtcs[0], res.count_connectors);

        create.width = WIDTH;
        create.height = HEIGHT;
        create.bpp = 16;
        if (drm_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create,
                      "CREATE_DUMB") < 0)
                goto out;

        fb.width = WIDTH;
        fb.height = HEIGHT;
        fb.pixel_format = DRM_FORMAT_RGB565;
        fb.handles[0] = create.handle;
        fb.pitches[0] = create.pitch;
        fb.modifier[0] = DRM_FORMAT_MOD_LINEAR;
        if (drm_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb, "ADDFB2") < 0)
                goto out;

        map.handle = create.handle;
        if (drm_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map, "MAP_DUMB") < 0)
                goto out;
        map_addr = mmap(NULL, create.size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, map.offset);
        if (map_addr == MAP_FAILED) {
                fprintf(stderr, "mmap: %s\n", strerror(errno));
                goto out;
        }
        fill_bars((uint16_t *)map_addr, create.pitch);

        mode.clock = CLOCK_KHZ;
        mode.hdisplay = WIDTH;
        mode.hsync_start = 840;
        mode.hsync_end = 968;
        mode.htotal = 1056;
        mode.vdisplay = HEIGHT;
        mode.vsync_start = 490;
        mode.vsync_end = 492;
        mode.vtotal = 525;
        mode.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
        mode.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
        snprintf(mode.name, sizeof(mode.name), "%ux%u", WIDTH, HEIGHT);

        crtc.crtc_id = crtcs[0];
        crtc.fb_id = fb.fb_id;
        crtc.set_connectors_ptr = (uintptr_t)connectors;
        crtc.count_connectors = res.count_connectors ? 1 : 0;
        crtc.mode_valid = 1;
        crtc.mode = mode;
        if (drm_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc, "SETCRTC") < 0)
                goto out;

        fprintf(stdout, "RGB565 test pattern active: %ux%u, %u kHz\n",
                WIDTH, HEIGHT, CLOCK_KHZ);
        fprintf(stdout, "Press Ctrl-C to stop.\n");
        while (!stop)
                pause();
        ret = EXIT_SUCCESS;

out:
        if (map_addr != MAP_FAILED)
                munmap(map_addr, create.size);
        if (fb.fb_id)
                ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb.fb_id);
        if (create.handle)
                ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &create);
        free(crtcs);
        if (fd >= 0)
                close(fd);
        return ret;
}
