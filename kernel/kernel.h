#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#define KERNEL_VERSION "0.0.1"
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define PANIC(msg) panic(msg)
#define CHECK_FLAG(flags, bit) ((flags) & (1 << (bit)))

#define MULTIBOOT_INFO_FRAMEBUFFER 12

typedef struct multiboot_info {
  uint32_t flags;

  uint32_t mem_lower;
  uint32_t mem_upper;

  uint32_t boot_device;
  uint32_t cmdline;

  uint32_t mods_count;
  uint32_t mods_addr;

  uint32_t syms[4];

  uint32_t mmap_length;
  uint32_t mmap_addr;

  uint32_t drives_length;
  uint32_t drives_addr;

  uint32_t config_table;

  uint32_t boot_loader_name;
  uint32_t apm_table;

  uint32_t vbe_control_info;
  uint32_t vbe_mode_info;

  uint16_t vbe_mode;
  uint16_t vbe_interface_seg;
  uint16_t vbe_interface_off;
  uint16_t vbe_interface_len;

  uint64_t framebuffer_addr;
  uint32_t framebuffer_pitch;
  uint32_t framebuffer_width;
  uint32_t framebuffer_height;
  uint8_t framebuffer_bpp;
  uint8_t framebuffer_type;

  union {
    struct {
      uint32_t framebuffer_palette_addr;
      uint16_t framebuffer_palette_num_colors;
    };

    struct {
      uint8_t framebuffer_red_field_position;
      uint8_t framebuffer_red_mask_size;
      uint8_t framebuffer_green_field_position;
      uint8_t framebuffer_green_mask_size;
      uint8_t framebuffer_blue_field_position;
      uint8_t framebuffer_blue_mask_size;
    };
  };

} __attribute__((packed)) multiboot_info_t;

typedef unsigned char multiboot_uint8_t;
typedef unsigned short multiboot_uint16_t;
typedef unsigned int multiboot_uint32_t;
typedef unsigned long long multiboot_uint64_t;

struct multiboot_mmap_entry {
  multiboot_uint32_t size;
  multiboot_uint64_t addr;
  multiboot_uint64_t len;

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED 2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS 4
#define MULTIBOOT_MEMORY_BADRAM 5

  multiboot_uint32_t type;
} __attribute__((packed));

typedef struct multiboot_mmap_entry multiboot_memory_map_t;

__attribute__((noreturn)) void panic(const char *message);

#endif