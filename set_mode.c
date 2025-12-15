#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm.h>

// For compatibility with older systems
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

struct drm_device {
    int fd;
    drmModeRes *resources;
    drmModeConnector *connector;
    drmModeEncoder *encoder;
    drmModeCrtc *crtc;
    drmModeModeInfo mode;
    uint32_t fb_id;
    uint32_t handle;
};

static int find_drm_device(struct drm_device *dev, const char* dri_index) {
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    int i;

    // concat DRI index to device path
    char device_path[64];
    snprintf(device_path, sizeof(device_path), "/dev/dri/card%s", dri_index);

    // Try to open the DRM device
    dev->fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (dev->fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", device_path, strerror(errno));
        return -1;
    }

    // Get DRM resources
    resources = drmModeGetResources(dev->fd);
    if (!resources) {
        fprintf(stderr, "Error: Cannot get DRM resources\n");
        close(dev->fd);
        return -1;
    }

    dev->resources = resources;

    // Look for a connected connector
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(dev->fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    if (!connector) {
        fprintf(stderr, "Error: No connected connector found\n");
        drmModeFreeResources(resources);
        close(dev->fd);
        return -1;
    }

    dev->connector = connector;

    // Find encoder - try current encoder first, then any compatible encoder
    if (connector->encoder_id) {
        encoder = drmModeGetEncoder(dev->fd, connector->encoder_id);
    }

    // If no current encoder, search for a compatible one
    if (!encoder) {
        for (i = 0; i < connector->count_encoders; i++) {
            encoder = drmModeGetEncoder(dev->fd, connector->encoders[i]);
            if (encoder) {
                // Check if this encoder has an available CRTC
                for (int j = 0; j < resources->count_crtcs; j++) {
                    if (encoder->possible_crtcs & (1 << j)) {
                        // Found a compatible encoder with possible CRTC
                        break;
                    }
                }
                if (encoder->possible_crtcs) {
                    break; // Use this encoder
                }
                drmModeFreeEncoder(encoder);
                encoder = NULL;
            }
        }
    }

    if (!encoder) {
        fprintf(stderr, "Error: No encoder found for connector\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(dev->fd);
        return -1;
    }

    dev->encoder = encoder;

    // Find a CRTC - try current one first, then find an available one
    uint32_t crtc_id = 0;
    if (encoder->crtc_id) {
        crtc_id = encoder->crtc_id;
        dev->crtc = drmModeGetCrtc(dev->fd, crtc_id);
    } else {
        // Find first available CRTC that this encoder can use
        for (i = 0; i < resources->count_crtcs; i++) {
            if (encoder->possible_crtcs & (1 << i)) {
                crtc_id = resources->crtcs[i];
                dev->crtc = drmModeGetCrtc(dev->fd, crtc_id);
                if (dev->crtc) {
                    break;
                }
            }
        }
    }

    if (!dev->crtc) {
        fprintf(stderr, "Error: No CRTC found\n");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(dev->fd);
        return -1;
    }

    // Find the largest mode
    int largest_area = 0;

    for (i = 0; i < connector->count_modes; i++) {
        drmModeModeInfo *current_mode = &connector->modes[i];
        int current_area = current_mode->hdisplay * current_mode->vdisplay;

        if (current_area > largest_area) {
            dev->mode = *current_mode;
            largest_area = current_area;
        }
    }

    printf("Selected mode: %dx%d@%dHz\n",
            dev->mode.hdisplay, dev->mode.vdisplay, dev->mode.vrefresh);

    // Create a minimal dumb buffer for the framebuffer
    struct drm_mode_create_dumb create_req = {0};
    create_req.width = dev->mode.hdisplay;
    create_req.height = dev->mode.vdisplay;
    create_req.bpp = 32;

    int ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req);
    if (ret) {
        fprintf(stderr, "Error: Cannot create dumb buffer: %s\n", strerror(errno));
        return -1;
    }

    dev->handle = create_req.handle;

    // Create framebuffer
    ret = drmModeAddFB(dev->fd, dev->mode.hdisplay, dev->mode.vdisplay,
                       24, 32, create_req.pitch, dev->handle, &dev->fb_id);
    if (ret) {
        fprintf(stderr, "Error: Cannot create framebuffer: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int set_mode(struct drm_device *dev) {
    int ret;

    printf("Setting mode to: %dx%d@%dHz\n",
           dev->mode.hdisplay, dev->mode.vdisplay, dev->mode.vrefresh);
    ret = drmModeSetCrtc(dev->fd, dev->crtc->crtc_id, dev->fb_id,
                         0, 0, &dev->connector->connector_id, 1, &dev->mode);
    if (ret) {
        fprintf(stderr, "Error: Cannot set CRTC: %s\n", strerror(errno));
        return -1;
    }

    printf("Mode set successfully\n");
    return 0;
}

static void cleanup_drm(struct drm_device *dev) {
    if (dev->fb_id) {
        drmModeRmFB(dev->fd, dev->fb_id);
    }
    if (dev->handle) {
        struct drm_mode_destroy_dumb destroy_req = {0};
        destroy_req.handle = dev->handle;
        drmIoctl(dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_req);
    }
    if (dev->crtc) {
        drmModeFreeCrtc(dev->crtc);
    }
    if (dev->encoder) {
        drmModeFreeEncoder(dev->encoder);
    }
    if (dev->connector) {
        drmModeFreeConnector(dev->connector);
    }
    if (dev->resources) {
        drmModeFreeResources(dev->resources);
    }
    if (dev->fd >= 0) {
        close(dev->fd);
    }
}

int main(int argc, char *argv[]) {
    struct drm_device dev = {0};

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <dri_index>\n", argv[0]);
        fprintf(stderr, "Example: %s 0\n", argv[0]);
        return 1;
    }

    printf("Starting DRM mode setter...\n");

    // Initialize DRM device
    if (find_drm_device(&dev, argv[1]) != 0) {
        return 1;
    }

    // Set mode
    if (set_mode(&dev) != 0) {
        cleanup_drm(&dev);
        return 1;
    }

    printf("Cleaning up...\n");
    cleanup_drm(&dev);

    return 0;
}
