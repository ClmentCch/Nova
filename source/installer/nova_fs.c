/*
 * NOVA Installer - SD card access through libnx's devoptab.
 *
 * All paths are relative to the SD root; "sdmc:/" is prepended internally.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "nova_fs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

void nova_io_path(const char *rel, char *out, size_t out_size)
{
    if (!rel || !rel[0] || strcmp(rel, "/") == 0)
    {
        snprintf(out, out_size, SD_ROOT);
    }
    else if (rel[0] == '/')
    {
        snprintf(out, out_size, "sdmc:%s", rel);
    }
    else
    {
        snprintf(out, out_size, SD_ROOT "%s", rel);
    }
}

bool nova_io_exists(const char *rel)
{
    struct stat st;
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));
    return stat(path, &st) == 0;
}

bool nova_io_is_dir(const char *rel)
{
    struct stat st;
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

long long nova_io_size(const char *rel)
{
    struct stat st;
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
        return -1;
    return (long long)st.st_size;
}

int nova_io_read(const char *rel, void *buffer, size_t capacity, size_t *read_out)
{
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    size_t got = fread(buffer, 1, capacity, f);
    bool ok = !ferror(f);
    fclose(f);

    if (read_out)
        *read_out = got;
    return ok ? 0 : -1;
}

void *nova_io_read_all(const char *rel, size_t *size_out)
{
    long long size = nova_io_size(rel);
    if (size < 0)
        return NULL;

    void *buffer = malloc((size_t)size + 1);
    if (!buffer)
        return NULL;

    size_t got = 0;
    if (nova_io_read(rel, buffer, (size_t)size, &got) != 0 || got != (size_t)size)
    {
        free(buffer);
        return NULL;
    }

    ((uint8_t *)buffer)[size] = 0;
    if (size_out)
        *size_out = (size_t)size;
    return buffer;
}

int nova_io_write(const char *rel, const void *data, size_t size)
{
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    size_t written = fwrite(data, 1, size, f);
    bool ok = !ferror(f);
    if (fflush(f) != 0)
        ok = false;
    fclose(f);

    return (ok && written == size) ? 0 : -1;
}

int nova_io_mkdir(const char *rel)
{
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));
    if (mkdir(path, 0777) == 0)
        return 0;
    return nova_io_is_dir(rel) ? 0 : -1;
}

int nova_io_mkdir_p(const char *rel)
{
    char temp[NOVA_MAX_PATH];
    size_t len = strlen(rel);

    if (len >= sizeof(temp))
        return -1;

    memcpy(temp, rel, len + 1);

    for (char *p = temp + 1; *p; p++)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        if (nova_io_mkdir(temp) != 0)
            return -1;
        *p = '/';
    }

    return nova_io_mkdir(temp);
}

int nova_io_remove(const char *rel)
{
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));
    if (!nova_io_exists(rel))
        return 0;
    return remove(path) == 0 ? 0 : -1;
}

/* Depth limited recursive delete used only on NOVA owned folders. */
int nova_io_remove_tree(const char *rel, int depth)
{
    if (depth > 8)
        return -1;

    if (!nova_io_is_dir(rel))
        return nova_io_remove(rel);

    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));

    DIR *dir = opendir(path);
    if (!dir)
        return -1;

    struct dirent *entry;
    int rc = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char child[NOVA_MAX_PATH * 2];
        snprintf(child, sizeof(child), "%s/%s", rel, entry->d_name);
        if (nova_io_remove_tree(child, depth + 1) != 0)
            rc = -1;
    }
    closedir(dir);

    if (rc == 0)
        rc = remove(path) == 0 ? 0 : -1;
    return rc;
}

int nova_io_sha256_file(const char *rel, char out[65])
{
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(rel, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    nova_sha256_t ctx;
    nova_sha256_init(&ctx);

    uint8_t buffer[16384];
    size_t got;
    while ((got = fread(buffer, 1, sizeof(buffer), f)) > 0)
        nova_sha256_update(&ctx, buffer, got);

    bool ok = !ferror(f);
    fclose(f);
    if (!ok)
        return -1;

    uint8_t digest[32];
    nova_sha256_final(&ctx, digest);

    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    out[64] = '\0';
    return 0;
}

int nova_io_copy(const char *src, const char *dst, nova_progress_fn cb, void *cb_ctx,
                 const char *stage, uint64_t *done, uint64_t total)
{
    char src_path[NOVA_MAX_PATH * 2];
    char dst_path[NOVA_MAX_PATH * 2];
    nova_io_path(src, src_path, sizeof(src_path));
    nova_io_path(dst, dst_path, sizeof(dst_path));

    FILE *in = fopen(src_path, "rb");
    if (!in)
        return -1;

    FILE *out = fopen(dst_path, "wb");
    if (!out)
    {
        fclose(in);
        return -1;
    }

    uint8_t buffer[32768];
    size_t got;
    int rc = 0;
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if (fwrite(buffer, 1, got, out) != got)
        {
            rc = -1;
            break;
        }
        if (done)
            *done += got;
        if (cb)
            cb(cb_ctx, stage, *done, total);
    }

    if (ferror(in))
        rc = -1;
    if (fflush(out) != 0)
        rc = -1;
    fclose(in);
    fclose(out);
    return rc;
}

/* ------------------------------------------------------------------ */
/* nova_fs_t implementation for the shared detection core              */
/* ------------------------------------------------------------------ */
struct nova_dir_iter
{
    DIR *dir;
    char pattern[64];
};

static int fs_exists(void *ctx, const char *path)
{
    (void)ctx;
    return nova_io_exists(path) ? 1 : 0;
}

static int fs_file_size(void *ctx, const char *path, uint32_t *size)
{
    (void)ctx;
    long long value = nova_io_size(path);
    if (value < 0)
        return -1;
    *size = (uint32_t)value;
    return 0;
}

static void *fs_read_file(void *ctx, const char *path, uint32_t *size)
{
    (void)ctx;
    size_t got = 0;
    void *buffer = nova_io_read_all(path, &got);
    if (buffer && size)
        *size = (uint32_t)got;
    return buffer;
}

static void fs_free_file(void *ctx, void *buffer)
{
    (void)ctx;
    free(buffer);
}

static int fs_list_next(void *ctx, nova_dir_iter_t *it, char *name, uint32_t name_size);

static int fs_list_first(void *ctx, const char *dir, const char *pattern,
                         char *name, uint32_t name_size, nova_dir_iter_t **it)
{
    (void)ctx;
    char path[NOVA_MAX_PATH * 2];
    nova_io_path(dir, path, sizeof(path));

    DIR *d = opendir(path);
    if (!d)
        return -1;

    struct nova_dir_iter *iter = calloc(1, sizeof(*iter));
    if (!iter)
    {
        closedir(d);
        return -1;
    }

    iter->dir = d;
    snprintf(iter->pattern, sizeof(iter->pattern), "%s", pattern);
    *it = (nova_dir_iter_t *)iter;

    return fs_list_next(ctx, iter, name, name_size);
}

static int fs_list_next(void *ctx, nova_dir_iter_t *it, char *name, uint32_t name_size)
{
    (void)ctx;
    struct nova_dir_iter *iter = (struct nova_dir_iter *)it;
    if (!iter || !iter->dir)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(iter->dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (!nova_wildcard(iter->pattern, entry->d_name))
            continue;
        snprintf(name, name_size, "%s", entry->d_name);
        return 0;
    }
    return -1;
}

static void fs_list_close(void *ctx, nova_dir_iter_t *it)
{
    (void)ctx;
    struct nova_dir_iter *iter = (struct nova_dir_iter *)it;
    if (!iter)
        return;
    if (iter->dir)
        closedir(iter->dir);
    free(iter);
}

void nova_fs_init(nova_fs_t *fs)
{
    memset(fs, 0, sizeof(*fs));
    fs->exists = fs_exists;
    fs->file_size = fs_file_size;
    fs->read_file = fs_read_file;
    fs->free_file = fs_free_file;
    fs->list_first = fs_list_first;
    fs->list_next = fs_list_next;
    fs->list_close = fs_list_close;
}

int nova_sd_space(uint64_t *free_bytes, uint64_t *total_bytes)
{
    struct statvfs vfs;
    if (statvfs(SD_ROOT, &vfs) != 0)
        return -1;

    if (free_bytes)
        *free_bytes = (uint64_t)vfs.f_bsize * (uint64_t)vfs.f_bfree;
    if (total_bytes)
        *total_bytes = (uint64_t)vfs.f_bsize * (uint64_t)vfs.f_blocks;
    return 0;
}
