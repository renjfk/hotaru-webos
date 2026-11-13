/* ui/home.h - Home, the main menu: a TV screen in the spirit of the PS5 home screen.
 *
 * "Consoles | Settings" at the top switch the tile row below. Consoles: a tile per console
 * (app.h hosts, one per console even when it has a PIN pairing and a PSN entry) plus "Add a
 * console"; the focused tile is enlarged at the left edge with its name beside it, and under the
 * row the console's title, Start Remote Play, the connection route (when it has two: local network
 * first, PSN on request) and an options button (Wake up, Forget), then quick cards that jump to
 * their setting. The backdrop tint and the key art on the right follow the focus. Settings: a tile
 * per panel (home_settings.c), whose rows change values in place with LEFT/RIGHT.
 *
 * Driven by keys only (no pointer): ui.c forwards LV_KEY_* from the key sink that holds the main
 * group's focus. While a session connects Home shows a "Connecting" screen (Circle cancels);
 * while it streams Home is hidden (ui.c) and the stream menu takes over.
 */
#ifndef HOTARU_HOME_H
#define HOTARU_HOME_H

#include <stdbool.h>
#include <lvgl.h>

#include "../app.h"
#include "../input/input.h"

void home_create(lv_obj_t *parent);
void home_key(uint32_t key);     /* LV_KEY_UP/DOWN/LEFT/RIGHT/ENTER from the key sink */
bool home_back(void);            /* Circle / Back: false when there is nothing to go back from */
void home_section_step(int dir); /* L1 / R1: Consoles <-> Settings */
void home_app_changed(app_change_t what);
void home_set_pad(pad_state_t state);
void home_show(bool on, bool fade); /* hidden while a session streams; fade: in from nothing */
lv_obj_t *home_scroll_target(void); /* what the right stick scrolls: the log, on its panel */

/* for tests */
typedef enum { HOME_TOP, HOME_ROW, HOME_ACTIONS, HOME_CARDS, HOME_PANEL } home_zone_t;
enum { HOME_CONSOLES, HOME_SETTINGS };
enum { HOME_ACT_PLAY, HOME_ACT_ROUTE, HOME_ACT_OPTIONS };
home_zone_t home_zone(void);
int home_section(void);
int home_index(void);       /* the focused tile of the section */
int home_item(void);        /* the focused action, card or settings row */
bool home_menu_open(void);  /* the route or options pop-up */
bool home_connecting(void); /* the "Connecting" screen is up */
lv_obj_t *home_root(void);
const char *home_update_note(void); /* the update notice's first line while it is up, "" otherwise */
lv_obj_t *home_row(int section);    /* the tile row: tiles past the screen edge are by design */
const char *home_pad_battery(void); /* the quick card's battery line, "" when the pad reports none */

#endif
