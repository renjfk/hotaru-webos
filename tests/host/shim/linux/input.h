/* tests/host/shim/linux/input.h - minimal <linux/input.h> for macOS host tests.
 * Declares the evdev bits input/gamepad.c uses and fakes the ioctls: FIFOs in
 * the test's INPUT_DIR answer like a DualShock 4, so the real gamepad.c code
 * (scan, grab, hotplug) runs against events the test writes into the FIFO. */
#pragma once
#include <stdint.h>
#include <sys/time.h>
struct input_event { struct timeval time; uint16_t type; uint16_t code; int32_t value; };
struct input_absinfo { int32_t value, minimum, maximum, fuzz, flat, resolution; };
#define EV_KEY 0x01
#define EV_ABS 0x03
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_HAT0X 0x10
#define ABS_HAT0Y 0x11
#define ABS_CNT 0x40
#define KEY_CNT 0x300
#define BTN_A 0x130
#define BTN_B 0x131
#define BTN_X 0x133
#define BTN_GAMEPAD 0x130
#define BTN_JOYSTICK 0x120
#define EVIOCGBIT(ev, len) (0x1000 + (ev))
#define EVIOCGNAME(len) 0x2000
#define EVIOCGRAB 0x3000
#define EVIOCGABS(abs) (0x4000 + (abs))
#define ABS_Z 0x02
#define ABS_RZ 0x05
#define EVIOCGVERSION 0x5000
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static int shim_ioctl(int fd, unsigned long req, ...) {
    va_list ap; va_start(ap, req); void *arg = va_arg(ap, void *); va_end(ap);
    struct stat st;
    if (fstat(fd, &st) < 0) { errno = EBADF; return -1; }
    if (!S_ISFIFO(st.st_mode)) { errno = ENOTTY; return -1; }
    if (req == 0x1000 + EV_KEY) { memset(arg, 0, (KEY_CNT + 7) / 8); ((unsigned char *)arg)[0x130 / 8] |= 1 << (0x130 % 8); return 0; }
    if (req == 0x1000 + EV_ABS) { memset(arg, 0, (ABS_CNT + 7) / 8); ((unsigned char *)arg)[0] |= 3; return 0; }
    if (req == 0x2000) { strcpy(arg, "Wireless Controller"); return 0; }
    if (req == 0x3000) return 0;
    if (req >= 0x4000 && req < 0x4040) { struct input_absinfo *ai = arg; memset(ai, 0, sizeof(*ai)); ai->maximum = 255; ai->value = 128; return 0; }
    if (req == EVIOCGVERSION) { *(int *)arg = 0x10001; return 0; }
    errno = ENOTTY; return -1;
}
#define ioctl shim_ioctl
