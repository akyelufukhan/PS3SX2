#include "gfx.h"

#include "coverflow.h"
#include "covers.h"
#include "lang.h"
#include "serial.h"
#include "text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <jpgdec/jpgdec.h>
#include <libfont.h>
#include <pngdec/pngdec.h>
#include <tiny3d.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define SCREEN_W 848.0f
#define SCREEN_H 512.0f

/* Ships with every PS3 firmware, so we don't have to bundle (and license) a font. */
#define FONT_PATH "/dev_flash/data/font/SCE-PS3-SR-R-LATIN2.TTF"

#define TEXTURE_POOL (16 * 1024 * 1024)
#define COVER_SLOTS  24
#define COVER_MAX_W  256
#define COVER_MAX_H  360
#define COVER_Y      (-0.35f)  /* lift the carousel above the details panel */

#define COLOR_CLEAR    0xff0a0f1e  /* ARGB, for tiny3d_Clear */
#define C_TEXT         0xe8ecf5ff  /* RGBA from here on */
#define C_DIM          0x9aa6c8ff
#define C_FAINT        0x6d7899ff
#define C_ACCENT       0x7fa6ffff
#define C_RULE         0xffffff18
#define C_PANEL        0x050812e8
#define C_TRACK        0x1d2a55ff
#define C_COVER_BAND   0x111111ff
#define C_BTN_CROSS    0x7fa6ffff
#define C_BTN_CIRCLE   0xff7a7aff
#define C_BTN_TRIANGLE 0x5fd3a0ff

enum { SLOT_FREE, SLOT_READY, SLOT_MISSING };
enum { ICON_CROSS, ICON_CIRCLE, ICON_TRIANGLE };

typedef struct {
    int game;     /* library index this slot holds */
    int state;
    u32 *pixels;  /* inside the RSX texture pool */
    u32 offset;
    int w, h;
} cover_slot;

static cover_slot slots[COVER_SLOTS];
static int font_ok;
static float anim_time;
static FT_Library ft;
static FT_Face face;

static const u32 placeholder_colors[] = {
    0x4a1010ff, 0x2c3530ff, 0x3d2a12ff, 0x1f3320ff, 0x0f2a5aff,
    0x1a2350ff, 0x3a0d2aff, 0x4a1d06ff, 0x11253aff,
};

/* ------------------------------------------------------------------ fonts */

/*
 * libfont asks for one 8-bit character at a time. Our codes are Windows-1254,
 * so map them to Unicode before asking FreeType for the glyph.
 */
static void ttf_glyph(u8 chr, u8 *bitmap, short *w, short *h, short *y_correction)
{
    FT_UInt index = 0;
    FT_GlyphSlot slot = face->glyph;
    unsigned uni = cp1254_to_unicode(chr);
    int n, m, src = 0;

    FT_Set_Pixel_Sizes(face, *w, *h);
    memset(bitmap, 0, (*w) * (*h));

    if (uni)
        index = FT_Get_Char_Index(face, uni);
    if (!index && cp1254_ascii_fallback(chr))
        index = FT_Get_Char_Index(face, (unsigned char)cp1254_ascii_fallback(chr));
    if (!index || FT_Load_Glyph(face, index, FT_LOAD_RENDER)) {
        *w = 0;
        return;
    }

    *y_correction = (*h) - 1 - slot->bitmap_top;
    for (n = 0; n < (int)slot->bitmap.rows; n++) {
        for (m = 0; m < (int)slot->bitmap.width; m++) {
            if (m < *w && n < *h)
                bitmap[m] = slot->bitmap.buffer[src + m];
        }
        bitmap += *w;
        src += slot->bitmap.pitch;
    }
    *w = ((slot->advance.x + 31) >> 6) + (slot->bitmap_left < 0 ? -slot->bitmap_left : 0);
    *h = slot->bitmap.rows;
}

static u8 *load_font(u8 *texture)
{
    struct stat st;

    ResetFont();
    if (stat(FONT_PATH, &st) != 0 || FT_Init_FreeType(&ft) != 0)
        return texture;
    if (FT_New_Face(ft, FONT_PATH, 0, &face) != 0) {
        FT_Done_FreeType(ft);
        return texture;
    }
    texture = AddFontFromTTF(texture, 32, 255, 32, 32, ttf_glyph);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    font_ok = 1;
    return texture;
}

/* Draws UTF-8 text and returns the x where the next string should start. */
static float text(float x, float y, float size, u32 color, const char *utf8)
{
    char buf[512];

    if (!font_ok)
        return x;
    utf8_to_cp1254(utf8, buf, sizeof(buf));
    SetFontSize((int)size, (int)size);
    SetFontColor(color, 0);
    SetFontAutoCenter(0);
    return DrawString(x, y, buf);
}

static void text_centered(float y, float size, u32 color, const char *utf8)
{
    char buf[512];

    if (!font_ok)
        return;
    utf8_to_cp1254(utf8, buf, sizeof(buf));
    SetFontSize((int)size, (int)size);
    SetFontColor(color, 0);
    SetFontAutoCenter(1);
    DrawString(0, y, buf);
    SetFontAutoCenter(0);
}

/* ------------------------------------------------------------------ init */

void gfx_init(void)
{
    u8 *pool;
    int i;

    tiny3d_Init(1024 * 1024);
    pool = tiny3d_AllocTexture(TEXTURE_POOL);
    if (!pool)
        return;

    pool = load_font(pool);

    for (i = 0; i < COVER_SLOTS; i++) {
        slots[i].game = -1;
        slots[i].state = SLOT_FREE;
        slots[i].pixels = (u32 *)pool;
        slots[i].offset = tiny3d_TextureOffset(pool);
        pool += COVER_MAX_W * COVER_MAX_H * 4;
    }
}

void gfx_shutdown(void)
{
    tiny3d_Exit();
}

/* ----------------------------------------------------------------- covers */

void gfx_reset_covers(void)
{
    int i;

    for (i = 0; i < COVER_SLOTS; i++) {
        slots[i].game = -1;
        slots[i].state = SLOT_FREE;
    }
}

static cover_slot *find_slot(int game)
{
    int i;

    for (i = 0; i < COVER_SLOTS; i++) {
        if (slots[i].state != SLOT_FREE && slots[i].game == game)
            return &slots[i];
    }
    return NULL;
}

/* A free slot, or the one holding the cover furthest from the selection. */
static cover_slot *pick_slot(int sel)
{
    cover_slot *best = NULL;
    int i, best_dist = -1;

    for (i = 0; i < COVER_SLOTS && slots[i].pixels; i++) {
        int dist = slots[i].state == SLOT_FREE ? 1 << 30 : abs(slots[i].game - sel);

        if (dist > best_dist) {
            best = &slots[i];
            best_dist = dist;
        }
    }
    return best;
}

/* Nearest-neighbour copy into the slot, shrunk to fit COVER_MAX_W x COVER_MAX_H. */
static void fit_into_slot(cover_slot *slot, const u8 *src, u32 sw, u32 sh, u32 pitch)
{
    float s = fminf(1.0f, fminf((float)COVER_MAX_W / sw, (float)COVER_MAX_H / sh));
    int dw = (int)(sw * s), dh = (int)(sh * s);
    int x, y;

    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    for (y = 0; y < dh; y++) {
        const u32 *row = (const u32 *)(src + (u32)(y * sh / dh) * pitch);

        for (x = 0; x < dw; x++)
            slot->pixels[y * dw + x] = row[x * sw / dw];
    }
    slot->w = dw;
    slot->h = dh;
}

static int load_image(cover_slot *slot, const char *path)
{
    struct stat st;
    size_t len = strlen(path);

    if (stat(path, &st) != 0)
        return -1;

    if (len > 4 && strcasecmp(path + len - 4, ".png") == 0) {
        pngData png;

        memset(&png, 0, sizeof(png));
        if (pngLoadFromFile(path, &png) != 0 || !png.bmp_out)
            return -1;
        fit_into_slot(slot, png.bmp_out, png.width, png.height, png.pitch);
        free(png.bmp_out);
    } else {
        jpgData jpg;

        memset(&jpg, 0, sizeof(jpg));
        if (jpgLoadFromFile(path, &jpg) != 0 || !jpg.bmp_out)
            return -1;
        fit_into_slot(slot, jpg.bmp_out, jpg.width, jpg.height, jpg.pitch);
        free(jpg.bmp_out);
    }
    return 0;
}

static void load_cover(const library_t *lib, int game, int sel)
{
    char paths[COVER_MAX_CANDIDATES][GAME_PATH_MAX];
    cover_slot *slot = pick_slot(sel);
    int i, n;

    if (!slot)
        return;
    slot->game = game;
    slot->state = SLOT_MISSING;

    n = covers_candidates(&lib->games[game], paths, COVER_MAX_CANDIDATES);
    for (i = 0; i < n; i++) {
        if (load_image(slot, paths[i]) == 0) {
            slot->state = SLOT_READY;
            return;
        }
    }
}

void gfx_update_covers(const library_t *lib, int sel)
{
    int d, k;

    /* Nearest first: sel, sel+1, sel-1, sel+2, ... */
    for (d = 0; d < (int)CF_VISIBLE; d++) {
        for (k = 0; k < (d ? 2 : 1); k++) {
            int i = k ? sel - d : sel + d;

            if (i < 0 || i >= lib->count || find_slot(i))
                continue;
            load_cover(lib, i, sel);
            return;
        }
    }
}

/* -------------------------------------------------------------- primitives */

static void rect(float x, float y, float w, float h, u32 color)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 0);
    tiny3d_VertexColor(color);
    tiny3d_VertexPos(x + w, y, 0);
    tiny3d_VertexPos(x + w, y + h, 0);
    tiny3d_VertexPos(x, y + h, 0);
    tiny3d_End();
}

static void button_icon(float cx, float cy, int kind)
{
    const float r = 7.0f;
    int i;

    switch (kind) {
    case ICON_CROSS:
        tiny3d_SetPolygon(TINY3D_LINES);
        tiny3d_VertexPos(cx - r, cy - r, 0);
        tiny3d_VertexColor(C_BTN_CROSS);
        tiny3d_VertexPos(cx + r, cy + r, 0);
        tiny3d_VertexPos(cx + r, cy - r, 0);
        tiny3d_VertexPos(cx - r, cy + r, 0);
        tiny3d_End();
        break;
    case ICON_CIRCLE:
        tiny3d_SetPolygon(TINY3D_LINE_LOOP);
        for (i = 0; i < 20; i++) {
            float a = i * 6.2831853f / 20;

            tiny3d_VertexPos(cx + cosf(a) * r, cy + sinf(a) * r, 0);
            if (i == 0)
                tiny3d_VertexColor(C_BTN_CIRCLE);
        }
        tiny3d_End();
        break;
    case ICON_TRIANGLE:
        tiny3d_SetPolygon(TINY3D_LINE_LOOP);
        tiny3d_VertexPos(cx, cy - r, 0);
        tiny3d_VertexColor(C_BTN_TRIANGLE);
        tiny3d_VertexPos(cx + r, cy + r * 0.8f, 0);
        tiny3d_VertexPos(cx - r, cy + r * 0.8f, 0);
        tiny3d_End();
        break;
    }
}

/* An icon followed by its label; returns where the next hint goes. */
static float hint(float x, float y, int icon, const char *label)
{
    button_icon(x + 7, y + 8, icon);
    return text(x + 22, y, 15, C_DIM, label) + 26;
}

/* Slow XMB-style waves behind everything. */
static void draw_waves(float t)
{
    static const u32 colors[] = { 0x3b4f9e70, 0x5a6fc44c, 0x3b4f9e38 };
    int k, i;

    for (k = 0; k < 3; k++) {
        tiny3d_SetPolygon(TINY3D_LINE_STRIP);
        for (i = 0; i <= 64; i++) {
            float x = SCREEN_W * i / 64;
            float y = 300 + k * 18 + sinf(x * 0.008f + t * (0.35f + 0.15f * k) + k) * (26 - k * 6);

            tiny3d_VertexPos(x, y, 65535);
            if (i == 0)
                tiny3d_VertexColor(colors[k]);
        }
        tiny3d_End();
    }
}

static u32 shaded(u32 rgba, float shade, float alpha)
{
    u32 r = (u32)(((rgba >> 24) & 0xff) * shade);
    u32 g = (u32)(((rgba >> 16) & 0xff) * shade);
    u32 b = (u32)(((rgba >> 8) & 0xff) * shade);
    u32 a = (u32)((rgba & 0xff) * alpha);

    return (r << 24) | (g << 16) | (b << 8) | a;
}

static u32 placeholder_color(const char *title)
{
    unsigned h = 5381;

    while (*title)
        h = h * 33 + (unsigned char)*title++;
    return placeholder_colors[h % (sizeof(placeholder_colors) / sizeof(placeholder_colors[0]))];
}

/* One cover as a quad centered on the model origin. */
static void draw_cover(const game_t *g, const cover_slot *slot, const cf_pose *p)
{
    const float w = CF_COVER_W / 2, h = CF_COVER_H / 2;
    MATRIX m = MatrixMultiply(MatrixRotationY(p->angle), MatrixTranslation(p->x, COVER_Y, p->z));

    tiny3d_SetMatrixModelView(&m);

    if (slot && slot->state == SLOT_READY) {
        tiny3d_SetTexture(0, slot->offset, slot->w, slot->h, slot->w * 4, TINY3D_TEX_FORMAT_A8R8G8B8, TEXTURE_LINEAR);
        tiny3d_SetPolygon(TINY3D_QUADS);
        tiny3d_VertexPos(-w, -h, 0);
        tiny3d_VertexColor(shaded(0xffffffff, p->shade, p->alpha));
        tiny3d_VertexTexture(0.0f, 0.0f);
        tiny3d_VertexPos(w, -h, 0);
        tiny3d_VertexTexture(1.0f, 0.0f);
        tiny3d_VertexPos(w, h, 0);
        tiny3d_VertexTexture(1.0f, 1.0f);
        tiny3d_VertexPos(-w, h, 0);
        tiny3d_VertexTexture(0.0f, 1.0f);
        tiny3d_End();
        return;
    }

    /* No artwork: a plain case in a color derived from the title, with the dark top band. */
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(-w, -h, 0);
    tiny3d_VertexColor(shaded(C_COVER_BAND, p->shade, p->alpha));
    tiny3d_VertexPos(w, -h, 0);
    tiny3d_VertexPos(w, -h + 0.22f, 0);
    tiny3d_VertexPos(-w, -h + 0.22f, 0);
    tiny3d_End();

    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(-w, -h + 0.22f, 0);
    tiny3d_VertexColor(shaded(placeholder_color(g->title), p->shade, p->alpha));
    tiny3d_VertexPos(w, -h + 0.22f, 0);
    tiny3d_VertexPos(w, h, 0);
    tiny3d_VertexPos(-w, h, 0);
    tiny3d_End();
}

/* ------------------------------------------------------------------ frame */

void gfx_frame_begin(void)
{
    anim_time += 1.0f / 60.0f;

    tiny3d_Clear(COLOR_CLEAR, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);
    tiny3d_BlendFunc(1, TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
                     TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO,
                     TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);

    tiny3d_Project2D();
    draw_waves(anim_time);
}

void gfx_frame_end(void)
{
    tiny3d_Flip();
}

static void format_size(unsigned long long bytes, char *out, size_t size)
{
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(out, size, "%.2f GB", bytes / (1024.0 * 1024 * 1024));
    else
        snprintf(out, size, "%.0f MB", bytes / (1024.0 * 1024));
}

static void draw_details(const library_t *lib, int sel)
{
    const game_t *g = &lib->games[sel];
    const char *name = strrchr(g->path, '/');
    char size[32], line[256];

    format_size(g->size, size, sizeof(size));
    text_centered(352, 26, C_TEXT, g->title);

    if (g->serial[0])
        snprintf(line, sizeof(line), "%s  ·  %s  ·  %s  ·  %s", g->serial, serial_region(g->serial), size,
                 g->format == GAME_FMT_ISO ? "ISO" : "PS2 Classic");
    else
        snprintf(line, sizeof(line), "%s  ·  %s  ·  %s", lang_str(STR_UNKNOWN_SERIAL), size,
                 g->format == GAME_FMT_ISO ? "ISO" : "PS2 Classic");
    text_centered(390, 15, C_DIM, line);

    snprintf(line, sizeof(line), "%d / %d  ·  %s", sel + 1, lib->count, name ? name + 1 : g->path);
    text_centered(414, 13, C_FAINT, line);
}

void gfx_draw_library(const library_t *lib, float pos, int sel, int show_ui)
{
    char line[64];
    float x;
    int order[2 * (int)CF_VISIBLE + 2];
    int i, n = 0, first, last;

    if (lib->count > 0) {
        MATRIX proj = MatrixProjPerspective(90, (Video_aspect == 1) ? 9.0f / 16.0f : 1.0f, 0.00125f, 300.0f);

        tiny3d_Project3D();
        tiny3d_SetProjectionMatrix(&proj);

        first = (int)floorf(pos - CF_VISIBLE);
        last = (int)ceilf(pos + CF_VISIBLE);
        for (i = first < 0 ? 0 : first; i <= last && i < lib->count; i++)
            order[n++] = i;

        /* Painter's order: outermost covers first, the selected one last. */
        for (i = 1; i < n; i++) {
            int v = order[i], j = i - 1;

            while (j >= 0 && fabsf(order[j] - pos) < fabsf(v - pos)) {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = v;
        }
        for (i = 0; i < n; i++) {
            cf_pose p;

            coverflow_pose(order[i] - pos, &p);
            if (p.alpha > 0.0f)
                draw_cover(&lib->games[order[i]], find_slot(order[i]), &p);
        }
        tiny3d_SetMatrixModelView(NULL);
    }

    if (!show_ui)
        return;

    tiny3d_Project2D();

    x = text(40, 26, 20, C_ACCENT, "PS3SX2");
    x = text(x + 14, 29, 16, C_DIM, lang_str(STR_LIBRARY));
    snprintf(line, sizeof(line), lang_str(STR_GAME_COUNT), lib->count);
    text(x + 14, 29, 16, C_FAINT, line);

    if (lib->count > 0) {
        draw_details(lib, sel);
    } else {
        text_centered(210, 24, C_TEXT, lang_str(STR_EMPTY_TITLE));
        text_centered(250, 14, C_DIM, lang_str(STR_EMPTY_HINT));
    }

    rect(0, 448, SCREEN_W, 1, C_RULE);
    x = 40;
    if (lib->count > 0)
        x = hint(x, 466, ICON_CROSS, lang_str(STR_LAUNCH));
    x = hint(x, 466, ICON_CIRCLE, lang_str(STR_EXIT));
    x = hint(x, 466, ICON_TRIANGLE, lang_str(STR_RESCAN));
    if (lib->count > 0) {
        x = text(x, 466, 15, C_FAINT, "L1/R1");
        text(x + 8, 466, 15, C_DIM, lang_str(STR_FAST_SCROLL));
    }
}

void gfx_draw_overlay(const char *title, const char *detail, float progress, int show_continue)
{
    tiny3d_Project2D();
    rect(0, 0, SCREEN_W, SCREEN_H, C_PANEL);

    text_centered(200, 22, C_TEXT, title);
    if (detail && detail[0])
        text_centered(238, 15, C_DIM, detail);

    if (progress >= 0.0f) {
        if (progress > 1.0f)
            progress = 1.0f;
        rect(224, 282, 400, 6, C_TRACK);
        rect(224, 282, 400 * progress, 6, C_ACCENT);
        hint(386, 308, ICON_CIRCLE, lang_str(STR_CANCEL));
    } else if (show_continue) {
        hint(370, 290, ICON_CROSS, lang_str(STR_CONTINUE));
    }
}
