/* Text for the settings panel (t2_text.h). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "t2_text.h"

/* t2_font_data.c, written by CMake from src/fonts/NotoSans-Regular.ttf */
extern const unsigned char t2_font_ttf[];
extern const unsigned int t2_font_ttf_size;

struct T2Font {
    stbtt_fontinfo info;
};

T2Font *t2_font_new(void)
{
    T2Font *f = calloc(1, sizeof(T2Font));

    if (!f) {
        return NULL;
    }
    if (!t2_font_ttf_size || !stbtt_InitFont(&f->info, t2_font_ttf, stbtt_GetFontOffsetForIndex(t2_font_ttf, 0))) {
        free(f);
        return NULL;
    }
    return f;
}

void t2_font_free(T2Font *f)
{
    free(f);
}

/* the next code point of UTF-8 text (Latin-1 is all the font has) */
static uint32_t next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;

    if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
    } else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
    } else if (c >= 0x80) {
        c = '?';
    }
    *s = (const char *)p;
    return c;
}

static int glyph(const stbtt_fontinfo *fi, uint32_t cp)
{
    int g = stbtt_FindGlyphIndex(fi, (int)cp);

    return g ? g : stbtt_FindGlyphIndex(fi, '?');
}

float t2_text_width(T2Font *f, float px, const char *s)
{
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, px), w = 0;
    int prev = 0;

    while (*s) {
        int g = glyph(&f->info, next_cp(&s)), adv, lsb;

        stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
        if (prev) {
            w += stbtt_GetGlyphKernAdvance(&f->info, prev, g) * scale;
        }
        w += adv * scale;
        prev = g;
    }
    return w;
}

float t2_text_ascent(T2Font *f, float px)
{
    int asc, desc, gap;

    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    return asc * stbtt_ScaleForMappingEmToPixels(&f->info, px);
}

void t2_blend(T2Image *img, int x, int y, uint32_t color, int cover)
{
    uint32_t *d;
    unsigned sa, da, oa;

    if (x < 0 || y < 0 || x >= img->w || y >= img->h || cover <= 0) {
        return;
    }
    sa = ((color >> 24) * (unsigned)cover + 127) / 255;
    if (!sa) {
        return;
    }
    d = &img->px[(size_t)y * img->w + x];
    da = *d >> 24;
    /* straight alpha "over" */
    oa = sa + da * (255 - sa) / 255;
    if (!oa) {
        return;
    }
    {
        unsigned out = oa << 24;
        int sh;

        for (sh = 0; sh <= 16; sh += 8) {
            unsigned sc = (color >> sh) & 0xFF, dc = (*d >> sh) & 0xFF;
            unsigned c = (sc * sa + dc * da * (255 - sa) / 255) / oa;

            out |= (c > 255 ? 255 : c) << sh;
        }
        *d = out;
    }
}

void t2_fill(T2Image *img, int x, int y, int w, int h, uint32_t color)
{
    int i, j;

    for (j = y < 0 ? 0 : y; j < y + h && j < img->h; j++) {
        for (i = x < 0 ? 0 : x; i < x + w && i < img->w; i++) {
            t2_blend(img, i, j, color, 255);
        }
    }
}

void t2_fill_round(T2Image *img, int x, int y, int w, int h, int r, uint32_t color)
{
    int i, j;

    if (r * 2 > w) {
        r = w / 2;
    }
    if (r * 2 > h) {
        r = h / 2;
    }
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            /* distance from the nearest corner centre, for pixels in a corner square */
            float cx = i < r ? r - 0.5f : i >= w - r ? w - r - 0.5f : (float)i;
            float cy = j < r ? r - 0.5f : j >= h - r ? h - r - 0.5f : (float)j;
            float dx = i - cx, dy = j - cy, dist = sqrtf(dx * dx + dy * dy);
            float c = (float)r + 0.5f - dist;

            if (c >= 1.0f || r == 0) {
                t2_blend(img, x + i, y + j, color, 255);
            } else if (c > 0.0f) {
                t2_blend(img, x + i, y + j, color, (int)(c * 255.0f));
            }
        }
    }
}

void t2_text_draw(T2Font *f, T2Image *img, float x, float y, float px, const char *s, uint32_t color)
{
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, px);
    int prev = 0, baseline = (int)floorf(y + 0.5f);

    while (*s) {
        int g = glyph(&f->info, next_cp(&s)), adv, lsb, x0, y0, x1, y1, w, h, i, j;
        float sub;
        unsigned char *bmp;

        if (prev) {
            x += stbtt_GetGlyphKernAdvance(&f->info, prev, g) * scale;
        }
        stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
        sub = x - floorf(x);
        stbtt_GetGlyphBitmapBoxSubpixel(&f->info, g, scale, scale, sub, 0, &x0, &y0, &x1, &y1);
        w = x1 - x0;
        h = y1 - y0;
        if (w > 0 && h > 0 && (bmp = malloc((size_t)w * h)) != NULL) {
            stbtt_MakeGlyphBitmapSubpixel(&f->info, bmp, w, h, w, scale, scale, sub, 0, g);
            for (j = 0; j < h; j++) {
                for (i = 0; i < w; i++) {
                    t2_blend(img, (int)floorf(x) + x0 + i, baseline + y0 + j, color, bmp[j * w + i]);
                }
            }
            free(bmp);
        }
        x += adv * scale;
        prev = g;
    }
}
