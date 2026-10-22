#ifndef PS3SX2_COVERDL_H
#define PS3SX2_COVERDL_H

#include <stddef.h>

#include "library.h"

/*
 * Downloading missing covers, minus the network itself so it can be tested on a PC.
 * Covers are saved as <PS2ISO folder>/covers/<SERIAL>.jpg, the first place
 * covers_candidates() looks.
 */

/* Front covers named by serial, the archive PCSX2 users rely on. "%s" becomes the serial. */
#define COVERDL_DEFAULT_URL "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/%s.jpg"

/* Builds the URL for `serial` from a template holding exactly one "%s". Returns 0 on success. */
int coverdl_url(const char *tmpl, const char *serial, char *out, size_t size);

/* True for a usable URL template: http(s), one "%s" and no other '%'. */
int coverdl_template_ok(const char *tmpl);

/* Where the cover for `g` gets saved. Returns 0 on success. */
int coverdl_target(const game_t *g, char *out, size_t size);

/* Non-zero when `g` has a serial and none of its cover files exists yet. */
int coverdl_needs_cover(const game_t *g);

/* Non-zero if the buffer starts like a JPEG, so an error page is never kept as a cover. */
int coverdl_is_jpeg(const unsigned char *data, size_t len);

/* Downloads `url` into `dst`: 0 on success, 1 when the archive has no such cover, -1 on errors. */
typedef int (*coverdl_fetch_fn)(const char *url, const char *dst, void *user);

/* Called before each game; return non-zero to stop. */
typedef int (*coverdl_progress_fn)(int done, int total, const game_t *g, void *user);

typedef struct {
    int total;       /* games that needed a cover */
    int downloaded;
    int missing;     /* not in the archive */
    int failed;      /* network or disk errors */
    int cancelled;
} coverdl_result;

/*
 * Fetches a cover for every game that lacks one, each serial once. Gives up
 * early after a few errors in a row, since that usually means no network.
 */
void coverdl_run(const library_t *lib, const char *tmpl, coverdl_fetch_fn fetch,
                 coverdl_progress_fn progress, void *user, coverdl_result *out);

#endif
