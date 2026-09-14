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

  /* 保留内核镜像自身（含内嵌的用户 ELF blob）。 */
  uint32_t reserved_end = (uint32_t)(uintptr_t)&_kernel_end;
  pmm_mark_used(0x100000, reserved_end - 0x100000);
  print("[OK] PMM init\n", 0);
}

/*
 * Roving allocation hint.  Scanning the bitmap from 0 on every allocation is
 * O(used_pages) per call, which makes fork/exec (hundreds of allocations)
 * quadratic once /bin has been unpacked into memory.  Start where the last
 * allocation stopped instead.
 */
static uint32_t pmm_hint = 0;

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
  const uint32_t total = PMM_IDENTITY_MAPPED_LIMIT / PAGE_SIZE;

  for (uint32_t n = 0; n < total; n++) {
    uint32_t i = pmm_hint;
    if (++pmm_hint >= total)
      pmm_hint = 0;
    uint8_t mask = (uint8_t)(1u << (i & 7));
    if (!(bitmap[i >> 3] & mask)) {
      bitmap[i >> 3] |= mask;
      return i * PAGE_SIZE;
    }
  }
  PANIC("No free memory pages available");
}

/*
 * pmm_alloc_contig - Allocate `pages` physically contiguous pages.
 *
 * The legacy virtio ring layout the device computes depends on the descriptor
 * table, available ring and used ring living in one contiguous, page-aligned
 * block, so single-page pmm_alloc() is not enough.  Scan the whole bitmap
 * (called rarely, at device init) for a run of free pages.
 *
 * Returns the physical address of the first page, or 0 on failure.
 */
uint32_t pmm_alloc_contig(uint32_t pages) {
  const uint32_t total = PMM_IDENTITY_MAPPED_LIMIT / PAGE_SIZE;
  if (pages == 0 || pages > total)
    return 0;

  uint32_t run = 0, start = 0;
  for (uint32_t i = 0; i < total; i++) {
    uint8_t mask = (uint8_t)(1u << (i & 7));
    if (!(bitmap[i >> 3] & mask)) {
      if (run == 0)
        start = i;
      if (++run == pages) {
        for (uint32_t j = start; j < start + pages; j++)
          bitmap[j >> 3] |= (uint8_t)(1u << (j & 7));
        return start * PAGE_SIZE;
      }
    } else {
      run = 0;
    }
  }
  return 0;
}

void pmm_free(uint32_t addr) {
  uint32_t page = addr / PAGE_SIZE;
  bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
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
    bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7));
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
    bitmap[i >> 3] |= (uint8_t)(1u << (i & 7));
}
