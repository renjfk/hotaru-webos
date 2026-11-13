/* media/feeder.c - bounded copy queue + worker thread (see feeder.h). */
#include "feeder.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int tag;
    uint32_t pushed_ms;
    uint8_t *data;
    size_t len, cap;
} slot_t;

struct feeder {
    SDL_mutex *lock;
    SDL_cond *cond;
    SDL_Thread *thread;
    slot_t *slots;
    unsigned n, head, count; /* the queued slots are head .. head + count - 1 (mod n) */
    bool stop;
    feeder_fn fn;
    void *user;
    feeder_stats_t stats;
};

static int feeder_thread(void *arg) {
    feeder_t *f = arg;
    SDL_LockMutex(f->lock);
    for (;;) {
        while (!f->count && !f->stop) SDL_CondWait(f->cond, f->lock);
        if (f->stop) break;
        /* the head slot stays out of the producers' reach until count drops, so it is read unlocked */
        slot_t *s = &f->slots[f->head];
        SDL_UnlockMutex(f->lock);
        f->fn(s->tag, s->data, s->len, s->pushed_ms, f->user);
        SDL_LockMutex(f->lock);
        f->head = (f->head + 1) % f->n;
        f->count--;
    }
    SDL_UnlockMutex(f->lock);
    return 0;
}

static void feeder_free(feeder_t *f) {
    if (f->slots)
        for (unsigned i = 0; i < f->n; i++) free(f->slots[i].data);
    free(f->slots);
    if (f->cond) SDL_DestroyCond(f->cond);
    if (f->lock) SDL_DestroyMutex(f->lock);
    free(f);
}

feeder_t *feeder_start(const char *name, unsigned slots, feeder_fn fn, void *user) {
    if (!slots || !fn) return NULL;
    feeder_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->n = slots;
    f->fn = fn;
    f->user = user;
    f->slots = calloc(slots, sizeof(*f->slots));
    f->lock = SDL_CreateMutex();
    f->cond = SDL_CreateCond();
    if (f->slots && f->lock && f->cond) f->thread = SDL_CreateThread(feeder_thread, name, f);
    if (!f->thread) {
        feeder_free(f);
        return NULL;
    }
    return f;
}

bool feeder_push(feeder_t *f, int tag, const void *data, size_t len) {
    if (!f) return false;
    SDL_LockMutex(f->lock);
    if (f->count == f->n || f->stop) {
        f->stats.dropped++;
        SDL_UnlockMutex(f->lock);
        return false;
    }
    slot_t *s = &f->slots[(f->head + f->count) % f->n];
    if (s->cap < len) {
        /* frame sizes vary: grow with headroom so a slot rarely has to grow again */
        size_t cap = len + len / 4;
        uint8_t *p = malloc(cap);
        if (!p) {
            f->stats.dropped++;
            SDL_UnlockMutex(f->lock);
            return false;
        }
        free(s->data);
        s->data = p;
        s->cap = cap;
    }
    if (len) memcpy(s->data, data, len);
    s->tag = tag;
    s->len = len;
    s->pushed_ms = SDL_GetTicks();
    f->count++;
    f->stats.pushed++;
    if (f->count > f->stats.depth_max) f->stats.depth_max = f->count;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->lock);
    return true;
}

void feeder_stats(feeder_t *f, feeder_stats_t *out, bool reset) {
    if (!f) {
        memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(f->lock);
    *out = f->stats;
    if (reset) {
        memset(&f->stats, 0, sizeof(f->stats));
        f->stats.depth_max = f->count;
    }
    SDL_UnlockMutex(f->lock);
}

unsigned feeder_slots(const feeder_t *f) { return f ? f->n : 0; }

unsigned feeder_depth(feeder_t *f) {
    if (!f) return 0;
    SDL_LockMutex(f->lock);
    unsigned n = f->count;
    SDL_UnlockMutex(f->lock);
    return n;
}

void feeder_stop(feeder_t *f) {
    if (!f) return;
    SDL_LockMutex(f->lock);
    f->stop = true;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->lock);
    SDL_WaitThread(f->thread, NULL);
    feeder_free(f);
}
