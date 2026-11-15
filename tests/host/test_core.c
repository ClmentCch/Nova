/*
 * NOVA Boot host tests.
 *
 * These run the exact detection/ini/hekate code that ships in the payload,
 * against a real directory tree, so SD inspection logic is genuinely tested
 * without hardware.
 */
#define _GNU_SOURCE
#include "nova_core.h"
#include "nova_posix_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        if (!(cond))                                                         \
        {                                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                   \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        if (strcmp((a), (b)) != 0)                                           \
        {                                                                    \
            printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,   \
                   (a), (b));                                                \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static const char *g_root = "build/testroot";

static void rm_tree(const char *path)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    if (system(cmd) != 0)
        printf("warning: cleanup failed\n");
}

static void mkdirs(const char *rel)
{
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", g_root, rel);
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", full);
    if (system(cmd) != 0)
        printf("warning: mkdir failed\n");
}

static void write_file(const char *rel, const void *data, size_t size)
{
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", g_root, rel);
    /* make sure parent exists */
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", full);
    char *slash = strrchr(parent, '/');
    if (slash)
    {
        *slash = '\0';
        char cmd[1200];
        snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", parent);
        if (system(cmd) != 0)
            printf("warning: mkdir failed\n");
    }
    FILE *f = fopen(full, "wb");
    if (!f)
    {
        printf("  FAIL cannot write %s\n", full);
        g_failures++;
        return;
    }
    if (size)
        fwrite(data, 1, size, f);
    fclose(f);
}

static void write_text(const char *rel, const char *text)
{
    write_file(rel, text, strlen(text));
}

/* Builds a synthetic hekate payload with the "ICTC" magic at 0x118. */
static void write_hekate(const char *rel, const char *version)
{
    (void)version;
    size_t size = 0x12000;
    unsigned char *buf = calloc(1, size);
    uint32_t magic = NOVA_HEKATE_MAGIC;
    memcpy(buf + NOVA_HEKATE_VER_OFFSET, &magic, 4);
    buf[0x200] = 0xAA;
    write_file(rel, buf, size);
    free(buf);
}

static void write_bad_hekate(const char *rel)
{
    unsigned char buf[0x400];
    memset(buf, 0x11, sizeof(buf));
    write_file(rel, buf, sizeof(buf));
}

static void setup_base(void)
{
    rm_tree(g_root);
    mkdirs("");
}

static void emummc_ini(const char *enabled, const char *path, const char *nintendo)
{
    char text[512];
    snprintf(text, sizeof(text),
             "[emummc]\r\nenabled=%s\r\nsector=0x0\r\nid=0x0000\r\n"
             "path=%s\r\nnintendo_path=%s\r\n",
             enabled, path, nintendo);
    write_text(NOVA_EMUMMC_INI, text);
}

static void test_empty_sd(void)
{
    printf("test_empty_sd\n");
    setup_base();
    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.sd_mounted);
    CHECK(st.atmosphere == NOVA_ST_MISSING);
    CHECK(st.emummc == NOVA_ST_MISSING);
    CHECK(st.hekate == NOVA_ST_MISSING);
    CHECK(!st.can_stock);
    CHECK(!st.can_hekate);
    CHECK(!st.can_emummc);
    CHECK(st.bootcfg == NOVA_ST_MISSING);
}

static void test_atmosphere_incomplete(void)
{
    printf("test_atmosphere_incomplete\n");
    setup_base();
    mkdirs("atmosphere");
    write_text("atmosphere/hashes.ini", "; nothing\n");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.atmosphere == NOVA_ST_ERROR);
    CHECK_STR_EQ(st.atm_detail, "package3 missing");
    CHECK(!st.can_stock);
}

static void test_stock_ready(void)
{
    printf("test_stock_ready\n");
    setup_base();
    mkdirs("atmosphere");
    {
        unsigned char pkg3[8192];
        memset(pkg3, 0x5A, sizeof(pkg3));
        write_file(NOVA_ATMOSPHERE_PKG3, pkg3, sizeof(pkg3));
    }
    write_hekate("hekate_ctcaer_6.5.5.bin", "6.5.5");
    write_text(NOVA_HEKATE_INI, "[NOVA Stock (SysNAND)]\nid=nvstk\n"
                               "[NOVA Atmosph\303\250re (emuMMC)]\nid=nvemu\n");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.atmosphere == NOVA_ST_OK);
    CHECK(st.hekate == NOVA_ST_OK);
    CHECK_STR_EQ(st.hekate_path, "hekate_ctcaer_6.5.5.bin");
    CHECK_STR_EQ(st.hekate_version, "6.5.5");
    CHECK(st.can_stock);
    CHECK(st.can_hekate);
    CHECK(!st.can_emummc);
    CHECK(st.bootcfg == NOVA_ST_OK);
}

static void test_emummc_raw_ready(void)
{
    printf("test_emummc_raw_ready\n");
    setup_base();
    mkdirs("atmosphere");
    {
        unsigned char pkg3[8192];
        memset(pkg3, 0x5A, sizeof(pkg3));
        write_file(NOVA_ATMOSPHERE_PKG3, pkg3, sizeof(pkg3));
    }
    mkdirs("bootloader/sys");
    write_text(NOVA_EMUMMC_KIPM, "kipm");
    mkdirs("emuMMC/RAW1/Nintendo");
    {
        unsigned char sector[4] = {0x00, 0x00, 0x20, 0x00}; /* 0x200000 */
        write_file("emuMMC/RAW1/raw_based", sector, sizeof(sector));
    }
    emummc_ini("1", "emuMMC/RAW1", "emuMMC/RAW1/Nintendo");
    write_hekate("hekate_ctcaer_6.5.5.bin", "6.5.5");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.emummc == NOVA_ST_OK);
    CHECK(st.emu_mode == NOVA_EMU_MODE_RAW);
    CHECK(st.emu_sector == 0x200000);
    CHECK(st.can_emummc);
    CHECK(st.can_stock);
}

static void test_emummc_file_ready(void)
{
    printf("test_emummc_file_ready\n");
    setup_base();
    mkdirs("atmosphere");
    {
        unsigned char pkg3[8192];
        memset(pkg3, 0x5A, sizeof(pkg3));
        write_file(NOVA_ATMOSPHERE_PKG3, pkg3, sizeof(pkg3));
    }
    mkdirs("bootloader/sys");
    write_text(NOVA_EMUMMC_KIPM, "kipm");
    mkdirs("emuMMC/SD00/Nintendo");
    write_text("emuMMC/SD00/file_based", "0");
    emummc_ini("1", "emuMMC/SD00", "emuMMC/SD00/Nintendo");
    write_hekate("hekate_ctcaer_6.5.5.bin", "6.5.5");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.emummc == NOVA_ST_OK);
    CHECK(st.emu_mode == NOVA_EMU_MODE_FILE);
    CHECK(st.can_emummc);
}

static void test_emummc_disabled(void)
{
    printf("test_emummc_disabled\n");
    setup_base();
    mkdirs("emuMMC/RAW1/Nintendo");
    emummc_ini("0", "emuMMC/RAW1", "emuMMC/RAW1/Nintendo");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.emummc == NOVA_ST_MISSING);
    CHECK(!st.can_emummc);
}

static void test_emummc_missing_path(void)
{
    printf("test_emummc_missing_path\n");
    setup_base();
    emummc_ini("1", "emuMMC/RAW9", "emuMMC/RAW9/Nintendo");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.emummc == NOVA_ST_ERROR);
    CHECK(!st.can_emummc);
    CHECK(strstr(st.emu_detail, "path not found") != NULL);
}

static void test_emummc_bad_marker(void)
{
    printf("test_emummc_bad_marker\n");
    setup_base();
    mkdirs("emuMMC/RAW1/Nintendo");
    {
        unsigned char sector[4] = {0, 0, 0, 0};
        write_file("emuMMC/RAW1/raw_based", sector, sizeof(sector));
    }
    emummc_ini("1", "emuMMC/RAW1", "emuMMC/RAW1/Nintendo");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.emummc == NOVA_ST_ERROR);
    CHECK(!st.can_emummc);
    CHECK(strstr(st.emu_detail, "raw_based") != NULL);
}

static void test_emummc_missing_kipm(void)
{
    printf("test_emummc_missing_kipm\n");
    setup_base();
    mkdirs("atmosphere");
    {
        unsigned char pkg3[8192];
        memset(pkg3, 0x5A, sizeof(pkg3));
        write_file(NOVA_ATMOSPHERE_PKG3, pkg3, sizeof(pkg3));
    }
    mkdirs("emuMMC/RAW1/Nintendo");
    {
        unsigned char sector[4] = {0x00, 0x00, 0x20, 0x00};
        write_file("emuMMC/RAW1/raw_based", sector, sizeof(sector));
    }
    emummc_ini("1", "emuMMC/RAW1", "emuMMC/RAW1/Nintendo");
    write_hekate("hekate_ctcaer_6.5.5.bin", "6.5.5");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.emummc == NOVA_ST_WARN);
    CHECK(!st.can_emummc);
    CHECK(strstr(st.emu_detail, "kipm") != NULL);
}

static void test_hekate_invalid(void)
{
    printf("test_hekate_invalid\n");
    setup_base();
    mkdirs("atmosphere");
    {
        unsigned char pkg3[8192];
        memset(pkg3, 0x5A, sizeof(pkg3));
        write_file(NOVA_ATMOSPHERE_PKG3, pkg3, sizeof(pkg3));
    }
    write_bad_hekate("hekate_ctcaer_6.5.5.bin");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.hekate == NOVA_ST_ERROR);
    CHECK(!st.can_hekate);
    CHECK(!st.can_stock);
}

static void test_hekate_in_payload_dir(void)
{
    printf("test_hekate_in_payload_dir\n");
    setup_base();
    write_hekate("bootloader/payloads/hekate_ctcaer_6.5.5.bin", "6.5.5");

    nova_fs_t fs;
    nova_posix_fs_init(&fs, g_root);
    nova_status_t st;
    nova_detect(&fs, &st);

    CHECK(st.hekate == NOVA_ST_OK);
    CHECK_STR_EQ(st.hekate_path, "bootloader/payloads/hekate_ctcaer_6.5.5.bin");
}

static void test_ini_parser(void)
{
    printf("test_ini_parser\n");
    const char *ini =
        "; comment\r\n"
        "[emummc]\r\n"
        "  enabled = 1  \r\n"
        "path=emuMMC/RAW1\r\n"
        "\r\n"
        "[other]\r\n"
        "enabled=0\r\n";
    char out[64];

    CHECK(nova_ini_get(ini, strlen(ini), "emummc", "enabled", out, sizeof(out)));
    CHECK_STR_EQ(out, "1");
    CHECK(nova_ini_get(ini, strlen(ini), "emummc", "path", out, sizeof(out)));
    CHECK_STR_EQ(out, "emuMMC/RAW1");
    CHECK(!nova_ini_get(ini, strlen(ini), "other", "path", out, sizeof(out)));
    CHECK(!nova_ini_get(ini, strlen(ini), "missing", "enabled", out, sizeof(out)));

    const char *entries = "[A]\nid=nvstk\n[B]\nid = nvemu ; trailing\n";
    CHECK(nova_ini_has_id(entries, strlen(entries), "nvstk"));
    CHECK(nova_ini_has_id(entries, strlen(entries), "NVEMU"));
    CHECK(!nova_ini_has_id(entries, strlen(entries), "nvxxx"));
}

static void test_wildcard_and_version(void)
{
    printf("test_wildcard_and_version\n");
    CHECK(nova_wildcard("hekate*.bin", "hekate_ctcaer_6.5.5.bin"));
    CHECK(nova_wildcard("hekate*.bin", "HEKATE.BIN"));
    CHECK(!nova_wildcard("hekate*.bin", "payload.bin"));
    CHECK(nova_wildcard("?v*.txt", "Nv1.txt"));

    char ver[16];
    nova_parse_hekate_version("hekate_ctcaer_6.5.5.bin", ver, sizeof(ver));
    CHECK_STR_EQ(ver, "6.5.5");
    nova_parse_hekate_version("hekate.bin", ver, sizeof(ver));
    CHECK_STR_EQ(ver, "unknown");
}

static void test_hekate_boot_id_patch(void)
{
    printf("test_hekate_boot_id_patch\n");
    unsigned char img[0x1000];
    memset(img, 0, sizeof(img));
    uint32_t magic = NOVA_HEKATE_MAGIC;
    memcpy(img + NOVA_HEKATE_VER_OFFSET, &magic, 4);

    CHECK(!nova_hekate_set_boot_id(img, 0x100, NOVA_ID_STOCK)); /* too small */
    CHECK(nova_hekate_set_boot_id(img, sizeof(img), NOVA_ID_STOCK));

    const unsigned char *cfg = img + NOVA_HEKATE_CFG_OFFSET;
    CHECK(cfg[0] == 0x05); /* autoboot enable | from id */
    CHECK(cfg[1] == 1);
    CHECK_STR_EQ((const char *)cfg + 4, "nvstk");

    CHECK(nova_hekate_set_boot_id(img, sizeof(img), NULL));
    CHECK(cfg[0] == 0);
    CHECK(cfg[4] == 0);

    /* Clearing must not destroy the payload data itself. */
    CHECK(img[0x200] == 0);

    unsigned char bad[0x1000];
    memset(bad, 0, sizeof(bad));
    CHECK(!nova_hekate_set_boot_id(bad, sizeof(bad), NOVA_ID_STOCK));
}

static void test_build_ini(void)
{
    printf("test_build_ini\n");
    char ini[1024];
    uint32_t n = nova_build_hekate_ini(ini, sizeof(ini));
    CHECK(n > 0);
    CHECK(strstr(ini, "id=nvstk") != NULL);
    CHECK(strstr(ini, "id=nvemu") != NULL);
    CHECK(strstr(ini, "emummcforce=1") != NULL);
    CHECK(strstr(ini, "stock=1") != NULL);
    CHECK(strstr(ini, "pkg3=atmosphere/package3") != NULL);

    char small[16];
    CHECK(nova_build_hekate_ini(small, sizeof(small)) == 0);
}

static void test_no_fs(void)
{
    printf("test_no_fs\n");
    nova_status_t st;
    nova_detect(NULL, &st);
    CHECK(!st.sd_mounted);
    CHECK(!st.can_stock && !st.can_hekate && !st.can_emummc);
    CHECK(st.atmosphere == NOVA_ST_MISSING);
}

int main(void)
{
    printf("NOVA core host tests\n");
    printf("====================\n");

    test_empty_sd();
    test_atmosphere_incomplete();
    test_stock_ready();
    test_emummc_raw_ready();
    test_emummc_file_ready();
    test_emummc_disabled();
    test_emummc_missing_path();
    test_emummc_bad_marker();
    test_emummc_missing_kipm();
    test_hekate_invalid();
    test_hekate_in_payload_dir();
    test_ini_parser();
    test_wildcard_and_version();
    test_hekate_boot_id_patch();
    test_build_ini();
    test_no_fs();

    printf("====================\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    rm_tree(g_root);
    return g_failures ? 1 : 0;
}
