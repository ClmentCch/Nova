/*
 * NOVA Boot payload entry point.
 *
 * Modeled on hekate's own payload entry: relocate to the link address when
 * an injector loads us elsewhere, set up the SYS stack, zero .bss, install
 * the exception vectors and enter C. NOVA must behave exactly like a
 * first-stage Tegra payload because that is what it is.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

.section .text._start
.arm
.syntax unified

.extern _reloc_ipl
.extern memset
.extern _irq_setup

.globl _start
.type _start, %function

_start:
    ADR     R0, _start
    LDR     R1, =__ipl_start
    CMP     R0, R1
    BEQ     _real_start

    /* Loaded elsewhere: copy a relocator to the top of IRAM and use it. */
    ADR     R2, _reloc_ipl
    LDR     R3, =0x4003FF00
    MOV     R4, #(_real_start - _reloc_ipl)
.Lcopy_loop:
    LDMIA   R2!, {R5}
    STMIA   R3!, {R5}
    SUBS    R4, #4
    BNE     .Lcopy_loop

    LDR     R2, =__ipl_end
    SUB     R2, R2, R1
    LDR     R3, =_real_start
    LDR     R4, =0x4003FF00
    BX      R4

_reloc_ipl:
    LDMIA   R0!, {R4-R7}
    STMIA   R1!, {R4-R7}
    SUBS    R2, R2, #0x10
    BNE     _reloc_ipl
    BX      R3

_real_start:
    /* Temporary stack below the relocator; _irq_setup installs the real one. */
    LDR     SP, =0x4003FF00

    LDR     R0, =__bss_start
    EOR     R1, R1, R1
    LDR     R2, =__bss_end
    SUB     R2, R2, R0
    BL      memset

    BL      _irq_setup

.Lhalt:
    B       .Lhalt

.size _start, . - _start
