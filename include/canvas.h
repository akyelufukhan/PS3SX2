#ifndef PS3SX2_CANVAS_H
#define PS3SX2_CANVAS_H

#include <stdint.h>

/*
 * Tiny software painter for generated cover art. Pixels are 0xAARRGGBB, the
 * layout Tiny3D's A8R8G8B8 textures use. The canvas stays opaque: every draw
 * blends onto it by the source alpha. Shapes are clipped to the canvas.
 */
typedef struct {
    uint32_t *px;
    int w, h;
} canvas;

void canvas_fill(canvas *c, uint32_t argb);
void canvas_rect(canvas *c, int x, int y, int w, int h, uint32_t argb);

/* Filled circle and circle outline, both with anti-aliased edges. */
void canvas_disc(canvas *c, float cx, float cy, float r, uint32_t argb);
void canvas_ring(canvas *c, float cx, float cy, float r, float thickness, uint32_t argb);

/* Paints `rgb` through an 8-bit coverage mask, such as a FreeType glyph bitmap. */
void canvas_mask(canvas *c, int x, int y, const unsigned char *mask, int w, int h, int pitch, uint32_t rgb);

#endif
