/*
 * vmm.c — Virtual Memory Manager
 *
 * x86 two-level paging.  The boot page directory (vmm_init) identity-maps the
 * first 128 MiB so kernel code and pmm_alloc() pages are directly
 * dereferenceable.  Each process gets its own page directory created by
 * vmm_create_pdir(), which shares those first 32 PDEs; user mappings live at
 * higher addresses and are private to the process.
 */

#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "vga.h"
#include <stdint.h>

static uint32_t kernel_pdir[1024] __attribute__((aligned(4096)));

#define PDIR_ENTRIES 1024
#define KERNEL_PDES 32 /* 32 * 4 MiB = 128 MiB identity-mapped */

static inline uint32_t *pdir_ptr(uint32_t phys) {
  return (uint32_t *)(uintptr_t)phys;
}

void vmm_init() {
  // 恒等映射前 128MB（内核 + 页表）
  for (int pd_idx = 0; pd_idx < KERNEL_PDES; pd_idx++) {
    uint32_t *pt = (uint32_t *)pmm_alloc();
    for (int i = 0; i < 1024; i++)
      pt[i] = (pd_idx * 1024 + i) * 0x1000 | PAGE_PRESENT | PAGE_WRITE;
    kernel_pdir[pd_idx] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE;
  }

  asm volatile("mov %0, %%cr3" ::"r"((uint32_t)kernel_pdir));

  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000;
  asm volatile("mov %0, %%cr0" ::"r"(cr0));

  print("[OK] VMM init\n", 0);
}

uint32_t vmm_kernel_pdir(void) { return (uint32_t)(uintptr_t)kernel_pdir; }

uint32_t vmm_current_pdir(void) {
  uint32_t cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3;
}

void vmm_switch_pdir(uint32_t pdir) {
  asm volatile("mov %0, %%cr3" ::"r"(pdir) : "memory");
}

uint32_t vmm_create_pdir(void) {
  uint32_t pdir = pmm_alloc();
  if (!pdir)
    return 0;
  uint32_t *pd = pdir_ptr(pdir);
  kmemset(pd, 0, 4096);
  for (int i = 0; i < KERNEL_PDES; i++)
    pd[i] = kernel_pdir[i];
  return pdir;
}

/* Free every user page/table in `pdir`, then the directory itself.  The
 * caller must not be running on it. */
void vmm_destroy_pdir(uint32_t pdir) {
  uint32_t *pd = pdir_ptr(pdir);

  for (int i = KERNEL_PDES; i < PDIR_ENTRIES; i++) {
    if (!(pd[i] & PAGE_PRESENT))
      continue;
    uint32_t *pt = pdir_ptr(pd[i] & ~0xFFFu);
    for (int j = 0; j < 1024; j++) {
      if (pt[j] & PAGE_PRESENT)
        pmm_free(pt[j] & ~0xFFFu);
    }
    pmm_free(pd[i] & ~0xFFFu);
  }
  pmm_free(pdir);
}

/* Deep-copy every user mapping from src into dst (which must already have the
 * kernel PDEs).  Each page is copied to a fresh physical page. */
void vmm_copy_pdir(uint32_t dst, uint32_t src) {
  uint32_t *spd = pdir_ptr(src);
  uint32_t *dpd = pdir_ptr(dst);

  for (int i = KERNEL_PDES; i < PDIR_ENTRIES; i++) {
    if (!(spd[i] & PAGE_PRESENT))
      continue;
    uint32_t *spt = pdir_ptr(spd[i] & ~0xFFFu);
    uint32_t npt = pmm_alloc();
    if (!npt)
      return;
    uint32_t *dpt = pdir_ptr(npt);
    kmemset(dpt, 0, 4096);
    dpd[i] = npt | (spd[i] & 0xFFFu);

    for (int j = 0; j < 1024; j++) {
      if (!(spt[j] & PAGE_PRESENT))
        continue;
      uint32_t np = pmm_alloc();
      if (!np)
        return;
      kmemcpy((void *)(uintptr_t)np, (void *)(uintptr_t)(spt[j] & ~0xFFFu),
              4096);
      dpt[j] = np | (spt[j] & 0xFFFu);
    }
  }
}

uint32_t vmm_alloc_at(uint32_t pdir, uint32_t virt) {
  uint32_t *pd = pdir_ptr(pdir);
  uint32_t pd_idx = virt >> 22;
  uint32_t pt_idx = (virt >> 12) & 0x3FF;
  uint32_t *pt;

  if (pd[pd_idx] & PAGE_PRESENT) {
    pt = pdir_ptr(pd[pd_idx] & ~0xFFFu);
  } else {
    uint32_t npt = pmm_alloc();
    if (!npt)
      return 0;
    pd[pd_idx] = npt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    pt = pdir_ptr(npt);
    kmemset(pt, 0, 4096);
  }

  if (pt[pt_idx] & PAGE_PRESENT)
    return 0;

  uint32_t phys = pmm_alloc();
  if (!phys)
    return 0;
  pt[pt_idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
  asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
  return virt;
}

uint32_t vmm_phys(uint32_t pdir, uint32_t virt) {
  uint32_t *pd = pdir_ptr(pdir);
  uint32_t pd_idx = virt >> 22;
  if (!(pd[pd_idx] & PAGE_PRESENT))
    return 0;
  uint32_t *pt = pdir_ptr(pd[pd_idx] & ~0xFFFu);
  uint32_t pte = pt[(virt >> 12) & 0x3FF];
  if (!(pte & PAGE_PRESENT))
    return 0;
  return (pte & ~0xFFFu) + (virt & 0xFFFu);
}

int vmm_page_present(uint32_t pdir, uint32_t virt) {
  return vmm_phys(pdir, virt) != 0;
}

void vmm_free(uint32_t pdir, uint32_t virt) {
  uint32_t *pd = pdir_ptr(pdir);
  uint32_t pd_idx = virt >> 22;
  uint32_t pt_idx = (virt >> 12) & 0x3FF;

  if (!(pd[pd_idx] & PAGE_PRESENT))
    return;
  uint32_t *pt = pdir_ptr(pd[pd_idx] & ~0xFFFu);
  if (!(pt[pt_idx] & PAGE_PRESENT))
    return;

  uint32_t phys = pt[pt_idx] & ~0xFFFu;
  pt[pt_idx] = 0;
  asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
  pmm_free(phys);
}
