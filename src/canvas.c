/* Canvas implementation + bitmap font rendering. */
#include "canvas.h"
/* Public-domain 8x8 font (Daniel Hepper, font8x8, CC0) — vendored verbatim: */
#include "../vendor/font8x8_basic.h"
#include <string.h>

void cv_fill(Canvas *c, Uint32 col)
{
    col |= 0xff000000u;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++)
            c->px[y * c->pitch + x] = col;
}

void cv_rect(Canvas *c, int x, int y, int w, int h, Uint32 col)
{
    col |= 0xff000000u;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > c->w) w = c->w - x;
    if (y + h > c->h) h = c->h - y;
    for (int j = 0; j < h; j++) {
        Uint32 *row = c->px + (y + j) * c->pitch + x;
        for (int i = 0; i < w; i++) row[i] = col;
    }
}

void cv_border(Canvas *c, int x, int y, int w, int h, int t, Uint32 col)
{
    cv_rect(c, x, y, w, t, col);
    cv_rect(c, x, y + h - t, w, t, col);
    cv_rect(c, x, y + t, t, h - 2*t, col);
    cv_rect(c, x + w - t, y + t, t, h - 2*t, col);
}

int cv_text_w(const char *s, int scale)
{
    return (int)strlen(s) * 8 * scale;
}

void cv_text(Canvas *c, int x, int y, const char *s, int scale, Uint32 col)
{
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch > 127) ch = '?';
        const char *glyph = font8x8_basic[ch];
        for (int r = 0; r < 8; r++) {
            unsigned char bits = (unsigned char)glyph[r];
            for (int b = 0; b < 8; b++) {
                if (bits & (1 << b))
                    cv_rect(c, x + b * scale, y + r * scale, scale, scale, col);
            }
        }
        x += 8 * scale;
    }
}
