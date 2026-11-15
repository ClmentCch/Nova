/*
 * NOVA Boot - software renderer.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ui.h"
#include "font8x8.h"

static uint32_t *g_fb;
static int g_w, g_h, g_stride;

void nova_ui_bind(uint32_t *fb, int width, int height, int stride)
{
    g_fb = fb;
    g_w = width;
    g_h = height;
    g_stride = stride;
}

int nova_ui_width(void) { return g_w; }
int nova_ui_height(void) { return g_h; }

uint32_t nova_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return 0xFF000000u | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

uint32_t nova_mix(uint32_t a, uint32_t b, uint32_t t)
{
    if (t > 255)
        t = 255;
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        uint32_t ca = (a >> shift) & 0xFF;
        uint32_t cb = (b >> shift) & 0xFF;
        out |= ((ca * (255 - t) + cb * t) / 255) << shift;
    }
    return out;
}

uint32_t nova_dim(uint32_t color, uint32_t t)
{
    return nova_mix(0xFF000000u, color, t);
}

static inline uint32_t blend_px(uint32_t dst, uint32_t src, uint32_t alpha)
{
    if (alpha >= 255)
        return 0xFF000000u | (src & 0x00FFFFFFu);
    if (alpha == 0)
        return dst;
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        uint32_t d = (dst >> shift) & 0xFF;
        uint32_t s = (src >> shift) & 0xFF;
        out |= (((d * (255 - alpha)) + (s * alpha)) / 255) << shift;
    }
    return out;
}

static inline uint32_t get_px(const uint32_t *fb, int x, int y)
{
    return fb[y * g_stride + x];
}

static inline void put_px(uint32_t *fb, int x, int y, uint32_t color)
{
    fb[y * g_stride + x] = color;
}

void nova_clear(uint32_t color)
{
    if (!g_fb)
        return;
    for (int y = 0; y < g_h; y++)
    {
        uint32_t *row = g_fb + y * g_stride;
        for (int x = 0; x < g_w; x++)
            row[x] = color;
    }
}

void nova_fill(int x, int y, int w, int h, uint32_t color)
{
    if (!g_fb)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_w ? g_w : x + w;
    int y1 = y + h > g_h ? g_h : y + h;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int yy = y0; yy < y1; yy++)
    {
        uint32_t *row = g_fb + yy * g_stride;
        for (int xx = x0; xx < x1; xx++)
            row[xx] = color;
    }
}

void nova_fill_blend(int x, int y, int w, int h, uint32_t color, uint32_t alpha)
{
    if (!g_fb || alpha == 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_w ? g_w : x + w;
    int y1 = y + h > g_h ? g_h : y + h;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int yy = y0; yy < y1; yy++)
    {
        uint32_t *row = g_fb + yy * g_stride;
        for (int xx = x0; xx < x1; xx++)
            row[xx] = blend_px(row[xx], color, alpha);
    }
}

void nova_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    if (!g_fb || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > g_w ? g_w : x + w;
    if (x0 >= x1)
        return;
    for (int yy = 0; yy < h; yy++)
    {
        int py = y + yy;
        if (py < 0 || py >= g_h)
            continue;
        uint32_t t = (uint32_t)((yy * 255) / (h > 1 ? h - 1 : 1));
        uint32_t color = nova_mix(top, bottom, t);
        uint32_t *row = g_fb + py * g_stride;
        for (int xx = x0; xx < x1; xx++)
            row[xx] = color;
    }
}

static int sqrt_approx(int value)
{
    if (value <= 0)
        return 0;
    int x = value;
    int y = (x + 1) / 2;
    /* Integer Newton iteration converges quickly for our radii. */
    for (int i = 0; i < 12 && y < x; i++)
    {
        x = y;
        y = (x + value / x) / 2;
    }
    return x;
}

/* Rounded rectangle row inset for a given distance from the corner center. */
static int corner_inset(int radius, int dy)
{
    int d = radius - 1 - dy;
    int sq = radius * radius - d * d;
    if (sq < 0)
        sq = 0;
    return radius - sqrt_approx(sq);
}

static void round_span(int y, int x, int w, int radius, int h, uint32_t color, uint32_t alpha)
{
    int inset = 0;
    if (y < radius)
        inset = corner_inset(radius, y);
    else if (y >= h - radius)
        inset = corner_inset(radius, h - 1 - y);

    int sx = x + inset;
    int ex = x + w - inset;
    if (sx < 0)
        sx = 0;
    if (ex > g_w)
        ex = g_w;
    if (sx >= ex)
        return;

    uint32_t *row = g_fb + y * g_stride;
    for (int xx = sx; xx < ex; xx++)
        row[xx] = alpha >= 255 ? color : blend_px(row[xx], color, alpha);
}

void nova_round(int x, int y, int w, int h, int radius, uint32_t color)
{
    nova_round_blend(x, y, w, h, radius, color, 255);
}

void nova_round_blend(int x, int y, int w, int h, int radius, uint32_t color, uint32_t alpha)
{
    if (!g_fb || w <= 0 || h <= 0)
        return;
    if (radius > w / 2)
        radius = w / 2;
    if (radius > h / 2)
        radius = h / 2;
    if (radius < 0)
        radius = 0;

    for (int yy = 0; yy < h; yy++)
    {
        int py = y + yy;
        if (py < 0 || py >= g_h)
            continue;
        round_span(py, x, w, radius, h, color, alpha);
    }
}

void nova_round_ring(int x, int y, int w, int h, int radius, int thickness, uint32_t color, uint32_t alpha)
{
    if (!g_fb || thickness <= 0 || w <= 0 || h <= 0)
        return;
    if (radius > w / 2)
        radius = w / 2;
    if (radius > h / 2)
        radius = h / 2;
    if (thickness > radius)
        thickness = radius;

    /* Straight edges. */
    nova_fill_blend(x + radius, y, w - 2 * radius, thickness, color, alpha);
    nova_fill_blend(x + radius, y + h - thickness, w - 2 * radius, thickness, color, alpha);
    nova_fill_blend(x, y + radius, thickness, h - 2 * radius, color, alpha);
    nova_fill_blend(x + w - thickness, y + radius, thickness, h - 2 * radius, color, alpha);

    /* Corner annuli. */
    int r2_out = radius * radius;
    int inner = radius - thickness;
    int r2_in = inner > 0 ? inner * inner : -1;

    for (int corner = 0; corner < 4; corner++)
    {
        bool right = (corner & 1) != 0;
        bool bottom = (corner & 2) != 0;
        int cx = right ? x + w - radius : x + radius - 1;
        int cy = bottom ? y + h - radius : y + radius - 1;

        /* Only the quadrant that belongs to this corner. */
        int dx0 = right ? 0 : -radius;
        int dx1 = right ? radius : 0;
        int dy0 = bottom ? 0 : -radius;
        int dy1 = bottom ? radius : 0;

        for (int dy = dy0; dy <= dy1; dy++)
        {
            int py = cy + dy;
            if (py < 0 || py >= g_h)
                continue;
            uint32_t *row = g_fb + py * g_stride;
            for (int dx = dx0; dx <= dx1; dx++)
            {
                int px = cx + dx;
                if (px < 0 || px >= g_w)
                    continue;
                int d2 = dx * dx + dy * dy;
                if (d2 <= r2_out && d2 > r2_in)
                    row[px] = blend_px(row[px], color, alpha);
            }
        }
    }
}

void nova_hline(int x, int y, int w, uint32_t color)
{
    nova_fill(x, y, w, 1, color);
}

void nova_vline(int x, int y, int h, uint32_t color)
{
    nova_fill(x, y, 1, h, color);
}

void nova_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx);
    if ((dy < 0 ? -dy : dy) > steps)
        steps = (dy < 0 ? -dy : dy);
    if (steps == 0)
    {
        if (x0 >= 0 && x0 < g_w && y0 >= 0 && y0 < g_h)
            put_px(g_fb, x0, y0, color);
        return;
    }
    for (int i = 0; i <= steps; i++)
    {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        if (x >= 0 && x < g_w && y >= 0 && y < g_h)
            put_px(g_fb, x, y, color);
    }
}

void nova_dot(int cx, int cy, int radius, uint32_t color)
{
    nova_ring(cx, cy, radius, radius, color, 255);
}

void nova_glow(int cx, int cy, int radius, uint32_t color, uint32_t max_alpha)
{
    if (!g_fb || radius <= 0)
        return;

    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++)
    {
        int py = cy + dy;
        if (py < 0 || py >= g_h)
            continue;
        uint32_t *row = g_fb + py * g_stride;
        for (int dx = -radius; dx <= radius; dx++)
        {
            int px = cx + dx;
            if (px < 0 || px >= g_w)
                continue;
            int d2 = dx * dx + dy * dy;
            if (d2 > r2)
                continue;
            int falloff = radius - sqrt_approx(d2);
            uint32_t a = (uint32_t)((falloff * falloff) * (int)max_alpha / (r2 ? r2 : 1));
            if (a)
                row[px] = blend_px(row[px], color, a > 255 ? 255 : a);
        }
    }
}

void nova_ring(int cx, int cy, int radius, int thickness, uint32_t color, uint32_t alpha)
{
    if (!g_fb || radius <= 0 || thickness <= 0)
        return;

    int inner = radius - thickness;
    int inner2 = inner > 0 ? inner * inner : -1;
    int outer2 = radius * radius;

    for (int dy = -radius; dy <= radius; dy++)
    {
        int y = cy + dy;
        if (y < 0 || y >= g_h)
            continue;
        uint32_t *row = g_fb + y * g_stride;
        for (int dx = -radius; dx <= radius; dx++)
        {
            int x = cx + dx;
            if (x < 0 || x >= g_w)
                continue;
            int d2 = dx * dx + dy * dy;
            if (d2 <= outer2 && d2 > inner2)
                row[x] = blend_px(row[x], color, alpha);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */
static int glyph_columns(char c)
{
    if (c < NOVA_FONT_FIRST || c > NOVA_FONT_LAST)
        return 0;
    const uint8_t *g = &nova_font8x8[(c - NOVA_FONT_FIRST) * 8];
    int last = -1;
    for (int col = 0; col < 8; col++)
    {
        for (int row = 0; row < 8; row++)
        {
            if (g[row] & (1u << col))
            {
                last = col;
                break;
            }
        }
    }
    return last + 1;
}

static void draw_glyph(int x, int y, char c, int scale, uint32_t color)
{
    if (c < NOVA_FONT_FIRST || c > NOVA_FONT_LAST)
        return;

    const uint8_t *g = &nova_font8x8[(c - NOVA_FONT_FIRST) * 8];
    for (int row = 0; row < 8; row++)
    {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++)
        {
            if (!(bits & (1u << col)))
                continue;
            for (int sy = 0; sy < scale; sy++)
            {
                int py = y + row * scale + sy;
                if (py < 0 || py >= g_h)
                    continue;
                uint32_t *line = g_fb + py * g_stride;
                for (int sx = 0; sx < scale; sx++)
                {
                    int px = x + col * scale + sx;
                    if (px >= 0 && px < g_w)
                        line[px] = color;
                }
            }
        }
    }
}

int nova_text_width(const char *text, int scale, int tracking)
{
    if (!text)
        return 0;
    int width = 0;
    for (const char *p = text; *p; p++)
    {
        int cols = glyph_columns(*p);
        if (cols == 0)
            cols = 4; /* space */
        width += cols * scale;
        if (p[1])
            width += scale + tracking;
    }
    return width;
}

static void text_impl(int x, int y, const char *text, int scale, int tracking,
                      uint32_t color)
{
    if (!g_fb || !text || scale <= 0)
        return;

    int cursor = x;
    for (const char *p = text; *p; p++)
    {
        int cols = glyph_columns(*p);
        if (cols == 0)
            cols = 4;
        draw_glyph(cursor, y, *p, scale, color);
        cursor += cols * scale;
        if (p[1])
            cursor += scale + tracking;
    }
}

void nova_text(int x, int y, const char *text, int scale, uint32_t color)
{
    text_impl(x, y, text, scale, 0, color);
}

void nova_text_tracked(int x, int y, const char *text, int scale, int tracking, uint32_t color)
{
    text_impl(x, y, text, scale, tracking, color);
}

void nova_text_grad(int x, int y, const char *text, int scale, uint32_t from, uint32_t to)
{
    if (!g_fb || !text || scale <= 0)
        return;

    int total = nova_text_width(text, scale, 0);
    int cursor = x;
    for (const char *p = text; *p; p++)
    {
        int cols = glyph_columns(*p);
        if (cols == 0)
            cols = 4;
        int glyph_w = cols * scale;
        int t = (total > 1) ? ((cursor - x) * 255 / total) : 0;
        if (t < 0)
            t = 0;
        if (t > 255)
            t = 255;
        draw_glyph(cursor, y, *p, scale, nova_mix(from, to, (uint32_t)t));
        cursor += glyph_w;
        if (p[1])
            cursor += scale;
    }
}

void nova_text_right(int x_right, int y, const char *text, int scale, uint32_t color)
{
    nova_text(x_right - nova_text_width(text, scale, 0), y, text, scale, color);
}

void nova_text_center(int x_center, int y, const char *text, int scale, uint32_t color)
{
    nova_text(x_center - nova_text_width(text, scale, 0) / 2, y, text, scale, color);
}

void nova_text_center_tracked(int x_center, int y, const char *text, int scale, int tracking, uint32_t color)
{
    nova_text_tracked(x_center - nova_text_width(text, scale, tracking) / 2, y,
                      text, scale, tracking, color);
}

void nova_blit(uint32_t *dst, const uint32_t *src, int x, int y, int w, int h)
{
    if (!dst || !src)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_w ? g_w : x + w;
    int y1 = y + h > g_h ? g_h : y + h;
    for (int yy = y0; yy < y1; yy++)
    {
        uint32_t *drow = dst + yy * g_stride;
        const uint32_t *srow = src + yy * g_stride;
        for (int xx = x0; xx < x1; xx++)
            drow[xx] = srow[xx];
    }
}
