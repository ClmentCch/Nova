/*
 * NOVA Boot - minimal compatibility layer for hekate's gfx.h.
 *
 * The hekate BDK storage drivers report errors through the bootloader's gfx
 * console (EPRINTF / gfx_con.mute). NOVA draws its own UI and reports errors
 * itself, so it provides the required symbols as a quiet console instead of
 * linking hekate's full graphical stack.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_GFX_COMPAT_H
#define NOVA_GFX_COMPAT_H

#include <utils/types.h>

#define TXT_CLR_DEFAULT   0xFFCCCCCC
#define TXT_CLR_ERROR     0xFFFF0000
#define TXT_CLR_WARNING   0xFFFFBA00

#define EPRINTF(text) gfx_eputs(text)
#define EPRINTFARGS(text, args...) gfx_printf("%k" text "%k\n", TXT_CLR_ERROR, args, TXT_CLR_DEFAULT)
#define WPRINTF(text) gfx_wputs(text)
#define WPRINTFARGS(text, args...) gfx_printf("%k" text "%k\n", TXT_CLR_WARNING, args, TXT_CLR_DEFAULT)

typedef struct _gfx_ctxt_t
{
    u32 *fb;
    u32 width;
    u32 height;
    u32 stride;
} gfx_ctxt_t;

typedef struct _gfx_con_t
{
    gfx_ctxt_t *gfx_ctxt;
    u32 fntsz;
    u32 x;
    u32 y;
    u32 savedx;
    u32 savedy;
    u32 fgcol;
    int fillbg;
    u32 bgcol;
    bool mute;
} gfx_con_t;

extern gfx_ctxt_t gfx_ctxt;
extern gfx_con_t gfx_con;

void gfx_init_ctxt(u32 *fb, u32 width, u32 height, u32 stride);
void gfx_con_init(void);
void gfx_con_setpos(u32 x, u32 y);
void gfx_clear_grey(u8 color);
void gfx_clear_color(u32 color);
void gfx_putc(char c);
void gfx_puts(const char *s);
void gfx_eputs(const char *s);
void gfx_wputs(const char *s);
void gfx_printf(const char *fmt, ...);

#endif
