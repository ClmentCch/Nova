/*
 * NOVA Installer - embedded NOVA Boot payload.
 *
 * The payload is compiled into the NRO from data/NOVA.bin, so transferring
 * files never depends on the payload already being on the SD card.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
.section .rodata.nova_payload, "a"
.balign 4
.global nova_payload_blob
nova_payload_blob:
.incbin "data/NOVA.bin"
.global nova_payload_blob_end
nova_payload_blob_end:
.balign 4
.global nova_payload_blob_len
nova_payload_blob_len:
.word nova_payload_blob_end - nova_payload_blob
