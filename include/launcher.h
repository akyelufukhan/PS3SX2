#ifndef PS3SX2_LAUNCHER_H
#define PS3SX2_LAUNCHER_H

#include "library.h"

/* Sony's PS2 Classics Placeholder, which plays whatever ISO.BIN.ENC sits in its USRDIR. */
#define PS2_PLACEHOLDER_DIR "/dev_hdd0/game/PS2U10000"

typedef enum {
    LAUNCH_OK = 0,
    LAUNCH_ERR_NO_PLACEHOLDER,  /* PS2 Classics Placeholder isn't installed */
    LAUNCH_ERR_COPY,            /* reading the game or writing to the placeholder failed */
    LAUNCH_ERR_CANCELLED,
    LAUNCH_ERR_NOT_IMPLEMENTED, /* raw .iso launching via Cobra isn't written yet */
} launch_result;

/* Called while copying; return non-zero to cancel. */
typedef int (*launch_progress_cb)(unsigned long long done, unsigned long long total, void *user);

/*
 * PS2 Classics route, works on HEN and CFW: copies the encrypted image into the
 * placeholder (plus its .CONFIG, if one sits next to it). The user then starts
 * "PS2 Classics Placeholder" from the XMB.
 */
launch_result launch_ps2classic(const game_t *g, const char *placeholder_dir,
                                launch_progress_cb cb, void *user);

/*
 * Raw ISO route, Cobra CFW only: mount the ISO as a PS2 disc and let the firmware
 * hand it to ps2_netemu. Not implemented yet, see docs/launching.md.
 */
launch_result launch_ps2iso(const game_t *g);

/* Picks the route that matches the game's format. */
launch_result launcher_start(const game_t *g, launch_progress_cb cb, void *user);

#endif
