/*
 * PS3SX2 - a cover-flow launcher for PS2 games on PS3 Slim and Super Slim.
 */
#include <string.h>

#include <io/pad.h>
#include <sysmodule/sysmodule.h>
#include <sysutil/sysutil.h>

#include "coverflow.h"
#include "gfx.h"
#include "lang.h"
#include "launcher.h"
#include "library.h"

#define PADS          7
#define REPEAT_DELAY  18  /* frames before a held direction starts repeating */
#define REPEAT_RATE   5   /* frames between repeats */
#define FAST_STEP     5   /* covers skipped by L1 / R1 */

enum {
    IN_LEFT     = 1 << 0,
    IN_RIGHT    = 1 << 1,
    IN_L1       = 1 << 2,
    IN_R1       = 1 << 3,
    IN_CROSS    = 1 << 4,
    IN_CIRCLE   = 1 << 5,
    IN_TRIANGLE = 1 << 6,
};

typedef struct {
    u32 held;
    u32 pressed;  /* went down this frame, or auto-repeated */
    int hold_frames;
} input_state;

typedef struct {
    int active;
    str_id title;
    str_id hint;
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

static void show_message(str_id title, str_id hint, int exit_on_close)
{
    msg.active = 1;
    msg.title = title;
    msg.hint = hint;
    msg.exit_on_close = exit_on_close;
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

/* Keeps the screen alive while a game is copied into the placeholder. */
static int on_copy_progress(unsigned long long done, unsigned long long total, void *user)
{
    const game_t *g = user;

    sysUtilCheckCallback();
    update_input();

    gfx_frame_begin();
    gfx_draw_library(&lib, pos, sel, 0);
    gfx_draw_overlay(lang_str(STR_COPYING), g->title, total ? (float)((double)done / total) : 0.0f, 0);
    gfx_frame_end();

    return !running || (input.pressed & IN_CIRCLE);
}

static void launch_selected(void)
{
    const game_t *g = &lib.games[sel];

    switch (launcher_start(g, on_copy_progress, (void *)g)) {
    case LAUNCH_OK:
        show_message(STR_READY_TITLE, STR_READY_HINT, 1);
        break;
    case LAUNCH_ERR_NO_PLACEHOLDER:
        show_message(STR_ERR_NO_PLACEHOLDER, STR_ERR_NO_PLACEHOLDER_HINT, 0);
        break;
    case LAUNCH_ERR_CANCELLED:
        show_message(STR_ERR_CANCELLED, STR_COUNT, 0);
        break;
    case LAUNCH_ERR_NOT_IMPLEMENTED:
        show_message(STR_ERR_ISO_TITLE, STR_ERR_ISO_HINT, 0);
        break;
    case LAUNCH_ERR_COPY:
    default:
        show_message(STR_ERR_COPY, STR_COUNT, 0);
        break;
    }
}

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
            gfx_draw_overlay(lang_str(msg.title), lang_str(msg.hint), -1.0f, 1);
        gfx_frame_end();
    }

    library_free(&lib);
    gfx_shutdown();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    sysModuleUnload(SYSMODULE_JPGDEC);
    sysModuleUnload(SYSMODULE_PNGDEC);
    ioPadEnd();
    return 0;
}
