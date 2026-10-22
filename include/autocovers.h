#ifndef PS3SX2_AUTOCOVERS_H
#define PS3SX2_AUTOCOVERS_H

#include "library.h"

/*
 * Downloads missing covers on a background thread while the carousel keeps
 * running. PS3 only. The library must not change while it runs: call
 * autocovers_stop() before rescanning, before a manual download and on exit.
 */

/* Starts when some game needs a cover and the network is up (net_start() first). Returns 1 if it did. */
int autocovers_start(const library_t *lib, const char *tmpl);

/* Cancels a running download, waits for the thread and frees everything. */
void autocovers_stop(void);

/* Non-zero while covers are still being fetched. */
int autocovers_running(void);

/* Covers handled so far and how many there are in total. */
void autocovers_progress(int *done, int *total);

/*
 * Index of a game whose cover just arrived, or -1. Call it every frame until it
 * returns -1; once the thread has finished it also cleans up after it.
 */
int autocovers_take_saved(void);

#endif
