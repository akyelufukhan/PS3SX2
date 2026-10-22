#ifndef PS3SX2_LAUNCHER_H
#define PS3SX2_LAUNCHER_H

#include "library.h"

/* Sony's PS2 Classics Placeholder, which plays whatever ISO.BIN.ENC sits in its USRDIR. */
#define PS2_PLACEHOLDER_DIR "/dev_hdd0/game/PS2U10000"

typedef enum {
    LAUNCH_OK = 0,              /* .BIN.ENC is in the placeholder; start it from the XMB */
    LAUNCH_OK_DISC,             /* .iso is mounted as a PS2 disc; start it from the XMB */
    LAUNCH_ERR_NO_PLACEHOLDER,  /* PS2 Classics Placeholder isn't installed */
    LAUNCH_ERR_COPY,            /* reading the game or writing to the placeholder failed */
    LAUNCH_ERR_CANCELLED,
    LAUNCH_ERR_NO_COBRA,        /* .iso needs a CFW with Cobra; HEN and OFW can't mount it */
    LAUNCH_ERR_ISO_NOT_ON_HDD,  /* Cobra mounts PS2 ISOs from the internal drive only */
    LAUNCH_ERR_MOUNT,           /* Cobra refused the image */
} launch_result;

/* Called while copying; return non-zero to cancel. */
typedef int (*launch_progress_cb)(unsigned long long done, unsigned long long total, void *user);

/*
 * The console-specific half of launching, so the rules here can be tested on a
 * PC. main.c plugs in the Cobra implementation; without it .iso games report
 * LAUNCH_ERR_NO_COBRA.
 */
typedef struct {
    int (*can_mount_iso)(void);              /* non-zero on a CFW with Cobra */
    int (*mount_ps2_iso)(const char *path);  /* 0 once the disc is mounted and inserted */
} launch_platform;

void launcher_set_platform(const launch_platform *platform);

/*
 * Folders searched, in order, for a ps2_netemu CONFIG named after the disc's
 * boot file (e.g. SLES_542.03.CONFIG). The array must outlive the launcher.
 */
void launcher_set_config_dirs(const char *const *dirs, int count);

/*
 * Cobra hands ps2_netemu the file <iso>.CONFIG when it sits next to the image.
 * If there's none yet, copies the first match from the config folders there; a
 * CONFIG the user put next to the ISO always wins. Returns 1 if the ISO has a
 * CONFIG afterwards, 0 if not.
 */
int launcher_place_iso_config(const game_t *g);

/*
 * PS2 Classics route, works on HEN and CFW: copies the encrypted image into the
 * placeholder (plus its .CONFIG, if one sits next to it). The user then starts
 * "PS2 Classics Placeholder" from the XMB.
 */
launch_result launch_ps2classic(const game_t *g, const char *placeholder_dir,
                                launch_progress_cb cb, void *user);

/*
 * Raw ISO route, CFW with Cobra only: mounts the ISO as a PS2 DVD. The firmware
 * shows it as an inserted disc and runs it with ps2_netemu on Slim consoles.
 */
launch_result launch_ps2iso(const game_t *g);

/* Picks the route that matches the game's format. */
launch_result launcher_start(const game_t *g, launch_progress_cb cb, void *user);

#endif
