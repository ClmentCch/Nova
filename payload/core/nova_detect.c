/*
 * NOVA Boot - SD card inspection.
 *
 * Detection is deliberately stricter than "does the folder exist". The
 * Atmosphère emuMMC boot target is only offered when every piece that
 * hekate and Atmosphère need at boot time is present and consistent:
 *
 *   atmosphere/package3                     (Atmosphère boot package)
 *   emummc/emummc.ini                       (enabled=1, valid path)
 *   <emu path>/raw_based   with sector != 0 (raw partition based emuMMC)
 *     or <emu path>/file_based              (file based emuMMC)
 *   <emu path>/Nintendo                     (Nintendo content of the emuMMC)
 *   bootloader/sys/emummc.kipm              (hekate emuMMC kernel module)
 *   a valid hekate payload                  (chainload target)
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "nova_core.h"

static void set_detail(char *dst, size_t size, const char *text)
{
    nova_cstr_copy(dst, size, text);
}

static void join1(char *out, size_t out_size, const char *a, const char *b)
{
    nova_cstr_copy(out, out_size, a);
    nova_strcat(out, b);
}

static void join2(char *out, size_t out_size, const char *a, const char *b, const char *c)
{
    nova_cstr_copy(out, out_size, a);
    nova_strcat(out, b);
    nova_strcat(out, c);
}

static void detect_atmosphere(const nova_fs_t *fs, nova_status_t *st)
{
    if (!fs)
    {
        st->atmosphere = NOVA_ST_MISSING;
        set_detail(st->atm_detail, sizeof(st->atm_detail), "SD not mounted");
        return;
    }

    if (!fs->exists(fs->ctx, NOVA_ATMOSPHERE_DIR))
    {
        st->atmosphere = NOVA_ST_MISSING;
        set_detail(st->atm_detail, sizeof(st->atm_detail), "atmosphere/ not found");
        return;
    }

    if (fs->exists(fs->ctx, NOVA_ATMOSPHERE_PKG3))
    {
        uint32_t size = 0;
        st->atmosphere = NOVA_ST_OK;
        if (fs->file_size && fs->file_size(fs->ctx, NOVA_ATMOSPHERE_PKG3, &size) == 0 &&
            size > 4096)
        {
            st->package3_size = size;
            set_detail(st->atm_detail, sizeof(st->atm_detail), "package3 verified");
        }
        else
        {
            st->atmosphere = NOVA_ST_ERROR;
            set_detail(st->atm_detail, sizeof(st->atm_detail), "package3 is empty");
        }
        return;
    }

    if (fs->exists(fs->ctx, NOVA_ATMOSPHERE_LEGACY))
    {
        st->atmosphere = NOVA_ST_WARN;
        set_detail(st->atm_detail, sizeof(st->atm_detail), "legacy fusee layout");
        return;
    }

    st->atmosphere = NOVA_ST_ERROR;
    set_detail(st->atm_detail, sizeof(st->atm_detail), "package3 missing");
}

static void detect_emummc(const nova_fs_t *fs, nova_status_t *st)
{
    st->emummc = NOVA_ST_MISSING;
    st->emu_mode = NOVA_EMU_MODE_NONE;
    st->emu_sector = 0;
    st->emu_path[0] = '\0';
    st->emu_nintendo[0] = '\0';
    set_detail(st->emu_detail, sizeof(st->emu_detail), "emummc/ not found");

    if (!fs)
        return;

    if (!fs->exists(fs->ctx, NOVA_EMUMMC_DIR))
        return;

    uint32_t size = 0;
    void *raw = fs->read_file(fs->ctx, NOVA_EMUMMC_INI, &size);
    if (!raw)
    {
        st->emummc = NOVA_ST_ERROR;
        set_detail(st->emu_detail, sizeof(st->emu_detail), "emummc.ini unreadable");
        return;
    }

    char enabled[16] = {0};
    char path[80] = {0};
    char nintendo[96] = {0};
    bool has_enabled = nova_ini_get((const char *)raw, size, "emummc", "enabled",
                                    enabled, sizeof(enabled));
    nova_ini_get((const char *)raw, size, "emummc", "path", path, sizeof(path));
    bool has_nintendo = nova_ini_get((const char *)raw, size, "emummc", "nintendo_path",
                                     nintendo, sizeof(nintendo));
    fs->free_file(fs->ctx, raw);

    if (!has_enabled || nova_atoi(enabled) != 1)
    {
        st->emummc = NOVA_ST_MISSING;
        set_detail(st->emu_detail, sizeof(st->emu_detail), "disabled in emummc.ini");
        return;
    }

    if (!path[0])
    {
        st->emummc = NOVA_ST_ERROR;
        set_detail(st->emu_detail, sizeof(st->emu_detail), "path missing in emummc.ini");
        return;
    }

    nova_cstr_copy(st->emu_path, sizeof(st->emu_path), path);

    if (!fs->exists(fs->ctx, path))
    {
        st->emummc = NOVA_ST_ERROR;
        join1(st->emu_detail, sizeof(st->emu_detail), "path not found: ", path);
        return;
    }

    /* Raw based emuMMC: raw_based holds the start sector (little endian). */
    char marker[128];
    nova_cstr_copy(marker, sizeof(marker), path);
    nova_strcat(marker, NOVA_EMUMMC_RAW_MARKER);

    uint32_t marker_size = 0;
    void *marker_data = fs->read_file(fs->ctx, marker, &marker_size);
    if (marker_data)
    {
        if (marker_size >= 4)
        {
            uint8_t *b = (uint8_t *)marker_data;
            st->emu_sector = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                             ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        }
        fs->free_file(fs->ctx, marker_data);
    }

    if (st->emu_sector)
    {
        st->emu_mode = NOVA_EMU_MODE_RAW;
    }
    else
    {
        nova_cstr_copy(marker, sizeof(marker), path);
        nova_strcat(marker, NOVA_EMUMMC_FILE_MARKER);
        if (fs->exists(fs->ctx, marker))
        {
            st->emu_mode = NOVA_EMU_MODE_FILE;
        }
        else
        {
            st->emummc = NOVA_ST_ERROR;
            set_detail(st->emu_detail, sizeof(st->emu_detail),
                       "no raw_based/file_based marker");
            return;
        }
    }

    if (has_nintendo && nintendo[0])
    {
        nova_cstr_copy(st->emu_nintendo, sizeof(st->emu_nintendo), nintendo);
    }
    else
    {
        nova_cstr_copy(st->emu_nintendo, sizeof(st->emu_nintendo), path);
        nova_strcat(st->emu_nintendo, NOVA_EMUMMC_NINTENDO);
    }

    if (!fs->exists(fs->ctx, st->emu_nintendo))
    {
        st->emummc = NOVA_ST_ERROR;
        set_detail(st->emu_detail, sizeof(st->emu_detail), "Nintendo folder missing");
        return;
    }

    if (!fs->exists(fs->ctx, NOVA_EMUMMC_KIPM))
    {
        st->emummc = NOVA_ST_WARN;
        set_detail(st->emu_detail, sizeof(st->emu_detail), "emummc.kipm missing");
        return;
    }

    st->emummc = NOVA_ST_OK;
    if (st->emu_mode == NOVA_EMU_MODE_RAW)
    {
        join1(st->emu_detail, sizeof(st->emu_detail), "raw emuMMC: ", path);
    }
    else
    {
        join1(st->emu_detail, sizeof(st->emu_detail), "file emuMMC: ", path);
    }
}

static void detect_bootcfg(const nova_fs_t *fs, nova_status_t *st)
{
    st->bootcfg = NOVA_ST_MISSING;
    set_detail(st->cfg_detail, sizeof(st->cfg_detail), "NOVA boot entries absent");

    if (!fs)
        return;

    uint32_t size = 0;
    void *raw = fs->read_file(fs->ctx, NOVA_HEKATE_INI, &size);
    if (!raw)
        return;

    bool stock = nova_ini_has_id((const char *)raw, size, NOVA_ID_STOCK);
    bool emummc = nova_ini_has_id((const char *)raw, size, NOVA_ID_EMUMMC);
    fs->free_file(fs->ctx, raw);

    if (stock && emummc)
    {
        st->bootcfg = NOVA_ST_OK;
        set_detail(st->cfg_detail, sizeof(st->cfg_detail), "entries verified");
    }
    else if (stock || emummc)
    {
        st->bootcfg = NOVA_ST_WARN;
        set_detail(st->cfg_detail, sizeof(st->cfg_detail), "incomplete entries");
    }
    else
    {
        st->bootcfg = NOVA_ST_WARN;
        set_detail(st->cfg_detail, sizeof(st->cfg_detail), "ids not found");
    }
}

void nova_detect(const nova_fs_t *fs, nova_status_t *out)
{
    if (!out)
        return;

    nova_memset(out, 0, sizeof(*out));

    out->sd_mounted = (fs != NULL);
    if (fs)
        set_detail(out->sd_detail, sizeof(out->sd_detail), "mounted");
    else
        set_detail(out->sd_detail, sizeof(out->sd_detail), "not detected");

    detect_atmosphere(fs, out);
    detect_emummc(fs, out);

    out->hekate = NOVA_ST_MISSING;
    set_detail(out->hek_detail, sizeof(out->hek_detail), "payload not found");
    out->hekate_path[0] = '\0';
    set_detail(out->hekate_version, sizeof(out->hekate_version), "unknown");

    bool hekate_ok = false;
    if (fs)
    {
        char path[96], version[16];
        bool valid = false;
        if (nova_find_hekate(fs, path, sizeof(path), version, sizeof(version), &valid))
        {
            nova_cstr_copy(out->hekate_path, sizeof(out->hekate_path), path);
            nova_cstr_copy(out->hekate_version, sizeof(out->hekate_version), version);

            if (valid)
            {
                uint32_t size = 0;
                void *img = fs->read_file(fs->ctx, path, &size);
                if (img)
                {
                    hekate_ok = nova_hekate_set_boot_id((uint8_t *)img, size, NULL);
                    fs->free_file(fs->ctx, img);
                }
            }

            if (hekate_ok)
            {
                out->hekate = NOVA_ST_OK;
                join2(out->hek_detail, sizeof(out->hek_detail), "payload ", version, " verified");
            }
            else
            {
                out->hekate = NOVA_ST_ERROR;
                set_detail(out->hek_detail, sizeof(out->hek_detail), "not a hekate payload");
            }
        }
    }

    detect_bootcfg(fs, out);

    /* Boot targets. */
    out->can_hekate = hekate_ok;

    out->can_stock = hekate_ok && out->atmosphere == NOVA_ST_OK;
    if (!hekate_ok)
        set_detail(out->stock_detail, sizeof(out->stock_detail), "needs hekate payload");
    else if (out->atmosphere != NOVA_ST_OK)
        set_detail(out->stock_detail, sizeof(out->stock_detail), "needs atmosphere/package3");
    else
        set_detail(out->stock_detail, sizeof(out->stock_detail), "SysNAND OFW ready");

    out->can_emummc = hekate_ok && out->atmosphere == NOVA_ST_OK &&
                      out->emummc == NOVA_ST_OK;
}
