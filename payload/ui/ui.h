/*
 * NOVA Boot - software renderer for the Tegra framebuffer.
 *
 * Framebuffer format is WIN_COLOR_DEPTH_B8G8R8A8, addressed as u32 with the
 * value 0xAARRGGBB (identical to hekate's gfx helper colors).
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_UI_H
#define NOVA_UI_H

#include <stdbool.h>
#include <stdint.h>

void nova_ui_bind(uint32_t *fb, int width, int height, int stride);
int  nova_ui_width(void);
int  nova_ui_height(void);

/* Color helpers. */
uint32_t nova_rgb(uint32_t r, uint32_t g, uint32_t b);
uint32_t nova_mix(uint32_t a, uint32_t b, uint32_t t);   /* t: 0..255 */
uint32_t nova_dim(uint32_t color, uint32_t t);           /* toward black */

/* Primitives. */
void nova_clear(uint32_t color);
void nova_fill(int x, int y, int w, int h, uint32_t color);
void nova_fill_blend(int x, int y, int w, int h, uint32_t color, uint32_t alpha);
void nova_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void nova_round(int x, int y, int w, int h, int radius, uint32_t color);
void nova_round_blend(int x, int y, int w, int h, int radius, uint32_t color, uint32_t alpha);
void nova_round_ring(int x, int y, int w, int h, int radius, int thickness, uint32_t color, uint32_t alpha);
void nova_hline(int x, int y, int w, uint32_t color);
void nova_vline(int x, int y, int h, uint32_t color);
void nova_line(int x0, int y0, int x1, int y1, uint32_t color);
void nova_dot(int cx, int cy, int radius, uint32_t color);
void nova_glow(int cx, int cy, int radius, uint32_t color, uint32_t max_alpha);
void nova_ring(int cx, int cy, int radius, int thickness, uint32_t color, uint32_t alpha);

/* Text (8x8 bitmap font, integer scaling). */
void nova_text(int x, int y, const char *text, int scale, uint32_t color);
void nova_text_tracked(int x, int y, const char *text, int scale, int tracking, uint32_t color);
void nova_text_grad(int x, int y, const char *text, int scale, uint32_t from, uint32_t to);
int  nova_text_width(const char *text, int scale, int tracking);
void nova_text_right(int x_right, int y, const char *text, int scale, uint32_t color);
void nova_text_center(int x_center, int y, const char *text, int scale, uint32_t color);
void nova_text_center_tracked(int x_center, int y, const char *text, int scale, int tracking, uint32_t color);

/* Copies a rectangle between buffers of the same geometry. */
void nova_blit(uint32_t *dst, const uint32_t *src, int x, int y, int w, int h);

#endif
