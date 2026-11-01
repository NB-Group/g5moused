// g5moused - userspace driver for EWEADN G5 (YJX-CHIP A8A5/A8A4:2255) mice.
//
// The mouse sends motion/buttons in a vendor-defined report (id 0x02)
// on interface 0 endpoint 0x81. The kernel hid stack drops it because
// the report descriptor declares vendor usages with no mouse mapping,
// so the pointer is dead on Linux. This daemon reads the endpoint with
// libusb (detaching usbhid from the interface) and replays the packets
// into a uinput mouse device.
//
// v2 (2026-11-01): relay BOTH devices at once. 0xa8a5 is the original
// receiver, 0xa8a4 is the replacement unit (and the mouse itself when
// plugged by cable, same vendor protocol on its own interface 0). With
// the old single-handle logic the daemon bound to whichever receiver it
// found first and the wired mouse went untranslated; now every present
// device gets a transfer feeding the same uinput device, so wired,
// wireless and both-receivers-plugged all work with no restart.
//
// Frame (9 bytes, little-endian deltas):
//   [0] 0x02 report id
//   [1] button bitmask (bit0 left, bit1 right, bit2 middle,
//       bit3 back, bit4 forward)
//   [2] unused
//   [3:5] int16 dx
//   [5:7] int16 dy
//   [7]  int8 wheel
//
// Protocol reverse-engineered from usbhid-dump captures of a live G5
// and the vendor web driver bundle (hub.eweadn.cn yjs/web, 2026-10).
//
// Build: gcc -O2 -o g5moused g5moused.c -lusb-1.0
// Run:   sudo ./g5moused

#define _GNU_SOURCE
#include <libusb-1.0/libusb.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#define PID 0x2255
#define IFACE 0
#define EP_IN 0x81
static const unsigned short try_vids[] = { 0xA8A5, 0xA8A4 };
#define NVIDS 2
#define MAX_DEVS 8

static volatile int running = 1;
static void on_sig(int _) { (void)_; running = 0; }

static int ufd = -1;
static unsigned short first_vid = 0xA8A5;

static void uinput_create(void) {
    ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) { perror("open /dev/uinput"); exit(1); }
    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_REL);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);
    ioctl(ufd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(ufd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(ufd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(ufd, UI_SET_KEYBIT, BTN_BACK);
    ioctl(ufd, UI_SET_KEYBIT, BTN_FORWARD);
    ioctl(ufd, UI_SET_RELBIT, REL_X);
    ioctl(ufd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(ufd, UI_SET_RELBIT, REL_Y);
    ioctl(ufd, UI_SET_RELBIT, REL_HWHEEL);

    struct uinput_user_dev u;
    memset(&u, 0, sizeof(u));
    snprintf(u.name, UINPUT_MAX_NAME_SIZE, "EWEADN G5 (g5moused)");
    u.id.bustype = BUS_USB;
    u.id.vendor = first_vid;
    u.id.product = PID;
    if (write(ufd, &u, sizeof(u)) < 0) { perror("uinput setup"); exit(1); }
    if (ioctl(ufd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); exit(1); }
}


/* --- async transfer state --- */
static volatile int alive[MAX_DEVS];   /* per-device liveness */
static int prev_buttons = 0;

static void transfer_cb(struct libusb_transfer *t) {
    int idx = (int)(intptr_t)t->user_data;
    if (t->status != LIBUSB_TRANSFER_COMPLETED || !running) {
        alive[idx] = 0;   /* retire this device, keep the other going */
        return;
    }
    /* Resubmit FIRST, before any processing, so the endpoint
     * always has a pending URB while we emit events. */
    libusb_submit_transfer(t);

    unsigned char *buf = t->buffer;
    int len = t->actual_length;
    if (len < 7 || buf[0] != 0x02) return;

    int buttons = buf[1] & 0x1F;
    int dx = (int16_t)(buf[3] | (buf[4] << 8));
    int dy = (int16_t)(buf[5] | (buf[6] << 8));
    int wheel = (int8_t)buf[7];

    /* Batch all events into one write() to minimize syscalls */
    struct input_event ev[9];
    int n = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct timeval tv = {.tv_sec = ts.tv_sec, .tv_usec = ts.tv_nsec / 1000};

    if (buttons != prev_buttons) {
        if ((buttons & 1) != (prev_buttons & 1)) { ev[n].time=tv; ev[n].type=EV_KEY; ev[n].code=BTN_LEFT; ev[n].value=!!(buttons & 1); n++; }
        if ((buttons & 2) != (prev_buttons & 2)) { ev[n].time=tv; ev[n].type=EV_KEY; ev[n].code=BTN_RIGHT; ev[n].value=!!(buttons & 2); n++; }
        if ((buttons & 4) != (prev_buttons & 4)) { ev[n].time=tv; ev[n].type=EV_KEY; ev[n].code=BTN_MIDDLE; ev[n].value=!!(buttons & 4); n++; }
        if ((buttons & 8) != (prev_buttons & 8)) { ev[n].time=tv; ev[n].type=EV_KEY; ev[n].code=BTN_BACK; ev[n].value=!!(buttons & 8); n++; }
        if ((buttons & 16) != (prev_buttons & 16)) { ev[n].time=tv; ev[n].type=EV_KEY; ev[n].code=BTN_FORWARD; ev[n].value=!!(buttons & 16); n++; }
        prev_buttons = buttons;
    }
    if (dx) { ev[n].time=tv; ev[n].type=EV_REL; ev[n].code=REL_X; ev[n].value=dx; n++; }
    if (dy) { ev[n].time=tv; ev[n].type=EV_REL; ev[n].code=REL_Y; ev[n].value=dy; n++; }
    if (wheel) { ev[n].time=tv; ev[n].type=EV_REL; ev[n].code=REL_WHEEL; ev[n].value=wheel; n++; }
    if (n > 0) {
        ev[n].time=tv; ev[n].type=EV_SYN; ev[n].code=SYN_REPORT; ev[n].value=0; n++;
        if (write(ufd, ev, n * sizeof(ev[0])) < 0) perror("uinput batch write");
    }
}

int main(void) {
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    libusb_context *ctx;
    if (libusb_init(&ctx) < 0) { perror("libusb_init"); return 1; }

    libusb_device_handle *h[MAX_DEVS] = {0};
    int detached[MAX_DEVS] = {0};
    struct libusb_transfer *xf[MAX_DEVS] = {0};
    static unsigned char bulk_buf[MAX_DEVS][64];
    int ndev = 0;

    libusb_device **devlist = NULL;
    ssize_t ndevs = libusb_get_device_list(ctx, &devlist);
    for (ssize_t i = 0; i < ndevs && ndev < MAX_DEVS; i++) {
        struct libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(devlist[i], &dd) < 0) continue;
        if (dd.idProduct != PID) continue;
        int vid_ok = 0;
        for (int v = 0; v < NVIDS; v++) vid_ok |= (dd.idVendor == try_vids[v]);
        if (!vid_ok) continue;

        libusb_device_handle *d = NULL;
        if (libusb_open(devlist[i], &d) < 0) continue;
        if (libusb_kernel_driver_active(d, IFACE)) {
            if (libusb_detach_kernel_driver(d, IFACE) == 0) detached[ndev] = 1;
            else perror("detach usbhid (device may be held)");
        }
        if (libusb_claim_interface(d, IFACE) < 0) {
            perror("claim interface 0");
            libusb_close(d);
            continue;
        }
        h[ndev] = d;
        if (!ndev) first_vid = dd.idVendor;
        fprintf(stderr, "g5moused: relay %d attached (vid %04x)\n", ndev, dd.idVendor);
        ndev++;
    }
    libusb_free_device_list(devlist, 1);
    if (!ndev) { fprintf(stderr, "g5moused: no G5 device (a8a5/a8a4:2255) found\n"); return 1; }

    uinput_create();
    fprintf(stderr, "g5moused: %d device(s) alive, relaying reports (async)\n", ndev);

    for (int i = 0; i < ndev; i++) {
        alive[i] = 1;
        xf[i] = libusb_alloc_transfer(0);
        libusb_fill_interrupt_transfer(xf[i], h[i], EP_IN, bulk_buf[i], 64,
            transfer_cb, (void *)(intptr_t)i, 0);
        libusb_submit_transfer(xf[i]);
    }

    for (;;) {
        int any = 0;
        for (int i = 0; i < ndev; i++) any |= alive[i];
        if (!running || !any) break;
        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
        libusb_handle_events_timeout(ctx, &tv);
    }

    for (int i = 0; i < ndev; i++) {
        libusb_free_transfer(xf[i]);
        libusb_release_interface(h[i], IFACE);
        if (detached[i]) libusb_attach_kernel_driver(h[i], IFACE);
        libusb_close(h[i]);
    }
    libusb_exit(ctx);
    ioctl(ufd, UI_DEV_DESTROY);
    fprintf(stderr, "g5moused: stopped\n");
    return 0;
}
