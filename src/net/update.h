/* net/update.h - whether a newer release is out: one look at GitHub per launch.
 *
 * update_start() asks GitHub's API for the latest release of UPDATE_REPO on a worker thread, so the
 * main loop never waits on the network; update_poll() hands the answer to the main thread. "Latest"
 * is GitHub's: the newest release that is neither a draft nor a prerelease. Its tag (vMAJOR.MINOR,
 * see RELEASE_PROCESS.md) is newer when it is above CHIAKI_WEBOS_VERSION part by part, so a
 * prerelease build (<base>.<run number>) hears of the release after its base, not of its base. One
 * .ipk serves every TV (one ABI, see docs/STATUS.md), so the tag is all there is to compare.
 *
 * Without libchiaki there is no libcurl (the host tests, a simulator without out/chiaki-lib-macos)
 * and nothing is asked. CHIAKI_UPDATE_LATEST=<tag> answers in GitHub's place in every build (to see
 * the notice in the simulator). A failure (no network yet, no release, GitHub's rate limit) is only
 * logged: the next launch asks again.
 */
#ifndef CHIAKI_WEBOS_UPDATE_H
#define CHIAKI_WEBOS_UPDATE_H

#include <stdbool.h>

#define UPDATE_REPO "renjfk/chiaki-webos"

void update_start(void);         /* main thread: asks once per run, later calls do nothing */
bool update_poll(void);          /* main thread: true once for every answer that arrives */
const char *update_latest(void); /* the newer release's version ("0.3"), "" while none is known */
/* tag "v0.3" (the v is optional, missing parts are 0) against installed "0.2.0"; not a version: false */
bool update_newer(const char *tag, const char *installed);

/* for tests: an answer as if GitHub had given `tag` (update_poll() reports it) */
void update_fake(const char *tag);

#endif
