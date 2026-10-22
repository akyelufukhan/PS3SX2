/*
 * PS3SX2 - a cover-flow launcher for PS2 games on PS3 Slim and Super Slim.
 */
#include <stdio.h>
#include <string.h>

#include <io/pad.h>
#include <sysmodule/sysmodule.h>
#include <sysutil/sysutil.h>

#include "cobra.h"
#include "coverdl.h"
#include "coverflow.h"
#include "gfx.h"
#include "lang.h"
#include "launcher.h"
#include "library.h"
#include "net.h"

#define PADS          7
#define REPEAT_DELAY  18  /* frames before a held direction starts repeating */
#define REPEAT_RATE   5   /* frames between repeats */
#define FAST_STEP     5   /* covers skipped by L1 / R1 */

#define APP_USRDIR    "/dev_hdd0/game/PS3SX2001/USRDIR"
#define CA_BUNDLE     APP_USRDIR "/cacert.pem"
#define COVER_URL_TXT "/dev_hdd0/PS2ISO/covers_url.txt"  /* optional: another cover archive */

enum {
    IN_LEFT     = 1 << 0,
    IN_RIGHT    = 1 << 1,
    IN_L1       = 1 << 2,
    IN_R1       = 1 << 3,
    IN_CROSS    = 1 << 4,
    IN_CIRCLE   = 1 << 5,
    IN_TRIANGLE = 1 << 6,
    IN_SQUARE   = 1 << 7,
};

typedef struct {
    u32 held;
    u32 pressed;  /* went down this frame, or auto-repeated */
    int hold_frames;
} input_state;

typedef struct {
    int active;
    char title[160];
    char hint[192];
    int exit_on_close;
} message;

static volatile int running = 1;
static u32 pad_buttons[PADS];
static input_state input;
static library_t lib;
static int sel;
static float pos;
static message msg;

static void on_sysutil(u64 status, u64 param, void *user)
{
    (void)param;
    (void)user;
    if (status == SYSUTIL_EXIT_GAME)
        running = 0;
}

/* Buttons held on any connected pad. ioPadGetData only reports changes, so keep the last state. */
static u32 poll_pads(void)
{
    padInfo info;
    padData data;
    u32 all = 0;
    int i;

    ioPadGetInfo(&info);
    for (i = 0; i < PADS; i++) {
        if (!info.status[i]) {
            pad_buttons[i] = 0;
            continue;
        }
        if (ioPadGetData(i, &data) == 0 && data.len > 0) {
            u32 b = 0;

            if (data.BTN_LEFT)     b |= IN_LEFT;
            if (data.BTN_RIGHT)    b |= IN_RIGHT;
            if (data.BTN_L1)       b |= IN_L1;
            if (data.BTN_R1)       b |= IN_R1;
            if (data.BTN_CROSS)    b |= IN_CROSS;
            if (data.BTN_CIRCLE)   b |= IN_CIRCLE;
            if (data.BTN_TRIANGLE) b |= IN_TRIANGLE;
            if (data.BTN_SQUARE)   b |= IN_SQUARE;
            pad_buttons[i] = b;
        }
        all |= pad_buttons[i];
    }
    return all;
}

static void update_input(void)
{
    const u32 nav = IN_LEFT | IN_RIGHT | IN_L1 | IN_R1;
    u32 now = poll_pads();

    input.pressed = now & ~input.held;
    if ((now & nav) && (now & nav) == (input.held & nav)) {
        input.hold_frames++;
        if (input.hold_frames >= REPEAT_DELAY && (input.hold_frames - REPEAT_DELAY) % REPEAT_RATE == 0)
            input.pressed |= now & nav;
    } else {
        input.hold_frames = 0;
    }
    input.held = now;
}

static void show_message_text(const char *title, const char *hint, int exit_on_close)
{
    msg.active = 1;
    snprintf(msg.title, sizeof(msg.title), "%s", title);
    snprintf(msg.hint, sizeof(msg.hint), "%s", hint);
    msg.exit_on_close = exit_on_close;
}

static void show_message(str_id title, str_id hint, int exit_on_close)
{
    show_message_text(lang_str(title), lang_str(hint), exit_on_close);
}

static void scan_library(void)
{
    library_free(&lib);
    library_scan_default(&lib);
    library_sort(&lib);
    gfx_reset_covers();
    if (sel >= lib.count)
        sel = lib.count > 0 ? lib.count - 1 : 0;
    pos = (float)sel;
}

/* Draws one frame with a panel over the carousel, for work that blocks the main loop. */
static void busy_frame(const char *title, const char *detail, float progress)
{
    gfx_frame_begin();
    gfx_draw_library(&lib, pos, sel, 0);
    gfx_draw_overlay(title, detail, progress, 0);
    gfx_frame_end();
}

/* Keeps the screen alive while a game is copied into the placeholder. */
static int on_copy_progress(unsigned long long done, unsigned long long total, void *user)
{
    const game_t *g = user;

    sysUtilCheckCallback();
    update_input();

    busy_frame(lang_str(STR_COPYING), g->title, total ? (float)((double)done / total) : 0.0f);

    return !running || (input.pressed & IN_CIRCLE);
}

static void launch_selected(void)
{
    const game_t *g = &lib.games[sel];

    /* Mounting blocks for a few seconds while the XMB catches up; say so first. */
    if (g->format == GAME_FMT_ISO) {
        busy_frame(lang_str(STR_MOUNTING), g->title, -1.0f);
    }

    switch (launcher_start(g, on_copy_progress, (void *)g)) {
    case LAUNCH_OK:
        show_message(STR_READY_TITLE, STR_READY_HINT, 1);
        break;
    case LAUNCH_OK_DISC:
        show_message(STR_READY_DISC_TITLE, STR_READY_DISC_HINT, 1);
        break;
    case LAUNCH_ERR_NO_PLACEHOLDER:
        show_message(STR_ERR_NO_PLACEHOLDER, STR_ERR_NO_PLACEHOLDER_HINT, 0);
        break;
    case LAUNCH_ERR_CANCELLED:
        show_message(STR_ERR_CANCELLED, STR_COUNT, 0);
        break;
    case LAUNCH_ERR_NO_COBRA:
        show_message(STR_ERR_NO_COBRA, STR_ERR_NO_COBRA_HINT, 0);
        break;
    case LAUNCH_ERR_ISO_NOT_ON_HDD:
        show_message(STR_ERR_ISO_NOT_ON_HDD, STR_ERR_ISO_NOT_ON_HDD_HINT, 0);
        break;
    case LAUNCH_ERR_MOUNT:
        show_message(STR_ERR_MOUNT, STR_COUNT, 0);
        break;
    case LAUNCH_ERR_COPY:
    default:
        show_message(STR_ERR_COPY, STR_COUNT, 0);
        break;
    }
}

static int on_cover_progress(int done, int total, const game_t *g, void *user)
{
    char detail[GAME_TITLE_MAX + 32];

    (void)user;
    sysUtilCheckCallback();
    update_input();
    snprintf(detail, sizeof(detail), "%d / %d  ·  %s", done + 1, total, g ? g->title : "");
    busy_frame(lang_str(STR_DOWNLOADING_COVERS), detail, total ? (float)done / total : 0.0f);
    return !running || (input.pressed & IN_CIRCLE);
}

/* A cover archive URL from PS2ISO/covers_url.txt, or the default one. */
static void cover_url_template(char *out, size_t size)
{
    FILE *fp = fopen(COVER_URL_TXT, "r");

    snprintf(out, size, "%s", COVERDL_DEFAULT_URL);
    if (fp) {
        char line[256];

        if (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (coverdl_template_ok(line))
                snprintf(out, size, "%s", line);
        }
        fclose(fp);
    }
}

static void download_covers(void)
{
    char tmpl[256], title[160], hint[192];
    coverdl_result r;
    int i, needed = 0;

    for (i = 0; i < lib.count; i++)
        needed += coverdl_needs_cover(&lib.games[i]);
    if (!needed) {
        show_message(STR_COVERS_NONE, STR_COUNT, 0);
        return;
    }

    busy_frame(lang_str(STR_DOWNLOADING_COVERS), "", -1.0f);
    if (net_start(CA_BUNDLE) != 0 || !net_connected()) {
        show_message(STR_ERR_NO_NETWORK, STR_COUNT, 0);
        return;
    }

    cover_url_template(tmpl, sizeof(tmpl));
    coverdl_run(&lib, tmpl, net_fetch, on_cover_progress, NULL, &r);
    gfx_reset_covers();

    if (r.cancelled) {
        show_message(STR_ERR_CANCELLED, STR_COUNT, 0);
    } else if (r.downloaded == 0 && r.failed > 0) {
        show_message(STR_ERR_COVERS_FAILED, STR_ERR_COVERS_FAILED_HINT, 0);
    } else {
        snprintf(title, sizeof(title), lang_str(STR_COVERS_DONE), r.downloaded);
        hint[0] = '\0';
        if (r.missing > 0)
            snprintf(hint, sizeof(hint), lang_str(STR_COVERS_MISSING), r.missing);
        show_message_text(title, hint, 0);
    }
}

/* Asked on the first .iso launch only, so unusual setups never pay for it at startup. */
static int can_mount_iso(void)
{
    static int cobra = -1;

    if (cobra < 0)
        cobra = cobra_detect() == PAYLOAD_COBRA;
    return cobra;
}

static const launch_platform console = { can_mount_iso, cobra_mount_ps2_iso };

/* Where ps2_netemu CONFIGs for .iso games are looked up, best first. */
static const char *const config_dirs[] = {
    "/dev_hdd0/game/PS3SX2001/USRDIR/ps2config",      /* community configs shipped in our pkg */
    "/dev_hdd0/game/PS2CONFIG/USRDIR/CONFIG/CUSTOM",  /* webMAN MOD's database, if installed */
    "/dev_hdd0/game/PS2CONFIG/USRDIR/CONFIG/NET",
};

static void handle_input(void)
{
    if (msg.active) {
        if (input.pressed & (IN_CROSS | IN_CIRCLE)) {
            msg.active = 0;
            if (msg.exit_on_close)
                running = 0;  /* back to the XMB, where the placeholder is waiting */
        }
        return;
    }

    if (input.pressed & IN_CIRCLE) {
        running = 0;
        return;
    }
    if (input.pressed & IN_TRIANGLE) {
        scan_library();
        return;
    }
    if (lib.count == 0)
        return;

    if (input.pressed & IN_LEFT)  sel--;
    if (input.pressed & IN_RIGHT) sel++;
    if (input.pressed & IN_L1)    sel -= FAST_STEP;
    if (input.pressed & IN_R1)    sel += FAST_STEP;
    if (sel < 0) sel = 0;
    if (sel >= lib.count) sel = lib.count - 1;

    if (input.pressed & IN_CROSS)
        launch_selected();
    else if (input.pressed & IN_SQUARE)
        download_covers();
}

int main(int argc, const char *argv[])
{
    s32 system_lang, tz_minutes = 0, summer_time = 0;

    (void)argc;
    (void)argv;

    ioPadInit(PADS);
    sysModuleLoad(SYSMODULE_PNGDEC);
    sysModuleLoad(SYSMODULE_JPGDEC);
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, on_sysutil, NULL);

    if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_LANG, &system_lang) == 0 &&
        system_lang == SYSUTIL_LANG_TURKISH)
        lang_set(LANG_TR);

    gfx_init();
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz_minutes);
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &summer_time);
    gfx_set_utc_offset(tz_minutes + (summer_time ? 60 : 0));

    launcher_set_platform(&console);
    launcher_set_config_dirs(config_dirs, sizeof(config_dirs) / sizeof(config_dirs[0]));
    library_init(&lib);
    scan_library();

    while (running) {
        sysUtilCheckCallback();
        update_input();
        handle_input();

        if (lib.count > 0) {
            pos = coverflow_step(pos, (float)sel);
            gfx_update_covers(&lib, sel);
        }

        gfx_frame_begin();
        gfx_draw_library(&lib, pos, sel, !msg.active);
        if (msg.active)
            gfx_draw_overlay(msg.title, msg.hint, -1.0f, 1);
        gfx_frame_end();
    }

    library_free(&lib);
    net_stop();
    gfx_shutdown();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysModuleUnload(SYSMODULE_JPGDEC);
    sysModuleUnload(SYSMODULE_PNGDEC);
    ioPadEnd();
    return 0;
}
