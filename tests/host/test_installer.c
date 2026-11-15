/*
 * NOVA Installer - host tests for the installation engine.
 *
 * The libnx SD path "sdmc:/" is a relative path on Linux, so the real
 * installer engine, filesystem adapter and shared detection core run
 * unchanged against ./sdmc:/ in this directory.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    image[0] = 0x00;
    image[1] = 0x00;
    image[2] = 0x00;
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

static void test_payload_validation(void)
{
    char detail[64];

    CHECK(nova_payload_valid(g_blob, g_blob_size, detail, sizeof(detail)),
          "real payload accepted");
    CHECK(!nova_payload_valid((const uint8_t *)"NRO0" "not a payload", 17, detail,
                              sizeof(detail)),
          "NRO rejected");
    CHECK(!nova_payload_valid(g_blob, 16, detail, sizeof(detail)), "tiny image rejected");

    uint8_t copy[0x400];
    memset(copy, 0, sizeof(copy));
    copy[3] = 0xE0;
    CHECK(!nova_payload_valid(copy, sizeof(copy), detail, sizeof(detail)),
          "payload without version marker rejected");
}

static void test_install_flow(void)
{
    nova_installer_t st;
    char err[192];

    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "scan succeeds");
    CHECK(st.sd_ok, "SD mounted");
    CHECK(st.payload_ok, "embedded payload valid");
    CHECK(st.hekate_present, "hekate detected");
    CHECK(st.atmosphere_ready, "Atmosphere detected");
    CHECK(!st.installed, "not installed initially");

    char expected[65];
    nova_sha256_hex(g_blob, g_blob_size, expected);
    CHECK(strcmp(st.payload_sha256, expected) == 0, "embedded sha256 reported");

    progress_calls = 0;
    CHECK(nova_transfer(&st, progress, NULL, err, sizeof(err)) == 0, "transfer succeeds");
    CHECK(progress_calls > 0, "transfer reports progress");
    CHECK(nova_io_exists(NOVA_PAYLOAD_PATH), "Nova/NOVA.bin written");
    CHECK(nova_io_exists(NOVA_HEKATE_INSTALL), "bootloader payload written");
    CHECK(nova_io_exists(NOVA_HEKATE_INI), "NOVA.ini written");
    CHECK(nova_io_exists(NOVA_VERSION_FILE), "version file written");
    CHECK(nova_verify_installation(&st, err, sizeof(err)) == 0, "installation verifies");

    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "rescan succeeds");
    CHECK(st.installed, "installed after transfer");
    CHECK(st.boot_entries_present, "boot entries detected");
    CHECK(strcmp(st.installed_sha256, expected) == 0, "installed copy matches embedded");

    /* Backup of the installed files, then verification. */
    CHECK(nova_backup_create(&st, progress, NULL, err, sizeof(err)) == 0, "backup created");
    CHECK(st.backup.present, "backup present");
    CHECK(st.backup.files >= 3, "backup contains NOVA files");
    CHECK(nova_backup_verify(&st, progress, NULL, err, sizeof(err)) == 0, "backup verifies");

    /* Corrupt the installed payload; verification must fail, restore must fix it. */
    write_bytes(NOVA_PAYLOAD_PATH, 0x00, 64);
    CHECK(nova_verify_installation(&st, err, sizeof(err)) != 0,
          "corrupted payload fails verification");
    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "scan with corrupt payload");
    CHECK(!st.installed, "corrupt payload not reported as installed");

    CHECK(nova_backup_restore(&st, progress, NULL, err, sizeof(err)) == 0, "restore succeeds");
    CHECK(nova_verify_installation(&st, err, sizeof(err)) == 0, "restored files verify");

    /* Uninstall without backup must remove NOVA files and keep user data. */
    CHECK(nova_io_remove_tree("Nova/backup", 0) == 0, "backup removed for uninstall test");
    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "scan before uninstall");
    CHECK(nova_uninstall(&st, progress, NULL, err, sizeof(err)) == 0, "uninstall succeeds");
    CHECK(!nova_io_exists(NOVA_PAYLOAD_PATH), "Nova/NOVA.bin removed");
    CHECK(!nova_io_exists(NOVA_HEKATE_INSTALL), "bootloader payload removed");
    CHECK(!nova_io_exists(NOVA_HEKATE_INI), "NOVA.ini removed");
    CHECK(!nova_io_exists(NOVA_VERSION_FILE), "version file removed");
    CHECK(nova_io_exists("atmosphere/package3"), "Atmosphere preserved");
    CHECK(nova_io_exists("hekate_ctcaer_6.5.5.bin"), "hekate preserved");

    /* Missing hekate blocks installation with a clear error. */
    nova_io_remove("hekate_ctcaer_6.5.5.bin");
    CHECK(nova_scan(&st, err, sizeof(err)) == 0, "scan without hekate");
    err[0] = '\0';
    CHECK(nova_install(&st, progress, NULL, err, sizeof(err)) != 0,
          "install refuses without hekate");
    CHECK(strstr(err, "hekate") != NULL, "error mentions hekate");
}

int main(void)
{
    printf("installer engine tests\n");

    if (!load_payload())
    {
        printf("  FAIL cannot read ../../data/NOVA.bin\n");
        return 1;
    }

    prepare_sd();
    test_payload_validation();
    test_install_flow();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
