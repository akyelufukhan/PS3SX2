#include "canvas.h"

#include <math.h>

/* Blends `rgb` onto one pixel with coverage `a` (0-255). */
static void blend(uint32_t *p, uint32_t rgb, unsigned a)
{
    uint32_t d = *p;
    unsigned r = (((rgb >> 16) & 0xff) * a + ((d >> 16) & 0xff) * (255 - a)) / 255;
    unsigned g = (((rgb >> 8) & 0xff) * a + ((d >> 8) & 0xff) * (255 - a)) / 255;
    unsigned b = ((rgb & 0xff) * a + (d & 0xff) * (255 - a)) / 255;

    *p = 0xff000000u | (r << 16) | (g << 8) | b;
}

void canvas_fill(canvas *c, uint32_t argb)
{
    int i;

    for (i = 0; i < c->w * c->h; i++)
        c->px[i] = argb | 0xff000000u;
}

void canvas_rect(canvas *c, int x, int y, int w, int h, uint32_t argb)
{
    unsigned a = argb >> 24;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->w ? c->w : x + w, y1 = y + h > c->h ? c->h : y + h;
    int i, j;

    for (j = y0; j < y1; j++) {
        for (i = x0; i < x1; i++)
            blend(&c->px[j * c->w + i], argb, a);
    }
}

/* Walks the circle's bounding box and blends by coverage(distance from center). */
static void shade_circle(canvas *c, float cx, float cy, float reach, uint32_t argb,
                         float (*coverage)(float dist, float r, float t), float r, float t)
{
    unsigned a = argb >> 24;
    int x0 = (int)floorf(cx - reach), x1 = (int)ceilf(cx + reach);
    int y0 = (int)floorf(cy - reach), y1 = (int)ceilf(cy + reach);
    int i, j;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w - 1) x1 = c->w - 1;
    if (y1 > c->h - 1) y1 = c->h - 1;

    for (j = y0; j <= y1; j++) {
        for (i = x0; i <= x1; i++) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            float cov = coverage(sqrtf(dx * dx + dy * dy), r, t);

            if (cov > 0.0f)
                blend(&c->px[j * c->w + i], argb, (unsigned)(a * (cov > 1.0f ? 1.0f : cov)));
        }
    }
}

static float disc_coverage(float dist, float r, float t)
{
    (void)t;
    return r + 0.5f - dist;
}

static float ring_coverage(float dist, float r, float t)
{
    return t / 2 + 0.5f - fabsf(dist - r);
}

void canvas_disc(canvas *c, float cx, float cy, float r, uint32_t argb)
{
    shade_circle(c, cx, cy, r + 1, argb, disc_coverage, r, 0);
}

void canvas_ring(canvas *c, float cx, float cy, float r, float thickness, uint32_t argb)
{
    shade_circle(c, cx, cy, r + thickness / 2 + 1, argb, ring_coverage, r, thickness);
}

void canvas_mask(canvas *c, int x, int y, const unsigned char *mask, int w, int h, int pitch, uint32_t rgb)
{
    int i, j;

    for (j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= c->h)
            continue;
        for (i = 0; i < w; i++) {
            if (x + i >= 0 && x + i < c->w && mask[j * pitch + i])
                blend(&c->px[(y + j) * c->w + x + i], rgb, mask[j * pitch + i]);
        }
    }
}
