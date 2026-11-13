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
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/input.h>

#include "../log.h"
#include "input_internal.h"

#define DS4_CROSS    0x131
#define DS4_CIRCLE   0x132
#define DS4_L1       0x134
#define DS4_R1       0x135
#define DS4_L2       0x136 /* the triggers' clicks; their travel is ABS_RX / ABS_RY */
#define DS4_R2       0x137
#define DS4_OPTIONS  0x139
#define DS4_RSTICK_X ABS_Z /* not ABS_RX: that is the L2 trigger here */
#define DS4_RSTICK_Y ABS_RZ

#define STICK_DEADZONE    0.15f
#define DPAD_REPEAT_DELAY 350 /* ms before a held D-pad starts repeating */
#define DPAD_REPEAT_MS    180
#define STICK_NAV_ON      0.5f /* left stick as a D-pad: deflection (after the curve) that presses */
#define STICK_NAV_OFF     0.3f /* ... and that releases, so a stick resting near the edge does not chatter */

#ifndef INPUT_DIR
#define INPUT_DIR "/dev/input"
#endif
#ifndef SYS_INPUT_DIR
#define SYS_INPUT_DIR "/sys/class/input"
#endif
#define PAD_RESCAN_MS 1500
#define PAD_HEALTH_MS 2000

/* webOS maps a controller's HID button usages sequentially onto 0x130+ (see
 * docs/ds4-map-observed.txt): the map is a property of the webOS uhid stack, so each
 * controller kind has its own table, chosen by EVIOCGNAME. 0x136/0x137 (L2/R2 clicks) are
 * covered by the analog triggers. The DualSense has the DualShock 4's order and axes, as probed
 * on the TV (docs/dualsense-map-observed.txt); its Mute button is not reported. */
typedef struct {
    int code;
    uint32_t btn;
} pad_btn_map_t;
static const pad_btn_map_t g_ds4_buttons[] = {
    { 0x130, PAD_BTN_SQUARE }, { 0x131, PAD_BTN_CROSS }, { 0x132, PAD_BTN_CIRCLE }, { 0x133, PAD_BTN_TRIANGLE },
    { 0x134, PAD_BTN_L1 },     { 0x135, PAD_BTN_R1 },    { 0x138, PAD_BTN_SHARE },  { 0x139, PAD_BTN_OPTIONS },
    { 0x13a, PAD_BTN_L3 },     { 0x13b, PAD_BTN_R3 },    { 0x13c, PAD_BTN_PS },     { 0x13d, PAD_BTN_TOUCHPAD },
};
static const pad_btn_map_t g_dualsense_buttons[] = {
    { 0x130, PAD_BTN_SQUARE }, { 0x131, PAD_BTN_CROSS }, { 0x132, PAD_BTN_CIRCLE }, { 0x133, PAD_BTN_TRIANGLE },
    { 0x134, PAD_BTN_L1 },     { 0x135, PAD_BTN_R1 },    { 0x138, PAD_BTN_SHARE },  { 0x139, PAD_BTN_OPTIONS },
    { 0x13a, PAD_BTN_L3 },     { 0x13b, PAD_BTN_R3 },    { 0x13c, PAD_BTN_PS },     { 0x13d, PAD_BTN_TOUCHPAD },
};
static const pad_btn_map_t *g_btn_map = g_ds4_buttons;
static size_t g_btn_map_n = sizeof(g_ds4_buttons) / sizeof(g_ds4_buttons[0]);

static const char *btn_name(uint32_t b) {
    static const char *const n[] = { "Cross", "Circle", "Square", "Triangle", "Left",    "Right", "Up",       "Down",
                                     "L1",    "R1",     "L3",     "R3",       "Options", "Share", "Touchpad", "PS" };
    for (int i = 0; i < 16; i++)
        if (b == 1u << i) return n[i];
    return "?";
}

typedef struct { int value, min, max; } axis_t;
static int g_pad_fd = -1;
static axis_t g_lx = { 128, 0, 255 }, g_ly = { 128, 0, 255 }, g_rx = { 128, 0, 255 }, g_ry = { 128, 0, 255 };
static axis_t g_l2 = { 0, 0, 255 }, g_r2 = { 0, 0, 255 };

/* the raw state the session gets (see gamepad_get); the UI's own keys are driven
 * separately, from the same events */
static struct {
    uint32_t buttons; /* PAD_BTN_* */
    uint16_t touch_x, touch_y;
    bool touch_down;
} g_pad;

/* Share + Options = the stream menu. The session gets those two buttons PAD_CHORD_MS late (a
 * one-press delay line each), so a chord is taken out before the console sees either half.
 * `hidden` buttons stay away from the console until released: the chord, and whatever was
 * held when streaming started (the Circle that closed the menu). */
#define CHORD_BTNS (PAD_BTN_SHARE | PAD_BTN_OPTIONS)
static struct {
    Uint32 down_at[2], up_at[2]; /* [0] Share, [1] Options; down_at 0 = nothing to replay */
    uint32_t hidden;
} g_chord;

static int g_ff_id = -1;         /* uploaded FF_RUMBLE effect id, -1 when unavailable */
static uint8_t g_rum_l, g_rum_r; /* the magnitudes last applied */
static bool g_ff_warned;         /* log a failed rumble upload once */
static int g_hatx, g_haty;
static lv_key_t g_dpad_key;
static Uint32 g_dpad_next, g_last_tick;
static char g_pad_path[300], g_pad_name[160];
static bool g_pad_seen; /* attached at least once: offline vs never found */
/* what the node offers: a DualSense on webOS has neither (no EV_FF, no ABS_MT_*), a DualShock 4 both */
static bool g_can_rumble, g_can_touch;
static Uint32 g_pad_next_scan, g_pad_next_health;

pad_state_t gamepad_state(void) { return g_pad_fd >= 0 ? PAD_ONLINE : g_pad_seen ? PAD_OFFLINE : PAD_NONE; }
const char *gamepad_name(void) { return g_pad_name; }

static void notify_state(void) {
    if (g_in.hooks.pad_changed) g_in.hooks.pad_changed(gamepad_state());
}

#define GETBIT(b, n) (((const unsigned char *)(b))[(n) / 8] >> ((n) % 8) & 1)
static int looks_like_gamepad(int fd) {
    unsigned char key[(KEY_CNT + 7) / 8] = { 0 }, abs[(ABS_CNT + 7) / 8] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0) return 0;
    int btn = GETBIT(key, BTN_GAMEPAD) || GETBIT(key, BTN_B) || GETBIT(key, BTN_X) || GETBIT(key, BTN_JOYSTICK);
    int sticks = GETBIT(abs, ABS_X) && GETBIT(abs, ABS_Y);
    return btn && sticks;
}

/* probe mode: what the node reports, so a new controller's map can be written from the log */
static void probe_device(int fd) {
    if (!log_probe_on()) return;
    struct input_id id = { 0 };
    ioctl(fd, EVIOCGID, &id);
    log_probe("pad: bus 0x%04x vendor 0x%04x product 0x%04x version 0x%04x", id.bustype, id.vendor, id.product,
              id.version);
    unsigned char key[(KEY_CNT + 7) / 8] = { 0 }, abs[(ABS_CNT + 7) / 8] = { 0 }, ff[(FF_CNT + 7) / 8] = { 0 };
    char line[512];
    size_t n = 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) >= 0)
        for (int c = BTN_MISC; c < KEY_CNT && n < sizeof(line) - 8; c++)
            if (GETBIT(key, c)) n += (size_t)snprintf(line + n, sizeof(line) - n, " 0x%x", c);
    line[n] = 0;
    log_probe("pad: keys%s", n ? line : " none");
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) >= 0)
        for (int c = 0; c < ABS_CNT; c++) {
            struct input_absinfo ai;
            if (GETBIT(abs, c) && ioctl(fd, EVIOCGABS(c), &ai) == 0)
                log_probe("pad: abs 0x%02x %d..%d (flat %d, fuzz %d)", c, ai.minimum, ai.maximum, ai.flat, ai.fuzz);
        }
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff)), ff) >= 0)
        log_probe("pad: force feedback%s%s%s", GETBIT(ff, FF_RUMBLE) ? " rumble" : "",
                  GETBIT(ff, FF_PERIODIC) ? " periodic" : "", GETBIT(ff, FF_CONSTANT) ? " constant" : "");
}

/* probe mode: every button, and an axis whenever it moves an eighth of its range */
static void probe_event(int type, int code, int value) {
    if (!log_probe_on()) return;
    if (type == EV_KEY) {
        const char *what = "unmapped";
        for (size_t i = 0; i < g_btn_map_n; i++)
            if (g_btn_map[i].code == code) what = btn_name(g_btn_map[i].btn);
        log_probe("pad: key 0x%x %s (%s)", code, value == 1 ? "down" : value == 0 ? "up" : "repeat", what);
    } else if (type == EV_ABS && code >= 0 && code < 64) {
        static int last[64];
        static bool seen[64];
        int step = 32; /* a 0..255 axis; ranges are probed at grab time */
        if (seen[code] && abs(value - last[code]) < step) return;
        seen[code] = true;
        last[code] = value;
        log_probe("pad: abs 0x%02x = %d", code, value);
    }
}

/* ---------- battery (the uhid node's power_supply) ---------- */

/* On this TV hid-sony publishes the DualShock 4's level on the pad's uhid node, in
 * <hid device>/power_supply/<name>/capacity. There is no /sys/class/power_supply here to
 * enumerate from, but the pad's own input node leads there: /sys/class/input/eventN/device
 * resolves to inputN, which sits in the HID device's input/ directory, so the HID device that
 * owns the power_supply is two levels up. A DualSense is only hid-generic, which registers no
 * battery: no node, no reading. See docs/pad-battery-observed.txt. */
bool gamepad_battery(int *pct) {
    if (g_pad_fd < 0 || !pct) return false;
    const char *node = strrchr(g_pad_path, '/');
    if (!node || !node[1]) return false;
    char dir[400], path[512];
    snprintf(dir, sizeof(dir), SYS_INPUT_DIR "/%s/device/../../power_supply", node + 1);
    DIR *d = opendir(dir);
    if (!d) return false;
    bool ok = false;
    for (struct dirent *e; !ok && (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s/capacity", dir, e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int v = -1;
        ok = fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 100;
        if (ok) *pct = v;
        fclose(f);
    }
    closedir(d);
    return ok;
}

bool gamepad_try_acquire(void) {
    DIR *d = opendir(INPUT_DIR);
    if (!d) return false;
    struct dirent *e;
    while (g_pad_fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300]; /* INPUT_DIR + d_name (up to 255) */
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        /* rumble needs a writable fd (the probe uploaded an FF_RUMBLE effect). A FIFO
         * (host tests) stays read-only: an extra writer would hide EOF on unplug. */
        struct stat st_fd;
        if (fstat(fd, &st_fd) == 0 && !S_ISFIFO(st_fd.st_mode)) {
            int rw = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
            if (rw >= 0) {
                close(fd);
                fd = rw;
            }
        }
        char name[160] = { 0 };
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        bool lg = strncmp(name, "LGE", 3) == 0 || strncmp(name, "LG ", 3) == 0, pad = !lg && looks_like_gamepad(fd);
        log_probe("pad: %s \"%s\": %s", path, name, lg ? "the TV's own, skipped" : pad ? "a gamepad" : "not a gamepad");
        if (!pad) {
            close(fd);
            continue;
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) log_msg("gamepad %s grab FAILED: %s", path, strerror(errno));
        else log_msg("gamepad %s \"%s\" grabbed", path, name);
        struct {
            int code;
            axis_t *axis;
        } axes[] = { { ABS_X, &g_lx },        { ABS_Y, &g_ly },  { DS4_RSTICK_X, &g_rx },
                     { DS4_RSTICK_Y, &g_ry }, { ABS_RX, &g_l2 }, { ABS_RY, &g_r2 } };
        for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(axes[i].code), &ai) == 0 && ai.maximum > ai.minimum) {
                axes[i].axis->min = ai.minimum;
                axes[i].axis->max = ai.maximum;
                axes[i].axis->value = (ai.minimum + ai.maximum) / 2;
            }
        }
        g_l2.value = g_l2.min; /* a released trigger is at the axis minimum, not the middle */
        g_r2.value = g_r2.min;
        log_msg("pad abs X %d..%d Y %d..%d Z %d..%d RZ %d..%d RX %d..%d RY %d..%d", g_lx.min, g_lx.max, g_ly.min,
                g_ly.max, g_rx.min, g_rx.max, g_ry.min, g_ry.max, g_l2.min, g_l2.max, g_r2.min, g_r2.max);
        snprintf(g_pad_path, sizeof(g_pad_path), "%s", path);
        snprintf(g_pad_name, sizeof(g_pad_name), "%s", name);
        if (strstr(name, "DualSense")) {
            g_btn_map = g_dualsense_buttons;
            g_btn_map_n = sizeof(g_dualsense_buttons) / sizeof(g_dualsense_buttons[0]);
        } else {
            g_btn_map = g_ds4_buttons;
            g_btn_map_n = sizeof(g_ds4_buttons) / sizeof(g_ds4_buttons[0]);
        }
        unsigned char ff[(FF_CNT + 7) / 8] = { 0 }, abs_bits[(ABS_CNT + 7) / 8] = { 0 };
        g_can_rumble = ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff)), ff) >= 0 && GETBIT(ff, FF_RUMBLE);
        g_can_touch =
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0 && GETBIT(abs_bits, ABS_MT_POSITION_X);
        log_msg("gamepad: rumble %s, touchpad %s", g_can_rumble ? "yes" : "no",
                g_can_touch ? "touches and click" : "click only");
        probe_device(fd);
        memset(&g_pad, 0, sizeof(g_pad));
        g_ff_id = -1;
        g_rum_l = g_rum_r = 0;
        g_pad_fd = fd;
        g_pad_seen = true;
        int pct;
        if (gamepad_battery(&pct)) log_msg("gamepad: battery %d%%", pct);
        g_pad_next_health = SDL_GetTicks() + PAD_HEALTH_MS;
        g_last_tick = 0;
    }
    closedir(d);
    if (g_pad_fd >= 0) notify_state();
    return g_pad_fd >= 0;
}

/* drop the pad and reset transient input so nothing stays held */
static void gamepad_lost(const char *why) {
    if (g_pad_fd < 0) return;
    ioctl(g_pad_fd, EVIOCGRAB, 0);
    close(g_pad_fd);
    g_pad_fd = -1;
    scroll_reset();
    g_hatx = g_haty = 0;
    g_dpad_key = 0;
    memset(&g_pad, 0, sizeof(g_pad)); /* nothing stays held on the console side either */
    memset(&g_chord, 0, sizeof(g_chord));
    g_ff_id = -1; /* the effect dies with the fd */
    g_rum_l = g_rum_r = 0;
    g_can_rumble = g_can_touch = false;
    g_lx.value = (g_lx.min + g_lx.max) / 2;
    g_ly.value = (g_ly.min + g_ly.max) / 2;
    g_rx.value = (g_rx.min + g_rx.max) / 2;
    g_ry.value = (g_ry.min + g_ry.max) / 2;
    g_l2.value = g_l2.min;
    g_r2.value = g_r2.min;
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

/* D-pad -> the raw bits, and (unless streaming) the same keypad queue as the TV remote */
static void pad_dpad_bits(void) {
    g_pad.buttons &= ~(PAD_BTN_DPAD_UP | PAD_BTN_DPAD_DOWN | PAD_BTN_DPAD_LEFT | PAD_BTN_DPAD_RIGHT);
    if (g_haty < 0) g_pad.buttons |= PAD_BTN_DPAD_UP;
    else if (g_haty > 0) g_pad.buttons |= PAD_BTN_DPAD_DOWN;
    if (g_hatx < 0) g_pad.buttons |= PAD_BTN_DPAD_LEFT;
    else if (g_hatx > 0) g_pad.buttons |= PAD_BTN_DPAD_RIGHT;
    g_chord.hidden &= g_pad.buttons;
}

static void dpad_event(int code, int value) {
    int *axis = code == ABS_HAT0X ? &g_hatx : &g_haty;
    if (*axis == value) return;
    *axis = value;
    pad_dpad_bits();
    if (g_in.streaming) return; /* the D-pad drives the console, not the UI */
    lv_key_t k = 0;
    if (value)
        k = code == ABS_HAT0X ? (value < 0 ? LV_KEY_LEFT : LV_KEY_RIGHT) : (value < 0 ? LV_KEY_PREV : LV_KEY_NEXT);
    else if (g_haty) k = g_haty < 0 ? LV_KEY_PREV : LV_KEY_NEXT; /* other axis still held */
    else if (g_hatx) k = g_hatx < 0 ? LV_KEY_LEFT : LV_KEY_RIGHT;
    if (value) keypad_push(k);
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

/* Share/Options press or release: feed the delay line, and fire the menu once both are down */
static void chord_button(uint32_t btn, int value) {
    int i = btn == PAD_BTN_SHARE ? 0 : btn == PAD_BTN_OPTIONS ? 1 : -1;
    if (i < 0 || value == 2) return; /* 2: evdev auto-repeat */
    Uint32 now = SDL_GetTicks();
    if (!value) {
        g_chord.up_at[i] = now ? now : 1;
        return;
    }
    g_chord.down_at[i] = now ? now : 1;
    g_chord.up_at[i] = 0;
    if ((g_pad.buttons & CHORD_BTNS) != CHORD_BTNS) return;
    g_chord.hidden |= CHORD_BTNS;
    g_chord.down_at[0] = g_chord.down_at[1] = 0; /* neither half is replayed to the console */
    log_msg("gamepad: Share + Options -> menu");
    if (g_in.hooks.menu) g_in.hooks.menu();
}

/* the delay line: the button's state PAD_CHORD_MS ago (its last press only) */
static bool chord_delayed(int i, Uint32 now) {
    Uint32 down = g_chord.down_at[i], up = g_chord.up_at[i];
    if (!down || now - down < PAD_CHORD_MS) return false;
    return !up || now - up < PAD_CHORD_MS;
}

void gamepad_streaming(bool on) {
    if (!on) return;
    g_chord.hidden = g_pad.buttons;
    g_chord.down_at[0] = g_chord.down_at[1] = 0;
}

/* keep the raw button bits in step with the evdev stream, whatever the mode */
static void pad_button_bits(int code, int value) {
    for (size_t i = 0; i < g_btn_map_n; i++)
        if (g_btn_map[i].code == code) {
            if (value) g_pad.buttons |= g_btn_map[i].btn;
            else g_pad.buttons &= ~g_btn_map[i].btn;
            g_chord.hidden &= g_pad.buttons;
            chord_button(g_btn_map[i].btn, value);
            return;
        }
}

static void gamepad_button(int code, int value) {
    pad_button_bits(code, value);
    if (g_in.streaming || value != 1) return; /* streaming: the pad drives the console, not the UI */
    if (code == DS4_CROSS) {
        keypad_push(LV_KEY_ENTER);
    } else if (code == DS4_L1) {
        if (g_in.hooks.tab_step) g_in.hooks.tab_step(-1, "L1");
    } else if (code == DS4_R1) {
        if (g_in.hooks.tab_step) g_in.hooks.tab_step(+1, "R1");
    } else if (code == DS4_CIRCLE) {
        if (g_in.hooks.back) g_in.hooks.back();
    } else if (code == DS4_L2 || code == DS4_R2) {
        if (g_in.hooks.shift) g_in.hooks.shift();
    }
}

/* Left stick as a second D-pad: the dominant axis past STICK_NAV_ON presses that
 * direction, with the D-pad's auto-repeat; back under STICK_NAV_OFF releases it. */
static lv_key_t g_stick_key;
static Uint32 g_stick_next;

static void stick_nav(float x, float y, Uint32 now) {
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    lv_key_t k = 0;
    if (ax >= STICK_NAV_ON || ay >= STICK_NAV_ON) {
        if (ax > ay) k = x < 0 ? LV_KEY_LEFT : LV_KEY_RIGHT;
        else k = y < 0 ? LV_KEY_PREV : LV_KEY_NEXT;
    } else if (g_stick_key && (ax >= STICK_NAV_OFF || ay >= STICK_NAV_OFF)) {
        k = g_stick_key; /* still held in the hysteresis band */
    }
    if (k != g_stick_key) {
        g_stick_key = k;
        if (!k) return;
        keypad_push(k);
        g_stick_next = now + DPAD_REPEAT_DELAY;
    } else if (k && (Sint32)(now - g_stick_next) >= 0) {
        keypad_push(k);
        g_stick_next = now + DPAD_REPEAT_MS;
    }
}

void gamepad_tick(void) {
    Uint32 now = SDL_GetTicks();
    if (g_pad_fd < 0) {
        if ((Sint32)(now - g_pad_next_scan) < 0) return;
        g_pad_next_scan = now + PAD_RESCAN_MS;
        bool again = g_pad_seen;
        if (!gamepad_try_acquire()) return;
        log_event(LOG_OK, "INPUT", "gamepad %s: %s \"%s\"", again ? "reconnected" : "connected", g_pad_path,
                  g_pad_name);
    }
    if ((Sint32)(now - g_pad_next_health) >= 0) {
        g_pad_next_health = now + PAD_HEALTH_MS;
        if (!gamepad_healthy()) {
            gamepad_lost("node gone");
            return;
        }
    }
    struct input_event ev;
    ssize_t n;
    while ((n = read(g_pad_fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        probe_event(ev.type, ev.code, ev.value);
        if (ev.type == EV_KEY) gamepad_button(ev.code, ev.value);
        else if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) g_lx.value = ev.value;
            else if (ev.code == ABS_Y) g_ly.value = ev.value;
            else if (ev.code == DS4_RSTICK_X) g_rx.value = ev.value;
            else if (ev.code == DS4_RSTICK_Y) g_ry.value = ev.value;
            else if (ev.code == ABS_RX) g_l2.value = ev.value; /* L2 analog is ABS_RX here */
            else if (ev.code == ABS_RY) g_r2.value = ev.value; /* R2 analog is ABS_RY here */
            else if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) dpad_event(ev.code, ev.value);
            else if (ev.code == ABS_MT_TRACKING_ID) g_pad.touch_down = ev.value >= 0; /* the console's only */
            else if (ev.code == ABS_MT_POSITION_X) g_pad.touch_x = (uint16_t)ev.value;
            else if (ev.code == ABS_MT_POSITION_Y) g_pad.touch_y = (uint16_t)ev.value;
        }
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        gamepad_lost(n == 0 ? "end of stream" : strerror(errno));
        return;
    }
    if (g_in.streaming) return; /* no scrolling or key repeat: the pad is the console's */
    float dt = g_last_tick ? (float)(now - g_last_tick) / 1000.0f : 0.0f;
    g_last_tick = now;
    if (dt > 0.1f) dt = 0.1f;

    if (g_dpad_key && now >= g_dpad_next) {
        keypad_push(g_dpad_key);
        g_dpad_next = now + DPAD_REPEAT_MS;
    }
    scroll_tick(stick_axis(&g_rx), stick_axis(&g_ry), dt);
    stick_nav(stick_axis(&g_lx), stick_axis(&g_ly), now);
}

/* ---------- raw state for the session ---------- */

static int16_t axis_i16(const axis_t *a) {
    int c = (a->min + a->max) / 2, h = (a->max - a->min) / 2;
    if (h < 1) return 0;
    int32_t v = (int32_t)(a->value - c) * 32767 / h;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

static uint8_t axis_u8(const axis_t *a, uint8_t dflt) {
    int span = a->max - a->min;
    if (span < 1) return dflt;
    int32_t v = (int32_t)(a->value - a->min) * 255 / span;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

bool gamepad_get(input_pad_t *out) {
    if (g_pad_fd < 0 || !out) return false;
    Uint32 now = SDL_GetTicks();
    out->buttons = g_pad.buttons & ~g_chord.hidden & ~CHORD_BTNS;
    if (chord_delayed(0, now)) out->buttons |= PAD_BTN_SHARE;
    if (chord_delayed(1, now)) out->buttons |= PAD_BTN_OPTIONS;
    out->l2 = axis_u8(&g_l2, 0);
    out->r2 = axis_u8(&g_r2, 0);
    out->left_x = axis_i16(&g_lx);
    out->left_y = axis_i16(&g_ly);
    out->right_x = axis_i16(&g_rx);
    out->right_y = axis_i16(&g_ry);
    out->touch_down = g_pad.touch_down;
    out->touch_x = g_pad.touch_x;
    out->touch_y = g_pad.touch_y;
    return true;
}

/* ---------- rumble (FF_RUMBLE over evdev) ---------- */

/* The motor strength is part of the effect, so a magnitude change needs a fresh
 * upload (remove the old effect, add the new one). A node without FF fails once. */
static void ff_upload(uint8_t left, uint8_t right) {
    if (g_pad_fd < 0) return;
    if (g_ff_id >= 0) {
        ioctl(g_pad_fd, EVIOCRMFF, g_ff_id);
        g_ff_id = -1;
    }
    struct ff_effect e;
    memset(&e, 0, sizeof(e));
    e.type = FF_RUMBLE;
    e.id = -1;
    e.replay.length = 0; /* infinite; play/stop with an EV_FF value of 1/0 */
    e.u.rumble.strong_magnitude = (uint16_t)(left * 257);
    e.u.rumble.weak_magnitude = (uint16_t)(right * 257);
    if (ioctl(g_pad_fd, EVIOCSFF, &e) < 0) {
        if (!g_ff_warned) {
            g_ff_warned = true;
            log_msg("gamepad: node has no force feedback");
        }
        return;
    }
    g_ff_id = e.id;
}

bool gamepad_can_rumble(void) { return g_pad_fd >= 0 && g_can_rumble; }
bool gamepad_can_touch(void) { return g_pad_fd >= 0 && g_can_touch; }

void gamepad_rumble(uint8_t left, uint8_t right) {
    if (g_pad_fd < 0 || !g_can_rumble) return;
    if (left != g_rum_l || right != g_rum_r) {
        g_rum_l = left;
        g_rum_r = right;
        ff_upload(left, right);
    }
    if (g_ff_id < 0) return;
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_FF;
    ev.code = (uint16_t)g_ff_id;
    ev.value = (left || right) ? 1 : 0;
    if (write(g_pad_fd, &ev, sizeof(ev)) < 0) return; /* best effort */
}
