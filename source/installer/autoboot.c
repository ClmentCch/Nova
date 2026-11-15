/*
 * NOVA Installer - automatic cold boot management.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "autoboot.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nova_fs.h"
#include "nova_sha256.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

static void set_err(char *err, size_t err_size, const char *fmt, ...)
{
    if (!err || !err_size)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, err_size, fmt, args);
    va_end(args);
}

static bool read_text(const char *path, char **out, size_t *size_out)
{
    size_t size = 0;
    char *text = (char *)nova_io_read_all(path, &size);
    if (!text)
        return false;
    *out = text;
    if (size_out)
        *size_out = size;
    return true;
}

static bool read_u32_key(const char *text, size_t size, const char *section,
                         const char *key, uint32_t *value)
{
    char buffer[24];
    if (!nova_ini_get(text, (uint32_t)size, section, key, buffer, sizeof(buffer)))
        return false;
    *value = (uint32_t)nova_atoi(buffer);
    return true;
}

const char *nova_autoboot_kind_label(const char *kind)
{
    if (!kind)
        return "unknown";
    if (!strcmp(kind, "nova"))
        return "NOVA payload";
    if (!strcmp(kind, "hekate"))
        return "hekate payload";
    if (!strcmp(kind, "none"))
        return "none";
    return "unrecognized payload";
}

const char *nova_autoboot_state_label(nova_autoboot_state_t state)
{
    switch (state)
    {
    case NOVA_AUTO_CONFIGURED:
        return "CONFIGURED - AWAITING COLD BOOT";
    case NOVA_AUTO_ACTIVE:
        return "ACTIVE - NOVA RAN AFTER CONFIG";
    case NOVA_AUTO_OVERWRITTEN:
        return "OVERWRITTEN - PAYLOAD.BIN CHANGED";
    case NOVA_AUTO_DISABLED:
        return "DISABLED - PREVIOUS BOOT RESTORED";
    default:
        return "NOT CONFIGURED";
    }
}

const char *nova_autoboot_compat_label(const nova_autoboot_t *ab)
{
    if (!ab->sd_ok)
        return "NOT SUPPORTED - SD CARD UNAVAILABLE";
    if (!ab->injector_limit_ok)
        return "NOT SUPPORTED - PAYLOAD TOO LARGE";
    if (ab->state == NOVA_AUTO_ACTIVE)
        return "SUPPORTED - COLD BOOT CONFIRMED";
    return "UNKNOWN - DEPENDS ON INJECTOR";
}

static void detect_hardware(nova_autoboot_t *ab)
{
#ifdef __SWITCH__
    u64 value = 0;

    if (R_SUCCEEDED(splInitialize()))
    {
        if (R_SUCCEEDED(splGetConfig(SplConfigItem_HardwareType, &value)))
        {
            ab->hardware_known = true;
            if (value == 0)
            {
                ab->mariko = false;
                snprintf(ab->soc, sizeof(ab->soc), "Erista (T210)");
            }
            else if (value == 1)
            {
                ab->mariko = true;
                snprintf(ab->soc, sizeof(ab->soc), "Mariko (T210B01)");
            }
            else
            {
                ab->mariko = true;
                snprintf(ab->soc, sizeof(ab->soc), "Mariko/other (id %llu)",
                         (unsigned long long)value);
            }
        }
        splExit();
    }
#endif

    if (!ab->hardware_known)
        snprintf(ab->soc, sizeof(ab->soc), "unknown");
}

static void inspect_payload(nova_autoboot_t *ab)
{
    snprintf(ab->payload_kind, sizeof(ab->payload_kind), "none");
    ab->payload_present = nova_io_exists(NOVA_AUTOBOOT_PAYLOAD);
    if (!ab->payload_present)
        return;

    long long size = nova_io_size(NOVA_AUTOBOOT_PAYLOAD);
    ab->payload_size = size > 0 ? (uint64_t)size : 0;
    nova_io_sha256_file(NOVA_AUTOBOOT_PAYLOAD, ab->payload_sha);

    if (size <= 0 || size > (long long)NOVA_MAX_PAYLOAD_SIZE)
    {
        snprintf(ab->payload_kind, sizeof(ab->payload_kind), "unknown");
        return;
    }

    size_t read = 0;
    uint8_t *image = (uint8_t *)nova_io_read_all(NOVA_AUTOBOOT_PAYLOAD, &read);
    if (!image)
    {
        snprintf(ab->payload_kind, sizeof(ab->payload_kind), "unknown");
        return;
    }

    char detail[64];
    if (nova_payload_valid(image, (uint32_t)read, detail, sizeof(detail)))
    {
        ab->payload_is_nova = true;
        snprintf(ab->payload_kind, sizeof(ab->payload_kind), "nova");
    }
    else if (nova_hekate_set_boot_id(image, (uint32_t)read, NULL))
    {
        ab->payload_is_hekate = true;
        snprintf(ab->payload_kind, sizeof(ab->payload_kind), "hekate");
    }
    else
    {
        snprintf(ab->payload_kind, sizeof(ab->payload_kind), "unknown");
    }

    free(image);
}

static void inspect_backup(nova_autoboot_t *ab)
{
    char *text = NULL;
    size_t size = 0;

    if (!read_text(NOVA_AUTOBOOT_META_FILE, &text, &size))
        return;

    char kind[24] = {0};
    char hash[65] = {0};
    uint32_t stored_size = 0;

    nova_ini_get(text, (uint32_t)size, "autoboot-backup", "kind", kind, sizeof(kind));
    nova_ini_get(text, (uint32_t)size, "autoboot-backup", "sha256", hash, sizeof(hash));
    read_u32_key(text, size, "autoboot-backup", "size", &stored_size);
    free(text);

    if (!kind[0])
        return;

    snprintf(ab->backup_kind, sizeof(ab->backup_kind), "%s", kind);
    snprintf(ab->backup_sha, sizeof(ab->backup_sha), "%s", hash);
    ab->backup_size = stored_size;
    ab->backup_present = true;

    if (strcmp(kind, "none") == 0)
        return;

    if (!nova_io_exists(NOVA_AUTOBOOT_BACKUP_FILE))
    {
        ab->backup_present = false;
        return;
    }

    char actual[65];
    if (nova_io_sha256_file(NOVA_AUTOBOOT_BACKUP_FILE, actual) != 0 ||
        strcmp(actual, hash) != 0)
    {
        ab->backup_present = false;
        return;
    }

    long long backup_size = nova_io_size(NOVA_AUTOBOOT_BACKUP_FILE);
    ab->backup_size = backup_size > 0 ? (uint64_t)backup_size : 0;
}

static void read_boot_marker(nova_autoboot_t *ab)
{
    char *text = NULL;
    size_t size = 0;

    ab->nova_boots = 0;
    if (!read_text(NOVA_BOOT_MARKER, &text, &size))
        return;

    read_u32_key(text, size, "nova", "boots", &ab->nova_boots);
    free(text);
}

static void read_state(nova_autoboot_t *ab)
{
    char *text = NULL;
    size_t size = 0;
    char state[16] = {0};
    char hash[65] = {0};
    char version[16] = {0};
    uint32_t boots_at = 0;

    ab->state = NOVA_AUTO_NOT_CONFIGURED;
    ab->configured_boots = 0;
    ab->state_version[0] = '\0';

    if (!read_text(NOVA_AUTOBOOT_STATE_FILE, &text, &size))
        return;

    nova_ini_get(text, (uint32_t)size, "autoboot", "state", state, sizeof(state));
    nova_ini_get(text, (uint32_t)size, "autoboot", "payload_sha256", hash, sizeof(hash));
    nova_ini_get(text, (uint32_t)size, "autoboot", "version", version, sizeof(version));
    read_u32_key(text, size, "autoboot", "boots_at_configure", &boots_at);
    free(text);

    snprintf(ab->state_version, sizeof(ab->state_version), "%s", version);
    ab->configured_boots = boots_at;

    if (!strcmp(state, "disabled"))
    {
        ab->state = NOVA_AUTO_DISABLED;
        return;
    }

    if (strcmp(state, "pending") != 0)
        return;

    if (!ab->payload_is_nova || strcmp(ab->payload_sha, hash) != 0)
    {
        ab->state = NOVA_AUTO_OVERWRITTEN;
        return;
    }

    ab->state = ab->nova_boots > ab->configured_boots ? NOVA_AUTO_ACTIVE
                                                      : NOVA_AUTO_CONFIGURED;
}

void nova_autoboot_scan(nova_autoboot_t *ab)
{
    memset(ab, 0, sizeof(*ab));
    ab->sd_ok = nova_io_is_dir("");

    detect_hardware(ab);

    uint32_t embedded_size = 0;
    const uint8_t *embedded = nova_embedded_payload(&embedded_size);
    ab->embedded_size = embedded_size;
    ab->injector_limit_ok =
        embedded_size > 0 && embedded_size <= NOVA_AUTOBOOT_MAX_SIZE &&
        nova_payload_valid(embedded, embedded_size, ab->embedded_detail,
                           sizeof(ab->embedded_detail));

    if (!ab->sd_ok)
    {
        snprintf(ab->detail, sizeof(ab->detail), "SD card not mounted");
        return;
    }

    snprintf(ab->payload_kind, sizeof(ab->payload_kind), "none");
    snprintf(ab->backup_kind, sizeof(ab->backup_kind), "none");

    inspect_payload(ab);
    inspect_backup(ab);
    read_boot_marker(ab);
    read_state(ab);

    switch (ab->state)
    {
    case NOVA_AUTO_CONFIGURED:
        snprintf(ab->detail, sizeof(ab->detail),
                 "payload.bin is NOVA; power-cycle to confirm");
        break;
    case NOVA_AUTO_ACTIVE:
        snprintf(ab->detail, sizeof(ab->detail),
                 "NOVA boot counter advanced from %lu to %lu",
                 (unsigned long)ab->configured_boots,
                 (unsigned long)ab->nova_boots);
        break;
    case NOVA_AUTO_OVERWRITTEN:
        snprintf(ab->detail, sizeof(ab->detail),
                 "payload.bin no longer matches the configured NOVA payload");
        break;
    case NOVA_AUTO_DISABLED:
        snprintf(ab->detail, sizeof(ab->detail),
                 "previous cold boot payload restored");
        break;
    default:
        if (!strcmp(ab->payload_kind, "nova"))
            snprintf(ab->detail, sizeof(ab->detail),
                     "NOVA payload present but unmanaged; enable to verify it");
        else if (ab->payload_present)
            snprintf(ab->detail, sizeof(ab->detail),
                     "an existing payload.bin is present; enable will back it up");
        else
            snprintf(ab->detail, sizeof(ab->detail),
                     "requires a modchip or SD-payload capable dongle");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static int write_meta(const nova_autoboot_t *ab, char *err, size_t err_size)
{
    char text[256];
    int length = snprintf(text, sizeof(text),
                          "[autoboot-backup]\nkind=%s\nsha256=%s\nsize=%llu\n",
                          ab->payload_kind, ab->payload_sha,
                          (unsigned long long)ab->payload_size);
    if (length < 0 || length >= (int)sizeof(text))
    {
        set_err(err, err_size, "backup metadata too large");
        return -1;
    }

    if (nova_io_write(NOVA_AUTOBOOT_META_FILE, text, (size_t)length) != 0)
    {
        set_err(err, err_size, "cannot write backup metadata");
        return -1;
    }
    return 0;
}

static int write_state(const char *state, const char *sha, uint32_t boots_at,
                       char *err, size_t err_size)
{
    if (nova_io_mkdir_p(NOVA_STATE_DIR) != 0)
    {
        set_err(err, err_size, "cannot create the state folder");
        return -1;
    }

    char text[256];
    int length = snprintf(text, sizeof(text),
                          "[autoboot]\nstate=%s\nversion=%s\npayload_sha256=%s\n"
                          "boots_at_configure=%lu\nmode=%s\n",
                          state, NOVA_VERSION_STRING, sha,
                          (unsigned long)boots_at, NOVA_AUTOBOOT_MODE);
    if (length < 0 || length >= (int)sizeof(text))
    {
        set_err(err, err_size, "autoboot state too large");
        return -1;
    }

    if (nova_io_write(NOVA_AUTOBOOT_STATE_FILE, text, (size_t)length) != 0)
    {
        set_err(err, err_size, "cannot write autoboot state");
        return -1;
    }
    return 0;
}

static int write_payload_progress(const uint8_t *data, uint32_t size,
                                  nova_progress_fn cb, void *ctx,
                                  uint64_t *done, uint64_t total)
{
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(NOVA_AUTOBOOT_PAYLOAD, path, sizeof(path));

    FILE *f = fopen(path, "wb");
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
            cb(ctx, "writing payload.bin", *done, total);
    }

    if (fflush(f) != 0)
        rc = -1;
    fclose(f);
    return rc;
}

static int restore_previous(nova_autoboot_t *ab, nova_progress_fn cb, void *ctx,
                            char *err, size_t err_size)
{
    if (!ab->backup_present)
    {
        set_err(err, err_size, "no previous payload backup");
        return -1;
    }

    if (!strcmp(ab->backup_kind, "none"))
    {
        if (nova_io_exists(NOVA_AUTOBOOT_PAYLOAD) &&
            nova_io_remove(NOVA_AUTOBOOT_PAYLOAD) != 0)
        {
            set_err(err, err_size, "cannot clear payload.bin");
            return -1;
        }
        return 0;
    }

    uint64_t total = ab->backup_size;
    uint64_t done = 0;
    if (nova_io_copy(NOVA_AUTOBOOT_BACKUP_FILE, NOVA_AUTOBOOT_PAYLOAD, cb, ctx,
                     "restoring previous payload", &done, total) != 0)
    {
        set_err(err, err_size, "cannot restore " NOVA_AUTOBOOT_BACKUP_FILE);
        return -1;
    }

    char hash[65];
    if (nova_io_sha256_file(NOVA_AUTOBOOT_PAYLOAD, hash) != 0 ||
        strcmp(hash, ab->backup_sha) != 0)
    {
        set_err(err, err_size, "restored payload failed checksum verification");
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Enable / disable                                                    */
/* ------------------------------------------------------------------ */
int nova_autoboot_enable(nova_autoboot_t *ab, nova_progress_fn cb, void *ctx,
                         char *err, size_t err_size)
{
    if (!ab->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    if (!ab->injector_limit_ok)
    {
        set_err(err, err_size, "payload not suitable for the injector slot");
        return -1;
    }

    uint32_t payload_size = 0;
    const uint8_t *payload = nova_embedded_payload(&payload_size);

    if (!nova_io_exists(NOVA_PAYLOAD_PATH))
    {
        set_err(err, err_size, "install NOVA first");
        return -1;
    }

    nova_fs_t fs;
    nova_fs_init(&fs);
    nova_status_t detection;
    nova_detect(&fs, &detection);
    if (!detection.can_hekate)
    {
        set_err(err, err_size, "hekate payload missing - NOVA cannot boot");
        return -1;
    }

    bool keep_backup = ab->payload_is_nova && ab->backup_present;
    uint64_t total = payload_size;
    if (!keep_backup && ab->payload_present)
        total += ab->payload_size;

    uint64_t done = 0;
    if (cb)
        cb(ctx, "preparing automatic boot", 0, total);

    if (!keep_backup)
    {
        if (nova_io_exists(NOVA_AUTOBOOT_BACKUP_DIR))
        {
            if (nova_io_remove_tree(NOVA_AUTOBOOT_BACKUP_DIR, 0) != 0)
            {
                set_err(err, err_size, "cannot replace the previous backup");
                return -1;
            }
        }

        if (nova_io_mkdir_p(NOVA_AUTOBOOT_BACKUP_DIR) != 0)
        {
            set_err(err, err_size, "cannot create the rollback folder");
            return -1;
        }

        if (ab->payload_present)
        {
            if (nova_io_copy(NOVA_AUTOBOOT_PAYLOAD, NOVA_AUTOBOOT_BACKUP_FILE,
                             cb, ctx, "backing up payload.bin", &done, total) != 0)
            {
                set_err(err, err_size, "cannot back up payload.bin");
                return -1;
            }

            char backup_hash[65];
            if (nova_io_sha256_file(NOVA_AUTOBOOT_BACKUP_FILE, backup_hash) != 0 ||
                strcmp(backup_hash, ab->payload_sha) != 0)
            {
                set_err(err, err_size, "payload.bin backup failed verification");
                return -1;
            }
        }

        if (write_meta(ab, err, err_size) != 0)
            return -1;
    }

    if (write_payload_progress(payload, payload_size, cb, ctx, &done, total) != 0)
    {
        char restore_err[96];
        if (ab->backup_present)
            restore_previous(ab, NULL, NULL, restore_err, sizeof(restore_err));
        set_err(err, err_size, "cannot write payload.bin");
        return -1;
    }

    char expected[65];
    nova_sha256_hex(payload, payload_size, expected);

    char actual[65];
    size_t read = 0;
    uint8_t *readback = (uint8_t *)nova_io_read_all(NOVA_AUTOBOOT_PAYLOAD, &read);
    char detail[64];
    bool verified = nova_io_sha256_file(NOVA_AUTOBOOT_PAYLOAD, actual) == 0 &&
                    strcmp(actual, expected) == 0 && readback &&
                    nova_payload_valid(readback, (uint32_t)read, detail, sizeof(detail));
    free(readback);

    if (!verified)
    {
        char restore_err[96];
        if (ab->backup_present)
            restore_previous(ab, NULL, NULL, restore_err, sizeof(restore_err));
        set_err(err, err_size, "payload.bin failed post-write verification");
        return -1;
    }

    uint32_t boots_at = 0;
    char *marker = NULL;
    size_t marker_size = 0;
    if (read_text(NOVA_BOOT_MARKER, &marker, &marker_size))
    {
        read_u32_key(marker, marker_size, "nova", "boots", &boots_at);
        free(marker);
    }

    if (write_state("pending", expected, boots_at, err, err_size) != 0)
        return -1;

    nova_autoboot_scan(ab);
    if (ab->state == NOVA_AUTO_OVERWRITTEN || !ab->payload_is_nova)
    {
        set_err(err, err_size, "configuration verification failed");
        return -1;
    }

    if (cb)
        cb(ctx, "automatic boot configured", total, total);
    return 0;
}

int nova_autoboot_disable(nova_autoboot_t *ab, nova_progress_fn cb, void *ctx,
                          char *err, size_t err_size)
{
    if (!ab->sd_ok)
    {
        set_err(err, err_size, "SD card not available");
        return -1;
    }

    if (ab->backup_present)
    {
        if (restore_previous(ab, cb, ctx, err, err_size) != 0)
            return -1;
    }
    else
    {
        if (!ab->payload_is_nova)
        {
            set_err(err, err_size, "no NOVA automatic boot configuration found");
            return -1;
        }

        if (nova_io_exists(NOVA_AUTOBOOT_PAYLOAD) &&
            nova_io_remove(NOVA_AUTOBOOT_PAYLOAD) != 0)
        {
            set_err(err, err_size, "cannot remove payload.bin");
            return -1;
        }
    }

    uint32_t boots_at = 0;
    char *marker = NULL;
    size_t marker_size = 0;
    if (read_text(NOVA_BOOT_MARKER, &marker, &marker_size))
    {
        read_u32_key(marker, marker_size, "nova", "boots", &boots_at);
        free(marker);
    }

    if (write_state("disabled", "none", boots_at, err, err_size) != 0)
        return -1;

    nova_autoboot_scan(ab);
    if (ab->state != NOVA_AUTO_DISABLED)
    {
        set_err(err, err_size, "rollback verification failed");
        return -1;
    }

    return 0;
}
