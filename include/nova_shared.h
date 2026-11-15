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
#define NOVA_VERSION_STRING "2.0.1"
#define NOVA_VERSION_MAJOR  2
#define NOVA_VERSION_MINOR  0
#define NOVA_VERSION_PATCH  1

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

/*
 * Automatic cold boot.
 *
 * Modchip/dongle loaders (Spacecraft-NX, HWFLY-NX, Picofly, SD payload
 * capable dongles) load "payload.bin" from the SD root instead of BOOT0.
 * NOVA can occupy that slot; the previous payload is kept for rollback.
 * NOVA writes Nova/state/boot.txt on every start so the installer can show
 * whether a configured cold boot actually reached NOVA.
 *
 * BOOT0/BOOT1 are never written. AutoRCM is only read so NOVA can arm this
 * same slot when the console reaches RCM through an accidental AutoRCM boot.
 */
#define NOVA_AUTOBOOT_PAYLOAD      "payload.bin"
#define NOVA_AUTOBOOT_MAX_SIZE     0x20000u
#define NOVA_AUTOBOOT_BACKUP_DIR   "Nova/backup/autoboot"
#define NOVA_AUTOBOOT_BACKUP_FILE  "Nova/backup/autoboot/payload.bin"
#define NOVA_AUTOBOOT_META_FILE    "Nova/backup/autoboot/meta.txt"
#define NOVA_STATE_DIR             "Nova/state"
#define NOVA_BOOT_MARKER           "Nova/state/boot.txt"
#define NOVA_AUTOBOOT_STATE_FILE   "Nova/state/autoboot.txt"
#define NOVA_AUTOBOOT_MODE         "sd_payload"

/*
 * AutoRCM fallback.
 *
 * NOVA reads the BOOT0 BCT byte that AutoRCM changes, but never writes it.
 * When AutoRCM is active, NOVA prepares payload.bin for the RCM injector and
 * records the result here. A foreign payload is preserved under Nova/backup/
 * autorcm before the slot is replaced.
 */
#define NOVA_AUTORCM_STATE_FILE    "Nova/state/autorcm.txt"
#define NOVA_AUTORCM_BACKUP_DIR    "Nova/backup/autorcm"
#define NOVA_AUTORCM_BACKUP_FILE   "Nova/backup/autorcm/payload.bin"
#define NOVA_AUTORCM_META_FILE     "Nova/backup/autorcm/meta.txt"

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
