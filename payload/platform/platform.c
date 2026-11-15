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

#include "nova_sha256.h"
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
    f_mkdir(NOVA_ROOT_DIR);
    f_mkdir(NOVA_STATE_DIR);

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
/* Boot marker                                                         */
/* ------------------------------------------------------------------ */
static void marker_append(char *out, uint32_t size, uint32_t *pos, const char *text)
{
    while (*text && *pos + 1 < size)
        out[(*pos)++] = *text++;
    out[*pos] = '\0';
}

static void marker_append_u32(char *out, uint32_t size, uint32_t *pos, uint32_t value)
{
    char digits[12];
    uint32_t count = 0;

    if (!value)
        digits[count++] = '0';

    while (value && count < sizeof(digits))
    {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count && *pos + 1 < size)
        out[(*pos)++] = digits[--count];
    out[*pos] = '\0';
}

bool nova_platform_note_boot(void)
{
    if (!g_sd_ready)
        return false;

    f_mkdir(NOVA_ROOT_DIR);
    f_mkdir(NOVA_STATE_DIR);

    uint32_t boots = 0;
    char text[128];
    UINT read = 0;
    FIL fp;

    if (f_open(&fp, NOVA_BOOT_MARKER, FA_READ) == FR_OK)
    {
        if (f_read(&fp, text, sizeof(text) - 1, &read) == FR_OK)
        {
            text[read] = '\0';
            char value[16];
            if (nova_ini_get(text, read, "nova", "boots", value, sizeof(value)))
                boots = (uint32_t)nova_atoi(value);
        }
        f_close(&fp);
    }

    if (boots < 999999u)
        boots++;

    char out[160];
    uint32_t pos = 0;
    out[0] = '\0';
    marker_append(out, sizeof(out), &pos,
                  "[nova]\nversion=" NOVA_VERSION_STRING "\nboots=");
    marker_append_u32(out, sizeof(out), &pos, boots);
    marker_append(out, sizeof(out), &pos, "\n");

    return nova_platform_write_text(NOVA_BOOT_MARKER, out, pos);
}

/* ------------------------------------------------------------------ */
/* AutoRCM fallback                                                    */
/* ------------------------------------------------------------------ */
static u8 *read_whole_file(const char *path, uint32_t *size_out)
{
    if (!g_sd_ready || !path)
        return NULL;

    u32 bdk_size = 0;
    void *data = sd_file_read(path, &bdk_size);
    if (!data)
        return NULL;

    if (size_out)
        *size_out = (uint32_t)bdk_size;
    return (u8 *)data;
}

static bool write_whole_file(const char *path, const void *data, uint32_t size)
{
    if (!g_sd_ready || !path || (!data && size))
        return false;

    f_mkdir(NOVA_ROOT_DIR);
    f_mkdir("Nova/backup");
    f_mkdir(NOVA_AUTORCM_BACKUP_DIR);
    f_mkdir(NOVA_STATE_DIR);

    FIL fp;
    if (f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return false;

    UINT written = 0;
    bool ok = f_write(&fp, data, size, &written) == FR_OK && written == size;
    if (ok)
        ok = f_sync(&fp) == FR_OK;
    f_close(&fp);
    return ok;
}

static bool file_matches(const char *path, const u8 *data, uint32_t size)
{
    uint32_t got = 0;
    u8 *buffer = read_whole_file(path, &got);
    bool ok = buffer && got == size && !nova_memcmp(buffer, data, size);
    free(buffer);
    return ok;
}

static bool payload_looks_like_nova(const u8 *image, uint32_t size)
{
    if (!image || size < 0x400 || size > NOVA_AUTOBOOT_MAX_SIZE)
        return false;

    uint32_t first = (uint32_t)image[0] | ((uint32_t)image[1] << 8) |
                     ((uint32_t)image[2] << 16) | ((uint32_t)image[3] << 24);
    uint32_t cond = first >> 28;
    if (cond != 0xE && cond != 0xA)
        return false;

    uint32_t version_len = (uint32_t)nova_strlen(NOVA_VERSION_STRING);
    for (uint32_t i = 0; i + version_len <= size; i++)
    {
        if (!nova_memcmp(image + i, NOVA_VERSION_STRING, version_len))
            return true;
    }
    return false;
}

static nova_autorcm_state_t detect_autorcm(char *detail, size_t detail_size)
{
    if (hw_get_chip_id() == GP_HIDREV_MAJOR_T210B01)
    {
        s_printf(detail, "Mariko: AutoRCM is not supported");
        return NOVA_AUTORCM_UNSUPPORTED;
    }

    if (emmc_initialize(false) != 0)
    {
        s_printf(detail, "eMMC unavailable: AutoRCM state unknown");
        return NOVA_AUTORCM_UNKNOWN;
    }

    u8 *sector = (u8 *)malloc(0x200);
    if (!sector)
    {
        emmc_end();
        s_printf(detail, "out of memory reading BOOT0");
        return NOVA_AUTORCM_UNKNOWN;
    }

    int rc = emmc_set_partition(EMMC_BOOT0);
    if (!rc)
        rc = sdmmc_storage_read(&emmc_storage, 0x200 / EMMC_BLOCKSIZE, 1, sector);
    emmc_end();

    if (rc)
    {
        free(sector);
        s_printf(detail, "cannot read BOOT0: AutoRCM unknown");
        return NOVA_AUTORCM_UNKNOWN;
    }

    u8 mod0, mod1;
    nx_emmc_get_autorcm_masks(&mod0, &mod1);
    bool enabled = sector[0x11] == mod1 && sector[0x10] != mod0;
    free(sector);

    if (!enabled)
    {
        s_printf(detail, "AutoRCM disabled");
        return NOVA_AUTORCM_DISABLED;
    }

    if (fuse_check_patched_rcm())
        s_printf(detail, "AutoRCM enabled on an RCM-patched console");
    else
        s_printf(detail, "AutoRCM enabled: arming the RCM payload slot");
    return NOVA_AUTORCM_ENABLED;
}

static uint32_t current_boot_count(void)
{
    uint32_t size = 0;
    uint32_t boots = 0;
    char *text = (char *)read_whole_file(NOVA_BOOT_MARKER, &size);
    if (!text)
        return 0;

    char value[16];
    if (nova_ini_get(text, size, "nova", "boots", value, sizeof(value)))
        boots = (uint32_t)nova_atoi(value);
    free(text);
    return boots;
}

const char *nova_autorcm_state_label(nova_autorcm_state_t state)
{
    switch (state)
    {
    case NOVA_AUTORCM_DISABLED:
        return "DISABLED";
    case NOVA_AUTORCM_ENABLED:
        return "ENABLED";
    case NOVA_AUTORCM_UNSUPPORTED:
        return "UNSUPPORTED";
    default:
        return "UNKNOWN";
    }
}

const char *nova_autorcm_fallback_label(nova_autorcm_fallback_t fallback)
{
    switch (fallback)
    {
    case NOVA_AUTORCM_FALLBACK_READY:
        return "READY";
    case NOVA_AUTORCM_FALLBACK_COPIED:
        return "ARMED";
    case NOVA_AUTORCM_FALLBACK_FAILED:
        return "FAILED";
    default:
        return "NONE";
    }
}

static void write_autorcm_status(const nova_autorcm_t *out, uint32_t boots)
{
    const char *state = "unknown";
    const char *fallback = "none";

    if (out->state == NOVA_AUTORCM_DISABLED)
        state = "disabled";
    else if (out->state == NOVA_AUTORCM_ENABLED)
        state = "enabled";
    else if (out->state == NOVA_AUTORCM_UNSUPPORTED)
        state = "unsupported";

    if (out->fallback == NOVA_AUTORCM_FALLBACK_READY)
        fallback = "ready";
    else if (out->fallback == NOVA_AUTORCM_FALLBACK_COPIED)
        fallback = "armed";
    else if (out->fallback == NOVA_AUTORCM_FALLBACK_FAILED)
        fallback = "failed";

    char text[320];
    s_printf(text,
             "[autorcm]\nstate=%s\nfallback=%s\nversion=%s\n"
             "boots=%d\ndetail=%s\n",
             state, fallback, NOVA_VERSION_STRING,
             (unsigned)boots, out->detail);
    uint32_t length = (uint32_t)nova_strlen(text);
    if (length < sizeof(text))
        nova_platform_write_text(NOVA_AUTORCM_STATE_FILE, text, length);
}

static bool preserve_payload(const u8 *payload, uint32_t size)
{
    if (!payload || !size)
        return true;

    if (!write_whole_file(NOVA_AUTORCM_BACKUP_FILE, payload, size) ||
        !file_matches(NOVA_AUTORCM_BACKUP_FILE, payload, size))
        return false;

    char hash[65];
    nova_sha256_hex(payload, size, hash);

    char meta[192];
    s_printf(meta, "[autorcm-backup]\nkind=unknown\nsha256=%s\nsize=%d\n",
             hash, (unsigned)size);
    uint32_t length = (uint32_t)nova_strlen(meta);
    return length < sizeof(meta) && write_whole_file(NOVA_AUTORCM_META_FILE, meta, length);
}

static void write_empty_meta(void)
{
    const char *meta = "[autorcm-backup]\nkind=none\nsha256=\nsize=0\n";
    write_whole_file(NOVA_AUTORCM_META_FILE, meta, (uint32_t)nova_strlen(meta));
}

void nova_platform_autorcm_prepare(nova_autorcm_t *out)
{
    if (!out)
        return;

    nova_memset(out, 0, sizeof(*out));
    out->state = detect_autorcm(out->detail, sizeof(out->detail));
    out->fallback = NOVA_AUTORCM_FALLBACK_NONE;

    if (out->state != NOVA_AUTORCM_ENABLED)
    {
        if (g_sd_ready)
            write_autorcm_status(out, current_boot_count());
        return;
    }

    if (!g_sd_ready)
    {
        out->fallback = NOVA_AUTORCM_FALLBACK_FAILED;
        s_printf(out->detail,
                 "AutoRCM detected but the SD card is unavailable");
        return;
    }

    uint32_t boots = current_boot_count();
    uint32_t source_size = 0;
    u8 *source = read_whole_file(NOVA_PAYLOAD_PATH, &source_size);
    if (!source || !payload_looks_like_nova(source, source_size))
    {
        out->fallback = NOVA_AUTORCM_FALLBACK_FAILED;
        s_printf(out->detail,
                 "AutoRCM detected but Nova/NOVA.bin is missing or invalid");
        free(source);
        write_autorcm_status(out, boots);
        return;
    }

    char source_hash[65];
    nova_sha256_hex(source, source_size, source_hash);

    uint32_t old_size = 0;
    u8 *old = read_whole_file(NOVA_AUTOBOOT_PAYLOAD, &old_size);
    if (old && old_size == source_size && !nova_memcmp(old, source, source_size))
    {
        out->fallback = NOVA_AUTORCM_FALLBACK_READY;
        s_printf(out->detail,
                 "AutoRCM detected; payload.bin is already NOVA");
        free(old);
        free(source);
        write_autorcm_status(out, boots);
        return;
    }

    if (old && old_size)
    {
        if (!preserve_payload(old, old_size))
        {
            out->fallback = NOVA_AUTORCM_FALLBACK_FAILED;
            s_printf(out->detail,
                     "AutoRCM detected but the existing payload.bin could not be backed up");
            free(old);
            free(source);
            write_autorcm_status(out, boots);
            return;
        }
    }
    else
    {
        write_empty_meta();
    }

    if (!write_whole_file("payload.bin.new", source, source_size) ||
        !file_matches("payload.bin.new", source, source_size))
    {
        f_unlink("payload.bin.new");
        out->fallback = NOVA_AUTORCM_FALLBACK_FAILED;
        s_printf(out->detail,
                 "AutoRCM detected but the new payload could not be written");
        free(old);
        free(source);
        write_autorcm_status(out, boots);
        return;
    }

    FILINFO info;
    if (f_stat(NOVA_AUTOBOOT_PAYLOAD, &info) == FR_OK &&
        f_unlink(NOVA_AUTOBOOT_PAYLOAD) != FR_OK)
    {
        f_unlink("payload.bin.new");
        out->fallback = NOVA_AUTORCM_FALLBACK_FAILED;
        s_printf(out->detail,
                 "AutoRCM detected but the old payload.bin could not be replaced");
        free(old);
        free(source);
        write_autorcm_status(out, boots);
        return;
    }

    if (f_rename("payload.bin.new", NOVA_AUTOBOOT_PAYLOAD) != FR_OK ||
        !file_matches(NOVA_AUTOBOOT_PAYLOAD, source, source_size))
    {
        f_unlink("payload.bin.new");
        if (old && old_size)
            write_whole_file(NOVA_AUTOBOOT_PAYLOAD, old, old_size);
        else
            f_unlink(NOVA_AUTOBOOT_PAYLOAD);

        out->fallback = NOVA_AUTORCM_FALLBACK_FAILED;
        s_printf(out->detail,
                 "AutoRCM detected but payload.bin verification failed");
        free(old);
        free(source);
        write_autorcm_status(out, boots);
        return;
    }

    char state[256];
    s_printf(state,
             "[autoboot]\nstate=pending\nversion=%s\n"
             "payload_sha256=%s\nboots_at_configure=%d\nmode=%s\n",
             NOVA_VERSION_STRING, source_hash,
             (unsigned)boots, NOVA_AUTOBOOT_MODE);
    uint32_t length = (uint32_t)nova_strlen(state);
    bool state_ok = length < sizeof(state) &&
                    nova_platform_write_text(NOVA_AUTOBOOT_STATE_FILE,
                                             state, length);

    out->fallback = state_ok ? NOVA_AUTORCM_FALLBACK_COPIED
                             : NOVA_AUTORCM_FALLBACK_FAILED;
    if (state_ok)
        s_printf(out->detail,
                 "AutoRCM detected; NOVA armed for the RCM loader");
    else
        s_printf(out->detail,
                 "AutoRCM detected; payload.bin is NOVA but state could not be written");

    free(old);
    free(source);
    write_autorcm_status(out, boots);
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
