# Toolchain -------------------------------------------------------------------
CC            := $(shell command -v i686-elf-gcc 2>/dev/null || echo i686-elf-gcc)
NASM          := $(shell command -v nasm 2>/dev/null || echo nasm)
GRUB_MKRESCUE := $(shell command -v i686-elf-grub-mkrescue 2>/dev/null || command -v grub-mkrescue 2>/dev/null || echo grub-mkrescue)
QEMU          := $(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386)

BUILD   := build
KERNEL  := $(BUILD)/kernel.elf
ISO     := $(BUILD)/muxos.iso
ISO_DIR := $(BUILD)/isodir

CPPFLAGS := -I. -Iarch/x86 -Iarch/x86/include \
            -Ikernel -Ikernel/lib -Ikernel/mm -Ikernel/task -Ikernel/fs \
            -Idrivers/input -Idrivers/platform -Idrivers/serial -Idrivers/video \
            -Iuser/bin -Iuser/lib
CFLAGS   := -m32 -ffreestanding -fno-builtin -fno-pic -O0 -g \
            -Wall -Wextra -MMD -MP
LDFLAGS  := -m32 -T linker.ld -ffreestanding -nostdlib
QEMUFLAGS ?= -display cocoa,zoom-to-fit=on

# userlib.c currently includes user/bin/shell.c directly, so shell.c must not
# be compiled separately. minishell is not linked into the kernel image yet.
KERNEL_C_SRCS   := $(shell find arch kernel drivers -type f -name '*.c' | sort)
KERNEL_ASM_SRCS := $(shell find arch -type f -name '*.s' | sort)
USER_C_SRCS     := user/lib/userlib.c
USER_ASM_SRCS   := user/crt/user_crt.s

C_SRCS   := $(KERNEL_C_SRCS) $(USER_C_SRCS)
ASM_SRCS := $(KERNEL_ASM_SRCS) $(USER_ASM_SRCS)
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
            $(patsubst %.s,$(BUILD)/%.o,$(ASM_SRCS))
DEPS     := $(OBJS:.o=.d)

.DEFAULT_GOAL := all
.PHONY: all run clean print-sources

all: $(ISO)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(@D)
	$(NASM) -f elf32 $< -o $@

# Interrupt code cannot rely on SSE register state.
$(BUILD)/drivers/input/keyboard.o: CFLAGS += -mgeneral-regs-only

$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(ISO): $(KERNEL)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	printf '%s\n' \
		'set timeout=1' \
		'set default=0' \
		'set gfxmode=1024x768x32' \
		'set gfxpayload=1024x768x32' \
		'menuentry "MuxOS" { multiboot /boot/kernel.elf }' \
		> $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)

run: $(ISO)
	$(QEMU) $(QEMUFLAGS) -cdrom $(ISO)

clean:
	rm -rf $(BUILD)

print-sources:
	@printf '%s\n' $(C_SRCS) $(ASM_SRCS)

-include $(DEPS)
