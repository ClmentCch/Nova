/*
 * NOVA Installer - Nintendo Switch homebrew UI.
 *
 * Console based dashboard and action menus. Every privileged operation goes
 * through the installer engine in source/installer/installer.c.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "autoboot.h"
#include "installer.h"
#include "nova_fs.h"
#include "nova_shared.h"

#define COLOR_RESET   CONSOLE_RESET
#define COLOR_TITLE   CONSOLE_MAGENTA
#define COLOR_ACCENT  CONSOLE_CYAN
#define COLOR_OK      CONSOLE_GREEN
#define COLOR_WARN    CONSOLE_YELLOW
#define COLOR_ERR     CONSOLE_RED
#define COLOR_TEXT    CONSOLE_WHITE

static PadState g_pad;
static nova_installer_t g_state;
static nova_autoboot_t g_auto;
static char g_error[192];
static int g_selected;

static int g_last_percent = -1;
static char g_last_stage[48];

static void sleep_ms(u32 ms)
{
    svcSleepThread((u64)ms * 1000000ULL);
}

static void format_bytes(u64 bytes, char *out, size_t out_size)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        snprintf(out, out_size, "%llu.%llu GB",
                 (unsigned long long)(bytes / (1024ULL * 1024ULL * 1024ULL)),
                 (unsigned long long)((bytes / (1024ULL * 1024ULL * 1024ULL / 10))) % 10);
    else if (bytes >= 1024ULL * 1024ULL)
        snprintf(out, out_size, "%llu.%llu MB",
                 (unsigned long long)(bytes / (1024ULL * 1024ULL)),
                 (unsigned long long)((bytes / (1024ULL * 1024ULL / 10))) % 10);
    else if (bytes >= 1024ULL)
        snprintf(out, out_size, "%llu.%llu KB",
                 (unsigned long long)(bytes / 1024ULL),
                 (unsigned long long)((bytes / 102ULL)) % 10);
    else
        snprintf(out, out_size, "%llu B", (unsigned long long)bytes);
}

static void rule(void)
{
    printf("==============================================================================\n");
}

static void header(const char *screen)
{
    printf(COLOR_TITLE "NOVA" COLOR_RESET "  " COLOR_ACCENT "INSTALLER" COLOR_RESET
           "   " COLOR_TEXT "%s" COLOR_RESET "\n", screen);
    rule();
}

static void menu_item(const char *text, bool selected)
{
    if (selected)
        printf("  " CONSOLE_ESC(7m) COLOR_ACCENT " > %-70s" COLOR_RESET " \n", text);
    else
        printf("    %-70s \n", text);
}

static void draw_main(void)
{
    char buffer[64];
    char buffer2[64];

    header("CUSTOM BOOT SYSTEM INSTALLER");

    printf("  %-24s ", "INSTALLATION");
    if (g_state.installed)
        printf(COLOR_OK "INSTALLED  v%s" COLOR_RESET, g_state.installed_version);
    else
        printf(COLOR_WARN "NOT INSTALLED" COLOR_RESET);
    printf("    %-14s " COLOR_TEXT "v%s" COLOR_RESET "\n", "VERSION",
           NOVA_VERSION_STRING);

    format_bytes(g_state.total_bytes - g_state.free_bytes, buffer, sizeof(buffer));
    format_bytes(g_state.free_bytes, buffer2, sizeof(buffer2));
    printf("  %-24s " COLOR_OK "READY" COLOR_RESET "   %s free of %s\n",
           "SD CARD", buffer2, buffer);

    printf("  %-24s ", "EMBEDDED PAYLOAD");
    if (g_state.payload_ok)
        printf(COLOR_OK "VALID" COLOR_RESET "   %lu KB   sha256 %.16s\n",
               (unsigned long)(g_state.payload_size / 1024), g_state.payload_sha256);
    else
        printf(COLOR_ERR "%s" COLOR_RESET "\n", g_state.payload_detail);

    printf("  %-24s ", "INSTALLED FILES");
    if (g_state.installed)
    {
        bool match = strcmp(g_state.installed_sha256, g_state.payload_sha256) == 0;
        printf(match ? COLOR_OK "VERIFIED" COLOR_RESET : COLOR_WARN "DIFFERENT BUILD" COLOR_RESET);
        printf("   %s\n", g_state.boot_entries_present ? "boot entries ready"
                                                       : "boot entries missing");
    }
    else
    {
        printf(COLOR_WARN "NOT INSTALLED" COLOR_RESET "\n");
    }

    printf("  %-24s ", "HEKATE");
    if (g_state.hekate_present)
        printf(COLOR_OK "FOUND" COLOR_RESET "   %s\n", g_state.hekate_path);
    else
        printf(COLOR_ERR "MISSING" COLOR_RESET "   put hekate_ctcaer_x.x.x.bin in the SD root\n");

    printf("  %-24s ", "ATMOSPHERE");
    printf(g_state.atmosphere_ready ? COLOR_OK "READY" COLOR_RESET
                                    : COLOR_WARN "NOT READY" COLOR_RESET);
    printf("    %-10s ", "emuMMC");
    printf(g_state.emummc_ready ? COLOR_OK "READY" COLOR_RESET
                                : COLOR_WARN "DISABLED" COLOR_RESET);
    printf("   %s\n", g_state.emummc_detail);

    printf("  %-24s ", "BACKUP");
    if (g_state.backup.present && g_state.backup.files > 0)
    {
        format_bytes(g_state.backup.bytes, buffer, sizeof(buffer));
        printf(COLOR_OK "%d FILES" COLOR_RESET "   %s   %s\n", g_state.backup.files, buffer,
               g_state.backup.verified ? "verified" : "not verified");
    }
    else
    {
        printf(COLOR_WARN "NONE" COLOR_RESET "\n");
    }

    rule();
    printf("  " COLOR_TEXT "MENU" COLOR_RESET "\n\n");

    menu_item("TRANSFER FILES", g_selected == 0);
    menu_item("INSTALL NOVA", g_selected == 1);
    menu_item("UNINSTALL NOVA", g_selected == 2);
    menu_item("BACKUP & RESTORE", g_selected == 3);
    menu_item("AUTOMATIC BOOT", g_selected == 4);
    menu_item("EXIT", g_selected == 5);

    printf("\n");
    rule();
    printf("  " COLOR_TEXT "D-PAD" COLOR_RESET " MOVE    " COLOR_TEXT "A" COLOR_RESET
           " SELECT    " COLOR_TEXT "B" COLOR_RESET " EXIT\n");
}

static void draw_backup(void)
{
    char buffer[64];

    header("BACKUP & RESTORE");

    printf("  %-24s ", "BACKUP STATUS");
    if (g_state.backup.present && g_state.backup.files > 0)
    {
        format_bytes(g_state.backup.bytes, buffer, sizeof(buffer));
        printf(COLOR_OK "%d FILES" COLOR_RESET "   %s   %s\n", g_state.backup.files, buffer,
               g_state.backup.verified ? "verified" : "not verified");
    }
    else
    {
        printf(COLOR_WARN "NONE" COLOR_RESET "\n");
    }

    printf("  %-24s %s\n", "BACKUP LOCATION", "Nova/backup/latest/");
    printf("  %-24s %s\n", "PROTECTED DATA", "atmosphere/  emummc/  Nintendo/");
    printf("  %-24s %s\n", "HEKATE INI", "never modified");
    rule();
    printf("  " COLOR_TEXT "MENU" COLOR_RESET "\n\n");

    menu_item("CREATE BACKUP", g_selected == 0);
    menu_item("VERIFY BACKUP", g_selected == 1);
    menu_item("RESTORE BACKUP", g_selected == 2);
    menu_item("BACK", g_selected == 3);

    printf("\n");
    rule();
    printf("  " COLOR_TEXT "A" COLOR_RESET " SELECT    " COLOR_TEXT "B" COLOR_RESET
           " BACK\n");
}

static void draw_auto(void)
{
    header("AUTOMATIC COLD BOOT");

    printf("  %-24s %s\n", "CONSOLE", g_auto.soc);
    printf("  %-24s %s\n", "COMPATIBILITY", nova_autoboot_compat_label(&g_auto));

    printf("  %-24s ", "AUTORCM LAST BOOT");
    if (!g_auto.autorcm_seen)
    {
        printf("not checked yet\n");
    }
    else
    {
        if (!strcmp(g_auto.autorcm_state, "enabled"))
            printf(COLOR_WARN "ENABLED" COLOR_RESET);
        else if (!strcmp(g_auto.autorcm_state, "disabled"))
            printf(COLOR_OK "DISABLED" COLOR_RESET);
        else if (!strcmp(g_auto.autorcm_state, "unsupported"))
            printf("UNSUPPORTED");
        else
            printf(COLOR_WARN "UNKNOWN" COLOR_RESET);

        if (g_auto.autorcm_fallback[0])
            printf("   fallback %s", g_auto.autorcm_fallback);
        printf("   boots %lu\n", (unsigned long)g_auto.autorcm_boots);
    }

    printf("  %-24s ", "SD PAYLOAD SLOT");
    if (g_auto.payload_present)
        printf("%s  %llu B\n", nova_autoboot_kind_label(g_auto.payload_kind),
               (unsigned long long)g_auto.payload_size);
    else
        printf("payload.bin missing\n");

    printf("  %-24s ", "COLD BOOT STATE");
    if (g_auto.state == NOVA_AUTO_ACTIVE)
        printf(COLOR_OK "%s" COLOR_RESET "\n", nova_autoboot_state_label(g_auto.state));
    else if (g_auto.state == NOVA_AUTO_CONFIGURED)
        printf(COLOR_WARN "%s" COLOR_RESET "\n", nova_autoboot_state_label(g_auto.state));
    else if (g_auto.state == NOVA_AUTO_OVERWRITTEN)
        printf(COLOR_ERR "%s" COLOR_RESET "\n", nova_autoboot_state_label(g_auto.state));
    else
        printf("%s\n", nova_autoboot_state_label(g_auto.state));

    if (g_auto.state == NOVA_AUTO_CONFIGURED || g_auto.state == NOVA_AUTO_ACTIVE)
        printf("  %-24s %lu (configured at %lu)\n", "NOVA BOOTS",
               (unsigned long)g_auto.nova_boots, (unsigned long)g_auto.configured_boots);

    printf("  %-24s ", "ROLLBACK");
    if (g_auto.backup_present)
        printf("%s%s\n", nova_autoboot_kind_label(g_auto.backup_kind),
               strcmp(g_auto.backup_kind, "none") ? " (verified)" : " (slot was empty)");
    else
        printf("none\n");

    printf("  %-24s %s\n", "BOOT0 / NAND", "read only, never written");
    rule();
    printf("  %s\n", g_auto.detail);
    rule();
    printf("  " COLOR_TEXT "MENU" COLOR_RESET "\n\n");

    menu_item("ENABLE AUTOMATIC BOOT", g_selected == 0);
    menu_item("RESTORE PREVIOUS BOOT", g_selected == 1);
    menu_item("RE-CHECK STATUS", g_selected == 2);
    menu_item("BACK", g_selected == 3);

    printf("\n");
    rule();
    printf("  " COLOR_TEXT "A" COLOR_RESET " SELECT    " COLOR_TEXT "B" COLOR_RESET
           " BACK\n");
}

static void draw_progress(const char *stage, u64 done, u64 total)
{
    int percent = total ? (int)((done * 100ULL) / total) : 0;
    if (percent > 100)
        percent = 100;

    if (percent == g_last_percent && strcmp(stage, g_last_stage) == 0)
        return;

    g_last_percent = percent;
    snprintf(g_last_stage, sizeof(g_last_stage), "%s", stage);

    consoleClear();
    header(stage);
    printf("\n\n");

    const int width = 54;
    int filled = (percent * width) / 100;
    printf("  [");
    for (int i = 0; i < width; i++)
        putchar(i < filled ? '#' : '-');
    printf("]  " COLOR_ACCENT "%3d%%" COLOR_RESET "\n\n", percent);

    char done_text[32], total_text[32];
    format_bytes(done, done_text, sizeof(done_text));
    format_bytes(total, total_text, sizeof(total_text));
    printf("  %s of %s\n", done_text, total_text);
    printf("  %llu / %llu bytes\n\n", (unsigned long long)done, (unsigned long long)total);
    printf("  " COLOR_TEXT "Do not power off the console." COLOR_RESET "\n");
}

static void progress_cb(void *ctx, const char *stage, u64 done, u64 total)
{
    (void)ctx;
    draw_progress(stage, done, total);
}

static void wait_for_continue(void)
{
    while (appletMainLoop())
    {
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
            return;
        sleep_ms(16);
    }
}

static void show_result(const char *title, bool success, const char *line1, const char *line2)
{
    consoleClear();
    header(title);
    printf("\n");
    printf("  %s\n\n", success ? COLOR_OK "SUCCESS" COLOR_RESET : COLOR_ERR "FAILED" COLOR_RESET);
    if (line1 && line1[0])
        printf("  %s\n", line1);
    if (line2 && line2[0])
        printf("  %s\n", line2);
    printf("\n");
    rule();
    printf("  Press " COLOR_TEXT "A" COLOR_RESET " or " COLOR_TEXT "B" COLOR_RESET
           " to continue.\n");
    wait_for_continue();
}

static bool confirm_dialog(const char *title, const char *question)
{
    consoleClear();
    header(title);
    printf("\n  %s\n\n", question);
    printf("  " COLOR_TEXT "A" COLOR_RESET " CONFIRM    " COLOR_TEXT "B" COLOR_RESET
           " CANCEL\n");

    while (appletMainLoop())
    {
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & HidNpadButton_A)
            return true;
        if (down & (HidNpadButton_B | HidNpadButton_Plus))
            return false;
        sleep_ms(16);
    }
    return false;
}

static void begin_progress(const char *stage)
{
    g_last_percent = -1;
    g_last_stage[0] = '\0';
    consoleClear();
    draw_progress(stage, 0, 1);
}

static void run_transfer(void)
{
    begin_progress("Preparing transfer");
    if (nova_transfer(&g_state, progress_cb, NULL, g_error, sizeof(g_error)) != 0)
    {
        show_result("TRANSFER FILES", false, g_error, NULL);
        return;
    }

    if (nova_verify_installation(&g_state, g_error, sizeof(g_error)) != 0)
    {
        show_result("TRANSFER FILES", false, "Files written but verification failed.", g_error);
        return;
    }

    char line[144];
    snprintf(line, sizeof(line),
             "%lu KB copied to Nova/NOVA.bin and bootloader/payloads/NOVA.bin.",
             (unsigned long)(g_state.payload_size / 1024));
    show_result("TRANSFER FILES", true, line, "SHA-256 checksums verified.");
}

static void run_install(void)
{
    begin_progress("Checking compatibility");
    if (nova_install(&g_state, progress_cb, NULL, g_error, sizeof(g_error)) != 0)
    {
        show_result("INSTALL NOVA", false, g_error, NULL);
        return;
    }

    show_result("INSTALL NOVA", true, "NOVA Boot installed and verified.",
                "Pick NOVA in hekate, or chainload Nova/NOVA.bin.");
}

static void run_uninstall(void)
{
    if (!confirm_dialog("UNINSTALL NOVA",
                        "Remove NOVA files and restore the previous configuration?"))
        return;

    begin_progress("Preparing uninstall");
    if (nova_uninstall(&g_state, progress_cb, NULL, g_error, sizeof(g_error)) != 0)
    {
        show_result("UNINSTALL NOVA", false, g_error, NULL);
        return;
    }

    show_result("UNINSTALL NOVA", true, "NOVA files removed.",
                "Atmosphere, emuMMC and Nintendo files were not modified.");
}

static void run_auto_enable(void)
{
    if (!confirm_dialog("ENABLE AUTOMATIC BOOT",
                        "Set SD:/payload.bin to NOVA? The current payload is backed up first."))
        return;

    begin_progress("Checking automatic boot");
    if (nova_autoboot_enable(&g_auto, progress_cb, NULL, g_error, sizeof(g_error)) != 0)
    {
        show_result("ENABLE AUTOMATIC BOOT", false, g_error, NULL);
        return;
    }

    show_result("ENABLE AUTOMATIC BOOT", true,
                "payload.bin set to NOVA and SHA-256 verified.",
                "Power off and on. NOVA must appear before it is confirmed.");
}

static void run_auto_disable(void)
{
    char previous[24];
    snprintf(previous, sizeof(previous), "%s", g_auto.backup_kind);

    if (!confirm_dialog("RESTORE PREVIOUS BOOT",
                        "Restore the previous payload.bin and remove NOVA from the cold boot slot?"))
        return;

    begin_progress("Restoring previous boot");
    if (nova_autoboot_disable(&g_auto, progress_cb, NULL, g_error, sizeof(g_error)) != 0)
    {
        show_result("RESTORE PREVIOUS BOOT", false, g_error, NULL);
        return;
    }

    char line[128];
    if (!strcmp(previous, "none"))
        snprintf(line, sizeof(line), "payload.bin removed; the injector slot is empty.");
    else
        snprintf(line, sizeof(line), "Previous payload (%s) restored and verified.",
                 nova_autoboot_kind_label(previous));
    show_result("RESTORE PREVIOUS BOOT", true, line, NULL);
}

static void run_auto_recheck(void)
{
    nova_autoboot_scan(&g_auto);
    show_result("AUTOMATIC BOOT", true, nova_autoboot_state_label(g_auto.state), g_auto.detail);
}

static void run_backup_action(int action)
{
    begin_progress("Working");

    switch (action)
    {
    case 0:
        if (nova_backup_create(&g_state, progress_cb, NULL, g_error, sizeof(g_error)) == 0)
            show_result("CREATE BACKUP", true, "Backup created and checksummed.",
                        "Nova/backup/latest/manifest.txt");
        else
            show_result("CREATE BACKUP", false, g_error, NULL);
        break;

    case 1:
        if (nova_backup_verify(&g_state, progress_cb, NULL, g_error, sizeof(g_error)) == 0)
            show_result("VERIFY BACKUP", true, "All backup files match the manifest.", NULL);
        else
            show_result("VERIFY BACKUP", false, g_error, NULL);
        break;

    case 2:
        if (nova_backup_restore(&g_state, progress_cb, NULL, g_error, sizeof(g_error)) == 0)
            show_result("RESTORE BACKUP", true, "Previous configuration restored.",
                        "Every restored file was verified.");
        else
            show_result("RESTORE BACKUP", false, g_error, NULL);
        break;

    default:
        break;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    padInitializeDefault(&g_pad);

    int view = 0; /* 0 = main, 1 = backup, 2 = automatic boot */
    int selected = 0;
    int backup_selected = 0;
    int auto_selected = 0;

    while (appletMainLoop())
    {
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);

        if (nova_scan(&g_state, g_error, sizeof(g_error)) != 0)
        {
            consoleClear();
            header("SD CARD");
            printf("\n  " COLOR_ERR "SD card not available." COLOR_RESET "\n\n");
            printf("  %s\n\n", g_error);
            printf("  Insert the SD card, then press " COLOR_TEXT "A" COLOR_RESET
                   " to retry.\n\n");
            printf("  " COLOR_TEXT "+" COLOR_RESET " exit\n");

            if (down & HidNpadButton_Plus)
                break;
            sleep_ms(50);
            continue;
        }

        if (view == 0)
            g_selected = selected;
        else if (view == 1)
            g_selected = backup_selected;
        else
            g_selected = auto_selected;

        consoleClear();
        if (view == 0)
            draw_main();
        else if (view == 1)
            draw_backup();
        else
            draw_auto();

        bool up = (down & HidNpadButton_AnyUp) != 0;
        bool dn = (down & HidNpadButton_AnyDown) != 0;
        bool confirm = (down & HidNpadButton_A) != 0;
        bool back = (down & (HidNpadButton_B | HidNpadButton_Plus)) != 0;

        if (view == 0)
        {
            if (up)
                selected = (selected + 5) % 6;
            if (dn)
                selected = (selected + 1) % 6;

            if (back)
                break;

            if (confirm)
            {
                switch (selected)
                {
                case 0:
                    run_transfer();
                    break;
                case 1:
                    run_install();
                    break;
                case 2:
                    run_uninstall();
                    break;
                case 3:
                    view = 1;
                    backup_selected = 0;
                    break;
                case 4:
                    nova_autoboot_scan(&g_auto);
                    view = 2;
                    auto_selected = 0;
                    break;
                default:
                    goto done;
                }
            }
        }
        else if (view == 1)
        {
            if (up)
                backup_selected = (backup_selected + 3) % 4;
            if (dn)
                backup_selected = (backup_selected + 1) % 4;

            if (back)
            {
                view = 0;
                selected = 3;
            }
            else if (confirm)
            {
                if (backup_selected == 3)
                {
                    view = 0;
                    selected = 3;
                }
                else
                {
                    run_backup_action(backup_selected);
                }
            }
        }
        else
        {
            if (up)
                auto_selected = (auto_selected + 3) % 4;
            if (dn)
                auto_selected = (auto_selected + 1) % 4;

            if (back)
            {
                view = 0;
                selected = 4;
            }
            else if (confirm)
            {
                switch (auto_selected)
                {
                case 0:
                    run_auto_enable();
                    break;
                case 1:
                    run_auto_disable();
                    break;
                case 2:
                    run_auto_recheck();
                    break;
                default:
                    view = 0;
                    selected = 4;
                    break;
                }
            }
        }

        sleep_ms(16);
    }

done:
    consoleExit(NULL);
    return 0;
}
