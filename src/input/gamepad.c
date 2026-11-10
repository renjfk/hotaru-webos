/* input/gamepad.c - DualShock 4 via evdev.
 *
 * The pad is a webOS uhid device with a NON-standard map (HID button order
 * shifted onto 0x130+, right stick on ABS_Z/ABS_RZ, triggers on ABS_RX/RY):
 * see docs/ds4-map-observed.txt before touching any code below.
 *
 * Hotplug: while offline, INPUT_DIR is rescanned every PAD_RESCAN_MS (the
 * node number changes across reconnects). While attached, a read error or EOF
 * drops the pad at once, and every PAD_HEALTH_MS the node is checked for
 * ENODEV or for having disappeared/been replaced, in case a drop is silent.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input.h>

#include "../log.h"
#include "input_internal.h"

#define DS4_CROSS      0x131
#define DS4_L1         0x134
#define DS4_R1         0x135   /* 0x136/0x137 are the L2/R2 trigger clicks: unused */
#define DS4_OPTIONS    0x139
#define DS4_RSTICK_X   ABS_Z   /* not ABS_RX: that is the L2 trigger here */
#define DS4_RSTICK_Y   ABS_RZ

#define STICK_DEADZONE    0.15f
#define DPAD_REPEAT_DELAY 350   /* ms before a held D-pad starts repeating */
#define DPAD_REPEAT_MS    180

#ifndef INPUT_DIR
#define INPUT_DIR "/dev/input"
#endif
#define PAD_RESCAN_MS 1500
#define PAD_HEALTH_MS 2000

typedef struct { int value, min, max; } axis_t;
static int g_pad_fd = -1;
static axis_t g_lx = { 128, 0, 255 }, g_ly = { 128, 0, 255 }, g_rx = { 128, 0, 255 }, g_ry = { 128, 0, 255 };
static int g_hatx, g_haty;
static lv_key_t g_dpad_key;
static Uint32 g_dpad_next, g_last_tick;
static char g_pad_path[300], g_pad_name[160];
static bool g_pad_seen;   /* attached at least once: offline vs never found */
static Uint32 g_pad_next_scan, g_pad_next_health;

pad_state_t gamepad_state(void) { return g_pad_fd >= 0 ? PAD_ONLINE : g_pad_seen ? PAD_OFFLINE : PAD_NONE; }

static void notify_state(void) {
    if (g_in.hooks.pad_changed) g_in.hooks.pad_changed(gamepad_state());
}

#define GETBIT(b, n) (((const unsigned char *)(b))[(n) / 8] >> ((n) % 8) & 1)
static int looks_like_gamepad(int fd) {
    unsigned char key[(KEY_CNT + 7) / 8] = { 0 }, abs[(ABS_CNT + 7) / 8] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0) return 0;
    int btn = GETBIT(key, BTN_A) || GETBIT(key, BTN_B) || GETBIT(key, BTN_X) ||
              GETBIT(key, BTN_GAMEPAD) || GETBIT(key, BTN_JOYSTICK);
    int sticks = GETBIT(abs, ABS_X) && GETBIT(abs, ABS_Y);
    return btn && sticks;
}

bool gamepad_try_acquire(void) {
    DIR *d = opendir(INPUT_DIR);
    if (!d) return false;
    struct dirent *e;
    while (g_pad_fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300];   /* INPUT_DIR + d_name (up to 255) */
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[160] = { 0 };
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        if (strncmp(name, "LGE", 3) == 0 || strncmp(name, "LG ", 3) == 0 || !looks_like_gamepad(fd)) {
            close(fd);
            continue;
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) log_msg("gamepad %s grab FAILED: %s", path, strerror(errno));
        else log_msg("gamepad %s \"%s\" grabbed", path, name);
        struct { int code; axis_t *axis; } axes[] = {
            { ABS_X, &g_lx }, { ABS_Y, &g_ly }, { DS4_RSTICK_X, &g_rx }, { DS4_RSTICK_Y, &g_ry } };
        for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(axes[i].code), &ai) == 0 && ai.maximum > ai.minimum) {
                axes[i].axis->min = ai.minimum;
                axes[i].axis->max = ai.maximum;
                axes[i].axis->value = (ai.minimum + ai.maximum) / 2;
            }
        }
        snprintf(g_pad_path, sizeof(g_pad_path), "%s", path);
        snprintf(g_pad_name, sizeof(g_pad_name), "%s", name);
        g_pad_fd = fd;
        g_pad_seen = true;
        g_pad_next_health = SDL_GetTicks() + PAD_HEALTH_MS;
        g_last_tick = 0;
    }
    closedir(d);
    if (g_pad_fd >= 0) notify_state();
    return g_pad_fd >= 0;
}

/* drop the pad and reset transient input so nothing stays held; the cursor stays put */
static void gamepad_lost(const char *why) {
    if (g_pad_fd < 0) return;
    ioctl(g_pad_fd, EVIOCGRAB, 0);
    close(g_pad_fd);
    g_pad_fd = -1;
    pointer_drop_press();
    g_hatx = g_haty = 0;
    g_dpad_key = 0;
    g_lx.value = (g_lx.min + g_lx.max) / 2;
    g_ly.value = (g_ly.min + g_ly.max) / 2;
    g_rx.value = (g_rx.min + g_rx.max) / 2;
    g_ry.value = (g_ry.min + g_ry.max) / 2;
    g_pad_next_scan = SDL_GetTicks() + PAD_RESCAN_MS;
    log_msg("gamepad %s lost: %s", g_pad_path, why);
    log_event(LOG_ERR, "INPUT", "gamepad offline");
    notify_state();
}

static bool gamepad_healthy(void) {
    int ver;
    struct stat st_path, st_fd;
    if (ioctl(g_pad_fd, EVIOCGVERSION, &ver) < 0 && (errno == ENODEV || errno == EBADF)) return false;
    if (stat(g_pad_path, &st_path) < 0 || fstat(g_pad_fd, &st_fd) < 0) return false;
    return st_path.st_rdev == st_fd.st_rdev && st_path.st_ino == st_fd.st_ino;
}

/* D-pad -> the same keypad queue as the TV remote, with auto-repeat while held */
static void dpad_event(int code, int value) {
    int *axis = code == ABS_HAT0X ? &g_hatx : &g_haty;
    if (*axis == value) return;
    *axis = value;
    lv_key_t k = 0;
    if (value) k = code == ABS_HAT0X ? (value < 0 ? LV_KEY_LEFT : LV_KEY_RIGHT) : (value < 0 ? LV_KEY_PREV : LV_KEY_NEXT);
    else if (g_haty) k = g_haty < 0 ? LV_KEY_PREV : LV_KEY_NEXT;   /* other axis still held */
    else if (g_hatx) k = g_hatx < 0 ? LV_KEY_LEFT : LV_KEY_RIGHT;
    if (value) { pointer_keypad_mode(); keypad_push(k); }
    g_dpad_key = k;
    g_dpad_next = SDL_GetTicks() + DPAD_REPEAT_DELAY;
}

/* normalised stick axis in -1..1 with deadzone and a soft response curve */
static float stick_axis(const axis_t *a) {
    int c = (a->min + a->max) / 2, h = (a->max - a->min) / 2;
    float v = (float)(a->value - c) / (float)(h < 1 ? 1 : h);
    if (v > -STICK_DEADZONE && v < STICK_DEADZONE) return 0;
    if (v > 1) v = 1;
    if (v < -1) v = -1;
    return v * (0.4f + 0.6f * (v < 0 ? -v : v));
}

static void gamepad_button(int code, int value) {
    if (code == DS4_CROSS) {
        if (value == 1) {
            if (g_in.cursor_on) g_in.click = true;   /* pointer: LVGL decides press/release/click */
            else keypad_push(LV_KEY_ENTER);          /* keypad mode: activate the focused control */
        } else if (value == 0) {
            g_in.click = false;
        }
    } else if (value != 1) {
        return;
    } else if (code == DS4_OPTIONS) {
        if (g_in.hooks.quit) g_in.hooks.quit();
    } else if (code == DS4_L1) {
        if (g_in.hooks.tab_step) g_in.hooks.tab_step(-1, "L1");
    } else if (code == DS4_R1) {
        if (g_in.hooks.tab_step) g_in.hooks.tab_step(+1, "R1");
    }
}

void gamepad_tick(void) {
    Uint32 now = SDL_GetTicks();
    if (g_pad_fd < 0) {
        if ((Sint32)(now - g_pad_next_scan) < 0) return;
        g_pad_next_scan = now + PAD_RESCAN_MS;
        bool again = g_pad_seen;
        if (!gamepad_try_acquire()) return;
        log_event(LOG_OK, "INPUT", "gamepad %s: %s \"%s\"", again ? "reconnected" : "connected", g_pad_path, g_pad_name);
    }
    if ((Sint32)(now - g_pad_next_health) >= 0) {
        g_pad_next_health = now + PAD_HEALTH_MS;
        if (!gamepad_healthy()) { gamepad_lost("node gone"); return; }
    }
    struct input_event ev;
    ssize_t n;
    while ((n = read(g_pad_fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY) gamepad_button(ev.code, ev.value);
        else if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) g_lx.value = ev.value;
            else if (ev.code == ABS_Y) g_ly.value = ev.value;
            else if (ev.code == DS4_RSTICK_X) g_rx.value = ev.value;
            else if (ev.code == DS4_RSTICK_Y) g_ry.value = ev.value;
            else if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) dpad_event(ev.code, ev.value);
        }
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        gamepad_lost(n == 0 ? "end of stream" : strerror(errno));
        return;
    }
    float dt = g_last_tick ? (float)(now - g_last_tick) / 1000.0f : 0.0f;
    g_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;

    if (g_dpad_key && now >= g_dpad_next) {
        keypad_push(g_dpad_key);
        g_dpad_next = now + DPAD_REPEAT_MS;
    }
    pointer_scroll(stick_axis(&g_rx), stick_axis(&g_ry), dt);
    pointer_tick(stick_axis(&g_lx), stick_axis(&g_ly), dt);
}
