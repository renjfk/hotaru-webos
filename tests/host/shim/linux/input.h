/* tests/host/shim/linux/input.h - minimal <linux/input.h> for macOS host tests.
 * Declares the evdev bits input/gamepad.c uses and fakes the ioctls: FIFOs in
 * the test's INPUT_DIR answer like a DualShock 4, so the real gamepad.c code
 * (scan, grab, hotplug) runs against events the test writes into the FIFO. */
#pragma once
#include <stdint.h>
#include <sys/time.h>
struct input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};
struct input_absinfo { int32_t value, minimum, maximum, fuzz, flat, resolution; };
struct input_id { uint16_t bustype, vendor, product, version; };
#define EV_KEY             0x01
#define EV_ABS             0x03
#define ABS_X              0x00
#define ABS_Y              0x01
#define ABS_HAT0X          0x10
#define ABS_HAT0Y          0x11
#define ABS_CNT            0x40
#define KEY_CNT            0x300
#define BTN_A              0x130
#define BTN_B              0x131
#define BTN_X              0x133
#define BTN_GAMEPAD        0x130
#define BTN_JOYSTICK       0x120
#define EVIOCGBIT(ev, len) (0x1000 + (ev))
#define EVIOCGNAME(len)    0x2000
#define EVIOCGRAB          0x3000
#define EVIOCGABS(abs)     (0x4000 + (abs))
#define ABS_Z              0x02
#define ABS_RZ             0x05
#define ABS_RX             0x03 /* L2 analog on this TV */
#define ABS_RY             0x04 /* R2 analog on this TV */
#define ABS_MT_POSITION_X  0x35
#define ABS_MT_POSITION_Y  0x36
#define ABS_MT_TRACKING_ID 0x39
#define EVIOCGVERSION      0x5000
#define EV_FF              0x15
#define FF_RUMBLE          0x50
#define FF_PERIODIC        0x51
#define FF_CONSTANT        0x52
#define FF_CNT             0x80
#define BTN_MISC           0x100
#define EVIOCGID           0x6000
#define EVIOCSFF           0x40204580
#define EVIOCRMFF          0x40044581
struct ff_rumble_effect { uint16_t strong_magnitude, weak_magnitude; };
struct ff_effect {
    uint16_t type;
    int16_t id;
    uint16_t direction;
    struct { uint16_t length, delay; } replay;
    union { struct ff_rumble_effect rumble; } u;
};
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static int shim_ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    struct stat st;
    if (fstat(fd, &st) < 0) {
        errno = EBADF;
        return -1;
    }
    if (!S_ISFIFO(st.st_mode)) {
        errno = ENOTTY;
        return -1;
    }
    if (req == 0x1000 + EV_KEY) {
        memset(arg, 0, (KEY_CNT + 7) / 8);
        ((unsigned char *)arg)[0x130 / 8] |= 1 << (0x130 % 8);
        return 0;
    }
    /* SHIM_PAD_NAME picks the controller; a DualSense is as webOS shows it: no touches, no rumble */
    const char *name = getenv("SHIM_PAD_NAME") ? getenv("SHIM_PAD_NAME") : "Wireless Controller";
    bool dualsense = strstr(name, "DualSense") != NULL;
    if (req == 0x1000 + EV_ABS) {
        memset(arg, 0, (ABS_CNT + 7) / 8);
        ((unsigned char *)arg)[0] |= 3;
        if (!dualsense) ((unsigned char *)arg)[ABS_MT_POSITION_X / 8] |= 1 << (ABS_MT_POSITION_X % 8);
        return 0;
    }
    if (req == 0x1000 + EV_FF) {
        if (dualsense) {
            errno = EINVAL;
            return -1;
        }
        memset(arg, 0, (FF_CNT + 7) / 8);
        ((unsigned char *)arg)[FF_RUMBLE / 8] |= 1 << (FF_RUMBLE % 8);
        return 0;
    }
    if (req == 0x2000) {
        strcpy(arg, name);
        return 0;
    }
    if (req == 0x3000) return 0;
    if (req >= 0x4000 && req < 0x4040) {
        struct input_absinfo *ai = arg;
        memset(ai, 0, sizeof(*ai));
        ai->maximum = 255;
        ai->value = 128;
        return 0;
    }
    if (req == EVIOCGVERSION) {
        *(int *)arg = 0x10001;
        return 0;
    }
    if (req == EVIOCGID) {
        struct input_id *id = arg;
        id->bustype = 5;
        id->vendor = 0x054c;
        id->product = 0x09cc;
        id->version = 0x8100;
        return 0;
    }
    errno = ENOTTY;
    return -1;
}
#define ioctl shim_ioctl
