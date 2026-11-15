#define _GNU_SOURCE
#include "nova_posix_fs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct nova_dir_iter {
    DIR *dir;
    char pattern[64];
};

static void build_path(void *ctx, const char *path, char *out, size_t out_size)
{
    const char *root = (const char *)ctx;
    if (path[0] == '/')
        path++;
    snprintf(out, out_size, "%s/%s", root, path);
}

static int fs_list_next(void *ctx, nova_dir_iter_t *it, char *name, uint32_t name_size);

static int fs_exists(void *ctx, const char *path)
{
    char full[1024];
    struct stat st;
    build_path(ctx, path, full, sizeof(full));
    return stat(full, &st) == 0;
}

static int fs_file_size(void *ctx, const char *path, uint32_t *size)
{
    char full[1024];
    struct stat st;
    build_path(ctx, path, full, sizeof(full));
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    *size = (uint32_t)st.st_size;
    return 0;
}

static void *fs_read_file(void *ctx, const char *path, uint32_t *size)
{
    char full[1024];
    struct stat st;
    build_path(ctx, path, full, sizeof(full));
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode))
        return NULL;

    FILE *f = fopen(full, "rb");
    if (!f)
        return NULL;

    void *buf = malloc((size_t)st.st_size + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if (size)
        *size = (uint32_t)got;
    return buf;
}

static void fs_free_file(void *ctx, void *buf)
{
    (void)ctx;
    free(buf);
}

static int fs_list_first(void *ctx, const char *dir, const char *pattern,
                         char *name, uint32_t name_size, nova_dir_iter_t **it)
{
    char full[1024];
    build_path(ctx, dir, full, sizeof(full));

    DIR *d = opendir(full);
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
    *it = iter;

    return fs_list_next(ctx, iter, name, name_size);
}

static int fs_list_next(void *ctx, nova_dir_iter_t *it, char *name, uint32_t name_size)
{
    (void)ctx;
    if (!it || !it->dir)
        return -1;

    struct dirent *e;
    while ((e = readdir(it->dir)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        if (!nova_wildcard(it->pattern, e->d_name))
            continue;
        snprintf(name, name_size, "%s", e->d_name);
        return 0;
    }
    return -1;
}

static void fs_list_close(void *ctx, nova_dir_iter_t *it)
{
    (void)ctx;
    if (!it)
        return;
    if (it->dir)
        closedir(it->dir);
    free(it);
}

void nova_posix_fs_init(nova_fs_t *fs, const char *root)
{
    memset(fs, 0, sizeof(*fs));
    fs->ctx = (void *)root;
    fs->exists = fs_exists;
    fs->file_size = fs_file_size;
    fs->read_file = fs_read_file;
    fs->free_file = fs_free_file;
    fs->list_first = fs_list_first;
    fs->list_next = fs_list_next;
    fs->list_close = fs_list_close;
}
