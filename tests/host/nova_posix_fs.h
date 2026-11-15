/* Host test adapter: exposes a directory tree through the nova_fs_t API. */
#ifndef NOVA_POSIX_FS_H
#define NOVA_POSIX_FS_H

#include "nova_core.h"

/* root: absolute path used as "sdmc:/" for all NOVA paths. */
void nova_posix_fs_init(nova_fs_t *fs, const char *root);

#endif
