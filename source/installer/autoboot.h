/*
 * NOVA Installer - automatic cold boot management.
 *
 * Automatic startup is provided by the external loader (modchip sdloader or
 * an SD-payload capable dongle) that executes payload.bin from the SD root
 * instead of BOOT0. This module prepares, verifies and rolls back that slot.
 * It never touches BOOT0/BOOT1, the NAND or AutoRCM.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_AUTOBOOT_H
#define NOVA_AUTOBOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "installer.h"

/* Modchip sdloader rejects payloads larger than 128 KiB. */
#define NOVA_AUTOBOOT_MAX_SIZE 0x20000u

typedef enum nova_autoboot_state
{
    NOVA_AUTO_NOT_CONFIGURED = 0,
    NOVA_AUTO_CONFIGURED,   /* payload verified, waiting for a boot marker */
    NOVA_AUTO_ACTIVE,       /* NOVA started after the configuration */
    NOVA_AUTO_OVERWRITTEN,  /* payload.bin no longer the configured NOVA */
    NOVA_AUTO_DISABLED,     /* previous payload restored / slot cleared */
} nova_autoboot_state_t;

typedef struct nova_autoboot
{
    bool sd_ok;

    /* Best effort hardware information (unknown when spl: is unavailable). */
    bool hardware_known;
    bool mariko;
    char soc[40];

    /* Embedded NOVA payload suitability for the injector slot. */
    bool injector_limit_ok;
    uint32_t embedded_size;
    char embedded_detail[64];

    /* Current SD:/payload.bin. */
    bool payload_present;
    bool payload_is_nova;
    bool payload_is_hekate;
    uint64_t payload_size;
    char payload_sha[65];
    char payload_kind[24];

    /* Rollback record. */
    bool backup_present;
    char backup_kind[24]; /* "none" means the slot was empty */
    char backup_sha[65];
    uint64_t backup_size;

    /* Configuration status. */
    nova_autoboot_state_t state;
    uint32_t nova_boots;
    uint32_t configured_boots;
    char state_version[16];
    char detail[112];
} nova_autoboot_t;

/* Inspects hardware, payload.bin, the rollback record and boot marker. */
void nova_autoboot_scan(nova_autoboot_t *ab);

/* Writes NOVA.bin to payload.bin, backing up the previous payload first. */
int nova_autoboot_enable(nova_autoboot_t *ab, nova_progress_fn cb, void *ctx,
                         char *err, size_t err_size);

/* Restores the backed-up payload, or removes NOVA's payload.bin. */
int nova_autoboot_disable(nova_autoboot_t *ab, nova_progress_fn cb, void *ctx,
                          char *err, size_t err_size);

const char *nova_autoboot_state_label(nova_autoboot_state_t state);
const char *nova_autoboot_compat_label(const nova_autoboot_t *ab);
const char *nova_autoboot_kind_label(const char *kind);

#endif
