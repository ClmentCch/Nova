/*
 * NOVA Installer - automatic cold boot host tests.
 *
 * Exercises the real autoboot module against ./sdmc:/ on the host.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "autoboot.h"
#include "installer.h"
#include "nova_fs.h"
#include "nova_sha256.h"

static int checks;
static int failures;
static int progress_calls;

#define CHECK(cond, what)                                                     \
    do                                                                        \
    {                                                                         \
        checks++;                                                             \
        if (!(cond))                                                          \
        {                                                                     \
            failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, what);           \
        }                                                                     \
    } while (0)

static uint8_t g_blob[NOVA_MAX_PAYLOAD_SIZE];
static uint32_t g_blob_size;

const uint8_t *nova_embedded_payload(uint32_t *size)
{
    if (size)
        *size = g_blob_size;
    return g_blob;
}

static void progress(void *ctx, const char *stage, uint64_t done, uint64_t total)
{
    (void)ctx;
    (void)stage;
    (void)done;
    (void)total;
    progress_calls++;
}

static bool make_dir(const char *rel)
{
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));
    return mkdir(path, 0777) == 0 || nova_io_is_dir(rel);
}

static bool load_payload(void)
{
    FILE *f = fopen("../../data/NOVA.bin", "rb");
    if (!f)
        return false;
    size_t got = fread(g_blob, 1, sizeof(g_blob), f);
    fclose(f);
    if (got < 0x400)
        return false;
    g_blob_size = (uint32_t)got;
    return true;
}

static void write_bytes(const char *path, uint8_t value, size_t size)
{
    uint8_t *data = malloc(size);
    memset(data, value, size);
    nova_io_write(path, data, size);
    free(data);
}

static void make_fake_hekate(const char *path)
{
    uint8_t image[0x2000];
    memset(image, 0, sizeof(image));
    image[3] = 0xEA;
    memcpy(image + 0x118, "ICTC", 4);
    nova_io_write(path, image, sizeof(image));
}

static void prepare_sd(void)
{
    make_dir("");
    make_dir("atmosphere");
    make_dir("bootloader");
    make_dir("bootloader/ini");
    make_dir("bootloader/payloads");

    write_bytes("atmosphere/package3", 0x41, 5000);
    make_fake_hekate("hekate_ctcaer_6.5.5.bin");
}

static void write_boot_marker(uint32_t boots)
{
    char text[96];
    int length = snprintf(text, sizeof(text), "[nova]\nversion=%s\nboots=%lu\n",
                          NOVA_VERSION_STRING, (unsigned long)boots);
    CHECK(nova_io_write(NOVA_BOOT_MARKER, text, (size_t)length) == 0,
          "boot marker written");
}

static void test_autoboot_flow(void)
{
    nova_installer_t st;
    nova_autoboot_t ab;
    char err[192];

    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "installer scan");
    CHECK(nova_transfer(&st, progress, NULL, err, sizeof(err)) == 0, "NOVA transfer");
    CHECK(nova_verify_installation(&st, err, sizeof(err)) == 0, "NOVA installed");

    nova_autoboot_scan(&ab);
    CHECK(ab.sd_ok, "SD available");
    CHECK(!ab.payload_present, "no payload.bin initially");
    CHECK(ab.state == NOVA_AUTO_NOT_CONFIGURED, "not configured initially");
    CHECK(ab.injector_limit_ok, "embedded payload fits the injector limit");

    /* Existing hekate payload must be backed up before being replaced. */
    make_fake_hekate("payload.bin");
    nova_autoboot_scan(&ab);
    CHECK(ab.payload_is_hekate, "hekate payload detected");
    CHECK(!ab.backup_present, "no backup yet");

    char hekate_sha[65];
    snprintf(hekate_sha, sizeof(hekate_sha), "%s", ab.payload_sha);

    progress_calls = 0;
    CHECK(nova_autoboot_enable(&ab, progress, NULL, err, sizeof(err)) == 0,
          "automatic boot enabled");
    CHECK(progress_calls > 0, "enable reports progress");
    CHECK(ab.payload_is_nova, "payload.bin is NOVA");
    CHECK(ab.backup_present, "backup recorded");
    CHECK(strcmp(ab.backup_kind, "hekate") == 0, "backup is hekate");
    CHECK(ab.state == NOVA_AUTO_CONFIGURED, "configured, awaiting boot");
    CHECK(nova_io_exists(NOVA_AUTOBOOT_STATE_FILE), "state file written");

    char expected[65];
    nova_sha256_hex(g_blob, g_blob_size, expected);
    CHECK(strcmp(ab.payload_sha, expected) == 0, "payload.bin hash matches embedded");
    CHECK(ab.payload_size == g_blob_size, "payload.bin size matches embedded");

    /* A later NOVA start must promote the state to active. */
    write_boot_marker(4);
    nova_autoboot_scan(&ab);
    CHECK(ab.state == NOVA_AUTO_ACTIVE, "boot marker promotes to active");
    CHECK(ab.nova_boots == 4, "boot counter read");
    CHECK(ab.configured_boots == 0, "configuration counter preserved");

    /* Another tool replacing payload.bin must be detected. */
    nova_io_copy(NOVA_AUTOBOOT_BACKUP_FILE, NOVA_AUTOBOOT_PAYLOAD, NULL, NULL, NULL, NULL, 0);
    nova_autoboot_scan(&ab);
    CHECK(ab.state == NOVA_AUTO_OVERWRITTEN, "overwrite detected");

    /* Rollback restores the exact previous payload. */
    CHECK(nova_autoboot_disable(&ab, progress, NULL, err, sizeof(err)) == 0,
          "rollback succeeds");
    CHECK(ab.state == NOVA_AUTO_DISABLED, "state disabled after rollback");
    CHECK(ab.payload_is_hekate, "hekate restored");
    CHECK(strcmp(ab.payload_sha, hekate_sha) == 0, "restored hash matches original");

    /* Empty slot case: enable then rollback removes payload.bin. */
    CHECK(nova_io_remove(NOVA_AUTOBOOT_PAYLOAD) == 0, "slot cleared");
    nova_autoboot_scan(&ab);
    CHECK(!ab.payload_present, "slot empty");

    CHECK(nova_autoboot_enable(&ab, progress, NULL, err, sizeof(err)) == 0,
          "enable with empty previous slot");
    CHECK(ab.backup_present, "empty-slot backup record");
    CHECK(strcmp(ab.backup_kind, "none") == 0, "backup kind none");

    CHECK(nova_autoboot_disable(&ab, progress, NULL, err, sizeof(err)) == 0,
          "rollback to empty slot");
    CHECK(!ab.payload_present, "payload.bin removed on rollback");
    CHECK(ab.state == NOVA_AUTO_DISABLED, "empty rollback state");

    /* An unmanaged NOVA slot rolls back to empty, not to NOVA again. */
    CHECK(nova_io_remove_tree("Nova/backup", 0) == 0, "backup removed for unmanaged test");
    CHECK(nova_io_write(NOVA_AUTOBOOT_PAYLOAD, g_blob, g_blob_size) == 0,
          "unmanaged NOVA payload written");
    nova_autoboot_scan(&ab);
    CHECK(ab.payload_is_nova, "unmanaged payload detected as NOVA");
    CHECK(!ab.backup_present, "no rollback record for unmanaged NOVA");
    CHECK(nova_autoboot_enable(&ab, progress, NULL, err, sizeof(err)) == 0,
          "enable over unmanaged NOVA succeeds");
    CHECK(strcmp(ab.backup_kind, "none") == 0, "unmanaged NOVA is not a rollback target");
    CHECK(nova_autoboot_disable(&ab, progress, NULL, err, sizeof(err)) == 0,
          "disable removes unmanaged NOVA slot");
    nova_autoboot_scan(&ab);
    CHECK(!ab.payload_present, "unmanaged NOVA slot left empty");

    /* Oversized payloads are refused. */
    uint32_t saved = g_blob_size;
    g_blob_size = NOVA_AUTOBOOT_MAX_SIZE + 1;
    nova_autoboot_scan(&ab);
    CHECK(!ab.injector_limit_ok, "oversized payload rejected");
    err[0] = '\0';
    CHECK(nova_autoboot_enable(&ab, progress, NULL, err, sizeof(err)) != 0,
          "enable refuses oversized payload");
    g_blob_size = saved;

    /* UNINSTALL NOVA must also clear or restore the cold boot slot. */
    nova_autoboot_scan(&ab);
    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "scan before reinstall");
    CHECK(nova_transfer(&st, progress, NULL, err, sizeof(err)) == 0, "NOVA files restored");
    CHECK(nova_autoboot_enable(&ab, progress, NULL, err, sizeof(err)) == 0,
          "automatic boot enabled before uninstall");
    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "scan before uninstall");
    CHECK(nova_uninstall(&st, progress, NULL, err, sizeof(err)) == 0,
          "uninstall handles automatic boot");
    nova_autoboot_scan(&ab);
    CHECK(!ab.payload_present, "uninstall cleared NOVA payload.bin");
    CHECK(!nova_io_exists(NOVA_PAYLOAD_PATH), "uninstall removed Nova/NOVA.bin");
}

int main(void)
{
    printf("automatic boot tests\n");

    if (!load_payload())
    {
        printf("  FAIL cannot read ../../data/NOVA.bin\n");
        return 1;
    }

    prepare_sd();
    test_autoboot_flow();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
