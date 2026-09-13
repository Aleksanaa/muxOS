/* Linker script for muxOS user programs.  Linked at a fixed high virtual
 * address so the kernel can map segments directly. */
ENTRY(user_c_start)

SECTIONS {
    . = 0x20000000;

    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    }
}
