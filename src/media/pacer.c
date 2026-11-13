/* media/pacer.c - even playout for frames that arrive in bursts (see pacer.h). */
#include "pacer.h"

#include <string.h>

void pacer_reset(pacer_t *p) { memset(p, 0, sizeof(*p)); }

double pacer_schedule(pacer_t *p, double arrival_ms, double interval_ms, double max_ms) {
    if (p->next_ms <= 0) {
        p->next_ms = arrival_ms + max_ms;
    } else {
        p->next_ms += interval_ms + p->adjust_ms;
        if (p->next_ms < arrival_ms) { /* a stall longer than the delay: start over with all of it */
            p->next_ms = arrival_ms + max_ms;
            p->adjust_ms = 0;
            p->win_ms = 0;
            p->restarts++;
        } else if (p->next_ms > arrival_ms + max_ms) { /* never hold a frame longer than asked */
            p->next_ms = arrival_ms + max_ms;
        }
    }
    double lead = p->next_ms - arrival_ms;
    if (p->win_ms <= 0 || lead < p->win_min) p->win_min = lead;
    p->delay_ms = p->delay_ms > 0 ? p->delay_ms * 0.95 + lead * 0.05 : lead;
    p->win_ms += interval_ms;
    if (p->win_ms >= PACER_WINDOW_MS) {
        /* spend the spare lead (or win some back) over the next window, within PACER_MAX_SPEED */
        double frames = PACER_WINDOW_MS / interval_ms;
        double adj = (PACER_MARGIN_MS - p->win_min) / frames;
        double lim = interval_ms * PACER_MAX_SPEED;
        p->adjust_ms = adj < -lim ? -lim : adj > lim ? lim : adj;
        p->win_ms = 0;
    }
    return p->next_ms;
}
