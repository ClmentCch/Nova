/* NOVA Installer - SD access helpers. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NOVA_FS_H
#define NOVA_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "installer.h"
#include "nova_sha256.h"

void nova_io_path(const char *rel, char *out, size_t out_size);
bool nova_io_exists(const char *rel);
bool nova_io_is_dir(const char *rel);
long long nova_io_size(const char *rel);

int nova_io_read(const char *rel, void *buffer, size_t capacity, size_t *read_out);
void *nova_io_read_all(const char *rel, size_t *size_out);
int nova_io_write(const char *rel, const void *data, size_t size);
int nova_io_mkdir(const char *rel);
int nova_io_mkdir_p(const char *rel);
int nova_io_remove(const char *rel);
int nova_io_remove_tree(const char *rel, int depth);
int nova_io_sha256_file(const char *rel, char out[65]);
int nova_io_copy(const char *src, const char *dst, nova_progress_fn cb, void *cb_ctx,
                 const char *stage, uint64_t *done, uint64_t total);

/* Fills a nova_fs_t so the shared detection core can inspect the SD card. */
void nova_fs_init(nova_fs_t *fs);

int nova_sd_space(uint64_t *free_bytes, uint64_t *total_bytes);

#endif
