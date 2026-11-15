/*
 * NOVA Boot - platform independent core.
 *
 * Everything in this file is compiled unchanged for the Tegra payload and
 * for the host test harness, so the detection rules that run on the console
 * are the rules that are unit tested.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef NOVA_CORE_H
#define NOVA_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nova_shared.h"

/* ------------------------------------------------------------------ */
/* Minimal freestanding string/memory helpers (no libc in the payload) */
/* ------------------------------------------------------------------ */
void *nova_memset(void *dst, int c, size_t n);
void *nova_memcpy(void *dst, const void *src, size_t n);
int nova_memcmp(const void *a, const void *b, size_t n);
size_t nova_strlen(const char *s);
int nova_strcmp(const char *a, const char *b);
int nova_strncmp(const char *a, const char *b, size_t n);
int nova_strcasecmp(const char *a, const char *b);
char *nova_strcpy(char *dst, const char *src);
char *nova_strncpy(char *dst, const char *src, size_t n);
char *nova_strcat(char *dst, const char *src);
int nova_atoi(const char *s);
uint32_t nova_strtoul_hex(const char *s);

/* Copy with truncation and always NUL terminate. */
void nova_cstr_copy(char *dst, size_t dst_size, const char *src);

/* Wildcard match with '*' and '?', case insensitive (FAT semantics). */
bool nova_wildcard(const char *pattern, const char *name);

/* ------------------------------------------------------------------ */
/* Filesystem abstraction.                                             */
/* ------------------------------------------------------------------ */
typedef struct nova_dir_iter nova_dir_iter_t;

typedef struct nova_fs {
    void *ctx;

    /* Returns 1 when the path exists (file or directory), 0 otherwise. */
    int (*exists)(void *ctx, const char *path);
    /* Returns 0 on success, -1 on failure. */
    int (*file_size)(void *ctx, const char *path, uint32_t *size);
    /* Allocates and returns the whole file (NUL not guaranteed). */
    void *(*read_file)(void *ctx, const char *path, uint32_t *size);
    void (*free_file)(void *ctx, void *buf);

    /* Directory listing; pattern uses nova_wildcard syntax. */
    int (*list_first)(void *ctx, const char *dir, const char *pattern,
                      char *name, uint32_t name_size, nova_dir_iter_t **it);
    int (*list_next)(void *ctx, nova_dir_iter_t *it,
                     char *name, uint32_t name_size);
    void (*list_close)(void *ctx, nova_dir_iter_t *it);
} nova_fs_t;

/* ------------------------------------------------------------------ */
/* Status model                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    NOVA_ST_MISSING = 0, /* not present / not usable            */
    NOVA_ST_OK      = 1, /* verified working                    */
    NOVA_ST_WARN    = 2, /* present but something is incomplete */
    NOVA_ST_ERROR   = 3, /* present but invalid                 */
} nova_state_t;

#define NOVA_EMU_MODE_NONE 0
#define NOVA_EMU_MODE_RAW  1
#define NOVA_EMU_MODE_FILE 2

typedef struct nova_status {
    bool sd_mounted;
    char sd_detail[48];

    nova_state_t atmosphere;
    char atm_detail[48];
    uint32_t package3_size;

    nova_state_t emummc;
    char emu_detail[72];
    char emu_path[80];
    char emu_nintendo[96];
    uint8_t emu_mode; /* NOVA_EMU_MODE_* */
    uint32_t emu_sector;

    nova_state_t hekate;
    char hek_detail[64];
    char hekate_path[96];
    char hekate_version[16];

    nova_state_t stock;
    char stock_detail[56];

    nova_state_t bootcfg; /* NOVA entries present in bootloader/ini/NOVA.ini */
    char cfg_detail[64];

    bool can_stock;
    bool can_hekate;
    bool can_emummc;
} nova_status_t;

/* Full SD inspection. Safe with a NULL filesystem (reports SD missing). */
void nova_detect(const nova_fs_t *fs, nova_status_t *out);

/* Readable label for a state, e.g. "READY" / "MISSING". */
const char *nova_state_label(nova_state_t state);

/* ------------------------------------------------------------------ */
/* INI helpers (read-only, tolerant parser)                            */
/* ------------------------------------------------------------------ */
/* Finds key in [section] and copies the trimmed value. */
bool nova_ini_get(const char *text, uint32_t len,
                  const char *section, const char *key,
                  char *out, uint32_t out_size);
/* True when the ini declares id=<id> in any boot entry. */
bool nova_ini_has_id(const char *text, uint32_t len, const char *id);

/* ------------------------------------------------------------------ */
/* hekate integration                                                  */
/* ------------------------------------------------------------------ */
/*
 * Builds the NOVA owned hekate entry file (bootloader/ini/NOVA.ini).
 * Contains two entries identified by NOVA_ID_STOCK / NOVA_ID_EMUMMC.
 * Returns the number of bytes written (excluding NUL) or 0 on overflow.
 */
uint32_t nova_build_hekate_ini(char *out, uint32_t out_size);

/*
 * Validates a hekate payload image and applies the documented boot_cfg
 * storage so hekate boots the entry with the given id.
 * Returns true when the image is a hekate payload and was patched.
 * With id == NULL the boot config is cleared (hekate shows its menu).
 */
bool nova_hekate_set_boot_id(uint8_t *image, uint32_t size, const char *id);

/* Parses "hekate_ctcaer_6.5.5.bin" (or any name containing a version). */
void nova_parse_hekate_version(const char *filename, char *out, uint32_t out_size);

/*
 * Locates a hekate payload candidate on the SD card.
 * Returns true when a matching file name exists; *valid reports whether the
 * image passed the hekate magic/size validation.
 */
bool nova_find_hekate(const nova_fs_t *fs, char *path, uint32_t path_size,
                      char *version, uint32_t version_size, bool *valid);

#endif /* NOVA_CORE_H */
