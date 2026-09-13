# Toolchain -------------------------------------------------------------------
CC            := $(shell command -v i686-elf-gcc 2>/dev/null || echo i686-elf-gcc)
OBJCOPY       := $(shell command -v i686-elf-objcopy 2>/dev/null || echo i686-elf-objcopy)
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
LDFLAGS_USER := -m32 -T user/user.ld -ffreestanding -nostdlib
QEMUFLAGS ?= -display cocoa,zoom-to-fit=on

# userlib.c includes user/bin/shell.c directly, so shell.c must not be
# compiled separately.  The userland is linked into its own ELF, then embedded
# in the kernel image as a binary blob and loaded at boot by kernel/elf.c.
KERNEL_C_SRCS   := $(shell find arch kernel drivers -type f -name '*.c' | sort)
KERNEL_ASM_SRCS := $(shell find arch -type f -name '*.s' | sort)
USER_C_SRCS     := user/lib/userlib.c
USER_ASM_SRCS   := user/crt/user_crt.s

KERNEL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_C_SRCS)) \
               $(patsubst %.s,$(BUILD)/%.o,$(KERNEL_ASM_SRCS))
USER_OBJS   := $(patsubst %.c,$(BUILD)/%.o,$(USER_C_SRCS)) \
               $(patsubst %.s,$(BUILD)/%.o,$(USER_ASM_SRCS))

# USER_ELF can be overridden to embed a prebuilt ELF (e.g. one linked against
# mlibc).  It is always copied to a fixed path so the objcopy symbol name is
# stable regardless of where the ELF came from.
USER_ELF   ?= $(BUILD)/user.elf
USER_EMBED := $(BUILD)/user_embedded.elf
USER_BLOB  := $(BUILD)/user_elf.o
KERNEL_OBJS += $(USER_BLOB)

# Programs copied into /bin at boot: a space separated list of name=path
# pairs.  Each is embedded in the kernel image with .incbin and exposed via
# the table in kernel/fs/programs.h.
PROGRAMS     ?=
GEN_PROGRAMS := $(BUILD)/programs.S
KERNEL_OBJS  += $(BUILD)/programs.o

DEPS := $(KERNEL_OBJS:.o=.d) $(USER_OBJS:.o=.d)

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

# Always regenerate: PROGRAMS is not a file dependency make can track.
$(GEN_PROGRAMS): FORCE
	@mkdir -p $(@D)
	@{ \
	  printf '.section .rodata\n.align 4\n'; \
	  printf '.global embedded_program_count\nembedded_program_count:\n.long %d\n' $(words $(PROGRAMS)); \
	  printf '.global embedded_programs\nembedded_programs:\n'; \
	  i=0; for p in $(PROGRAMS); do \
	    printf '.long .Lpname%d\n.long .Lpstart%d\n.long .Lpend%d\n' $$i $$i $$i; \
	    i=$$((i+1)); \
	  done; \
	  i=0; for p in $(PROGRAMS); do \
	    n=$${p%%=*}; f=$${p#*=}; \
	    printf '.Lpname%d:\n.asciz "%s"\n' $$i "$$n"; \
	    printf '.Lpstart%d:\n.incbin "%s"\n.Lpend%d:\n' $$i "$$f" $$i; \
	    i=$$((i+1)); \
	  done; \
	} > $@

$(BUILD)/programs.o: $(GEN_PROGRAMS)
	@mkdir -p $(@D)
	$(CC) -m32 -c $< -o $@

.PHONY: FORCE
FORCE:

ifeq ($(USER_ELF),$(BUILD)/user.elf)
$(BUILD)/user.elf: $(USER_OBJS) user/user.ld
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS_USER) -o $@ $(USER_OBJS) -lgcc
endif

$(USER_EMBED): $(USER_ELF)
	@mkdir -p $(@D)
	cp $< $@

# Embed the userland ELF into the kernel image; symbols become
# _binary_build_user_embedded_elf_start/_end and are identity-mapped.
$(USER_BLOB): $(USER_EMBED)
	@mkdir -p $(@D)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

$(KERNEL): $(KERNEL_OBJS) linker.ld
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $(KERNEL_OBJS) -lgcc

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
	@printf '%s\n' $(KERNEL_C_SRCS) $(KERNEL_ASM_SRCS) $(USER_C_SRCS) $(USER_ASM_SRCS)

-include $(DEPS)
