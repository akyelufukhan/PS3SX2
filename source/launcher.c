#include "launcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_CHUNK (1024 * 1024)

static const launch_platform *platform;

void launcher_set_platform(const launch_platform *p)
{
    platform = p;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static launch_result copy_file(const char *src, const char *dst, launch_progress_cb cb, void *user)
{
    unsigned long long total, done = 0;
    launch_result r = LAUNCH_OK;
    struct stat st;
    FILE *in, *out;
    char *buf;
    size_t n;

    if (stat(src, &st) != 0 || !(in = fopen(src, "rb")))
        return LAUNCH_ERR_COPY;
    if (!(out = fopen(dst, "wb"))) {
        fclose(in);
        return LAUNCH_ERR_COPY;
    }
    total = (unsigned long long)st.st_size;

    if (!(buf = malloc(COPY_CHUNK)))
        r = LAUNCH_ERR_COPY;

    while (r == LAUNCH_OK && (n = fread(buf, 1, COPY_CHUNK, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            r = LAUNCH_ERR_COPY;
            break;
        }
        done += n;
        if (cb && cb(done, total, user))
            r = LAUNCH_ERR_CANCELLED;
    }
    if (r == LAUNCH_OK && (ferror(in) || done != total))
        r = LAUNCH_ERR_COPY;

    free(buf);
    fclose(in);
    if (fclose(out) != 0 && r == LAUNCH_OK)
        r = LAUNCH_ERR_COPY;
    if (r != LAUNCH_OK)
        unlink(dst);
    return r;
}

/* "<name>.BIN.ENC.CONFIG" or "<name>.CONFIG", the names webMAN MOD looks for too. */
static int find_config(const char *game_path, char *out, size_t outsize)
{
    size_t len = strlen(game_path);

    if (snprintf(out, outsize, "%s.CONFIG", game_path) < (int)outsize && is_file(out))
        return 0;
    if (len > 8 && strcasecmp(game_path + len - 8, ".BIN.ENC") == 0 &&
        snprintf(out, outsize, "%.*s.CONFIG", (int)(len - 8), game_path) < (int)outsize && is_file(out))
        return 0;
    return -1;
}

launch_result launch_ps2classic(const game_t *g, const char *placeholder_dir,
                                launch_progress_cb cb, void *user)
{
    char usrdir[GAME_PATH_MAX], iso[GAME_PATH_MAX], tmp[GAME_PATH_MAX];
    char config_src[GAME_PATH_MAX], config_dst[GAME_PATH_MAX];
    launch_result r;

    if (!is_dir(placeholder_dir))
        return LAUNCH_ERR_NO_PLACEHOLDER;

    /* A cut-off path would point somewhere else entirely, so refuse instead. */
    if (snprintf(usrdir, sizeof(usrdir), "%s/USRDIR", placeholder_dir) >= (int)sizeof(usrdir) ||
        snprintf(iso, sizeof(iso), "%s/ISO.BIN.ENC", usrdir) >= (int)sizeof(iso) ||
        snprintf(tmp, sizeof(tmp), "%s/ISO.BIN.ENC.tmp", usrdir) >= (int)sizeof(tmp) ||
        snprintf(config_dst, sizeof(config_dst), "%s/CONFIG", usrdir) >= (int)sizeof(config_dst))
        return LAUNCH_ERR_COPY;
    mkdir(usrdir, 0777);

    /* Drop the previous game first: the drive may not have room for two images,
       and a CONFIG left over from it could break this one. */
    unlink(iso);
    unlink(tmp);
    unlink(config_dst);

    /* Copy under a temporary name so a cut-off copy never looks like a playable game. */
    if ((r = copy_file(g->path, tmp, cb, user)) != LAUNCH_OK)
        return r;
    if (rename(tmp, iso) != 0) {
        unlink(tmp);
        return LAUNCH_ERR_COPY;
    }

    if (find_config(g->path, config_src, sizeof(config_src)) == 0)
        return copy_file(config_src, config_dst, NULL, NULL);
    return LAUNCH_OK;
}

launch_result launch_ps2iso(const game_t *g)
{
    if (!platform || !platform->can_mount_iso || !platform->can_mount_iso())
        return LAUNCH_ERR_NO_COBRA;

    /* Cobra only reads PS2 images straight off the internal drive; IRISMAN and
       webMAN MOD have the same limit for USB drives. */
    if (strncmp(g->path, "/dev_hdd0/", 10) != 0)
        return LAUNCH_ERR_ISO_NOT_ON_HDD;

    /* Flags Cobra 8.x leaves behind that would make the firmware treat the next
       disc as a PS2 Classic or as a real optical disc. */
    unlink("/dev_hdd0/classic_ps2");
    unlink("/dev_hdd0/tmp/loadoptical");

    return platform->mount_ps2_iso(g->path) == 0 ? LAUNCH_OK_DISC : LAUNCH_ERR_MOUNT;
}

launch_result launcher_start(const game_t *g, launch_progress_cb cb, void *user)
{
    if (g->format == GAME_FMT_CLASSIC)
        return launch_ps2classic(g, PS2_PLACEHOLDER_DIR, cb, user);
    return launch_ps2iso(g);
}
