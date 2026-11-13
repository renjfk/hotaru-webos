/* media/pacer.h - even playout for frames that arrive in bursts.
 *
 * Over Wi-Fi the console's frames can arrive in bursts: nothing for 100+ ms, then several
 * at once. Shown on arrival, that is a freeze and a jump. The pacer gives every frame a play
 * time on a steady cadence (one frame interval apart) anchored max_ms after the first
 * arrival, so a burst is played out evenly as long as the stall was shorter than the delay.
 *
 * The delay adapts down to what the stalls need: every PACER_WINDOW_MS it looks at the
 * smallest lead it had (how early the most delayed frame still arrived) and plays up to 2%
 * fast or slow until that lead is PACER_MARGIN_MS. A frame that arrives after its slot (a
 * longer stall) restarts the cadence with the full max_ms delay. Pure logic, no clock of its
 * own: times are in whatever millisecond clock the caller passes.
 */
#ifndef CHIAKI_WEBOS_PACER_H
#define CHIAKI_WEBOS_PACER_H

#define PACER_MARGIN_MS 15.0    /* lead kept in hand: stalls vary (109-142 ms seen on the TV) */
#define PACER_WINDOW_MS 10000.0 /* how long a stall pattern is watched before the delay shrinks */
#define PACER_MAX_SPEED 0.02    /* the cadence may run this much fast or slow */

typedef struct {
    double next_ms;    /* play time of the previous frame; 0 = start over */
    double adjust_ms;  /* added to every frame interval (negative = play faster) */
    double win_min;    /* smallest lead in the current window */
    double win_ms;     /* window length so far, in frame intervals */
    double delay_ms;   /* the delay the pacer currently adds (smoothed lead) */
    unsigned restarts; /* frames that arrived after their slot */
} pacer_t;

void pacer_reset(pacer_t *p);
/* the time the frame that arrived at arrival_ms should be shown; interval_ms is the frame
 * period, max_ms the most delay to add (> 0) */
double pacer_schedule(pacer_t *p, double arrival_ms, double interval_ms, double max_ms);

#endif
