#ifndef PS3SX2_GFX_H
#define PS3SX2_GFX_H

#include "library.h"

/* Rendering on top of Tiny3D. 2D coordinates use Tiny3D's virtual 848 x 512 screen. */

void gfx_init(void);
void gfx_shutdown(void);

/* Forgets every loaded cover; call after the library is rescanned. */
void gfx_reset_covers(void);

/* Decodes at most one missing cover near `sel` per call, so scrolling never stalls. */
void gfx_update_covers(const library_t *lib, int sel);

void gfx_frame_begin(void);
void gfx_frame_end(void);

/* Cover carousel; with `show_ui` also the header, game details and button hints. */
void gfx_draw_library(const library_t *lib, float pos, int sel, int show_ui);

/* Dimmed full-screen panel. `progress` in [0, 1] draws a bar, negative hides it. */
void gfx_draw_overlay(const char *title, const char *detail, float progress, int show_continue);

#endif
