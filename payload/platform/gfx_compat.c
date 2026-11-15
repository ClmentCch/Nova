/*
 * NOVA Boot - quiet gfx console used by the hekate BDK storage drivers.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "gfx_compat.h"

gfx_ctxt_t gfx_ctxt;
gfx_con_t gfx_con;

void gfx_init_ctxt(u32 *fb, u32 width, u32 height, u32 stride)
{
    gfx_ctxt.fb = fb;
    gfx_ctxt.width = width;
    gfx_ctxt.height = height;
    gfx_ctxt.stride = stride;
    gfx_con.gfx_ctxt = &gfx_ctxt;
}

void gfx_con_init(void)
{
    gfx_con.fntsz = 16;
    gfx_con.mute = true;
}

void gfx_con_setpos(u32 x, u32 y)
{
    gfx_con.x = x;
    gfx_con.y = y;
}

void gfx_clear_grey(u8 color)
{
    (void)color;
}

void gfx_clear_color(u32 color)
{
    (void)color;
}

void gfx_putc(char c)
{
    (void)c;
}

void gfx_puts(const char *s)
{
    (void)s;
}

void gfx_eputs(const char *s)
{
    (void)s;
}

void gfx_wputs(const char *s)
{
    (void)s;
}

void gfx_printf(const char *fmt, ...)
{
    (void)fmt;
}
