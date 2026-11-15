/*
 * NOVA Boot - Tegra X1 platform layer (hekate BDK backed).
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_PLATFORM_H
#define NOVA_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "nova_core.h"

#define NOVA_SCREEN_W 720
#define NOVA_SCREEN_H 1280

typedef struct nova_input {
    bool up;
    bool down;
    bool left;
    bool right;
    bool confirm;
    bool back;
    bool menu;      /* PLUS */
    bool minus;
    int  stick_y;   /* negative = up, positive = down, 0 = centered */
    bool connected_l;
    bool connected_r;
} nova_input_t;

/* Full hardware bring up: clocks, display, SD, input. */
void nova_platform_init(void);

/* Framebuffer (720x1280, ARGB u32). */
uint32_t *nova_platform_fb(void);
void nova_platform_backlight(uint32_t percent);
void nova_platform_delay(uint32_t ms);
uint32_t nova_platform_ms(void);

/* SD card state and filesystem access. */
bool nova_platform_sd_ready(void);
const nova_fs_t *nova_platform_fs(void);

/* Input polling with stick deadzone and repeat handled by the caller. */
void nova_platform_input(nova_input_t *out);

/* Writes a NOVA owned text file (used for bootloader/ini/NOVA.ini). */
bool nova_platform_write_text(const char *path, const char *text, uint32_t len);

/*
 * Records one NOVA start in Nova/state/boot.txt. Used by the installer to
 * confirm that a configured cold boot actually reached NOVA.
 */
bool nova_platform_note_boot(void);

/*
 * AutoRCM fallback. Reads the BOOT0 BCT byte that AutoRCM changes, but never
 * writes BOOT0. When AutoRCM is active, the SD payload slot is armed with
 * Nova/NOVA.bin so an RCM-capable loader starts NOVA instead of waiting at
 * RCM. The previous payload is preserved under Nova/backup/autorcm.
 */
typedef enum nova_autorcm_state
{
    NOVA_AUTORCM_UNKNOWN = 0,
    NOVA_AUTORCM_DISABLED,
    NOVA_AUTORCM_ENABLED,
    NOVA_AUTORCM_UNSUPPORTED,
} nova_autorcm_state_t;

typedef enum nova_autorcm_fallback
{
    NOVA_AUTORCM_FALLBACK_NONE = 0,
    NOVA_AUTORCM_FALLBACK_READY,
    NOVA_AUTORCM_FALLBACK_COPIED,
    NOVA_AUTORCM_FALLBACK_FAILED,
} nova_autorcm_fallback_t;

typedef struct nova_autorcm
{
    nova_autorcm_state_t state;
    nova_autorcm_fallback_t fallback;
    char detail[112];
} nova_autorcm_t;

/* Detects AutoRCM, arms payload.bin when needed and records the result. */
void nova_platform_autorcm_prepare(nova_autorcm_t *out);

const char *nova_autorcm_state_label(nova_autorcm_state_t state);
const char *nova_autorcm_fallback_label(nova_autorcm_fallback_t fallback);

/* Hand control to a payload image at 0x40010000. Does not return. */
void nova_platform_chainload(void *image, uint32_t size) __attribute__((noreturn));

/* Park the CPU (message is shown by the caller). */
void nova_platform_halt(void) __attribute__((noreturn));

/* Power off the console (PMIC). Falls back to halt. */
void nova_platform_power_off(void) __attribute__((noreturn));

#endif
