/*
 * NOVA Installer - installation engine.
 *
 * The installer manages only NOVA owned files. It never writes BOOT0/BOOT1,
 * the NAND, atmosphere/ or emummc/, and it never rewrites the user's
 * bootloader/hekate_ipl.ini.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_INSTALLER_H
#define NOVA_INSTALLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nova_core.h"

#define NOVA_MAX_PATH 256
#define NOVA_MAX_FILES 8

/* Absolute SD paths (libnx devoptab). */
#define SD_ROOT "sdmc:/"

typedef struct nova_backup_info
{
    bool present;
    int files;
    uint64_t bytes;
    bool verified;
} nova_backup_info_t;

typedef struct nova_state
{
    bool sd_ok;
    uint64_t free_bytes;
    uint64_t total_bytes;

    /* Embedded payload (compiled into the NRO). */
    bool payload_ok;
    uint64_t payload_size;
    char payload_sha256[65];
    char payload_version[16];
    char payload_detail[64];

    /* Installed state on the SD card. */
    bool installed;
    char installed_version[16];
    bool installed_payload_ok;
    char installed_sha256[65];
    bool boot_entries_present;

    bool hekate_present;
    char hekate_path[NOVA_MAX_PATH];
    char hekate_version[16];

    bool atmosphere_ready;
    bool emummc_ready;
    char emummc_detail[72];

    nova_backup_info_t backup;
} nova_installer_t;

typedef void (*nova_progress_fn)(void *ctx, const char *stage,
                                 uint64_t done, uint64_t total);

/* Full SD inspection. Returns 0 on success (SD must be mounted). */
int nova_scan(nova_installer_t *st, char *err, size_t err_size);

/* Copies the embedded payload and NOVA boot entries to the SD card. */
int nova_transfer(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                  char *err, size_t err_size);

/* Compatibility check, backup, transfer and verification. */
int nova_install(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                 char *err, size_t err_size);

/* Restores the previous configuration and removes NOVA installed files. */
int nova_uninstall(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                   char *err, size_t err_size);

/* Backup of the NOVA managed files with a SHA-256 manifest. */
int nova_backup_create(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                       char *err, size_t err_size);
int nova_backup_verify(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                       char *err, size_t err_size);
int nova_backup_restore(nova_installer_t *st, nova_progress_fn cb, void *ctx,
                        char *err, size_t err_size);

/* Recomputes installation status and checks the installed files. */
int nova_verify_installation(nova_installer_t *st, char *err, size_t err_size);

/* Embedded NOVA Boot payload. */
const uint8_t *nova_embedded_payload(uint32_t *size);

/* Validates a NOVA payload image (format, size and version marker). */
bool nova_payload_valid(const uint8_t *image, uint32_t size, char *detail, size_t detail_size);

#endif
