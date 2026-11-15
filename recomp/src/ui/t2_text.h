/* Text and boxes for the settings panel and the FPS readout: Noto Sans
 * (src/fonts, embedded at build time) through stb_truetype, drawn with
 * antialiasing into an ARGB8888 image with straight alpha, the format the
 * runtime composites over the game (psx_title_hooks.h). The approach is the
 * GGX+ port's text/tl_font.c (MIT, Copyright (c) 2026 Lil Gohan), with coverage kept as alpha instead of
 * one bit a pixel. */
#ifndef T2_TEXT_H
#define T2_TEXT_H

#include <stddef.h>
#include <stdint.h>

typedef struct T2Font T2Font;

typedef struct {
    uint32_t *px;   /* ARGB8888, 0xAARRGGBB */
    int w, h;
} T2Image;

/* The embedded font; NULL if it cannot be read. */
T2Font *t2_font_new(void);
void t2_font_free(T2Font *f);

/* Width in pixels of UTF-8 text at size px (pixels to the em). */
float t2_text_width(T2Font *f, float px, const char *s);
/* Ascent above the baseline, in pixels, at size px. */
float t2_text_ascent(T2Font *f, float px);
/* Draws UTF-8 text with its baseline at y, from x. color is 0xAARRGGBB. */
void t2_text_draw(T2Font *f, T2Image *img, float x, float y, float px, const char *s, uint32_t color);

/* Fills a rectangle, blending color over what is there. */
void t2_fill(T2Image *img, int x, int y, int w, int h, uint32_t color);
/* Fills a rectangle with rounded corners of radius r, antialiased. */
void t2_fill_round(T2Image *img, int x, int y, int w, int h, int r, uint32_t color);
/* Blends color over one pixel with an extra coverage 0-255. */
void t2_blend(T2Image *img, int x, int y, uint32_t color, int cover);

#endif
