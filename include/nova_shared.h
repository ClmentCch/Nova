/*
 * NOVA - shared layout and interface contract.
 *
 * This header is kept byte-identical in the Nova-boot and Nova-installer
 * branches. Both applications must agree on every path, identifier and
 * version string below.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_SHARED_H
#define NOVA_SHARED_H

/* Release version. Also written to Nova/version.txt by the installer. */
#define NOVA_VERSION_STRING "1.0.0"
#define NOVA_VERSION_MAJOR  1
#define NOVA_VERSION_MINOR  0
#define NOVA_VERSION_PATCH  0

/* Installer applet identity. */
#define NOVA_INSTALLER_NAME    "NOVA Installer"
#define NOVA_INSTALLER_NRO     "Nova-Installer.nro"
#define NOVA_INSTALLER_PATH    "switch/Nova-Installer.nro"
#define NOVA_INSTALLER_VERSION NOVA_VERSION_STRING

/*
 * SD card layout owned by NOVA.
 *
 *   sdmc:/Nova/                       NOVA boot resources
 *   sdmc:/Nova/NOVA.bin               compiled boot payload (canonical copy)
 *   sdmc:/Nova/version.txt            installed version
 *   sdmc:/Nova/config/nova.ini        boot configuration
 *   sdmc:/Nova/themes/                theme resources
 *   sdmc:/bootloader/payloads/NOVA.bin payload copy loadable from hekate
 *   sdmc:/bootloader/ini/NOVA.ini     NOVA owned hekate boot entries
 *
 * NOVA never writes inside atmosphere/ or emummc/, and never writes the
 * user's bootloader/hekate_ipl.ini.
 */
#define NOVA_ROOT_DIR           "Nova"
#define NOVA_PAYLOAD_NAME       "NOVA.bin"
#define NOVA_PAYLOAD_PATH       "Nova/NOVA.bin"
#define NOVA_VERSION_FILE       "Nova/version.txt"
#define NOVA_CONFIG_DIR         "Nova/config"
#define NOVA_CONFIG_FILE        "Nova/config/nova.ini"
#define NOVA_THEME_DIR          "Nova/themes"
#define NOVA_BACKUP_DIR         "Nova/backup"
#define NOVA_HEKATE_INSTALL     "bootloader/payloads/NOVA.bin"
#define NOVA_HEKATE_INI         "bootloader/ini/NOVA.ini"
#define NOVA_HEKATE_INI_MAIN    "bootloader/hekate_ipl.ini"
#define NOVA_HEKATE_SYS_KIPM    "bootloader/sys/emummc.kipm"

/* Atmosphère / emuMMC paths inspected for detection (read-only). */
#define NOVA_ATMOSPHERE_DIR     "atmosphere"
#define NOVA_ATMOSPHERE_PKG3    "atmosphere/package3"
#define NOVA_ATMOSPHERE_LEGACY  "atmosphere/fusee-secondary.bin"
#define NOVA_EMUMMC_DIR         "emummc"
#define NOVA_EMUMMC_INI         "emummc/emummc.ini"
#define NOVA_EMUMMC_KIPM        "bootloader/sys/emummc.kipm"

/* Hekate payload lookup order. */
#define NOVA_HEKATE_ROOT_PATTERN     "hekate*.bin"
#define NOVA_HEKATE_PAYLOAD_DIR      "bootloader/payloads"
#define NOVA_HEKATE_PAYLOAD_PATTERN  "hekate*.bin"

/*
 * Boot entry identifiers used with hekate's documented "boot from ID"
 * mechanism (boot_cfg bit 2). Max 7 characters, NUL terminated.
 */
#define NOVA_ID_STOCK   "nvstk"
#define NOVA_ID_EMUMMC  "nvemu"

/*
 * hekate's boot configuration storage inside a payload image
 * (documented in hekate's README "Payload storage").
 *   payload[0x94] boot_cfg, autoboot, autoboot_list, extra_cfg
 *   payload[0x98] xt_str[0x80] (id at 0x98)
 *   payload[0x118] ipl_ver_meta magic "ICTC"
 */
#define NOVA_HEKATE_CFG_OFFSET 0x94
#define NOVA_HEKATE_VER_OFFSET 0x118
#define NOVA_HEKATE_MAGIC      0x43544349u /* "ICTC" little endian */
#define NOVA_HEKATE_CFG_SIZE   0x84

/* hekate refuses to chainload payloads larger than this. */
#define NOVA_MAX_PAYLOAD_SIZE  0x30000u

/* EmuMMC marker files created by hekate. */
#define NOVA_EMUMMC_RAW_MARKER  "/raw_based"
#define NOVA_EMUMMC_FILE_MARKER "/file_based"
#define NOVA_EMUMMC_NINTENDO    "/Nintendo"

#endif /* NOVA_SHARED_H */
