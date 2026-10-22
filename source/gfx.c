#include "gfx.h"

#include "canvas.h"
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
#include <time.h>

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
#define FONT_CELL 32  /* libfont glyph cell size the TTF is baked at */

#define PI_F      3.1415926f
#define ARC_STEPS 6     /* segments per rounded corner */

#define TEXTURE_POOL (16 * 1024 * 1024)
#define COVER_SLOTS  24
#define COVER_MAX_W  256
#define COVER_MAX_H  360
#define COVER_Y      0.63f   /* lift the carousel above the details panel (+y is up in 3D) */
#define REFLECTION_H 0.32f   /* height of the mirrored cover below each case */

/* Layout rows, in Tiny3D's virtual 848 x 512 2D space. */
#define ROW_HEADER 18
#define ROW_TITLE  326
#define ROW_CHIPS  362
#define ROW_PATH   394
#define ROW_RULE   452
#define ROW_HINTS  466

#define COLOR_CLEAR 0xff0a0f1e  /* ARGB, for tiny3d_Clear */
#define C_TEXT      0xe8ecf5ff  /* RGBA from here on */
#define C_DIM       0x9aa6c8ff
#define C_FAINT     0x6d7899ff
#define C_ACCENT    0x7fa6ffff
#define C_RULE      0xffffff14
#define C_PANEL     0x050812eb
#define C_TRACK     0x1d2a55ff
#define C_CROSS     0x86b8f2ff  /* DualShock symbol colors */
#define C_CIRCLE    0xff6f7dff
#define C_TRIANGLE  0x45dcb0ff
#define C_SQUARE    0xf2a5dcff

enum { SLOT_FREE, SLOT_READY };
enum { ICON_CROSS, ICON_CIRCLE, ICON_TRIANGLE, ICON_SQUARE, ICON_SHOULDERS };

typedef struct {
    int game;     /* library index this slot holds */
    int state;
    u32 *pixels;  /* inside the RSX texture pool */
    u32 offset;
    int w, h;
} cover_slot;

/* Case color and the accent used for its title and rings, as in the design mockup. */
typedef struct {
    u32 base, accent;
} cover_palette;

static const cover_palette palettes[] = {
    { 0x4a1010, 0xd9a441 }, { 0x2c3530, 0xc9d6c4 }, { 0x3d2a12, 0xf2c14e },
    { 0x1f3320, 0xa7c46a }, { 0x0f2a5a, 0x6fb2ff }, { 0x1a2350, 0xf5d76e },
    { 0x3a0d2a, 0xff8fb1 }, { 0x4a1d06, 0xff9a3c }, { 0x11253a, 0x47e0c4 },
};

static cover_slot slots[COVER_SLOTS];
static short glyph_w[256];  /* advance of each baked character at FONT_CELL */
static int font_ok, face_ok;
static int utc_offset;
static char header_note[64];
static int on_hen;
static float anim_time;
static FT_Library ft;
static FT_Face face;

/* ------------------------------------------------------------------ fonts */

static FT_UInt glyph_index(unsigned uni)
{
    FT_UInt index = uni ? FT_Get_Char_Index(face, uni) : 0;

    if (!index && unicode_ascii_fallback(uni))
        index = FT_Get_Char_Index(face, (unsigned char)unicode_ascii_fallback(uni));
    return index;
}

/*
 * libfont asks for one 8-bit character at a time. Our codes are Windows-1254,
 * so map them to Unicode before asking FreeType for the glyph.
 */
static void ttf_glyph(u8 chr, u8 *bitmap, short *w, short *h, short *y_correction)
{
    FT_GlyphSlot slot = face->glyph;
    FT_UInt index = glyph_index(cp1254_to_unicode(chr));
    int n, m, src = 0;

    FT_Set_Pixel_Sizes(face, *w, *h);
    memset(bitmap, 0, (*w) * (*h));

    if (!index || FT_Load_Glyph(face, index, FT_LOAD_RENDER)) {
        *w = 0;
        glyph_w[chr] = 0;
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
    glyph_w[chr] = *w;
}

/* Bakes the 2D UI font into `texture`; the FreeType face stays open for cover art. */
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
    face_ok = 1;
    texture = AddFontFromTTF(texture, 32, 255, FONT_CELL, FONT_CELL, ttf_glyph);
    font_ok = 1;
    return texture;
}

static int font_px(float size)
{
    return size < 8 ? 8 : (int)size;
}

/* Width of UTF-8 text at `size`, computed the same way libfont advances. */
static int text_width(const char *utf8, float size)
{
    char buf[512];
    const unsigned char *p;
    int w = 0, s = font_px(size);

    if (!font_ok)
        return 0;
    utf8_to_cp1254(utf8, buf, sizeof(buf));
    for (p = (const unsigned char *)buf; *p; p++)
        w += s * glyph_w[*p] / FONT_CELL;
    return w;
}

/* Draws UTF-8 text with its top-left at (x, y); returns the x where the next string starts. */
static float text(float x, float y, float size, u32 color, const char *utf8)
{
    char buf[512];

    if (!font_ok)
        return x;
    utf8_to_cp1254(utf8, buf, sizeof(buf));
    SetFontSize(font_px(size), font_px(size));
    SetFontColor(color, 0);
    SetFontAutoCenter(0);
    return DrawString(x, y, buf);
}

static void text_centered(float y, float size, u32 color, const char *utf8)
{
    text((SCREEN_W - text_width(utf8, size)) / 2, y, size, color, utf8);
}

/* ------------------------------------------------------------- cover art */

/* Draws UTF-8 text onto a canvas from `x` on `baseline`, stopping before `max_w`. Returns the width used. */
static int art_text(canvas *c, int x, int baseline, int size, u32 rgb, const char *utf8, int max_w)
{
    FT_GlyphSlot slot = face->glyph;
    unsigned cp;
    int pen = 0;

    FT_Set_Pixel_Sizes(face, 0, size);
    while ((cp = utf8_decode(&utf8)) != 0) {
        FT_UInt index = glyph_index(cp);
        int advance;

        if (!index || FT_Load_Glyph(face, index, c ? FT_LOAD_RENDER : FT_LOAD_DEFAULT))
            continue;
        advance = slot->advance.x >> 6;
        if (pen + advance > max_w)
            break;
        if (c)
            canvas_mask(c, x + pen + slot->bitmap_left, baseline - slot->bitmap_top, slot->bitmap.buffer,
                        slot->bitmap.width, slot->bitmap.rows, slot->bitmap.pitch, rgb);
        pen += advance;
    }
    return pen;
}

static int art_text_width(int size, const char *utf8)
{
    return art_text(NULL, 0, 0, size, 0, utf8, 1 << 30);
}

/* Bounded copy; a word too long for the line buffer is simply cut. */
static void copy_text(char *dst, size_t size, const char *src, size_t len)
{
    if (len >= size)
        len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Greedy word wrap into at most `max_lines` lines no wider than `max_w`. */
static int wrap_words(const char *title, int size, int max_w, char lines[][GAME_TITLE_MAX], int max_lines)
{
    char trial[GAME_TITLE_MAX];
    const char *p = title;
    int n = 0;

    lines[0][0] = '\0';
    while (*p && n < max_lines) {
        size_t len = 0, used = strlen(lines[n]);

        while (*p == ' ')
            p++;
        while (p[len] && p[len] != ' ')
            len++;
        if (len == 0)
            break;

        if (used == 0) {
            copy_text(lines[n], GAME_TITLE_MAX, p, len);
        } else if (used + 1 + len < sizeof(trial)) {
            memcpy(trial, lines[n], used);
            trial[used] = ' ';
            copy_text(trial + used + 1, sizeof(trial) - used - 1, p, len);
            if (art_text_width(size, trial) <= max_w)
                memcpy(lines[n], trial, strlen(trial) + 1);
            else if (++n < max_lines)
                copy_text(lines[n], GAME_TITLE_MAX, p, len);
        } else if (++n < max_lines) {
            copy_text(lines[n], GAME_TITLE_MAX, p, len);
        }
        p += len;
    }
    return n < max_lines ? n + 1 : max_lines;
}

static const cover_palette *palette_for(const char *title)
{
    unsigned h = 5381;

    while (*title)
        h = h * 33 + (unsigned char)*title++;
    return &palettes[h % (sizeof(palettes) / sizeof(palettes[0]))];
}

/*
 * A cover for games without artwork, laid out like the design mockup: dark
 * "PS2" band, title in the accent color, two rings, serial along the bottom.
 */
static void render_cover_art(cover_slot *slot, const game_t *g)
{
    canvas c = { slot->pixels, COVER_MAX_W, COVER_MAX_H };
    const cover_palette *pal = palette_for(g->title);
    const char *region = serial_region(g->serial);
    char lines[5][GAME_TITLE_MAX];
    int i, n;

    canvas_fill(&c, pal->base);
    canvas_ring(&c, 201, 303, 102, 4, 0x73000000 | pal->accent);
    canvas_disc(&c, 194, 295, 43, 0x59000000 | pal->accent);
    canvas_rect(&c, 0, 0, COVER_MAX_W, 38, 0xff111111);
    canvas_rect(&c, 0, COVER_MAX_H - 38, COVER_MAX_W, 38, 0x59000000);

    if (face_ok) {
        art_text(&c, 14, 26, 19, 0xdddddd, "PS2", 80);
        if (region[0])
            art_text(&c, COVER_MAX_W - 14 - art_text_width(19, region), 26, 19, 0xdddddd, region, 120);

        n = wrap_words(g->title, 30, COVER_MAX_W - 34, lines, 5);
        for (i = 0; i < n; i++)
            art_text(&c, 18, 86 + i * 35, 30, pal->accent, lines[i], COVER_MAX_W - 34);

        art_text(&c, 14, COVER_MAX_H - 13, 18, 0xcccccc,
                 g->serial[0] ? g->serial : (g->format == GAME_FMT_ISO ? "ISO" : "PS2 Classic"), 200);
    }
    slot->w = COVER_MAX_W;
    slot->h = COVER_MAX_H;
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
    if (face_ok) {
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
    }
    tiny3d_Exit();
}

void gfx_set_utc_offset(int minutes)
{
    utc_offset = minutes;
}

void gfx_set_hen(int on)
{
    on_hen = on;
}

void gfx_set_note(const char *note)
{
    snprintf(header_note, sizeof(header_note), "%s", note ? note : "");
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

void gfx_forget_cover(int game)
{
    int i;

    for (i = 0; i < COVER_SLOTS; i++) {
        if (slots[i].state != SLOT_FREE && slots[i].game == game) {
            slots[i].state = SLOT_FREE;
            slots[i].game = -1;
        }
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
    slot->state = SLOT_READY;

    n = covers_candidates(&lib->games[game], paths, COVER_MAX_CANDIDATES);
    for (i = 0; i < n; i++) {
        if (load_image(slot, paths[i]) == 0)
            return;
    }
    render_cover_art(slot, &lib->games[game]);
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

static void vtx(float x, float y, u32 color)
{
    tiny3d_VertexPos(x, y, 0);
    tiny3d_VertexColor(color);
}

/* Per-channel blend of two RGBA colors. */
static u32 mix(u32 a, u32 b, float t)
{
    u32 out = 0;
    int shift;

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    for (shift = 0; shift < 32; shift += 8) {
        float ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;

        out |= (u32)(ca + (cb - ca) * t + 0.5f) << shift;
    }
    return out;
}

/* Outline points of a rounded rectangle, clockwise from the top-left corner. */
static int rounded_outline(float x, float y, float w, float h, float r, float *px, float *py)
{
    int corner, step, count = 0;

    if (r < 0.0f) r = 0.0f;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    for (corner = 0; corner < 4; corner++) {
        float cx = (corner == 0 || corner == 3) ? x + r : x + w - r;
        float cy = corner < 2 ? y + r : y + h - r;
        float start = PI_F + corner * PI_F * 0.5f;

        for (step = 0; step <= ARC_STEPS; step++) {
            float a = start + step * (PI_F * 0.5f) / ARC_STEPS;

            px[count] = cx + cosf(a) * r;
            py[count] = cy + sinf(a) * r;
            count++;
        }
    }
    return count;
}

/* Rounded rectangle shaded from `top` to `bottom`. */
static void round_grad(float x, float y, float w, float h, float r, u32 top, u32 bottom)
{
    float px[4 * (ARC_STEPS + 1)], py[4 * (ARC_STEPS + 1)];
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    int count, i;

    if (w <= 0.0f || h <= 0.0f)
        return;
    count = rounded_outline(x, y, w, h, r, px, py);
    tiny3d_SetPolygon(TINY3D_TRIANGLES);
    for (i = 0; i < count; i++) {
        int j = (i + 1) % count;

        vtx(cx, cy, mix(top, bottom, 0.5f));
        vtx(px[i], py[i], mix(top, bottom, (py[i] - y) / h));
        vtx(px[j], py[j], mix(top, bottom, (py[j] - y) / h));
    }
    tiny3d_End();
}

static void rounded_rect(float x, float y, float w, float h, float r, u32 color)
{
    round_grad(x, y, w, h, r, color, color);
}

/* Band around a rounded rectangle fading from `inner` to `outer`, for soft shadows. */
static void round_band(float x, float y, float w, float h, float r, float spread, u32 inner, u32 outer)
{
    float ix[4 * (ARC_STEPS + 1)], iy[4 * (ARC_STEPS + 1)];
    float ox[4 * (ARC_STEPS + 1)], oy[4 * (ARC_STEPS + 1)];
    int count, i;

    count = rounded_outline(x, y, w, h, r, ix, iy);
    rounded_outline(x - spread, y - spread, w + spread * 2, h + spread * 2, r + spread, ox, oy);
    tiny3d_SetPolygon(TINY3D_QUADS);
    for (i = 0; i < count; i++) {
        int j = (i + 1) % count;

        vtx(ix[i], iy[i], inner);
        vtx(ox[i], oy[i], outer);
        vtx(ox[j], oy[j], outer);
        vtx(ix[j], iy[j], inner);
    }
    tiny3d_End();
}

static void thick_line(float x1, float y1, float x2, float y2, float width, u32 color)
{
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    float px, py;

    if (len <= 0.0f)
        return;
    px = -dy / len * width * 0.5f;
    py = dx / len * width * 0.5f;
    tiny3d_SetPolygon(TINY3D_QUADS);
    vtx(x1 + px, y1 + py, color);
    vtx(x2 + px, y2 + py, color);
    vtx(x2 - px, y2 - py, color);
    vtx(x1 - px, y1 - py, color);
    tiny3d_End();
}

static void ring(float cx, float cy, float radius, float thickness, u32 color)
{
    int i;

    tiny3d_SetPolygon(TINY3D_QUADS);
    for (i = 0; i < 28; i++) {
        float a0 = 2 * PI_F * i / 28, a1 = 2 * PI_F * (i + 1) / 28;

        vtx(cx + cosf(a0) * radius, cy + sinf(a0) * radius, color);
        vtx(cx + cosf(a1) * radius, cy + sinf(a1) * radius, color);
        vtx(cx + cosf(a1) * (radius - thickness), cy + sinf(a1) * (radius - thickness), color);
        vtx(cx + cosf(a0) * (radius - thickness), cy + sinf(a0) * (radius - thickness), color);
    }
    tiny3d_End();
}

/* Glossy black DualShock-style key: drop shadow, rim, face and a highlight on top. */
static void button_body(float x, float y, float w, float h, float r)
{
    rounded_rect(x, y + h * 0.16f, w, h, r, 0x00000070);
    round_band(x, y + h * 0.16f, w, h, r, 2.2f, 0x00000070, 0x00000000);
    round_grad(x, y, w, h, r, 0x7a808cff, 0x24272eff);
    round_grad(x + 0.9f, y + 0.9f, w - 1.8f, h - 1.8f, r - 0.9f, 0x343841ff, 0x101216ff);
    round_grad(x + w * 0.18f, y + 1.4f, w * 0.64f, h * 0.38f, h * 0.19f, 0xffffff40, 0xffffff00);
}

static void face_button(float cx, float cy, float r, int icon)
{
    float s = r * 0.5f, lw = r * 0.19f;
    int i;

    button_body(cx - r, cy - r, r * 2, r * 2, r);
    switch (icon) {
    case ICON_CROSS:
        thick_line(cx - s, cy - s, cx + s, cy + s, lw, C_CROSS);
        thick_line(cx + s, cy - s, cx - s, cy + s, lw, C_CROSS);
        break;
    case ICON_CIRCLE:
        ring(cx, cy, s * 1.05f, lw, C_CIRCLE);
        break;
    case ICON_TRIANGLE: {
        float radius = s * 1.32f, k = 1.0f - 2.0f * lw / radius, oy = cy + radius * 0.14f;
        float ax[3], ay[3];

        for (i = 0; i < 3; i++) {
            float a = -PI_F * 0.5f + i * 2 * PI_F / 3;

            ax[i] = cx + cosf(a) * radius;
            ay[i] = oy + sinf(a) * radius;
        }
        tiny3d_SetPolygon(TINY3D_QUADS);
        for (i = 0; i < 3; i++) {
            int j = (i + 1) % 3;

            vtx(ax[i], ay[i], C_TRIANGLE);
            vtx(ax[j], ay[j], C_TRIANGLE);
            vtx(cx + (ax[j] - cx) * k, oy + (ay[j] - oy) * k, C_TRIANGLE);
            vtx(cx + (ax[i] - cx) * k, oy + (ay[i] - oy) * k, C_TRIANGLE);
        }
        tiny3d_End();
        break;
    }
    case ICON_SQUARE: {
        float a = s * 0.92f;

        rect(cx - a, cy - a, a * 2, lw, C_SQUARE);
        rect(cx - a, cy + a - lw, a * 2, lw, C_SQUARE);
        rect(cx - a, cy - a + lw, lw, a * 2 - lw * 2, C_SQUARE);
        rect(cx + a - lw, cy - a + lw, lw, a * 2 - lw * 2, C_SQUARE);
        break;
    }
    }
}

#define CAP_TEXT 10

static float cap_width(const char *label)
{
    float w = text_width(label, CAP_TEXT) + 12;

    return w < 24 ? 24 : w;
}

/* Small shoulder-button cap with its name on it, e.g. L1. */
static float key_cap(float x, float cy, const char *label)
{
    float w = cap_width(label), h = 16;

    button_body(x, cy - h / 2, w, h, 4);
    text(x + (w - text_width(label, CAP_TEXT)) / 2, cy - CAP_TEXT / 2.0f - 1, CAP_TEXT, 0xe8edf5ff, label);
    return w;
}

typedef struct {
    int icon;
    const char *label;
} hint_item;

#define HINT_SIZE   15
#define HINT_GAP    24
#define HINT_RADIUS 10

static float glyph_width(int icon)
{
    if (icon == ICON_SHOULDERS)
        return cap_width("L1") + 3 + cap_width("R1");
    return HINT_RADIUS * 2;
}

static float hint_width(const hint_item *h)
{
    return glyph_width(h->icon) + 8 + text_width(h->label, HINT_SIZE);
}

/* A centered row of controller hints with DualShock-style keys, as in AkTeknoloji Store V2. */
static void draw_hint_row(const hint_item *items, int n, float y)
{
    float total = 0, x, cy = y + HINT_SIZE / 2.0f + 1;
    int i;

    for (i = 0; i < n; i++)
        total += hint_width(&items[i]) + (i ? HINT_GAP : 0);

    x = (SCREEN_W - total) / 2;
    for (i = 0; i < n; i++) {
        if (items[i].icon == ICON_SHOULDERS) {
            float w = key_cap(x, cy, "L1");

            key_cap(x + w + 3, cy, "R1");
        } else {
            face_button(x + HINT_RADIUS, cy, HINT_RADIUS, items[i].icon);
        }
        text(x + glyph_width(items[i].icon) + 8, y, HINT_SIZE, C_DIM, items[i].label);
        x += hint_width(&items[i]) + HINT_GAP;
    }
}

/* Slow XMB-style waves behind everything, drawn as thin translucent ribbons. */
static void draw_waves(float t)
{
    static const u32 colors[] = { 0x3b4f9e73, 0x5a6fc44d, 0x3b4f9e38 };
    static const float thickness[] = { 1.6f, 1.1f, 1.1f };
    int k, i;

    for (k = 0; k < 3; k++) {
        tiny3d_SetPolygon(TINY3D_TRIANGLE_STRIP);
        for (i = 0; i <= 96; i++) {
            float x = SCREEN_W * i / 96;
            float y = 286 + k * 20 + sinf(x * 0.0075f + t * (0.35f + 0.12f * k) + k * 1.3f) * (30 - k * 7);

            tiny3d_VertexPos(x, y - thickness[k], 65535);
            if (i == 0)
                tiny3d_VertexColor(colors[k]);
            tiny3d_VertexPos(x, y + thickness[k], 65535);
        }
        tiny3d_End();
    }
}

static u32 rgba(float r, float g, float b, float a)
{
    return ((u32)(r * 255) << 24) | ((u32)(g * 255) << 16) | ((u32)(b * 255) << 8) | (u32)(a * 255);
}

/* ------------------------------------------------------------------ covers */

static void textured_quad(float w, float top, float bottom, float v_top, float v_bottom, u32 c_top, u32 c_bottom)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(-w, top, 0);
    tiny3d_VertexColor(c_top);
    tiny3d_VertexTexture(0.0f, v_top);
    tiny3d_VertexPos(w, top, 0);
    tiny3d_VertexColor(c_top);
    tiny3d_VertexTexture(1.0f, v_top);
    tiny3d_VertexPos(w, bottom, 0);
    tiny3d_VertexColor(c_bottom);
    tiny3d_VertexTexture(1.0f, v_bottom);
    tiny3d_VertexPos(-w, bottom, 0);
    tiny3d_VertexColor(c_bottom);
    tiny3d_VertexTexture(0.0f, v_bottom);
    tiny3d_End();
}

/* One case centered on the model origin, with its reflection and frame. Unlike 2D mode, +y is up in 3D. */
static void draw_cover(const game_t *g, const cover_slot *slot, const cf_pose *p)
{
    const float w = CF_COVER_W / 2, top = CF_COVER_H / 2, bottom = -CF_COVER_H / 2;
    const float s = p->shade;
    MATRIX m = MatrixMultiply(MatrixRotationY(p->angle), MatrixTranslation(p->x, COVER_Y, p->z));

    tiny3d_SetMatrixModelView(&m);

    if (slot && slot->state == SLOT_READY) {
        tiny3d_SetTexture(0, slot->offset, slot->w, slot->h, slot->w * 4, TINY3D_TEX_FORMAT_A8R8G8B8, TEXTURE_LINEAR);
        textured_quad(w, top, bottom, 0.0f, 1.0f, rgba(s, s, s, p->alpha), rgba(s, s, s, p->alpha));
        /* Mirror of the lower part of the case, fading out downwards. */
        textured_quad(w, bottom - 0.03f, bottom - 0.03f - REFLECTION_H, 1.0f, 1.0f - REFLECTION_H / CF_COVER_H,
                      rgba(s * 0.7f, s * 0.7f, s * 0.7f, 0.24f * p->alpha), rgba(s * 0.7f, s * 0.7f, s * 0.7f, 0.0f));
    } else {
        /* Artwork not decoded yet: a plain case for a frame or two. */
        u32 base = palette_for(g->title)->base;

        tiny3d_SetPolygon(TINY3D_QUADS);
        tiny3d_VertexPos(-w, top, 0);
        tiny3d_VertexColor(rgba(((base >> 16) & 0xff) / 255.0f * s, ((base >> 8) & 0xff) / 255.0f * s,
                                (base & 0xff) / 255.0f * s, p->alpha));
        tiny3d_VertexPos(w, top, 0);
        tiny3d_VertexPos(w, bottom, 0);
        tiny3d_VertexPos(-w, bottom, 0);
        tiny3d_End();
    }

    /* Hairline frame, brighter on the selected case. */
    tiny3d_SetPolygon(TINY3D_LINE_LOOP);
    tiny3d_VertexPos(-w, top, 0);
    tiny3d_VertexColor(rgba(1, 1, 1, (0.18f + 0.37f * p->focus) * p->alpha));
    tiny3d_VertexPos(w, top, 0);
    tiny3d_VertexPos(w, bottom, 0);
    tiny3d_VertexPos(-w, bottom, 0);
    tiny3d_End();
}

static void draw_carousel(const library_t *lib, float pos)
{
    MATRIX proj = MatrixProjPerspective(CF_FOV, (Video_aspect == 1) ? 9.0f / 16.0f : 1.0f, 0.00125f, 300.0f);
    int order[2 * (int)CF_VISIBLE + 2];
    int i, n = 0;
    int first = (int)floorf(pos - CF_VISIBLE), last = (int)ceilf(pos + CF_VISIBLE);

    tiny3d_Project3D();
    tiny3d_SetProjectionMatrix(&proj);

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

/* ------------------------------------------------------------------ frame */

void gfx_frame_begin(void)
{
    anim_time += 1.0f / 60.0f;

    tiny3d_Clear(COLOR_CLEAR, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(1, 0x01, TINY3D_ALPHA_FUNC_GEQUAL);
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

static void draw_header(const library_t *lib)
{
    char count[48], clock[32] = "", right[96];
    time_t now = time(NULL) + utc_offset * 60;
    struct tm *t = gmtime(&now);
    float x;

    x = text(40, ROW_HEADER, 18, C_ACCENT, "PS3SX2");
    text(x + 12, ROW_HEADER, 18, C_TEXT, lang_str(STR_LIBRARY));

    snprintf(count, sizeof(count), lang_str(STR_GAME_COUNT), lib->count);
    if (t)
        snprintf(clock, sizeof(clock), "%02d:%02d", t->tm_hour, t->tm_min);
    snprintf(right, sizeof(right), "%s   %s", count, clock);
    x = SCREEN_W - 40 - text_width(right, 15);
    text(x, ROW_HEADER + 3, 15, C_DIM, right);
    if (header_note[0])
        text(x - 18 - text_width(header_note, 15), ROW_HEADER + 3, 15, C_ACCENT, header_note);
}

typedef struct {
    const char *label;
    u32 bg, fg;
} chip;

/* Serial, region, size and format as small rounded tags, centered under the title. */
static void draw_chips(const chip *chips, int n, float y)
{
    const float size = 14, pad = 10, gap = 8, h = 22;
    float total = 0, x;
    int i;

    for (i = 0; i < n; i++)
        total += text_width(chips[i].label, size) + 2 * pad + (i ? gap : 0);

    x = (SCREEN_W - total) / 2;
    for (i = 0; i < n; i++) {
        float w = text_width(chips[i].label, size) + 2 * pad;

        rounded_rect(x, y, w, h, 5, chips[i].bg);
        text(x + pad, y + 3, size, chips[i].fg, chips[i].label);
        x += w + gap;
    }
}

static void draw_details(const library_t *lib, int sel)
{
    const game_t *g = &lib->games[sel];
    const char *name = strrchr(g->path, '/');
    const char *region = serial_region(g->serial);
    char size[32], line[GAME_PATH_MAX + 32];
    chip chips[4];
    int n = 0;

    text_centered(ROW_TITLE, 24, C_TEXT, g->title);

    format_size(g->size, size, sizeof(size));
    chips[n++] = (chip){ g->serial[0] ? g->serial : lang_str(STR_UNKNOWN_SERIAL), 0x1d2a55ff, 0xb9c8ffff };
    if (region[0])
        chips[n++] = (chip){ region, 0x1c3a33ff, 0x9fe1cbff };
    chips[n++] = (chip){ size, 0x2a2a35ff, 0xc9cbd6ff };
    if (g->format == GAME_FMT_ISO && on_hen)
        chips[n++] = (chip){ lang_str(STR_ISO_HEN), 0x4a3510ff, 0xffc46bff };  /* amber warning */
    else
        chips[n++] = (chip){ g->format == GAME_FMT_ISO ? "ISO" : "PS2 Classic", 0x2d2550ff, 0xc8b8ffff };
    draw_chips(chips, n, ROW_CHIPS);

    snprintf(line, sizeof(line), "%d / %d  ·  %s", sel + 1, lib->count, name ? name + 1 : g->path);
    text_centered(ROW_PATH, 14, C_FAINT, line);
}

void gfx_draw_library(const library_t *lib, float pos, int sel, int show_ui)
{
    hint_item hints[5];
    int n = 0;

    if (lib->count > 0)
        draw_carousel(lib, pos);

    if (!show_ui)
        return;

    tiny3d_Project2D();
    draw_header(lib);

    if (lib->count > 0) {
        draw_details(lib, sel);
    } else {
        text_centered(210, 24, C_TEXT, lang_str(STR_EMPTY_TITLE));
        text_centered(250, 14, C_DIM, lang_str(STR_EMPTY_HINT));
    }

    rect(40, ROW_RULE, SCREEN_W - 80, 1, C_RULE);
    if (lib->count > 0)
        hints[n++] = (hint_item){ ICON_CROSS, lang_str(STR_LAUNCH) };
    hints[n++] = (hint_item){ ICON_CIRCLE, lang_str(STR_EXIT) };
    hints[n++] = (hint_item){ ICON_TRIANGLE, lang_str(STR_RESCAN) };
    if (lib->count > 0)
        hints[n++] = (hint_item){ ICON_SQUARE, lang_str(STR_COVERS) };
    if (lib->count > 0)
        hints[n++] = (hint_item){ ICON_SHOULDERS, lang_str(STR_FAST_SCROLL) };
    draw_hint_row(hints, n, ROW_HINTS);
}

/* Three-quarter ring that keeps turning while something is in progress. */
static void spinner(float cx, float cy, float r)
{
    float start = anim_time * 6.0f;
    int i;

    tiny3d_SetPolygon(TINY3D_LINE_STRIP);
    for (i = 0; i <= 18; i++) {
        float a = start + i * (4.712389f / 18);

        tiny3d_VertexPos(cx + cosf(a) * r, cy + sinf(a) * r, 0);
        if (i == 0)
            tiny3d_VertexColor(C_ACCENT);
    }
    tiny3d_End();
}

void gfx_draw_overlay(const char *title, const char *detail, float progress, int show_continue)
{
    hint_item h;

    tiny3d_Project2D();
    rect(0, 0, SCREEN_W, SCREEN_H, C_PANEL);

    if (progress >= 0.0f)
        spinner(SCREEN_W / 2, 176, 11);

    text_centered(204, 22, C_TEXT, title);
    if (detail && detail[0])
        text_centered(238, 15, C_DIM, detail);

    if (progress >= 0.0f) {
        if (progress > 1.0f)
            progress = 1.0f;
        rounded_rect(224, 278, 400, 8, 4, C_TRACK);
        if (progress > 0.02f)
            rounded_rect(224, 278, 400 * progress, 8, 4, C_ACCENT);
        h = (hint_item){ ICON_CIRCLE, lang_str(STR_CANCEL) };
        draw_hint_row(&h, 1, 306);
    } else if (show_continue) {
        h = (hint_item){ ICON_CROSS, lang_str(STR_CONTINUE) };
        draw_hint_row(&h, 1, 286);
    }
}
