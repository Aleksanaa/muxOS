/* Linker script for mlibc user programs on muxOS.
 *
 * mlibc's static startup goes through __dlapi_enter() -> interpreterMain(),
 * so it expects the usual glibc-ish symbols: the preinit/init/fini arrays,
 * __dso_handle (provided by crt1.S) and __ehdr_start. */
ENTRY(_start)

SECTIONS {
    . = 0x20000000;

    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .preinit_array : {
        __preinit_array_start = .;
        KEEP (*(.preinit_array*))
        __preinit_array_end = .;
    }

    .init_array : {
        __init_array_start = .;
        KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP (*(.init_array*))
        __init_array_end = .;
    }

    .fini_array : {
        __fini_array_start = .;
        KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP (*(.fini_array*))
        __fini_array_end = .;
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    }

    /* The ELF header lives in the first LOAD segment, which ld places at the
     * page boundary just below the load address. */
    __ehdr_start = 0x20000000 - ((SIZEOF_HEADERS + 0xFFF) & ~0xFFF);
}
