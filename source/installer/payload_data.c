/*
 * NOVA Installer - embedded payload access.
 *
 * payload_blob.s embeds data/NOVA.bin with .incbin. Keeping the accessor in
 * its own translation unit lets the host tests provide a payload from disk.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "installer.h"

extern const uint8_t nova_payload_blob[];
extern const uint32_t nova_payload_blob_len;

const uint8_t *nova_embedded_payload(uint32_t *size)
{
    if (size)
        *size = nova_payload_blob_len;
    return nova_payload_blob;
}
