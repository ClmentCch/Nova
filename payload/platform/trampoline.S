/*
 * NOVA Boot - payload chainload trampoline.
 *
 * Copying a payload down to 0x40010000 overwrites the code that is running,
 * so the copy loop is copied into a heap buffer first and executed from
 * there. The routine is position independent and uses no literal pool:
 *
 *   r0 = source, r1 = destination, r2 = size in bytes, r3 = entry point
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.section .text.nova_trampoline
.arm
.syntax unified

.globl nova_trampoline_begin
.globl nova_trampoline_end
.globl nova_chain_copy_jump

.type nova_chain_copy_jump, %function

nova_trampoline_begin:
nova_chain_copy_jump:
    CMP     R2, #0
    BEQ     .Ltramp_jump

.Ltramp_loop:
    LDR     R4, [R0], #4
    STR     R4, [R1], #4
    SUBS    R2, R2, #4
    BHI     .Ltramp_loop

.Ltramp_jump:
    /*
     * Flush write buffers and invalidate the instruction cache so the new
     * payload is fetched from memory. These CP15 operations are available on
     * the Tegra X1 cores even though NOVA is assembled for ARMv4T encoding.
     */
    MOV     R4, #0
    MCR     p15, 0, R4, c7, c10, 5  /* drain write buffer */
    MCR     p15, 0, R4, c7, c5, 0   /* invalidate I-cache */
    MCR     p15, 0, R4, c7, c10, 5
    BX      R3

.size nova_chain_copy_jump, . - nova_chain_copy_jump
nova_trampoline_end:
