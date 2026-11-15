/*
 * NOVA Installer - installation engine.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "installer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autoboot.h"
#include "nova_fs.h"
#include "nova_sha256.h"

#define BACKUP_DIR        "Nova/backup/latest"
#define BACKUP_MANIFEST   BACKUP_DIR "/manifest.txt"
#define BACKUP_FILES      BACKUP_DIR "/files"
#define NOVA_CONFIG_PATH  "Nova/config/nova.ini"
#define NOVA_THEME_README "Nova/themes/README.txt"

static const char *BACKUP_PATHS[] = {
    NOVA_HEKATE_INI,
    NOVA_HEKATE_INSTALL,
    NOVA_PAYLOAD_PATH,
    NOVA_VERSION_FILE,
    NOVA_CONFIG_PATH,
};

#define BACKUP_PATH_COUNT ((int)(sizeof(BACKUP_PATHS) / sizeof(BACKUP_PATHS[0])))

static const char *CONFIG_TEXT =
    "; NOVA boot configuration\n"
    "[nova]\n"
    "version=" NOVA_VERSION_STRING "\n"
    "theme=aurora\n"
    "\n"
    "; NOVA " NOVA_VERSION_STRING " ships the built-in aurora theme. This file is\n"
    "; reserved for future theme selection and is never required for booting.\n";

static const char *THEME_TEXT =
    "NOVA " NOVA_VERSION_STRING " uses the built-in \"aurora\" theme (pure black\n"
    "background, blue and purple accents). No external assets are required.\n";

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void set_err(char *err, size_t err_size, const char *fmt, ...)
{
    if (!err || !err_size)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, err_size, fmt, args);
    va_end(args);
}

static bool contains_bytes(const uint8_t *haystack, uint32_t haystack_size,
                           const char *needle)
{
    size_t len = strlen(needle);
    if (!haystack || !len || haystack_size < len)
        return false;

    for (uint32_t i = 0; i + len <= haystack_size; i++)
    {
        if (memcmp(haystack + i, needle, len) == 0)
            return true;
    }
    return false;
}

bool nova_payload_valid(const uint8_t *image, uint32_t size, char *detail, size_t detail_size)
{
    if (detail && detail_size)
        detail[0] = '\0';

    if (!image || size < 0x400)
    {
        set_err(detail, detail_size, "payload missing or too small");
        return false;
    }

    if (size > NOVA_MAX_PAYLOAD_SIZE)
    {
        set_err(detail, detail_size, "payload exceeds hekate 192KB limit");
        return false;
    }

    uint32_t first = (uint32_t)image[0] | ((uint32_t)image[1] << 8) |
                     ((uint32_t)image[2] << 16) | ((uint32_t)image[3] << 24);
    uint32_t cond = first >> 28;

    if (!memcmp(image, "NRO0", 4) || !memcmp(image, "\x7f" "ELF", 4) ||
        !memcmp(image, "NSO0", 4))
    {
        set_err(detail, detail_size, "not a raw payload");
        return false;
    }

    if (cond != 0xE && cond != 0xA)
    {
        set_err(detail, detail_size, "not ARM32 payload code");
        return false;
    }

    if (!contains_bytes(image, size, NOVA_VERSION_STRING))
    {
        set_err(detail, detail_size, "version marker missing");
        return false;
    }

    if (detail && detail_size)
        snprintf(detail, detail_size, "valid (%lu KB)", (unsigned long)(size / 1024));
    return true;
}

static int write_buffer_progress(const char *path, const uint8_t *data, uint32_t size,
                                 nova_progress_fn cb, void *ctx, const char *stage,
                                 uint64_t *done, uint64_t total)
{
    char full[NOVA_MAX_PATH * 2];
    nova_io_path(path, full, sizeof(full));

    FILE *f = fopen(full, "wb");
    if (!f)
        return -1;

    uint32_t offset = 0;
    int rc = 0;
    while (offset < size)
    {
        uint32_t chunk = size - offset;
        if (chunk > 16384)
            chunk = 16384;

        if (fwrite(data + offset, 1, chunk, f) != chunk)
        {
            rc = -1;
            break;
        }

        offset += chunk;
        if (done)
            *done += chunk;
        if (cb)
            cb(ctx, stage, *done, total);
    }

    if (fflush(f) != 0)
        rc = -1;
    fclose(f);
    return rc;
}

static int read_text_file(const char *path, char *out, size_t out_size)
{
    if (!out_size)
        return -1;

    out[0] = '\0';
    long long size = nova_io_size(path);
    if (size < 0)
        return -1;

    size_t capacity = (size_t)size;
    if (capacity >= out_size)
        capacity = out_size - 1;

    size_t got = 0;
    if (nova_io_read(path, out, capacity, &got) != 0)
        return -1;

    out[got] = '\0';

    /* Trim trailing whitespace/newlines. */
    while (got > 0 && (out[got - 1] == '\n' || out[got - 1] == '\r' || out[got - 1] == ' '))
        out[--got] = '\0';

    return 0;
}

/* ------------------------------------------------------------------ */
/* Backup manifest                                                     */
/* ------------------------------------------------------------------ */
static int backup_manifest_write(const char *entries, int count)
{
    char manifest[4096];
    int n = snprintf(manifest, sizeof(manifest), "NOVA-BACKUP 1\n%d\n%s", count, entries);
    if (n < 0 || n >= (int)sizeof(manifest))
        return -1;
    return nova_io_write(BACKUP_MANIFEST, manifest, (size_t)n);
}

typedef struct backup_entry
{
    char sha256[65];
    uint64_t size;
    char path[NOVA_MAX_PATH];
} backup_entry_t;

static int backup_manifest_read(backup_entry_t *entries, int max_entries, int *count_out)
{
    size_t size = 0;
    char *raw = (char *)nova_io_read_all(BACKUP_MANIFEST, &size);
    if (!raw)
        return -1;

    int count = 0;
    char header[32] = {0};
    int header_count = 0;
    int consumed = 0;

    if (sscanf(raw, "%31s %d%n", header, &header_count, &consumed) != 2 ||
        strcmp(header, "NOVA-BACKUP") != 0)
    {
        free(raw);
        return -1;
    }

    const char *cursor = raw + consumed;
    if (sscanf(cursor, "%d%n", &count, &consumed) != 1)
    {
        free(raw);
        return -1;
    }
    cursor += consumed;

    if (count < 0)
        count = 0;
    if (count > max_entries)
        count = max_entries;

    for (int i = 0; i < count; i++)
    {
        unsigned long long entry_size = 0;
        char sha[65];
        char path[NOVA_MAX_PATH];

        if (sscanf(cursor, "%64s %llu %255s%n", sha, &entry_size, path, &consumed) != 3)
        {
            free(raw);
            return -1;
        }
        cursor += consumed;

        snprintf(entries[i].sha256, sizeof(entries[i].sha256), "%s", sha);
        entries[i].size = (uint64_t)entry_size;
        snprintf(entries[i].path, sizeof(entries[i].path), "%s", path);
    }

    *count_out = count;
    free(raw);
    return 0;
}

static void backup_info_refresh(nova_installer_t *st)
{
    memset(&st->backup, 0, sizeof(st->backup));

    backup_entry_t entries[16];
    int count = 0;
    if (backup_manifest_read(entries, 16, &count) != 0)
        return;

    st->backup.present = true;
    st->backup.files = count;
    for (int i = 0; i < count; i++)
        st->backup.bytes += entries[i].size;
}

/* ------------------------------------------------------------------ */
/* Scan                                                                */
/* ------------------------------------------------------------------ */
int nova_scan(nova_installer_t *st, char *err, size_t err_size)
{
    if (!st)
        return -1;

    memset(st, 0, sizeof(*st));
    set_err(err, err_size, "");

    uint64_t free_bytes = 0, total_bytes = 0;
    if (nova_sd_space(&free_bytes, &total_bytes) != 0 || total_bytes == 0)
    {
        set_err(err, err_size, "SD card not mounted");
        return -1;
    }

    st->sd_ok = true;
    st->free_bytes = free_bytes;
    st->total_bytes = total_bytes;

    /* Embedded payload. */
    uint32_t payload_size = 0;
    const uint8_t *payload = nova_embedded_payload(&payload_size);
    st->payload_ok = nova_payload_valid(payload, payload_size, st->payload_detail,
                                        sizeof(st->payload_detail));
    st->payload_size = payload_size;
    nova_sha256_hex(payload, payload_size, st->payload_sha256);
    snprintf(st->payload_version, sizeof(st->payload_version), "%s", NOVA_VERSION_STRING);

    /* Installed files. */
    if (nova_io_exists(NOVA_PAYLOAD_PATH))
    {
        st->installed_payload_ok = false;
        long long size = nova_io_size(NOVA_PAYLOAD_PATH);
        if (size > 0 && size <= (long long)NOVA_MAX_PAYLOAD_SIZE)
        {
            size_t read = 0;
            uint8_t *image = (uint8_t *)nova_io_read_all(NOVA_PAYLOAD_PATH, &read);
            if (image)
            {
                char detail[64];
                st->installed_payload_ok = nova_payload_valid(image, (uint32_t)read,
                                                              detail, sizeof(detail));
                free(image);
            }
        }
        nova_io_sha256_file(NOVA_PAYLOAD_PATH, st->installed_sha256);
    }

    if (read_text_file(NOVA_VERSION_FILE, st->installed_version,
                       sizeof(st->installed_version)) != 0)
        st->installed_version[0] = '\0';

    st->installed = st->installed_payload_ok && st->installed_version[0] != '\0';

    /* NOVA boot entries. */
    size_t ini_size = 0;
    char *ini = (char *)nova_io_read_all(NOVA_HEKATE_INI, &ini_size);
    if (ini)
    {
        st->boot_entries_present = nova_ini_has_id(ini, (uint32_t)ini_size, NOVA_ID_STOCK) &&
                                   nova_ini_has_id(ini, (uint32_t)ini_size, NOVA_ID_EMUMMC);
        free(ini);
    }

    /* Shared detection (Atmosphère, emuMMC, hekate). */
    nova_fs_t fs;
    nova_fs_init(&fs);
    nova_status_t detection;
    nova_detect(&fs, &detection);

    st->hekate_present = detection.can_hekate;
    snprintf(st->hekate_path, sizeof(st->hekate_path), "%s", detection.hekate_path);
    snprintf(st->hekate_version, sizeof(st->hekate_version), "%s", detection.hekate_version);
    st->atmosphere_ready = detection.atmosphere == NOVA_ST_OK;
    st->emummc_ready = detection.emummc == NOVA_ST_OK;
    snprintf(st->emummc_detail, sizeof(st->emummc_detail), "%s", detection.emu_detail);

    backup_info_refresh(st);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Transfer                                                            */
/* ------------------------------------------------------------------ */
static uint32_t build_boot_ini(char *out, uint32_t out_size)
{
    return nova_build_hekate_ini(out, out_size);
}

int nova_transfer(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                  char *err, size_t err_size)
{
    if (!st || !st->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    uint32_t payload_size = 0;
    const uint8_t *payload = nova_embedded_payload(&payload_size);

    char detail[64];
    if (!nova_payload_valid(payload, payload_size, detail, sizeof(detail)))
    {
        set_err(err, err_size, "embedded payload invalid: %s", detail);
        return -1;
    }

    char ini[1024];
    uint32_t ini_len = build_boot_ini(ini, sizeof(ini));
    if (!ini_len)
    {
        set_err(err, err_size, "cannot build NOVA boot entries");
        return -1;
    }

    const char *version_text = NOVA_VERSION_STRING "\n";
    uint32_t version_len = (uint32_t)strlen(version_text);
    uint32_t config_len = (uint32_t)strlen(CONFIG_TEXT);
    uint32_t theme_len = (uint32_t)strlen(THEME_TEXT);

    uint64_t total = (uint64_t)payload_size * 2 + version_len + ini_len +
                     config_len + theme_len;
    uint64_t done = 0;

    const char *stage = "preparing folders";
    if (cb)
        cb(ctx, stage, 0, total);

    if (nova_io_mkdir_p(NOVA_ROOT_DIR) != 0 ||
        nova_io_mkdir_p(NOVA_CONFIG_DIR) != 0 ||
        nova_io_mkdir_p(NOVA_THEME_DIR) != 0 ||
        nova_io_mkdir_p("bootloader/payloads") != 0 ||
        nova_io_mkdir_p("bootloader/ini") != 0)
    {
        set_err(err, err_size, "cannot create folders on the SD card");
        return -1;
    }

    if (write_buffer_progress(NOVA_PAYLOAD_PATH, payload, payload_size, cb, ctx,
                              "writing Nova/NOVA.bin", &done, total) != 0)
    {
        set_err(err, err_size, "failed to write " NOVA_PAYLOAD_PATH);
        return -1;
    }

    if (write_buffer_progress(NOVA_HEKATE_INSTALL, payload, payload_size, cb, ctx,
                              "writing bootloader/payloads/NOVA.bin", &done, total) != 0)
    {
        set_err(err, err_size, "failed to write " NOVA_HEKATE_INSTALL);
        return -1;
    }

    if (write_buffer_progress(NOVA_VERSION_FILE, (const uint8_t *)version_text, version_len,
                              cb, ctx, "writing version", &done, total) != 0)
    {
        set_err(err, err_size, "failed to write " NOVA_VERSION_FILE);
        return -1;
    }

    if (write_buffer_progress(NOVA_HEKATE_INI, (const uint8_t *)ini, ini_len, cb, ctx,
                              "writing NOVA boot entries", &done, total) != 0)
    {
        set_err(err, err_size, "failed to write " NOVA_HEKATE_INI);
        return -1;
    }

    if (write_buffer_progress(NOVA_CONFIG_PATH, (const uint8_t *)CONFIG_TEXT, config_len,
                              cb, ctx, "writing configuration", &done, total) != 0)
    {
        set_err(err, err_size, "failed to write " NOVA_CONFIG_PATH);
        return -1;
    }

    if (write_buffer_progress(NOVA_THEME_README, (const uint8_t *)THEME_TEXT, theme_len,
                              cb, ctx, "writing theme notes", &done, total) != 0)
    {
        set_err(err, err_size, "failed to write " NOVA_THEME_README);
        return -1;
    }

    return 0;
}

int nova_verify_installation(nova_installer_t *st, char *err, size_t err_size)
{
    if (!st || !st->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    uint32_t payload_size = 0;
    const uint8_t *payload = nova_embedded_payload(&payload_size);
    char expected[65];
    nova_sha256_hex(payload, payload_size, expected);

    const char *paths[] = {NOVA_PAYLOAD_PATH, NOVA_HEKATE_INSTALL};
    for (int i = 0; i < 2; i++)
    {
        char actual[65];
        if (!nova_io_exists(paths[i]))
        {
            set_err(err, err_size, "missing %s", paths[i]);
            return -1;
        }
        if (nova_io_sha256_file(paths[i], actual) != 0 || strcmp(actual, expected) != 0)
        {
            set_err(err, err_size, "checksum mismatch: %s", paths[i]);
            return -1;
        }
    }

    char version[32];
    if (read_text_file(NOVA_VERSION_FILE, version, sizeof(version)) != 0 ||
        strcmp(version, NOVA_VERSION_STRING) != 0)
    {
        set_err(err, err_size, "version file mismatch");
        return -1;
    }

    size_t ini_size = 0;
    char *ini = (char *)nova_io_read_all(NOVA_HEKATE_INI, &ini_size);
    if (!ini)
    {
        set_err(err, err_size, "missing " NOVA_HEKATE_INI);
        return -1;
    }

    bool ok = nova_ini_has_id(ini, (uint32_t)ini_size, NOVA_ID_STOCK) &&
              nova_ini_has_id(ini, (uint32_t)ini_size, NOVA_ID_EMUMMC);
    free(ini);

    if (!ok)
    {
        set_err(err, err_size, "boot entries incomplete");
        return -1;
    }

    st->installed = true;
    snprintf(st->installed_version, sizeof(st->installed_version), "%s", NOVA_VERSION_STRING);
    st->installed_payload_ok = true;
    snprintf(st->installed_sha256, sizeof(st->installed_sha256), "%s", expected);
    st->boot_entries_present = true;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Install / uninstall                                                 */
/* ------------------------------------------------------------------ */
int nova_backup_create(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                       char *err, size_t err_size);

int nova_install(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                 char *err, size_t err_size)
{
    if (!st || !st->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    if (!st->payload_ok)
    {
        set_err(err, err_size, "payload failed validation: %s", st->payload_detail);
        return -1;
    }

    if (!st->hekate_present)
    {
        set_err(err, err_size, "hekate payload missing - copy the release files to the SD root");
        return -1;
    }

    uint32_t payload_size = 0;
    nova_embedded_payload(&payload_size);
    if (st->free_bytes < (uint64_t)payload_size * 3 + 1024 * 1024)
    {
        set_err(err, err_size, "not enough free space on the SD card");
        return -1;
    }

    /* Keep the first clean backup instead of overwriting it on reinstall. */
    if (!st->backup.present && nova_backup_create(st, cb, ctx, err, err_size) != 0)
        return -1;

    if (nova_transfer(st, cb, ctx, err, err_size) != 0)
        return -1;

    if (nova_verify_installation(st, err, err_size) != 0)
        return -1;

    nova_scan(st, err, err_size);
    return 0;
}

int nova_uninstall(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                   char *err, size_t err_size)
{
    if (!st || !st->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    /*
     * A cold boot slot pointing at NOVA must not outlive the installation.
     * Restore the previous payload first so a console cannot be left with a
     * NOVA cold boot that has no installed files to draw from.
     */
    nova_autoboot_t autoboot;
    nova_autoboot_scan(&autoboot);
    if (autoboot.state == NOVA_AUTO_CONFIGURED || autoboot.state == NOVA_AUTO_ACTIVE ||
        autoboot.state == NOVA_AUTO_OVERWRITTEN || autoboot.payload_is_nova)
    {
        if (nova_autoboot_disable(&autoboot, cb, ctx, err, err_size) != 0)
            return -1;
    }

    bool had_backup = st->backup.present && st->backup.files > 0;

    if (had_backup)
    {
        /* Put the previous configuration back first. */
        if (nova_backup_restore(st, cb, ctx, err, err_size) != 0)
            return -1;
    }

    const char *remove_paths[] = {
        NOVA_HEKATE_INSTALL,
        NOVA_PAYLOAD_PATH,
        NOVA_VERSION_FILE,
        NOVA_CONFIG_PATH,
        NOVA_THEME_README,
    };

    for (int i = 0; i < (int)(sizeof(remove_paths) / sizeof(remove_paths[0])); i++)
    {
        if (nova_io_exists(remove_paths[i]) && nova_io_remove(remove_paths[i]) != 0)
        {
            set_err(err, err_size, "cannot remove %s", remove_paths[i]);
            return -1;
        }
    }

    if (!had_backup)
    {
        if (nova_io_exists(NOVA_HEKATE_INI) && nova_io_remove(NOVA_HEKATE_INI) != 0)
        {
            set_err(err, err_size, "cannot remove " NOVA_HEKATE_INI);
            return -1;
        }
    }

    /* Remove now empty NOVA folders, keep Nova/backup intact. */
    nova_io_remove(NOVA_THEME_DIR);
    nova_io_remove(NOVA_CONFIG_DIR);
    nova_io_remove(NOVA_ROOT_DIR);

    if (nova_io_exists(NOVA_PAYLOAD_PATH) || nova_io_exists(NOVA_HEKATE_INSTALL))
    {
        set_err(err, err_size, "verification failed: NOVA files still present");
        return -1;
    }

    nova_scan(st, err, err_size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Backup and restore                                                  */
/* ------------------------------------------------------------------ */
int nova_backup_create(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                       char *err, size_t err_size)
{
    if (!st || !st->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    if (nova_io_exists(BACKUP_DIR))
    {
        if (nova_io_remove_tree(BACKUP_DIR, 0) != 0)
        {
            set_err(err, err_size, "cannot replace old backup");
            return -1;
        }
    }

    if (nova_io_mkdir_p(BACKUP_FILES) != 0)
    {
        set_err(err, err_size, "cannot create backup folder");
        return -1;
    }

    uint64_t total = 0;
    for (int i = 0; i < BACKUP_PATH_COUNT; i++)
    {
        if (nova_io_exists(BACKUP_PATHS[i]))
        {
            long long size = nova_io_size(BACKUP_PATHS[i]);
            if (size > 0)
                total += (uint64_t)size;
        }
    }

    char entries[3072];
    entries[0] = '\0';
    size_t used = 0;
    uint64_t done = 0;
    int count = 0;

    if (cb)
        cb(ctx, "creating backup", 0, total);

    for (int i = 0; i < BACKUP_PATH_COUNT; i++)
    {
        const char *path = BACKUP_PATHS[i];
        if (!nova_io_exists(path))
            continue;

        char destination[NOVA_MAX_PATH];
        snprintf(destination, sizeof(destination), BACKUP_FILES "/%s", path);

        char parent[NOVA_MAX_PATH];
        snprintf(parent, sizeof(parent), "%s", destination);
        char *slash = strrchr(parent, '/');
        if (slash)
        {
            *slash = '\0';
            if (nova_io_mkdir_p(parent) != 0)
            {
                set_err(err, err_size, "cannot create backup subfolders");
                return -1;
            }
        }

        if (nova_io_copy(path, destination, cb, ctx, "backing up", &done, total) != 0)
        {
            set_err(err, err_size, "failed to back up %s", path);
            return -1;
        }

        char hash[65];
        if (nova_io_sha256_file(path, hash) != 0)
        {
            set_err(err, err_size, "failed to hash %s", path);
            return -1;
        }

        long long size = nova_io_size(path);
        if (size < 0)
            size = 0;

        int written = snprintf(entries + used, sizeof(entries) - used, "%s %llu %s\n",
                               hash, (unsigned long long)size, path);
        if (written < 0 || used + (size_t)written >= sizeof(entries))
        {
            set_err(err, err_size, "backup manifest too large");
            return -1;
        }
        used += (size_t)written;
        count++;
    }

    if (backup_manifest_write(entries, count) != 0)
    {
        set_err(err, err_size, "cannot write backup manifest");
        return -1;
    }

    backup_info_refresh(st);
    if (st->backup.present)
        st->backup.verified = nova_backup_verify(st, NULL, NULL, err, err_size) == 0;

    return 0;
}

int nova_backup_verify(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                       char *err, size_t err_size)
{
    if (!st || !st->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    backup_entry_t entries[16];
    int count = 0;
    if (backup_manifest_read(entries, 16, &count) != 0)
    {
        set_err(err, err_size, "no valid backup manifest");
        return -1;
    }

    uint64_t total = 0;
    for (int i = 0; i < count; i++)
        total += entries[i].size;

    uint64_t done = 0;
    if (cb)
        cb(ctx, "verifying backup", 0, total);

    for (int i = 0; i < count; i++)
    {
        char path[NOVA_MAX_PATH];
        snprintf(path, sizeof(path), BACKUP_FILES "/%s", entries[i].path);

        if (!nova_io_exists(path))
        {
            set_err(err, err_size, "backup file missing: %s", entries[i].path);
            return -1;
        }

        long long size = nova_io_size(path);
        if (size < 0 || (uint64_t)size != entries[i].size)
        {
            set_err(err, err_size, "backup size mismatch: %s", entries[i].path);
            return -1;
        }

        char hash[65];
        if (nova_io_sha256_file(path, hash) != 0 || strcmp(hash, entries[i].sha256) != 0)
        {
            set_err(err, err_size, "backup checksum mismatch: %s", entries[i].path);
            return -1;
        }

        done += entries[i].size;
        if (cb)
            cb(ctx, "verifying backup", done, total);
    }

    backup_info_refresh(st);
    if (st->backup.present)
        st->backup.verified = true;
    return 0;
}

int nova_backup_restore(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                        char *err, size_t err_size)
{
    if (!st || !st->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    if (nova_backup_verify(st, cb, ctx, err, err_size) != 0)
        return -1;

    backup_entry_t entries[16];
    int count = 0;
    if (backup_manifest_read(entries, 16, &count) != 0 || count == 0)
    {
        set_err(err, err_size, "backup is empty");
        return -1;
    }

    uint64_t total = 0;
    for (int i = 0; i < count; i++)
        total += entries[i].size;

    uint64_t done = 0;
    if (cb)
        cb(ctx, "restoring backup", 0, total);

    for (int i = 0; i < count; i++)
    {
        char source[NOVA_MAX_PATH];
        snprintf(source, sizeof(source), BACKUP_FILES "/%s", entries[i].path);

        char parent[NOVA_MAX_PATH];
        snprintf(parent, sizeof(parent), "%s", entries[i].path);
        char *slash = strrchr(parent, '/');
        if (slash)
        {
            *slash = '\0';
            if (nova_io_mkdir_p(parent) != 0)
            {
                set_err(err, err_size, "cannot create folders for restore");
                return -1;
            }
        }

        if (nova_io_copy(source, entries[i].path, cb, ctx, "restoring", &done, total) != 0)
        {
            set_err(err, err_size, "failed to restore %s", entries[i].path);
            return -1;
        }

        char hash[65];
        if (nova_io_sha256_file(entries[i].path, hash) != 0 ||
            strcmp(hash, entries[i].sha256) != 0)
        {
            set_err(err, err_size, "restored file failed verification: %s", entries[i].path);
            return -1;
        }
    }

    return 0;
}
