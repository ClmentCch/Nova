/* NOVA Boot - SHA-256. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NOVA_SHA256_H
#define NOVA_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct nova_sha256 {
    uint32_t state[8];
    uint64_t length;
    uint8_t buffer[64];
    size_t buffered;
} nova_sha256_t;

void nova_sha256_init(nova_sha256_t *ctx);
void nova_sha256_update(nova_sha256_t *ctx, const void *data, size_t size);
void nova_sha256_final(nova_sha256_t *ctx, uint8_t digest[32]);

/* Writes a lowercase 64 character hex digest plus NUL. */
void nova_sha256_hex(const void *data, size_t size, char out[65]);

#endif
