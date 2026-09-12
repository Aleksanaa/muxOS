/*
 * pmm.c - Physical Memory Manager
 *
 * Bitmap-based physical page allocator for MuxOS.
 * Manages 4KB pages using a simple bitmap where each bit
 * represents one page (1=used, 0=free).
 */

#include "pmm.h"
#include "vga.h"
#include <stdint.h>

/* Bitmap for tracking page allocation status */
static uint8_t bitmap[MAX_PAGES / 8];

void pmm_mark_free(uint32_t start, uint32_t length);
void pmm_mark_used(uint32_t start, uint32_t length);

extern uint32_t _kernel_end;
/* 用户镜像的 LMA 位于内核镜像之后，也必须在首次复制前保留。 */
extern uint32_t _user_load_end;

/*
 * pmm_init - Initialize physical memory manager
 * @mbi: Multiboot info structure from bootloader
 *
 * Parses memory map from multiboot, marks all pages as used,
 * then marks available regions as free. Reserves kernel memory.
 */
void pmm_init(multiboot_info_t *mbi) {
  /* Mark all pages as used initially */
  for (int i = 0; i < MAX_PAGES / 8; i++) {
    bitmap[i] = 0xFF;
  }

  /* Parse multiboot memory map */
  if (CHECK_FLAG(mbi->flags, 6)) {
    multiboot_memory_map_t *mmap;
    for (mmap = (multiboot_memory_map_t *)mbi->mmap_addr;
         (unsigned long)mmap < mbi->mmap_addr + mbi->mmap_length;
         mmap = (multiboot_memory_map_t *)((unsigned long)mmap + mmap->size +
                                           sizeof(mmap->size))) {
      /* Mark available regions above 1MB as free */
      if (mmap->type == 1 && (uint32_t)mmap->addr >= 0x100000) {
        uint32_t start = (uint32_t)mmap->addr;
        uint32_t length = (uint32_t)mmap->len;
        pmm_mark_free(start, length);
      }
    }
  }

  /*
   * 保留内核及其紧随其后的用户镜像加载区。_kernel_end 只指向用户
   * 镜像 LMA 的起点；若只保留到那里，后续 pmm_alloc 会覆写用户代码尾部。
   */
  uint32_t reserved_end = (uint32_t)(uintptr_t)&_user_load_end;
  if (reserved_end < (uint32_t)(uintptr_t)&_kernel_end)
    reserved_end = (uint32_t)(uintptr_t)&_kernel_end;
  pmm_mark_used(0x100000, reserved_end - 0x100000);
  print("[OK] PMM init\n", 0);
}

/*
 * pmm_alloc - Allocate a single physical page
 *
 * Scans bitmap for first free page and marks it as used.
 *
 * Returns: Physical address of allocated page
 * Panics if no free pages available
 */
uint32_t pmm_alloc() {
  /*
   * 返回的物理页会被内核作为普通指针访问（页表、TSS 的 esp0 栈等）。
   * 因而不能分配到 vmm_init 尚未恒等映射的高端物理内存。
   */
  for (uint32_t i = 0; i < PMM_IDENTITY_MAPPED_LIMIT / PAGE_SIZE; i++) {
    if (!(bitmap[i / 8] & (1 << (i % 8)))) {
      bitmap[i / 8] |= (1 << (i % 8));
      return i * PAGE_SIZE;
    }
  }
  PANIC("No free memory pages available");
}

void pmm_free(uint32_t addr) {
  uint32_t page = addr / PAGE_SIZE;
  bitmap[page / 8] &= ~(1 << (page % 8));
}

/*
 * pmm_mark_free - Mark a memory region as free
 * @start: Physical start address
 * @length: Length in bytes
 *
 * Clears bitmap bits for all pages in the specified range.
 */
void pmm_mark_free(uint32_t start, uint32_t length) {
  uint32_t page = start / PAGE_SIZE;
  uint32_t count = length / PAGE_SIZE;
  for (uint32_t i = page; i < page + count; i++)
    bitmap[i / 8] &= ~(1 << (i % 8));
}

/*
 * pmm_mark_used - Mark a memory region as used
 * @start: Physical start address
 * @length: Length in bytes
 *
 * Sets bitmap bits for all pages in the specified range.
 * Rounds up length to nearest page boundary.
 */
void pmm_mark_used(uint32_t start, uint32_t length) {
  uint32_t page = start / PAGE_SIZE;
  uint32_t count = (length + PAGE_SIZE - 1) / PAGE_SIZE;
  for (uint32_t i = page; i < page + count; i++)
    bitmap[i / 8] |= (1 << (i % 8));
}
