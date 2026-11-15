/*
 * NOVA Boot payload linker script.
 *
 * A Nintendo Switch RCM/modchip payload is loaded into IRAM at 0x40010000
 * and entered in ARM (AArch32) state. hekate chainloads payloads to the same
 * address (see hekate bootloader/main.c: PATCHED_RELOC_ENTRY).
 *
 * IRAM is 0x40000000..0x40040000. The payload must stay below the stack at
 * 0x4003F000 and below hekate's own hard 0x30000 payload limit.
 */
ENTRY(_start)

SECTIONS
{
    PROVIDE(__ipl_start = 0x40010000);
    . = __ipl_start;

    .text : {
        *(.text._start)
        *(.text*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    . = ALIGN(0x10);
    __payload_end = .;
    __ipl_end = .;

    .bss : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    . = ALIGN(0x10);
    __bss_end_aligned = .;

    ASSERT(__payload_end <= 0x40038000, "NOVA payload exceeds IRAM budget")
    ASSERT(__bss_end_aligned <= 0x4003E000, "NOVA bss overlaps the stack")
}
