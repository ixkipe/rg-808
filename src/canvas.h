/* Tiny software canvas: opaque 640x480 ARGB8888 surface with 1-bit font blits. */
#ifndef RG808_CANVAS_H
#define RG808_CANVAS_H

#include <SDL.h>

typedef struct { Uint32 *px; int w, h, pitch; } Canvas;   /* pitch in pixels */

void cv_fill(Canvas *c, Uint32 col);
void cv_rect(Canvas *c, int x, int y, int w, int h, Uint32 col);
void cv_border(Canvas *c, int x, int y, int w, int h, int t, Uint32 col);
void cv_text(Canvas *c, int x, int y, const char *s, int scale, Uint32 col);
int  cv_text_w(const char *s, int scale);

static inline Uint32 cv_rgb(int r, int g, int b) { return (Uint32)((r << 16) | (g << 8) | b); }
static inline Uint32 cv_dim(Uint32 col, float k)
{
    int r = (int)(((col >> 16) & 0xff) * k), g = (int)(((col >> 8) & 0xff) * k), b = (int)((col & 0xff) * k);
    return cv_rgb(r, g, b);
}

#endif
