ENTRY(_start)

SECTIONS {
    . = 1M;

    .text ALIGN(4K) : {
        *(.multiboot)
        *(EXCLUDE_FILE (build/user/crt/user_crt.o build/user/lib/userlib.o) .text)
    }

    .rodata ALIGN(4K) : {
        *(EXCLUDE_FILE (build/user/lib/userlib.o) .rodata)
    }

    .data ALIGN(4K) : {
        *(EXCLUDE_FILE (build/user/lib/userlib.o) .data)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(EXCLUDE_FILE (build/user/lib/userlib.o) .bss)
    }

    _kernel_end = .;

    /* 用户程序使用固定高地址虚拟空间链接，避免和内核 0..128MB 恒等映射冲突。 */
    . = 0x20000000;
    .user_text ALIGN(4K) : AT(_kernel_end) {
        _user_start = .;
        _user_text_load_start = LOADADDR(.user_text);
        build/user/crt/user_crt.o(.text)
        build/user/lib/userlib.o(.text)
        _user_text_load_end = LOADADDR(.user_text) + SIZEOF(.user_text);
    }

    .user_rodata ALIGN(4K) : AT(LOADADDR(.user_text) + SIZEOF(.user_text)) {
        _user_rodata = .;
        _user_rodata_load_start = LOADADDR(.user_rodata);
        build/user/lib/userlib.o(.rodata*)
        _user_rodata_load_end = LOADADDR(.user_rodata) + SIZEOF(.user_rodata);
    }

    .user_data ALIGN(4K) : AT(LOADADDR(.user_rodata) + SIZEOF(.user_rodata)) {
        build/user/lib/userlib.o(.data*)
    }

    _user_end = .;
    _user_load_start = LOADADDR(.user_text);
    _user_load_end = LOADADDR(.user_data) + SIZEOF(.user_data);
}
