/* input/gamepad.c - DualShock 4, DualSense and DualShock 3 via evdev.
 *
 * Three layouts feed one set of tables. On webOS 6 the pad is a uhid device with a NON-standard map
 * (HID button order shifted onto 0x130+, right stick on ABS_Z/ABS_RZ, triggers on ABS_RX/RY):
 * see docs/ds4-map-observed.txt. On webOS 11 hid-playstation reports the standard Linux order,
 * normalised to that map as events are read: docs/dualsense-map-linux-observed.txt. A DualShock 3
 * on the kernel's hid-sony driver is the Sixaxis layout, normalised the same way: its own button
 * order from 0x120, no hat switch and its triggers as pressures: docs/ds3-map-observed.txt.
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

#define DS4_CROSS        0x131
#define DS4_CIRCLE       0x132
#define DS4_L1           0x134
#define DS4_R1           0x135
#define DS4_L2           0x136 /* the triggers' clicks; their travel is ABS_RX / ABS_RY */
#define DS4_R2           0x137
#define DS4_OPTIONS      0x139
#define DS4_RSTICK_X     ABS_Z /* not ABS_RX: that is the L2 trigger here */
#define DS4_RSTICK_Y     ABS_RZ
#define DS_TOUCHPAD_CODE 0x13d /* the Touchpad button in both tables (LG's order) */

/* A DualShock 3 on the kernel's hid-sony driver reports the Sixaxis layout instead
 * (docs/ds3-map-observed.txt): 19 buttons from 0x120 in the pad's own bit order, no hat switch (the
 * D-pad is four of those buttons), the triggers' travel as 0..255 pressures on ABS 0x30/0x31, and on
 * LG's kernel no force feedback (hid-sony without CONFIG_SONY_FF). Its codes are normalised to LG's
 * uhid order as they are read, like the standard layout's, and the D-pad's buttons become hat
 * events, so the tables and everything after them stay as they are. */
#define DS3_SELECT   0x120 /* Select: Share on a DualShock 4 */
#define DS3_L3       0x121
#define DS3_R3       0x122
#define DS3_START    0x123 /* Start: Options */
#define DS3_UP       0x124
#define DS3_RIGHT    0x125
#define DS3_DOWN     0x126
#define DS3_LEFT     0x127
#define DS3_L2       0x128 /* the click; its travel is ABS 0x30 */
#define DS3_R2       0x129 /* ... and ABS 0x31 */
#define DS3_L1       0x12a
#define DS3_R1       0x12b
#define DS3_TRIANGLE 0x12c
#define DS3_CIRCLE   0x12d
#define DS3_CROSS    0x12e
#define DS3_SQUARE   0x12f
#define DS3_PS       0x2c0
#define DS3_L2_AXIS  0x30
#define DS3_R2_AXIS  0x31

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

/* The tables above are LG's uhid order (webOS 6, docs/ds4-map-observed.txt). Newer TVs bind
 * the stock hid-playstation driver instead (webOS 11, docs/dualsense-map-linux-observed.txt):
 * the standard Linux layout, BTN_SOUTH = Cross from 0x130 on, the right stick on ABS_RX/RY and
 * the triggers on ABS_Z/RZ. That node is told apart at grab by BTN_THUMBR (0x13e) without BTN_C
 * (0x132) or BTN_Z (0x135), the rule in docs/TV-CHECKS.md: the uhid order never sends 0x13e, and a
 * pad on the generic driver in raw HID order sends all of 0x130..0x13e (Mute on 0x13e) and stays
 * on the tables as they are. The standard layout's codes are normalised to the uhid ones as they
 * are read, so the tables and everything after them stay as they are. A DualShock 3 is the third
 * layout, told apart by name and normalised the same way (ds3_key(), ds3_abs()). */
typedef enum { LAYOUT_UHID, LAYOUT_STD, LAYOUT_DS3 } layout_t;
static layout_t g_layout = LAYOUT_UHID;

static int std_key(int code) {
    switch (code) {
        case 0x130: return 0x131; /* BTN_SOUTH  Cross    */
        case 0x131: return 0x132; /* BTN_EAST   Circle   */
        case 0x133: return 0x133; /* BTN_NORTH  Triangle */
        case 0x134: return 0x130; /* BTN_WEST   Square   */
        case 0x136: return 0x134; /* BTN_TL     L1       */
        case 0x137: return 0x135; /* BTN_TR     R1       */
        case 0x138: return 0x136; /* BTN_TL2    L2 click */
        case 0x139: return 0x137; /* BTN_TR2    R2 click */
        case 0x13a: return 0x138; /* BTN_SELECT Create   */
        case 0x13b: return 0x139; /* BTN_START  Options  */
        case 0x13c: return 0x13c; /* BTN_MODE   PS       */
        case 0x13d: return 0x13a; /* BTN_THUMBL L3       */
        case 0x13e: return 0x13b; /* BTN_THUMBR R3       */
        default: return -1;
    }
}

/* the right stick and the triggers trade axes; the swap is its own inverse */
static int std_abs(int code) {
    switch (code) {
        case ABS_Z: return ABS_RX;
        case ABS_RZ: return ABS_RY;
        case ABS_RX: return ABS_Z;
        case ABS_RY: return ABS_RZ;
        default: return code;
    }
}

/* the Sixaxis layout's own bit order, as LG's uhid order (docs/ds3-map-observed.txt) */
static int ds3_key(int code) {
    switch (code) {
        case DS3_SQUARE: return 0x130;
        case DS3_CROSS: return 0x131;
        case DS3_CIRCLE: return 0x132;
        case DS3_TRIANGLE: return 0x133;
        case DS3_L1: return 0x134;
        case DS3_R1: return 0x135;
        case DS3_L2: return 0x136;
        case DS3_R2: return 0x137;
        case DS3_SELECT: return 0x138;
        case DS3_START: return 0x139;
        case DS3_L3: return 0x13a;
        case DS3_R3: return 0x13b;
        case DS3_PS: return 0x13c;
        default: return -1;
    }
}

/* the DS3's trigger travel is a pressure on its own axis, where the uhid map has ABS_RX / ABS_RY */
static int ds3_abs(int code) {
    switch (code) {
        case DS3_L2_AXIS: return ABS_RX;
        case DS3_R2_AXIS: return ABS_RY;
        default: return code;
    }
}

/* the layout's own codes, normalised to the uhid ones as they are read; -1: no table knows it */
static int layout_key(int code) {
    if (g_layout == LAYOUT_STD) return std_key(code);
    if (g_layout == LAYOUT_DS3) return ds3_key(code);
    return code;
}

static int layout_abs(int code) {
    if (g_layout == LAYOUT_STD) return std_abs(code);
    if (g_layout == LAYOUT_DS3) return ds3_abs(code);
    return code;
}

/* the raw code behind a uhid one, for the ranges read at grab: layout_abs() is not its own inverse
 * for the DS3, whose triggers travel on axes of their own */
static int layout_raw_abs(int code) {
    if (g_layout == LAYOUT_DS3) {
        if (code == ABS_RX) return DS3_L2_AXIS;
        if (code == ABS_RY) return DS3_R2_AXIS;
    }
    return layout_abs(code);
}

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
    bool motion;
    uint32_t motion_seq, motion_ts;
    float gyro[3], accel[3]; /* rad/s and g */
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
static uint8_t g_ds3_dpad; /* the DS3's D-pad buttons held (no hat switch: they become hat events) */
static lv_key_t g_dpad_key;
static Uint32 g_dpad_next, g_last_tick;
static char g_pad_path[300], g_pad_name[160];
static bool g_pad_seen; /* attached at least once: offline vs never found */
/* what the node offers: a DualSense on webOS 6 has neither (no EV_FF, no ABS_MT_*), a DualShock 4 both */
static bool g_can_rumble, g_can_touch;

/* webOS 11 (hid-playstation) gives a DualSense's touchpad a node of its own, "<pad name>
 * Touchpad" with the controller's Bluetooth address as its uniq: the click is BTN_LEFT there and
 * the touches ABS_MT_*. It is opened and grabbed next to the pad (docs/dualsense-map-linux-observed.txt). */
static int g_touch_fd = -1, g_touch_slot;
static axis_t g_tx, g_ty;        /* its ranges */
static Uint32 g_touch_next_scan; /* the node can show up just after the pad's */
static int g_touch_tries;        /* looks left for it; a pad without one stops looking */
#define TOUCH_TRIES 5
/* ... and its motion sensors on one more, "<pad name> Motion Sensors": accel on ABS_X/Y/Z, gyro on
 * ABS_RX/RY/RZ, each with its resolution (units per g, per degree/s) and MSC_TIMESTAMP in µs */
static int g_motion_fd = -1;
static int g_mres[6]; /* the resolutions */
static int g_mraw[6]; /* the last raw values */
static bool g_mdirty;
static uint32_t g_mts;
static Uint32 g_motion_next_scan;
static int g_motion_tries;
/* probe mode: the travel of each axis, read at grab, so a stick at full range logs a few lines and not hundreds */
static int g_abs_span[ABS_CNT];
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

/* The names linux/input-event-codes.h gives a raw code, independent of the map above: a probe log
 * then says which layout the node reports (BTN_SOUTH..BTN_THUMBR is the standard one, see
 * docs/TV-CHECKS.md) and, next to it, what Hotaru made of it. */
static const char *linux_key_name(int code) {
    switch (code) {
        case 0x130: return "BTN_SOUTH";
        case 0x131: return "BTN_EAST";
        case 0x132: return "BTN_C";
        case 0x133: return "BTN_NORTH";
        case 0x134: return "BTN_WEST";
        case 0x135: return "BTN_Z";
        case 0x136: return "BTN_TL";
        case 0x137: return "BTN_TR";
        case 0x138: return "BTN_TL2";
        case 0x139: return "BTN_TR2";
        case 0x13a: return "BTN_SELECT";
        case 0x13b: return "BTN_START";
        case 0x13c: return "BTN_MODE";
        case 0x13d: return "BTN_THUMBL";
        case 0x13e: return "BTN_THUMBR";
        default: return NULL;
    }
}

/* what the webOS map above makes of an axis, for the same log line: a TV whose codes are standard is
 * exactly where ABS_Z and ABS_RX mean something else. Only the axes gamepad_tick() reads are named. */
static const char *axis_role(int code) {
    switch (code) {
        case ABS_X: return "left stick X";
        case ABS_Y: return "left stick Y";
        case DS4_RSTICK_X: return "right stick X";
        case DS4_RSTICK_Y: return "right stick Y";
        case ABS_RX: return "L2";
        case ABS_RY: return "R2";
        case ABS_HAT0X: return "D-pad X";
        case ABS_HAT0Y: return "D-pad Y";
        case ABS_MT_TRACKING_ID:
        case ABS_MT_POSITION_X:
        case ABS_MT_POSITION_Y: return g_can_touch ? "touchpad" : NULL; /* a DS3's pressures */
        default: return NULL;
    }
}

static const char *linux_abs_name(int code) {
    switch (code) {
        case 0x00: return "ABS_X";
        case 0x01: return "ABS_Y";
        case 0x02: return "ABS_Z";
        case 0x03: return "ABS_RX";
        case 0x04: return "ABS_RY";
        case 0x05: return "ABS_RZ";
        case 0x10: return "ABS_HAT0X";
        case 0x11: return "ABS_HAT0Y";
        case 0x2f: return "ABS_MT_SLOT";
        case 0x35: return "ABS_MT_POSITION_X";
        case 0x36: return "ABS_MT_POSITION_Y";
        case 0x39: return "ABS_MT_TRACKING_ID";
        case 0x3a: return "ABS_MT_PRESSURE";
        default: return NULL;
    }
}

/* the DS3's D-pad is four buttons where the other layouts have a hat switch: no table knows them,
 * and they become hat events (see ds3_dpad_event) */
static bool ds3_dpad_button(int code) { return code >= DS3_UP && code <= DS3_LEFT; }
static const char *ds3_dpad_name(int code) {
    switch (code) {
        case DS3_UP: return "D-pad up";
        case DS3_RIGHT: return "D-pad right";
        case DS3_DOWN: return "D-pad down";
        case DS3_LEFT: return "D-pad left";
        default: return NULL;
    }
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
/* code: as the node sent it, named in Linux's terms; mapped: what the tables are asked about
 * after the layout's normalisation (the same code on LG's order, -1 for a key they never know) */
static void probe_event(int type, int code, int mapped, int value) {
    if (!log_probe_on()) return;
    if (type == EV_KEY) {
        const char *std = linux_key_name(code), *read = "nothing";
        const char *verb = value == 1 ? "down" : value == 0 ? "up" : "repeat";
        for (size_t i = 0; i < g_btn_map_n; i++)
            if (g_btn_map[i].code == mapped) read = btn_name(g_btn_map[i].btn);
        if (g_layout == LAYOUT_DS3 && ds3_dpad_name(code)) read = ds3_dpad_name(code);
        log_probe("pad: key 0x%x %s %s, Hotaru reads %s", code, verb, std ? std : "unlisted", read);
    } else if (type == EV_ABS && code >= 0 && code < 64) {
        static int last[64];
        static bool seen[64];
        int step = g_abs_span[code] / 8; /* ranges are read at grab: 31 on a 0..255 axis, 1 on the hat */
        if (step < 1) step = 1;
        if (seen[code] && abs(value - last[code]) < step) return;
        seen[code] = true;
        last[code] = value;
        const char *std = linux_abs_name(code), *read = axis_role(mapped);
        log_probe("pad: abs 0x%02x%s%s = %d, Hotaru reads %s", code, std ? " " : "", std ? std : "", value,
                  read ? read : "nothing");
    }
}

/* ---------- battery (the HID device's power_supply) ---------- */

/* On webOS 6 hid-sony publishes the DualShock 4's level on the pad's uhid node, in
 * <hid device>/power_supply/<name>/capacity; on webOS 11 hid-playstation publishes the DualSense's
 * battery (ps-controller-battery-<mac>) and the same lookup finds it. Rather than enumerate
 * /sys/class/power_supply, the pad's own input node leads there: /sys/class/input/eventN/device
 * resolves to inputN, which sits in the HID device's input/ directory, so the HID device that owns
 * the power_supply is two levels up. A DualSense on webOS 6 is only hid-generic, which registers no
 * battery: no node, no reading. See docs/pad-battery-observed.txt and
 * docs/dualsense-map-linux-observed.txt. */
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

static void motion_close(void) {
    if (g_motion_fd < 0) return;
    ioctl(g_motion_fd, EVIOCGRAB, 0);
    close(g_motion_fd);
    g_motion_fd = -1;
    g_pad.motion = false;
}

/* the pad's own motion node: the same name with " Motion Sensors", the same uniq when both have one */
static bool motion_open(const char *pad_path, int pad_fd, const char *pad_name) {
    char want[200], uniq[64] = { 0 };
    snprintf(want, sizeof(want), "%s Motion Sensors", pad_name);
    bool have_uniq = ioctl(pad_fd, EVIOCGUNIQ(sizeof(uniq) - 1), uniq) >= 0 && uniq[0];
    DIR *d = opendir(INPUT_DIR);
    if (!d) return false;
    struct dirent *e;
    while (g_motion_fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300]; /* INPUT_DIR + d_name (up to 255) */
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, e->d_name);
        if (!strcmp(path, pad_path)) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[200] = { 0 }, u[64] = { 0 };
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        unsigned char abs[(ABS_CNT + 7) / 8] = { 0 };
        bool mine = !strcmp(name, want) &&
                    (!have_uniq || (ioctl(fd, EVIOCGUNIQ(sizeof(u) - 1), u) >= 0 && !strcmp(u, uniq))) &&
                    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) >= 0 && GETBIT(abs, ABS_RX) && GETBIT(abs, ABS_X);
        if (!mine) {
            close(fd);
            continue;
        }
        for (int a = 0; a < 6; a++) { /* ABS_X..ABS_RZ are 0..5 */
            struct input_absinfo ai;
            g_mres[a] = a < 3 ? 8192 : 1024; /* hid-playstation's, when the node does not say */
            if (ioctl(fd, EVIOCGABS(a), &ai) == 0 && ai.resolution > 0) g_mres[a] = ai.resolution;
            g_mraw[a] = a == ABS_Y ? g_mres[a] : 0; /* at rest: 1 g down */
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) log_msg("gamepad: motion %s grab FAILED: %s", path, strerror(errno));
        g_motion_fd = fd;
        g_mdirty = false;
        log_msg("gamepad: motion %s \"%s\" (accel %d/g, gyro %d per degree/s)", path, name, g_mres[0], g_mres[3]);
    }
    closedir(d);
    return g_motion_fd >= 0;
}

static void touch_close(void) {
    if (g_touch_fd < 0) return;
    ioctl(g_touch_fd, EVIOCGRAB, 0);
    close(g_touch_fd);
    g_touch_fd = -1;
}

/* the pad's own touchpad node: the same name with " Touchpad", the same uniq when both have one */
static bool touch_open(const char *pad_path, int pad_fd, const char *pad_name) {
    char want[200], uniq[64] = { 0 };
    snprintf(want, sizeof(want), "%s Touchpad", pad_name);
    bool have_uniq = ioctl(pad_fd, EVIOCGUNIQ(sizeof(uniq) - 1), uniq) >= 0 && uniq[0];
    DIR *d = opendir(INPUT_DIR);
    if (!d) return false;
    struct dirent *e;
    while (g_touch_fd < 0 && (e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300]; /* INPUT_DIR + d_name (up to 255) */
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, e->d_name);
        if (!strcmp(path, pad_path)) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[200] = { 0 }, u[64] = { 0 };
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        unsigned char abs[(ABS_CNT + 7) / 8] = { 0 };
        bool mine = !strcmp(name, want) &&
                    (!have_uniq || (ioctl(fd, EVIOCGUNIQ(sizeof(u) - 1), u) >= 0 && !strcmp(u, uniq))) &&
                    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) >= 0 && GETBIT(abs, ABS_MT_POSITION_X);
        if (!mine) {
            close(fd);
            continue;
        }
        struct input_absinfo ai;
        g_tx = (axis_t){ .min = 0, .max = 1919 }; /* hid-playstation's 1920 x 1080 when the node does not say */
        g_ty = (axis_t){ .min = 0, .max = 1079 };
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0 && ai.maximum > ai.minimum) {
            g_tx.min = ai.minimum;
            g_tx.max = ai.maximum;
        }
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0 && ai.maximum > ai.minimum) {
            g_ty.min = ai.minimum;
            g_ty.max = ai.maximum;
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0) log_msg("gamepad: touchpad %s grab FAILED: %s", path, strerror(errno));
        g_touch_fd = fd;
        g_touch_slot = 0;
        log_msg("gamepad: touchpad %s \"%s\" (x %d..%d, y %d..%d)", path, name, g_tx.min, g_tx.max, g_ty.min, g_ty.max);
    }
    closedir(d);
    return g_touch_fd >= 0;
}

/* every touch leaves here in one range, a PS5's 0..1919 x 0..1079 (chiaki-ng gui/src/streamsession.cpp),
 * whichever node and pad it came from; app.c rescales it for a PS4 */
static uint16_t touch_scale(const axis_t *a, int v, int top) {
    if (a->max <= a->min) return 0;
    if (v < a->min) v = a->min;
    if (v > a->max) v = a->max;
    return (uint16_t)((int32_t)(v - a->min) * top / (a->max - a->min));
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
        /* the TV's own nodes: "LGE ..." / "LG ..." and, on webOS 11, the virtual "Smart Remote RCU Input"
         * (keys 0x100..0x153 and ABS_X/Y, so it passes looks_like_gamepad) */
        bool lg = strncmp(name, "LGE", 3) == 0 || strncmp(name, "LG ", 3) == 0 ||
                  strcmp(name, "Smart Remote RCU Input") == 0,
             pad = !lg && looks_like_gamepad(fd);
        log_probe("pad: %s \"%s\": %s", path, name, lg ? "the TV's own, skipped" : pad ? "a gamepad" : "not a gamepad");
        if (!pad) {
            close(fd);
            continue;
        }
        unsigned char keys[(KEY_CNT + 7) / 8] = { 0 };
        bool std = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 && GETBIT(keys, BTN_THUMBR) &&
                   !GETBIT(keys, BTN_Z) && !GETBIT(keys, BTN_C);
        g_layout = LAYOUT_UHID;
        if (std) g_layout = LAYOUT_STD;
        else if (strstr(name, "PLAYSTATION(R)3")) g_layout = LAYOUT_DS3;
        log_msg("gamepad %s: %s layout", path,
                g_layout == LAYOUT_STD   ? "standard Linux (BTN_THUMBR, no BTN_C/Z)"
                : g_layout == LAYOUT_DS3 ? "hid-sony Sixaxis (DualShock 3)"
                                         : "LG uhid");
        if (ioctl(fd, EVIOCGRAB, 1) < 0) log_msg("gamepad %s grab FAILED: %s", path, strerror(errno));
        else log_msg("gamepad %s \"%s\" grabbed", path, name);
        struct {
            int code;
            axis_t *axis;
        } axes[] = { { ABS_X, &g_lx },        { ABS_Y, &g_ly },  { DS4_RSTICK_X, &g_rx },
                     { DS4_RSTICK_Y, &g_ry }, { ABS_RX, &g_l2 }, { ABS_RY, &g_r2 } };
        for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
            struct input_absinfo ai;
            int code = layout_raw_abs(axes[i].code);
            if (ioctl(fd, EVIOCGABS(code), &ai) == 0 && ai.maximum > ai.minimum) {
                axes[i].axis->min = ai.minimum;
                axes[i].axis->max = ai.maximum;
                axes[i].axis->value = (ai.minimum + ai.maximum) / 2;
            }
        }
        g_l2.value = g_l2.min; /* a released trigger is at the axis minimum, not the middle */
        g_r2.value = g_r2.min;
        /* named by the code each range came from: on the standard layout the right stick is RX/RY
         * and the triggers Z/RZ; a DualShock 3's triggers are pressures on 0x30/0x31 */
        const char *rsx = g_layout == LAYOUT_STD ? "RX" : "Z", *rsy = g_layout == LAYOUT_STD ? "RY" : "RZ";
        const char *l2 = g_layout == LAYOUT_STD ? "Z" : g_layout == LAYOUT_DS3 ? "0x30" : "RX";
        const char *r2 = g_layout == LAYOUT_STD ? "RZ" : g_layout == LAYOUT_DS3 ? "0x31" : "RY";
        log_msg("pad abs X %d..%d Y %d..%d %s %d..%d %s %d..%d %s %d..%d %s %d..%d", g_lx.min, g_lx.max, g_ly.min,
                g_ly.max, rsx, g_rx.min, g_rx.max, rsy, g_ry.min, g_ry.max, l2, g_l2.min, g_l2.max, r2, g_r2.min,
                g_r2.max);
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
        /* A DualShock 3 has no touchpad, but it sends its button pressures on axes named ABS_MT_*
         * too (0x35 among them), so its kind decides, not the bitmap (docs/ds3-map-observed.txt) */
        bool has_mt =
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0 && GETBIT(abs_bits, ABS_MT_POSITION_X);
        g_can_touch = has_mt && g_layout != LAYOUT_DS3;
        touch_close();
        if (g_can_touch) { /* touches on the pad's own node (a DualShock 4 on LG's uhid): a DS4's range */
            struct input_absinfo ai;
            g_tx = (axis_t){ .min = 0, .max = 1919 };
            g_ty = (axis_t){ .min = 0, .max = 941 };
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0 && ai.maximum > ai.minimum)
                g_tx = (axis_t){ .min = ai.minimum, .max = ai.maximum };
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0 && ai.maximum > ai.minimum)
                g_ty = (axis_t){ .min = ai.minimum, .max = ai.maximum };
        }
        if (!g_can_touch && g_layout == LAYOUT_STD) g_can_touch = touch_open(path, fd, name);
        motion_close();
        if (g_layout == LAYOUT_STD) motion_open(path, fd, name);
        g_motion_next_scan = SDL_GetTicks() + PAD_RESCAN_MS;
        g_motion_tries = TOUCH_TRIES;
        memset(g_abs_span, 0, sizeof(g_abs_span));
        for (int c = 0; c < ABS_CNT; c++) {
            struct input_absinfo ai;
            if (GETBIT(abs_bits, c) && ioctl(fd, EVIOCGABS(c), &ai) == 0 && ai.maximum > ai.minimum)
                g_abs_span[c] = ai.maximum - ai.minimum;
        }
        log_msg("gamepad: rumble %s, touchpad %s", g_can_rumble ? "yes" : "no",
                g_touch_fd >= 0          ? "touches and click (own node)"
                : g_can_touch            ? "touches and click"
                : g_layout == LAYOUT_STD ? "on its own node, not found yet"
                : g_layout == LAYOUT_DS3 ? "none"
                                         : "click only");
        probe_device(fd);
        memset(&g_pad, 0, sizeof(g_pad));
        g_ds3_dpad = 0;
        g_ff_id = -1;
        g_rum_l = g_rum_r = 0;
        g_pad_fd = fd;
        g_pad_seen = true;
        int pct;
        if (gamepad_battery(&pct)) log_msg("gamepad: battery %d%%", pct);
        g_pad_next_health = SDL_GetTicks() + PAD_HEALTH_MS;
        g_touch_next_scan = SDL_GetTicks() + PAD_RESCAN_MS;
        g_touch_tries = TOUCH_TRIES;
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
    touch_close();
    motion_close();
    scroll_reset();
    g_hatx = g_haty = 0;
    g_ds3_dpad = 0;
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

/* The DS3's D-pad: four buttons where the other layouts have a hat switch. They are turned into
 * hat events, so the bits, the UI keys and their repeat stay in one place (dpad_event()). */
#define DS3_DPAD_UP    1
#define DS3_DPAD_RIGHT 2
#define DS3_DPAD_DOWN  4
#define DS3_DPAD_LEFT  8

static void ds3_dpad_event(int code, int value) {
    static const struct {
        int code;
        uint8_t bit;
    } pad[] = {
        { DS3_UP, DS3_DPAD_UP }, { DS3_RIGHT, DS3_DPAD_RIGHT }, { DS3_DOWN, DS3_DPAD_DOWN }, { DS3_LEFT, DS3_DPAD_LEFT }
    };
    for (size_t i = 0; i < sizeof(pad) / sizeof(pad[0]); i++) {
        if (pad[i].code != code) continue;
        if (value) g_ds3_dpad |= pad[i].bit;
        else g_ds3_dpad = (uint8_t)(g_ds3_dpad & ~pad[i].bit);
        break;
    }
    dpad_event(ABS_HAT0X, (g_ds3_dpad & DS3_DPAD_RIGHT ? 1 : 0) - (g_ds3_dpad & DS3_DPAD_LEFT ? 1 : 0));
    dpad_event(ABS_HAT0Y, (g_ds3_dpad & DS3_DPAD_DOWN ? 1 : 0) - (g_ds3_dpad & DS3_DPAD_UP ? 1 : 0));
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

/* the touchpad node next to a standard-layout pad: its click is the Touchpad button, slot 0 the touch */
static void touch_tick(Uint32 now) {
    if (g_touch_fd < 0) {
        if (g_layout != LAYOUT_STD || g_can_touch || g_touch_tries <= 0 || (Sint32)(now - g_touch_next_scan) < 0)
            return;
        g_touch_next_scan = now + PAD_RESCAN_MS;
        g_touch_tries--; /* each look opens every node: a pad without a touchpad node stops after a few */
        if (touch_open(g_pad_path, g_pad_fd, g_pad_name)) {
            g_can_touch = true;
            notify_state(); /* Settings > Controller > Touchpad changes its caption */
        }
        return;
    }
    struct input_event ev;
    ssize_t n;
    while ((n = read(g_touch_fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.code == BTN_LEFT) {
            probe_event(ev.type, ev.code, DS_TOUCHPAD_CODE, ev.value);
            gamepad_button(DS_TOUCHPAD_CODE, ev.value);
        } else if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_SLOT) g_touch_slot = ev.value;
            else if (g_touch_slot != 0) continue; /* one finger: the first */
            else if (ev.code == ABS_MT_TRACKING_ID) g_pad.touch_down = ev.value >= 0;
            else if (ev.code == ABS_MT_POSITION_X) g_pad.touch_x = touch_scale(&g_tx, ev.value, 1919);
            else if (ev.code == ABS_MT_POSITION_Y) g_pad.touch_y = touch_scale(&g_ty, ev.value, 1079);
        }
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        log_msg("gamepad: touchpad lost: %s", n == 0 ? "end of stream" : strerror(errno));
        touch_close();
        g_can_touch = false;
        g_pad.touch_down = false;
        g_pad.buttons &= ~PAD_BTN_TOUCHPAD; /* a click held as the node went is released with it */
        g_touch_next_scan = now + PAD_RESCAN_MS;
        g_touch_tries = TOUCH_TRIES;
        notify_state();
    }
}

/* the motion node next to a standard-layout pad: a report is complete at SYN_REPORT */
static void motion_tick(Uint32 now) {
    if (g_motion_fd < 0) {
        if (g_layout != LAYOUT_STD || g_motion_tries <= 0 || (Sint32)(now - g_motion_next_scan) < 0) return;
        g_motion_next_scan = now + PAD_RESCAN_MS;
        g_motion_tries--; /* each look opens every node: a pad without one stops after a few */
        motion_open(g_pad_path, g_pad_fd, g_pad_name);
        return;
    }
    struct input_event ev;
    ssize_t n;
    while ((n = read(g_motion_fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS && ev.code <= ABS_RZ) {
            g_mraw[ev.code] = ev.value;
            g_mdirty = true;
        } else if (ev.type == EV_MSC && ev.code == MSC_TIMESTAMP) {
            g_mts = (uint32_t)ev.value;
            g_mdirty = true;
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT && g_mdirty) {
            const float deg2rad = 3.14159265f / 180.0f;
            for (int a = 0; a < 3; a++) {
                g_pad.accel[a] = (float)g_mraw[a] / (float)g_mres[a];
                g_pad.gyro[a] = deg2rad * (float)g_mraw[a + 3] / (float)g_mres[a + 3];
            }
            g_pad.motion_ts = g_mts;
            g_pad.motion_seq++;
            g_pad.motion = true;
            g_mdirty = false;
        }
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        log_msg("gamepad: motion lost: %s", n == 0 ? "end of stream" : strerror(errno));
        motion_close();
        g_motion_next_scan = now + PAD_RESCAN_MS;
        g_motion_tries = TOUCH_TRIES;
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
        int raw = ev.code;
        /* the DS3's D-pad: four buttons where a hat switch would be (no table knows them) */
        if (ev.type == EV_KEY && g_layout == LAYOUT_DS3 && ds3_dpad_button(raw)) {
            probe_event(ev.type, raw, -1, ev.value);
            ds3_dpad_event(raw, ev.value);
            continue;
        }
        if (ev.type == EV_KEY && g_layout != LAYOUT_UHID) {
            int code = layout_key(ev.code);
            probe_event(ev.type, raw, code, ev.value);
            if (code < 0) continue; /* no button the tables know */
            ev.code = (uint16_t)code;
        } else {
            if (ev.type == EV_ABS) ev.code = (uint16_t)layout_abs(ev.code);
            probe_event(ev.type, raw, ev.code, ev.value);
        }
        if (ev.type == EV_KEY) gamepad_button(ev.code, ev.value);
        else if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) g_lx.value = ev.value;
            else if (ev.code == ABS_Y) g_ly.value = ev.value;
            else if (ev.code == DS4_RSTICK_X) g_rx.value = ev.value;
            else if (ev.code == DS4_RSTICK_Y) g_ry.value = ev.value;
            else if (ev.code == ABS_RX) g_l2.value = ev.value; /* L2 analog is ABS_RX here */
            else if (ev.code == ABS_RY) g_r2.value = ev.value; /* R2 analog is ABS_RY here */
            else if (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y) dpad_event(ev.code, ev.value);
            /* a DS3 sends button pressures on axes named like these: g_can_touch is off for it */
            else if (g_can_touch && ev.code == ABS_MT_TRACKING_ID) g_pad.touch_down = ev.value >= 0;
            else if (g_can_touch && ev.code == ABS_MT_POSITION_X) g_pad.touch_x = touch_scale(&g_tx, ev.value, 1919);
            else if (g_can_touch && ev.code == ABS_MT_POSITION_Y) g_pad.touch_y = touch_scale(&g_ty, ev.value, 1079);
        }
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        gamepad_lost(n == 0 ? "end of stream" : strerror(errno));
        return;
    }
    touch_tick(now);
    motion_tick(now);
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
    out->motion = g_pad.motion;
    out->motion_seq = g_pad.motion_seq;
    out->motion_ts = g_pad.motion_ts;
    out->gyro_x = g_pad.gyro[0];
    out->gyro_y = g_pad.gyro[1];
    out->gyro_z = g_pad.gyro[2];
    out->accel_x = g_pad.accel[0];
    out->accel_y = g_pad.accel[1];
    out->accel_z = g_pad.accel[2];
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
