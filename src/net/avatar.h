/* net/avatar.h - the PSN accounts' avatar pictures.
 *
 * avatar_fetch() asks PSN for an account's profile with its access token on a worker thread,
 * downloads the avatar it names and keeps it next to hotaru.conf (avatar-<account id>.png), so
 * later starts show it without the network. avatar_poll() tells the main thread a new one
 * arrived; avatar_image() decodes the kept file once and hands LVGL a picture, NULL while there
 * is none (the UI keeps the initials then). Without libcurl (the simulator, the host tests)
 * nothing is fetched and only a kept file is shown. */
#ifndef HOTARU_NET_AVATAR_H
#define HOTARU_NET_AVATAR_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

void avatar_fetch(const uint8_t account_id[8], const char *access_token); /* main thread */
bool avatar_poll(void);                                                   /* main thread: true once per new picture */
const lv_img_dsc_t *avatar_image(const uint8_t account_id[8]);            /* main thread; NULL: none yet */
void avatar_forget(const uint8_t account_id[8]);                          /* signed out: drop the picture and file */

#endif
