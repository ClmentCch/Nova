/*
 * NOVA - custom boot system for Nintendo Switch.
 *
 * NOVA is a real Tegra X1 payload: it runs before Horizon OS, draws its own
 * interface on the Tegra display controller, reads the SD card through the
 * SDMMC controller and hands the selected boot target to hekate (the
 * supported HOS bootloader) through hekate's documented boot_cfg storage.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_UI_PREVIEW
#include <bdk.h>
#else
#include <stdlib.h>
#endif

#ifndef BUILD_TAG
#define BUILD_TAG "preview"
#endif

#include "nova_core.h"
#include "platform/platform.h"
#include "ui/ui.h"

/* ------------------------------------------------------------------ */
/* Theme                                                               */
/* ------------------------------------------------------------------ */
#define COL_BG_TOP   0xFF04060C
#define COL_BG_BOT   0xFF0B0714
#define COL_CARD     0xFF0A0D18
#define COL_CARD_2   0xFF11162A
#define COL_EDGE     0xFF1B2340
#define COL_TEXT     0xFFF4F6FF
#define COL_DIM      0xFF8B93B4
#define COL_FAINT    0xFF47506F
#define COL_BLUE     0xFF2F8BFF
#define COL_CYAN     0xFF33E1FF
#define COL_PURPLE   0xFF8B5CFF
#define COL_PINK     0xFFC86BFF
#define COL_GREEN    0xFF37E8A8
#define COL_AMBER    0xFFFFC15A
#define COL_RED      0xFFFF5E7A

#define SCREEN_W 720
#define SCREEN_H 1280

#define MENU_X      64
#define MENU_W      592
#define MENU_Y      616
#define ITEM_H      128
#define ITEM_GAP    20
#define MENU_AREA_Y 560
#define MENU_AREA_H 500

typedef enum
{
    TARGET_STOCK = 0,
    TARGET_HEKATE,
    TARGET_EMUMMC,
    TARGET_COUNT
} nova_target_t;

typedef struct
{
    nova_target_t target;
    const char *title;
    const char *subtitle;
    const char *badge;
    bool available;
} nova_item_t;

static nova_status_t g_status;
static uint32_t *g_fb;
static uint32_t *g_bg;
static nova_item_t g_items[TARGET_COUNT];
static int g_item_count;
static int g_selected;

static char g_toast[64];
static uint32_t g_toast_until;
static nova_autorcm_t g_autorcm;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static uint32_t pulse(uint32_t now, uint32_t period)
{
    if (period == 0)
        return 128;
    uint32_t phase = now % period;
    uint32_t half = period / 2;
    if (phase < half)
        return (phase * 255) / (half ? half : 1);
    return ((period - phase) * 255) / (half ? half : 1);
}

static uint32_t state_color(nova_state_t state)
{
    switch (state)
    {
    case NOVA_ST_OK:
        return COL_GREEN;
    case NOVA_ST_WARN:
        return COL_AMBER;
    case NOVA_ST_ERROR:
        return COL_RED;
    case NOVA_ST_MISSING:
    default:
        return COL_FAINT;
    }
}

static void draw_hex_mark(int cx, int cy, int radius, uint32_t color, uint32_t alpha)
{
    /* A double ring mark with a filled core: NOVA's logo glyph. */
    nova_ring(cx, cy, radius, 3, color, alpha);
    nova_ring(cx, cy, radius - 9, 1, color, alpha / 2);
    nova_dot(cx, cy, 4, color);
}

/* Fills the whole canvas with the base vertical gradient plus ambience. */
static void draw_backdrop(void)
{
    nova_vgrad(0, 0, SCREEN_W, SCREEN_H, COL_BG_TOP, COL_BG_BOT);

    /* Soft glow behind the title: layered translucent discs. */
    for (int r = 260; r > 40; r -= 22)
        nova_ring(SCREEN_W / 2, 150, r, 24, COL_BLUE, 5);
    for (int r = 190; r > 30; r -= 26)
        nova_ring(SCREEN_W / 2 + 40, 190, r, 22, COL_PURPLE, 4);

    /* Deterministic star field. */
    uint32_t seed = 0x4E4F5641u;
    for (int i = 0; i < 110; i++)
    {
        seed = seed * 1103515245u + 12345u;
        int x = (int)((seed >> 8) % SCREEN_W);
        seed = seed * 1103515245u + 12345u;
        int y = (int)((seed >> 8) % SCREEN_H);
        seed = seed * 1103515245u + 12345u;
        int bright = 20 + (int)((seed >> 16) % 55);
        nova_dot(x, y, (i % 9 == 0) ? 2 : 1, nova_rgb(bright, bright + 4, bright + 18));
    }

    /* Faint vertical rhythm lines. */
    for (int x = 0; x < SCREEN_W; x += 90)
        nova_fill_blend(x, 0, 1, SCREEN_H, COL_EDGE, 40);

    /* Top and bottom vignette bars. */
    nova_vgrad(0, 0, SCREEN_W, 140, 0xFF000000, COL_BG_TOP);
    nova_vgrad(0, SCREEN_H - 200, SCREEN_W, 200, COL_BG_BOT, 0xFF000000);
}

static void status_row(int y, const char *label, const char *value,
                       nova_state_t state, uint32_t value_color)
{
    nova_dot(MENU_X + 34, y + 9, 5, state_color(state));

    nova_text(MENU_X + 54, y, label, 2, COL_DIM);
    nova_text_right(MENU_X + MENU_W - 32, y, value, 2, value_color);

    nova_fill_blend(MENU_X + 24, y + 30, MENU_W - 48, 1, COL_EDGE, 130);
}

static const char *emummc_value(void)
{
    switch (g_status.emummc)
    {
    case NOVA_ST_OK:
        return g_status.emu_mode == NOVA_EMU_MODE_RAW ? "RAW" : "FILE";
    case NOVA_ST_WARN:
        return "PARTIAL";
    case NOVA_ST_ERROR:
        return "INVALID";
    case NOVA_ST_MISSING:
    default:
        return "DISABLED";
    }
}

static void draw_status_card(void)
{
    int y = 262;
    int h = 300;

    nova_round(MENU_X, y, MENU_W, h, 26, COL_CARD);
    nova_round_ring(MENU_X, y, MENU_W, h, 26, 1, COL_EDGE, 255);

    /* Header strip. */
    nova_text(MENU_X + 32, y + 22, "SYSTEM STATUS", 2, COL_FAINT);
    nova_text_right(MENU_X + MENU_W - 32, y + 22, "NOVA " NOVA_VERSION_STRING, 2, COL_FAINT);
    nova_fill_blend(MENU_X + 24, y + 50, MENU_W - 48, 1, COL_EDGE, 200);

    int row_y = y + 58;
    status_row(row_y, "SD CARD", g_status.sd_mounted ? "READY" : "NOT FOUND",
               g_status.sd_mounted ? NOVA_ST_OK : NOVA_ST_ERROR,
               g_status.sd_mounted ? COL_TEXT : COL_RED);
    row_y += 42;
    status_row(row_y, "HEKATE", g_status.hekate_version,
               g_status.hekate, g_status.hekate == NOVA_ST_OK ? COL_TEXT : COL_RED);
    row_y += 42;
    status_row(row_y, "ATMOSPHERE", nova_state_label(g_status.atmosphere),
               g_status.atmosphere, state_color(g_status.atmosphere) == COL_FAINT ? COL_DIM : COL_TEXT);
    row_y += 42;
    status_row(row_y, "emuMMC", emummc_value(), g_status.emummc,
               g_status.emummc == NOVA_ST_OK ? COL_TEXT : COL_DIM);
    row_y += 42;
    status_row(row_y, "BOOT ENTRIES",
               g_status.bootcfg == NOVA_ST_OK ? "READY" : "PENDING",
               g_status.bootcfg, g_status.bootcfg == NOVA_ST_OK ? COL_TEXT : COL_AMBER);

    /* Detail line under the card, in the panel's remaining space. */
    const char *detail = g_status.emu_detail;
    if (!g_status.sd_mounted)
        detail = "insert a microSD card with Atmosph\303\250re";
    else if (g_status.hekate != NOVA_ST_OK)
        detail = "hekate payload not found - run Nova Installer";
    else if (g_status.atmosphere != NOVA_ST_OK)
        detail = "Atmosph\303\250re not installed";
    else if (g_status.emummc == NOVA_ST_MISSING)
        detail = "emuMMC disabled - stock boot available";

    nova_text(MENU_X + 32, y + h - 30, detail, 1, COL_FAINT);
}

static void draw_title(uint32_t now, uint32_t intro)
{
    uint32_t glow = pulse(now, 2400);

    nova_glow(SCREEN_W / 2, 92, 96, COL_BLUE, 26);
    nova_glow(SCREEN_W / 2 + 30, 130, 130, COL_PURPLE, 18);

    draw_hex_mark(SCREEN_W / 2, 92, 26, nova_mix(COL_CYAN, COL_PURPLE, glow), 255);

    nova_text_grad(SCREEN_W / 2 - nova_text_width("NOVA", 9, 0) / 2, 140,
                   "NOVA", 9, COL_CYAN, COL_PINK);

    nova_text_center_tracked(SCREEN_W / 2, 224, "CUSTOM BOOT SYSTEM", 2, 5,
                             nova_dim(COL_DIM, 140 + intro / 3));

    /* Animated accent line. */
    int line_y = 254;
    int line_w = 220;
    nova_fill(SCREEN_W / 2 - line_w / 2, line_y, line_w, 2, COL_CARD_2);
    int head = (int)((now / 6) % (line_w + 60)) - 30;
    for (int i = 0; i < 60; i++)
    {
        int x = head + i;
        if (x < 0 || x >= line_w)
            continue;
        int a = 255 - (i * 255 / 60);
        if (a < 0)
            a = 0;
        nova_fill_blend(SCREEN_W / 2 - line_w / 2 + x, line_y, 1, 2, COL_CYAN, (uint32_t)(a / 3));
    }
    nova_hline(SCREEN_W / 2 - line_w / 2, line_y, line_w, 0xFF000000 | (COL_BLUE & 0x00FFFFFF));
}

/* ------------------------------------------------------------------ */
/* Menu items                                                          */
/* ------------------------------------------------------------------ */
static void build_items(void)
{
    g_item_count = 0;

    g_items[g_item_count].target = TARGET_STOCK;
    g_items[g_item_count].title = "LAUNCH STOCK";
    g_items[g_item_count].subtitle = "SysNAND - original firmware";
    g_items[g_item_count].badge = "S";
    g_items[g_item_count].available = g_status.can_stock;
    g_item_count++;

    g_items[g_item_count].target = TARGET_HEKATE;
    g_items[g_item_count].title = "LAUNCH HEKATE";
    g_items[g_item_count].subtitle = "hekate boot menu";
    g_items[g_item_count].badge = "H";
    g_items[g_item_count].available = g_status.can_hekate;
    g_item_count++;

    /* Atmosphère emuMMC is only offered when emuMMC is fully functional. */
    if (g_status.can_emummc)
    {
        g_items[g_item_count].target = TARGET_EMUMMC;
        g_items[g_item_count].title = "LAUNCH ATMOSPHERE";
        g_items[g_item_count].subtitle = "emuMMC - virtual system";
        g_items[g_item_count].badge = "A";
        g_items[g_item_count].available = true;
        g_item_count++;
    }

    if (g_selected >= g_item_count)
        g_selected = g_item_count - 1;
    if (g_selected < 0)
        g_selected = 0;
}

static int item_y(int index)
{
    return MENU_Y + index * (ITEM_H + ITEM_GAP);
}

static void draw_item(const nova_item_t *item, int y, bool selected,
                      int anim_y, uint32_t now)
{
    uint32_t accent_a = selected ? nova_mix(COL_BLUE, COL_PURPLE, pulse(now, 2000)) : COL_EDGE;
    uint32_t card = selected ? COL_CARD_2 : COL_CARD;
    uint32_t title_col = item->available ? (selected ? COL_TEXT : COL_DIM) : COL_FAINT;
    uint32_t sub_col = item->available ? COL_FAINT : COL_RED;

    /* Smoothly moving focus highlight between rows. */
    (void)anim_y; (void)now;

    if (selected)
    {
        /* Outer glow. */
        for (int g = 8; g >= 2; g -= 2)
            nova_round_ring(MENU_X - g, y - g, MENU_W + g * 2, ITEM_H + g * 2,
                            28 + g, 1, accent_a, 22);
    }

    nova_round(MENU_X, y, MENU_W, ITEM_H, 24, card);
    nova_round_ring(MENU_X, y, MENU_W, ITEM_H, 24, selected ? 2 : 1,
                    selected ? accent_a : COL_EDGE, 255);

    /* Inner top highlight for a glassy feel. */
    nova_fill_blend(MENU_X + 26, y + 1, MENU_W - 52, 1, 0xFFFFFFFF, selected ? 30 : 12);

    /* Accent bar on the left when focused. */
    if (selected)
    {
        nova_round(MENU_X + 14, y + 26, 5, ITEM_H - 52, 3, accent_a);
        nova_round_blend(MENU_X + 10, y + 44, 13, ITEM_H - 88, 6, accent_a, 40);
    }

    /* Icon plate. */
    int ix = MENU_X + 44;
    int iy = y + (ITEM_H - 72) / 2;
    nova_round(ix, iy, 72, 72, 18, selected ? COL_BG_BOT : 0xFF080B14);
    nova_round_ring(ix, iy, 72, 72, 18, 1, selected ? accent_a : COL_EDGE, selected ? 200 : 120);
    nova_text(ix + 36 - nova_text_width(item->badge, 4, 0) / 2, iy + 22,
              item->badge, 4, item->available ? accent_a : COL_FAINT);

    /* Labels. */
    nova_text(MENU_X + 142, y + 34, item->title, 3, title_col);
    nova_text(MENU_X + 142, y + 78, item->subtitle, 2, sub_col);

    if (!item->available)
        nova_text_right(MENU_X + MENU_W - 34, y + 52, "! UNAVAILABLE", 2, COL_RED);
    else if (selected)
    {
        int cx = MENU_X + MENU_W - 46;
        int cy = y + ITEM_H / 2;
        nova_line(cx - 5, cy - 9, cx + 4, cy, accent_a);
        nova_line(cx + 4, cy, cx - 5, cy + 9, accent_a);
        nova_line(cx - 4, cy - 9, cx + 5, cy, accent_a);
        nova_line(cx + 5, cy, cx - 4, cy + 9, accent_a);
    }
}

static void draw_menu(uint32_t now, int anim_y, uint32_t intro)
{
    (void)intro;
    nova_blit(g_fb, g_bg, 0, MENU_AREA_Y, SCREEN_W, MENU_AREA_H);

    nova_text(MENU_X, MENU_Y - 44, "BOOT TARGETS", 2, COL_FAINT);
    nova_text_right(MENU_X + MENU_W, MENU_Y - 44, "(A) SELECT  (B) HEKATE MENU", 1, COL_FAINT);

    for (int i = 0; i < g_item_count; i++)
    {
        bool selected = (i == g_selected);
        draw_item(&g_items[i], item_y(i), selected, anim_y, now);
    }

    /* Focus rail: a vertical track with a sliding knob. */
    int rail_x = MENU_X - 22;
    nova_vline(rail_x, MENU_Y, item_y(g_item_count - 1) + ITEM_H - MENU_Y, COL_EDGE);
    nova_round(rail_x - 2, anim_y + 28, 5, 72, 2,
               nova_mix(COL_BLUE, COL_PURPLE, pulse(now, 2000)));
}

static void draw_footer(uint32_t now)
{
    nova_blit(g_fb, g_bg, 0, 1080, SCREEN_W, SCREEN_H - 1080);

    int y = 1140;
    nova_fill_blend(MENU_X, y - 24, MENU_W, 1, COL_EDGE, 120);

    if (g_status.sd_mounted)
    {
        /* Controller hints with button chips. */
        int x = MENU_X + 8;
        struct hint { const char *key; const char *label; };
        struct hint hints[3] = {
            {"A", "SELECT"},
            {"B", g_status.can_hekate ? "HEKATE" : "N/A"},
            {"D-PAD", "MOVE"},
        };
        for (int i = 0; i < 3; i++)
        {
            int chip_w = nova_text_width(hints[i].key, 2, 0) + 22;
            nova_round(x, y, chip_w, 34, 8, COL_CARD_2);
            nova_round_ring(x, y, chip_w, 34, 8, 1, COL_EDGE, 255);
            nova_text(x + 11, y + 9, hints[i].key, 2, COL_TEXT);
            nova_text(x + chip_w + 10, y + 9, hints[i].label, 2, COL_FAINT);
            x += chip_w + 20 + nova_text_width(hints[i].label, 2, 0) + 28;
        }
    }
    else
    {
        nova_text_center(SCREEN_W / 2, y + 4, "INSERT A microSD CARD WITH NOVA INSTALLED", 2, COL_RED);
    }

    /* Bottom identity line. */
    nova_text(MENU_X, 1216, "NOVA " NOVA_VERSION_STRING "  \x95  " BUILD_TAG, 1, COL_FAINT);
    nova_text_right(MENU_X + MENU_W, 1216, "payload 0x40010000  \x95  hekate bridge", 1, COL_FAINT);

    /* Subtle pulse in the bottom accent. */
    uint32_t p = pulse(now, 3000);
    nova_fill_blend(MENU_X, 1240, MENU_W, 2, nova_mix(COL_BLUE, COL_PURPLE, p), 90);
}

static void toast(const char *text)
{
    nova_cstr_copy(g_toast, sizeof(g_toast), text);
    g_toast_until = nova_platform_ms() + 2200;
}

static void draw_toast(uint32_t now)
{
    if (!g_toast[0] || now > g_toast_until)
        return;

    uint32_t remain = g_toast_until - now;
    uint32_t alpha = remain < 300 ? remain * 255 / 300 : 255;

    int w = nova_text_width(g_toast, 2, 0) + 72;
    int x = (SCREEN_W - w) / 2;
    int y = 1040;

    nova_round_blend(x, y, w, 52, 14, COL_CARD_2, alpha);
    nova_round_ring(x, y, w, 52, 14, 1, COL_AMBER, alpha);
    nova_text(x + 36, y + 17, g_toast, 2, nova_dim(COL_TEXT, alpha));
}

/* ------------------------------------------------------------------ */
/* Full screen states                                                  */
/* ------------------------------------------------------------------ */
static void draw_boot_overlay(const char *title, const char *detail, uint32_t now)
{
    nova_fill(0, 0, SCREEN_W, SCREEN_H, 0xFF03040A);

    nova_glow(SCREEN_W / 2, 560, 320, COL_BLUE, 22);
    nova_glow(SCREEN_W / 2, 700, 220, COL_PURPLE, 14);

    draw_hex_mark(SCREEN_W / 2, 500, 32, nova_mix(COL_CYAN, COL_PINK, pulse(now, 1200)), 255);

    nova_text_center(SCREEN_W / 2, 570, title, 4, COL_TEXT);
    nova_text_center(SCREEN_W / 2, 640, detail, 2, COL_DIM);

    /* Spinner: eight fading dots. */
    uint32_t phase = (now / 90) % 8;
    for (int i = 0; i < 8; i++)
    {
        int a = i == (int)phase ? 255 : 40 + (i * 12);
        if (a > 255)
            a = 255;
        int ang = i * 45;
        int dx = (ang % 360 == 0) ? 34 : (ang == 90 || ang == 270) ? 0 : 24;
        int dy = (ang == 0 || ang == 180) ? 0 : 24;
        if (ang == 45 || ang == 135 || ang == 225 || ang == 315)
        {
            dx = ang == 45 || ang == 135 ? 24 : -24;
            dy = ang == 45 || ang == 315 ? -24 : 24;
        }
        else if (ang == 90)
            dy = 34;
        else if (ang == 270)
            dy = -34;
        else if (ang == 180)
            dx = -34;
        else if (ang == 0)
            dx = 34;
        nova_dot(SCREEN_W / 2 + dx, 740 + dy, 6, nova_dim(COL_CYAN, (uint32_t)a));
    }

    nova_text_center(SCREEN_W / 2, 900, "DO NOT POWER OFF", 2, COL_FAINT);
}

static void draw_autorcm_overlay(const nova_autorcm_t *autorcm, uint32_t now)
{
    nova_fill(0, 0, SCREEN_W, SCREEN_H, 0xFF03040A);
    nova_glow(SCREEN_W / 2, 520, 300, COL_AMBER, 20);

    nova_text_center(SCREEN_W / 2, 400, "! AUTORCM DETECTED !", 3, COL_AMBER);
    nova_text_center(SCREEN_W / 2, 500, nova_autorcm_state_label(autorcm->state), 2, COL_TEXT);

    nova_text_center(SCREEN_W / 2, 570, "RCM FALLBACK", 2, COL_DIM);
    nova_text_center(SCREEN_W / 2, 610, nova_autorcm_fallback_label(autorcm->fallback), 3,
                     autorcm->fallback == NOVA_AUTORCM_FALLBACK_FAILED ? COL_RED : COL_GREEN);

    const char *line1 = "payload.bin is armed with NOVA";
    const char *line2 = "Boot again with your RCM loader or dongle";
    if (autorcm->fallback == NOVA_AUTORCM_FALLBACK_READY)
        line1 = "payload.bin already contains NOVA";
    else if (autorcm->fallback == NOVA_AUTORCM_FALLBACK_FAILED)
    {
        line1 = "NOVA could not arm payload.bin automatically";
        line2 = "Run Nova Installer or fix the SD card";
    }

    nova_text_center(SCREEN_W / 2, 720, line1, 2, COL_TEXT);
    nova_text_center(SCREEN_W / 2, 760, line2, 2, COL_DIM);
    nova_text_center(SCREEN_W / 2, 860, "BOOT0 is never modified by NOVA", 2, COL_FAINT);

    nova_text_center(SCREEN_W / 2, 960, "Continuing to the NOVA menu", 2, COL_FAINT);

    for (int i = 0; i < 8; i++)
    {
        int a = i == (int)((now / 120) % 8) ? 255 : 50 + i * 10;
        nova_dot(SCREEN_W / 2 - 84 + i * 24, 1020, 7, nova_dim(COL_CYAN, (uint32_t)a));
    }
}

static void draw_error_screen(const char *title, const char *line1, const char *line2)
{
    nova_fill(0, 0, SCREEN_W, SCREEN_H, 0xFF03040A);
    nova_glow(SCREEN_W / 2, 470, 300, COL_RED, 18);

    nova_text_center(SCREEN_W / 2, 430, "!", 10, COL_RED);
    nova_text_center(SCREEN_W / 2, 540, title, 3, COL_TEXT);
    nova_text_center(SCREEN_W / 2, 610, line1, 2, COL_DIM);
    nova_text_center(SCREEN_W / 2, 650, line2, 2, COL_DIM);
    nova_text_center(SCREEN_W / 2, 760, "(B) BACK", 2, COL_FAINT);
}

/* ------------------------------------------------------------------ */
/* Boot actions                                                        */
/* ------------------------------------------------------------------ */
static bool ensure_boot_entries(void)
{
    if (g_status.bootcfg == NOVA_ST_OK)
        return true;

    char ini[1024];
    uint32_t len = nova_build_hekate_ini(ini, sizeof(ini));
    if (!len)
        return false;

    if (!nova_platform_write_text(NOVA_HEKATE_INI, ini, len))
        return false;

    g_status.bootcfg = NOVA_ST_OK;
    nova_cstr_copy(g_status.cfg_detail, sizeof(g_status.cfg_detail), "entries created");
    return true;
}

static const char *target_title(const nova_item_t *item)
{
    switch (item->target)
    {
    case TARGET_STOCK:
        return "STARTING ORIGINAL FIRMWARE";
    case TARGET_EMUMMC:
        return "STARTING ATMOSPHERE (emuMMC)";
    case TARGET_HEKATE:
    default:
        return "STARTING HEKATE";
    }
}

static void boot_target(const nova_item_t *item)
{
    const nova_fs_t *fs = nova_platform_fs();
    if (!fs || !g_status.can_hekate || !g_status.hekate_path[0])
    {
        toast("HEKATE PAYLOAD NOT FOUND");
        return;
    }

    if (!item->available)
    {
        if (item->target == TARGET_STOCK)
            toast(g_status.stock_detail);
        else
            toast("TARGET NOT AVAILABLE");
        return;
    }

    uint32_t size = 0;
    void *image = fs->read_file(fs->ctx, g_status.hekate_path, &size);
    if (!image)
    {
        toast("CANNOT READ HEKATE PAYLOAD");
        return;
    }

    const char *id = NULL;
    if (item->target == TARGET_STOCK)
        id = NOVA_ID_STOCK;
    else if (item->target == TARGET_EMUMMC)
        id = NOVA_ID_EMUMMC;

    if (!nova_hekate_set_boot_id((uint8_t *)image, size, id))
    {
        fs->free_file(fs->ctx, image);
        toast("INVALID HEKATE PAYLOAD");
        return;
    }

    bool entries_ok = true;
    if (id)
        entries_ok = ensure_boot_entries();

    uint32_t start = nova_platform_ms();
    for (;;)
    {
        uint32_t now = nova_platform_ms();
        draw_boot_overlay(target_title(item),
                          entries_ok ? "handing over to hekate" : "hekate menu (entries pending)",
                          now);
        if (now - start > 900)
            break;
        nova_platform_delay(16);
    }

    nova_platform_backlight(100);
    nova_platform_chainload(image, size);
}

static void handle_confirm(void)
{
    if (g_item_count <= 0)
        return;
    boot_target(&g_items[g_selected]);
}

static void handle_back(void)
{
    if (!g_status.sd_mounted)
    {
        nova_platform_power_off();
        return;
    }

    if (!g_status.can_hekate)
    {
        toast("HEKATE PAYLOAD NOT FOUND");
        return;
    }

    /* "Back" leaves NOVA and opens hekate's own menu. */
    nova_item_t hekate_item;
    nova_memset(&hekate_item, 0, sizeof(hekate_item));
    hekate_item.target = TARGET_HEKATE;
    hekate_item.available = true;
    boot_target(&hekate_item);
}

#ifdef NOVA_UI_PREVIEW
/*
 * Host preview entry. Composes one real menu frame with the shipping drawing
 * code so the interface can be rendered and reviewed without hardware.
 */
void nova_preview_compose(const nova_status_t *status, int selected, uint32_t now,
                          uint32_t *fb, uint32_t *bg)
{
    g_status = *status;
    g_fb = fb;
    g_bg = bg;

    build_items();
    g_selected = selected;

    nova_ui_bind(g_bg, SCREEN_W, SCREEN_H, SCREEN_W);
    draw_backdrop();
    draw_title(now, 255);
    draw_status_card();

    nova_ui_bind(g_fb, SCREEN_W, SCREEN_H, SCREEN_W);
    nova_memcpy(g_fb, g_bg, (size_t)(SCREEN_W * SCREEN_H * 4));
    draw_menu(now, item_y(selected), 255);
    draw_footer(now);

    nova_cstr_copy(g_toast, sizeof(g_toast),
                   g_status.can_emummc ? "" : "emuMMC not detected - option hidden");
    g_toast_until = now + 1000;
    draw_toast(now);
}
#endif

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
static void build_background(uint32_t now)
{
    nova_ui_bind(g_bg, SCREEN_W, SCREEN_H, SCREEN_W);
    draw_backdrop();
    draw_title(now, 255);
    draw_status_card();
    nova_text(MENU_X, MENU_Y - 44, "BOOT TARGETS", 2, COL_FAINT);
    nova_ui_bind(g_fb, SCREEN_W, SCREEN_H, SCREEN_W);
}

void ipl_main(void)
{
    nova_platform_init();

    g_fb = nova_platform_fb();
    if (!g_fb)
        nova_platform_halt();

    nova_ui_bind(g_fb, SCREEN_W, SCREEN_H, SCREEN_W);
    nova_clear(0xFF03040A);

    g_bg = (uint32_t *)malloc((uint32_t)(SCREEN_W * SCREEN_H * 4));
    if (!g_bg)
        nova_platform_halt();

    /* Splash while the SD card is inspected. */
    draw_boot_overlay("NOVA", "scanning SD card", nova_platform_ms());
    nova_platform_delay(120);

    nova_detect(nova_platform_fs(), &g_status);

    if (!g_status.sd_mounted)
    {
        /* Nothing can boot without the SD card: report it and offer power off. */
        bool prev = false;
        for (;;)
        {
            draw_error_screen("SD CARD NOT FOUND",
                              "insert a microSD card prepared with",
                              "Nova Installer, then power the console on again");
            nova_input_t in;
            nova_platform_input(&in);
            if (in.back && !prev)
                nova_platform_power_off();
            prev = in.back;
            nova_platform_delay(32);
        }
    }

    /*
     * Record this start before doing anything else with the SD card. The
     * installer only reports a configured cold boot as working after this
     * counter advances, so a failed write can never look like success.
     */
    nova_platform_note_boot();

    /*
     * If AutoRCM was enabled, arm the RCM payload slot before showing the
     * menu. NOVA only reads BOOT0; it never writes it.
     */
    nova_platform_autorcm_prepare(&g_autorcm);
    if (g_autorcm.state == NOVA_AUTORCM_ENABLED)
    {
        uint32_t start = nova_platform_ms();
        uint32_t hold = g_autorcm.fallback == NOVA_AUTORCM_FALLBACK_FAILED ? 2600 : 1800;
        for (;;)
        {
            uint32_t now = nova_platform_ms();
            draw_autorcm_overlay(&g_autorcm, now);
            if (now - start > hold)
                break;
            nova_platform_delay(16);
        }
    }

    /* Self provision NOVA's own hekate entries (never touches user files). */
    if (g_status.sd_mounted && g_status.hekate == NOVA_ST_OK)
    {
        if (ensure_boot_entries())
            nova_detect(nova_platform_fs(), &g_status);
    }

    build_items();
    g_selected = 0;

    uint32_t start = nova_platform_ms();
    uint32_t last = start;
    int anim_y = item_y(0);
    bool prev_up = false, prev_down = false, prev_confirm = false, prev_back = false;
    uint32_t next_repeat = 0;

    build_background(start);
    nova_blit(g_fb, g_bg, 0, 0, SCREEN_W, SCREEN_H);

    for (;;)
    {
        uint32_t now = nova_platform_ms();
        uint32_t dt = now - last;
        if (dt > 250)
            dt = 250;
        last = now;

        nova_input_t in;
        nova_platform_input(&in);

        bool up = in.up;
        bool down = in.down;

        if ((up && !prev_up) || (down && !prev_down))
        {
            if (up && g_selected > 0)
                g_selected--;
            if (down && g_selected < g_item_count - 1)
                g_selected++;
            next_repeat = now + 280;
        }
        else if ((up || down) && now >= next_repeat)
        {
            if (up && g_selected > 0)
                g_selected--;
            if (down && g_selected < g_item_count - 1)
                g_selected++;
            next_repeat = now + 120;
        }
        prev_up = up;
        prev_down = down;

        if (in.confirm && !prev_confirm)
            handle_confirm();
        prev_confirm = in.confirm;

        if (in.back && !prev_back)
            handle_back();
        prev_back = in.back;

        /* Animate the focus position toward the selected row. */
        int target_y = item_y(g_selected);
        if (anim_y < target_y)
        {
            int step = (target_y - anim_y) / 4 + 2;
            anim_y += step;
            if (anim_y > target_y)
                anim_y = target_y;
        }
        else if (anim_y > target_y)
        {
            int step = (anim_y - target_y) / 4 + 2;
            anim_y -= step;
            if (anim_y < target_y)
                anim_y = target_y;
        }

        draw_menu(now, anim_y, 255);
        draw_footer(now);
        draw_toast(now);

        if (now - start < 420)
        {
            /* Intro: briefly dim the whole frame by drawing a fading veil. */
            uint32_t a = 190 - ((now - start) * 190 / 420);
            nova_fill_blend(0, 0, SCREEN_W, SCREEN_H, 0xFF03040A, a);
        }

        nova_platform_delay(16);
    }
}
