/*
 * NOVA Boot - Tegra X1 platform layer.
 *
 * This file is the only place that talks to hekate's BDK. Everything above
 * it (detection, UI, boot policy) is portable and host tested.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <bdk.h>
#include <libs/fatfs/ff.h>

#include "platform.h"

extern const uint8_t nova_trampoline_begin[];
extern const uint8_t nova_trampoline_end[];

#define NOVA_TARGET_ADDR 0x40010000u

static uint32_t *g_fb;
static bool g_sd_ready;
static nova_fs_t g_fs;

/* ------------------------------------------------------------------ */
/* FatFs backed nova_fs_t                                              */
/* ------------------------------------------------------------------ */
struct nova_dir_iter {
    DIR dir;
    FILINFO info;
    char pattern[32];
};

static int fs_exists(void *ctx, const char *path)
{
    (void)ctx;
    FILINFO info;
    return f_stat(path, &info) == FR_OK;
}

static int fs_file_size(void *ctx, const char *path, uint32_t *size)
{
    (void)ctx;
    FILINFO info;
    if (f_stat(path, &info) != FR_OK || (info.fattrib & AM_DIR))
        return -1;
    *size = (uint32_t)info.fsize;
    return 0;
}

static void *fs_read_file(void *ctx, const char *path, uint32_t *size)
{
    (void)ctx;
    /* devkitARM's u32 is unsigned int while stdint uint32_t is unsigned long. */
    u32 bdk_size = 0;
    void *buf = sd_file_read(path, &bdk_size);
    if (size)
        *size = (uint32_t)bdk_size;
    return buf;
}

static void fs_free_file(void *ctx, void *buf)
{
    (void)ctx;
    free(buf);
}

static int fs_list_first(void *ctx, const char *dir, const char *pattern,
                         char *name, uint32_t name_size, nova_dir_iter_t **it)
{
    (void)ctx;
    struct nova_dir_iter *iter = (struct nova_dir_iter *)calloc(1, sizeof(*iter));
    if (!iter)
        return -1;

    nova_cstr_copy(iter->pattern, sizeof(iter->pattern), pattern);

    const char *path = (dir && dir[0]) ? dir : "/";
    FRESULT rc = f_findfirst(&iter->dir, &iter->info, path, iter->pattern);
    if (rc != FR_OK)
    {
        free(iter);
        return -1;
    }

    *it = (nova_dir_iter_t *)iter;

    if (!iter->info.fname[0])
        return -1;

    nova_cstr_copy(name, name_size, iter->info.fname);
    return 0;
}

static int fs_list_next(void *ctx, nova_dir_iter_t *it, char *name, uint32_t name_size)
{
    (void)ctx;
    struct nova_dir_iter *iter = (struct nova_dir_iter *)it;
    if (!iter)
        return -1;

    FRESULT rc = f_findnext(&iter->dir, &iter->info);
    if (rc != FR_OK || !iter->info.fname[0])
        return -1;

    nova_cstr_copy(name, name_size, iter->info.fname);
    return 0;
}

static void fs_list_close(void *ctx, nova_dir_iter_t *it)
{
    (void)ctx;
    free(it);
}

/* ------------------------------------------------------------------ */
/* Platform bring up                                                   */
/* ------------------------------------------------------------------ */
void nova_platform_init(void)
{
    /* Initial hardware configuration (clocks, pinmux, PMIC, I2C, UART). */
    hw_init();

    /* heap_init must happen before any FatFs/sd_file_read allocation. */
    heap_init((void *)IPL_HEAP_START);

    display_init();

    /* Mount the SD card. sd_mount() returns 0 on success. */
    g_sd_ready = (sd_mount() == 0);

    g_fb = (uint32_t *)display_init_window_a_pitch();
    display_backlight_pwm_init();
    display_backlight_brightness(100, 0);

    /* Joy-Con / Sio input. Non fatal when nothing is attached. */
    jc_init_hw();

    nova_memset(&g_fs, 0, sizeof(g_fs));
    g_fs.exists = fs_exists;
    g_fs.file_size = fs_file_size;
    g_fs.read_file = fs_read_file;
    g_fs.free_file = fs_free_file;
    g_fs.list_first = fs_list_first;
    g_fs.list_next = fs_list_next;
    g_fs.list_close = fs_list_close;
}

uint32_t *nova_platform_fb(void)
{
    return g_fb;
}

bool nova_platform_sd_ready(void)
{
    return g_sd_ready;
}

const nova_fs_t *nova_platform_fs(void)
{
    return g_sd_ready ? &g_fs : NULL;
}

void nova_platform_backlight(uint32_t percent)
{
    if (percent > 100)
        percent = 100;
    display_backlight_brightness(percent, 0);
}

void nova_platform_delay(uint32_t ms)
{
    msleep(ms);
}

uint32_t nova_platform_ms(void)
{
    return get_tmr_ms();
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static int stick_direction(uint16_t raw)
{
    /* Joy-Con sticks report 12-bit values; Sio pads report 16-bit ones. */
    int center = (raw <= 4095) ? 2048 : 32768;
    int delta = (int)raw - center;
    int threshold = (raw <= 4095) ? 700 : 11000;

    if (delta < -threshold)
        return -1;
    if (delta > threshold)
        return 1;
    return 0;
}

void nova_platform_input(nova_input_t *out)
{
    if (!out)
        return;

    nova_memset(out, 0, sizeof(*out));

    jc_gamepad_rpt_t *rpt = joycon_poll();
    if (rpt)
    {
        out->up |= rpt->up;
        out->down |= rpt->down;
        out->left |= rpt->left;
        out->right |= rpt->right;
        out->confirm |= rpt->a;
        out->back |= rpt->b;
        out->menu |= rpt->plus;
        out->minus |= rpt->minus;
        out->connected_l = rpt->conn_l;
        out->connected_r = rpt->conn_r;

        if (rpt->conn_l)
        {
            int dir = stick_direction(rpt->lstick_y);
            if (dir == 0)
                dir = stick_direction(rpt->rstick_y);
            out->stick_y = dir;
        }
    }

    /* Volume buttons always work, even with detached Joy-Con. */
    uint8_t btn = btn_read();
    out->up |= (btn & BTN_VOL_UP) != 0;
    out->down |= (btn & BTN_VOL_DOWN) != 0;

    if (out->stick_y < 0)
        out->up = true;
    else if (out->stick_y > 0)
        out->down = true;
}

/* ------------------------------------------------------------------ */
/* SD writes (NOVA owned files only)                                   */
/* ------------------------------------------------------------------ */
bool nova_platform_write_text(const char *path, const char *text, uint32_t len)
{
    if (!g_sd_ready || !path || !text)
        return false;

    /* Make sure the parent folders exist. */
    f_mkdir("bootloader");
    f_mkdir("bootloader/ini");

    FIL fp;
    if (f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;

    UINT written = 0;
    bool ok = (f_write(&fp, text, len, &written) == FR_OK) && (written == len);
    if (ok)
        ok = f_sync(&fp) == FR_OK;
    f_close(&fp);

    return ok;
}

/* ------------------------------------------------------------------ */
/* Chainloading                                                        */
/* ------------------------------------------------------------------ */
void nova_platform_halt(void)
{
    for (;;)
        msleep(1000);
}

void nova_platform_power_off(void)
{
    power_set_state(POWER_OFF_RESET);
    nova_platform_halt();
}

void nova_platform_chainload(void *image, uint32_t size)
{
    if (!image || size < 4 || size > NOVA_MAX_PAYLOAD_SIZE)
        nova_platform_halt();

    uint32_t aligned = (size + 3u) & ~3u;

    /* The copy loop must survive overwriting our own IRAM image. */
    uint32_t tramp_len = (uint32_t)(nova_trampoline_end - nova_trampoline_begin);
    uint8_t *tramp = (uint8_t *)malloc(tramp_len);
    if (!tramp)
        nova_platform_halt();
    nova_memcpy(tramp, nova_trampoline_begin, tramp_len);

    /* Release SD and hardware the same way hekate does before a payload. */
    if (g_sd_ready)
        sd_end();
    g_sd_ready = false;
    hw_deinit(false);

    void (*copy_jump)(void *, void *, uint32_t, void *) =
        (void (*)(void *, void *, uint32_t, void *))tramp;
    copy_jump(image, (void *)NOVA_TARGET_ADDR, aligned, (void *)NOVA_TARGET_ADDR);

    nova_platform_halt();
}
