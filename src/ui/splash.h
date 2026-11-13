/* ui/splash.h - the startup overlay (see splash.c).
 *
 * splash_show() builds a full-screen overlay on the top layer with the app mark
 * and a short "starting" state, then dismisses itself after SPLASH_MS (or on
 * splash_hide()). main.c calls it once, after ui_create(); HOTARU_NO_SPLASH=1
 * skips it (useful for fast UI iteration in the simulator).
 */
#ifndef HOTARU_SPLASH_H
#define HOTARU_SPLASH_H

#include <stdbool.h>

void splash_show(void); /* build + fade in, auto-dismiss after SPLASH_MS */
void splash_hide(void); /* dismiss now (no-op if not shown) */
bool splash_active(void);

#endif
