#include "autocovers.h"

#include "coverdl.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>

#include <sys/thread.h>

#define THREAD_PRIORITY 1500           /* lower than the main loop, so drawing never waits */
#define THREAD_STACK    (256 * 1024)   /* TLS handshakes need room */

static const library_t *job_lib;
static char job_tmpl[256];
static sys_ppu_thread_t thread;
static int active;                       /* a thread exists and hasn't been joined */
static volatile int finished;            /* the thread is done */
static volatile int stop_requested;
static volatile int done_count, total_count;
static volatile unsigned char *saved;    /* one flag per game, set by the thread */

static int on_progress(int done, int total, const game_t *g, void *user)
{
    (void)g;
    (void)user;
    done_count = done;
    total_count = total;
    return stop_requested;
}

static void on_saved(const game_t *g, void *user)
{
    (void)user;
    /* The cover is renamed into place before the main thread may see the flag. */
    __sync_synchronize();
    saved[g - job_lib->games] = 1;
}

static void worker(void *arg)
{
    coverdl_result r;

    (void)arg;
    coverdl_run(job_lib, job_tmpl, net_fetch, on_progress, on_saved, NULL, &r);
    done_count = r.total;
    __sync_synchronize();
    finished = 1;
    sysThreadExit(0);
}

int autocovers_start(const library_t *lib, const char *tmpl)
{
    int i, needed = 0;

    if (active)
        return 0;
    for (i = 0; i < lib->count; i++)
        needed += coverdl_needs_cover(&lib->games[i]);
    if (!needed || !net_connected())
        return 0;
    if (!(saved = calloc(lib->count, 1)))
        return 0;

    job_lib = lib;
    snprintf(job_tmpl, sizeof(job_tmpl), "%s", tmpl);
    stop_requested = 0;
    finished = 0;
    done_count = 0;
    total_count = needed;
    net_cancel(0);

    if (sysThreadCreate(&thread, worker, NULL, THREAD_PRIORITY, THREAD_STACK, THREAD_JOINABLE, "ps3sx2_covers") != 0) {
        free((void *)saved);
        saved = NULL;
        return 0;
    }
    active = 1;
    return 1;
}

void autocovers_stop(void)
{
    u64 ret;

    if (!active)
        return;
    stop_requested = 1;
    net_cancel(1);
    sysThreadJoin(thread, &ret);
    net_cancel(0);
    free((void *)saved);
    saved = NULL;
    active = 0;
}

int autocovers_running(void)
{
    return active && !finished;
}

void autocovers_progress(int *done, int *total)
{
    *done = done_count;
    *total = total_count;
}

int autocovers_take_saved(void)
{
    int i, done;

    if (!active)
        return -1;
    done = finished;
    __sync_synchronize();
    for (i = 0; i < job_lib->count; i++) {
        if (saved[i]) {
            saved[i] = 0;
            return i;
        }
    }
    /* Nothing left to hand out from a finished thread: reap it. */
    if (done)
        autocovers_stop();
    return -1;
}
