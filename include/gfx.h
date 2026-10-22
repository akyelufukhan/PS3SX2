#ifndef PS3SX2_GFX_H
#define PS3SX2_GFX_H

#include "library.h"

/* Rendering on top of Tiny3D. 2D coordinates use Tiny3D's virtual 848 x 512 screen. */

void gfx_init(void);
void gfx_shutdown(void);

/* The console's time zone, in minutes east of UTC, for the header clock. */
void gfx_set_utc_offset(int minutes);

/* Forgets every loaded cover; call after the library is rescanned. */
void gfx_reset_covers(void);

/* Forgets one game's cover so it's loaded again, e.g. after it was downloaded. */
void gfx_forget_cover(int game);

/* Short status shown in the header next to the game count, e.g. download progress; "" hides it. */
void gfx_set_note(const char *note);

/* Decodes at most one missing cover near `sel` per call, so scrolling never stalls. */
void gfx_update_covers(const library_t *lib, int sel);

void gfx_frame_begin(void);
void gfx_frame_end(void);

/* Cover carousel; with `show_ui` also the header, game details and button hints. */
void gfx_draw_library(const library_t *lib, float pos, int sel, int show_ui);

/* Dimmed full-screen panel. `progress` in [0, 1] draws a bar, negative hides it. */
void gfx_draw_overlay(const char *title, const char *detail, float progress, int show_continue);

#endif
