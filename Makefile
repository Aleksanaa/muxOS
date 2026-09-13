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
            -Idrivers/input -Idrivers/platform -Idrivers/serial -Idrivers/video
CFLAGS   := -m32 -ffreestanding -fno-builtin -fno-pic -O0 -g \
            -Wall -Wextra -MMD -MP
LDFLAGS  := -m32 -T linker.ld -ffreestanding -nostdlib
QEMUFLAGS ?= -display cocoa,zoom-to-fit=on

# The initial user process (and every other program) is a static ELF linked
# against mlibc, built outside the kernel tree.  Each is embedded in the kernel
# image with .incbin, copied into /bin at boot, and loaded from the filesystem.
KERNEL_C_SRCS   := $(shell find arch kernel drivers -type f -name '*.c' | sort)
KERNEL_ASM_SRCS := $(shell find arch -type f -name '*.s' | sort)

KERNEL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_C_SRCS)) \
               $(patsubst %.s,$(BUILD)/%.o,$(KERNEL_ASM_SRCS))

# Programs copied into /bin at boot: a space separated list of name=path
# pairs.  Each is embedded in the kernel image with .incbin and exposed via
# the table in kernel/fs/programs.h.
PROGRAMS     ?=
# Applet names hardlinked to the single multicall "toybox" program in /bin.
APPLETS      ?=
GEN_PROGRAMS := $(BUILD)/programs.S
KERNEL_OBJS  += $(BUILD)/programs.o

DEPS := $(KERNEL_OBJS:.o=.d)

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
	  printf '.global embedded_applet_count\nembedded_applet_count:\n.long %d\n' $(words $(APPLETS)); \
	  printf '.global embedded_applets\nembedded_applets:\n'; \
	  i=0; for a in $(APPLETS); do \
	    printf '.long .Lapp%d\n' $$i; i=$$((i+1)); \
	  done; \
	  i=0; for a in $(APPLETS); do \
	    printf '.Lapp%d:\n.asciz "%s"\n' $$i "$$a"; i=$$((i+1)); \
	  done; \
	} > $@

$(BUILD)/programs.o: $(GEN_PROGRAMS)
	@mkdir -p $(@D)
	$(CC) -m32 -c $< -o $@

.PHONY: FORCE
FORCE:

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
	@printf '%s\n' $(KERNEL_C_SRCS) $(KERNEL_ASM_SRCS)

-include $(DEPS)
