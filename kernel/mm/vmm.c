/*
 * vmm.c — Virtual Memory Manager
 *
 * 实现 x86 二级分页（Page Directory + Page Table），建立内核启动所需的
 * 恒等映射（identity mapping），并在最后写 CR3 / CR0 开启分页。
 *
 * 地址空间布局：
 *   0x00000000 – 0x00FFFFFF  (前 16 MB)  内核专属，Ring 0 only
 *   0x01000000 – 0xFFFFFFFF  (剩余空间)  内核 + 用户均可访问
 *
 * 依赖：
 *   pmm_alloc()  — 分配一个 4 KB 物理页，用于存放页表
 */

#include "vmm.h"
#include "pmm.h"
#include "vga.h"
#include <stdint.h>

/*
 * 全局页目录，必须 4 KB 对齐，CR3 直接指向此数组。
 * 每个条目（PDE）指向一张 1024 项的页表，覆盖 4 MB 虚拟地址。
 */
static uint32_t page_directory[1024] __attribute__((aligned(4096)));

/*
 * vmm_init - 初始化虚拟内存管理器
 *
 * 为全部 4 GB 地址空间建立恒等映射，然后将页目录基址写入 CR3，
 * 最后置位 CR0.PG 正式开启分页模式。
 *
 * 调用时机：内核早期初始化，PMM 就绪之后、任何用户态代码之前。
 */
void vmm_init() {
  // 恒等映射前 128MB（内核 + 页表）
  // 这样 vmm_alloc 分配的页表物理地址可以直接当虚拟地址访问
  for (int pd_idx = 0; pd_idx < 32; pd_idx++) {
    uint32_t *pt = (uint32_t *)pmm_alloc();
    for (int i = 0; i < 1024; i++)
      pt[i] = (pd_idx * 1024 + i) * 0x1000 | PAGE_PRESENT | PAGE_WRITE;
    page_directory[pd_idx] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE;
  }

  /* 将页目录物理地址写入 CR3（PDBR），TLB 同时被隐式刷新 */
  asm volatile("mov %0, %%cr3" ::"r"((uint32_t)page_directory));

  /* 置位 CR0.PG (bit 31)，CPU 从下一条指令起进入保护模式分页 */
  uint32_t cr0;
  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000;
  asm volatile("mov %0, %%cr0" ::"r"(cr0));

  print("[OK] VMM init\n", 0);
}

uint32_t vmm_alloc() {
  uint32_t phys;
  uint32_t *pt;
  for (int pd_idx = 32; pd_idx < 768; pd_idx++) {
    phys = pmm_alloc();
    if (!phys)
      return 0;
    if (page_directory[pd_idx] & PAGE_PRESENT) {
      pt = (uint32_t *)(page_directory[pd_idx] & ~0xFFF);
      if (!(page_directory[pd_idx] & PAGE_USER)) {
        pmm_free(phys);
        continue;
      }
    } else {
      pt = (uint32_t *)pmm_alloc();
      page_directory[pd_idx] =
          (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
      for (int i = 0; i < 1024; i++) {
        pt[i] = 0;
      }
    }
    for (int pt_idx = 0; pt_idx < 1024; pt_idx++) {
      if (!(pt[pt_idx] & PAGE_PRESENT)) {
        pt[pt_idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        uint32_t virt = (pd_idx << 22) | (pt_idx << 12);
        asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
        return virt;
      }
    }
    pmm_free(phys);
  }
  return 0;
}

uint32_t vmm_alloc_at(uint32_t virt) {
  uint32_t pd_idx = virt >> 22;
  uint32_t pt_idx = (virt >> 12) & 0x3FF;
  uint32_t *pt;

  if (page_directory[pd_idx] & PAGE_PRESENT) {
    pt = (uint32_t *)(page_directory[pd_idx] & ~0xFFF);
  } else {
    pt = (uint32_t *)pmm_alloc();
    if (!pt)
      return 0;
    page_directory[pd_idx] =
        (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    for (int i = 0; i < 1024; i++)
      pt[i] = 0;
  }

  if (pt[pt_idx] & PAGE_PRESENT)
    return 0;

  uint32_t phys = pmm_alloc();
  if (!phys)
    return 0;
  pt[pt_idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
  asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
  return virt;
}

int vmm_page_present(uint32_t virt) {
  uint32_t pd_idx = virt >> 22;
  if (!(page_directory[pd_idx] & PAGE_PRESENT))
    return 0;
  uint32_t *pt = (uint32_t *)(uintptr_t)(page_directory[pd_idx] & ~0xFFF);
  return (pt[(virt >> 12) & 0x3FF] & PAGE_PRESENT) ? 1 : 0;
}

void vmm_free(uint32_t virt) {
  uint32_t pd_idx = virt >> 22;
  uint32_t pt_idx = (virt >> 12) & 0x3FF;

  if (!(page_directory[pd_idx] & PAGE_PRESENT))
    return;

  uint32_t *pt = (uint32_t *)(uintptr_t)(page_directory[pd_idx] & ~0xFFF);
  if (!(pt[pt_idx] & PAGE_PRESENT))
    return;

  uint32_t phys = pt[pt_idx] & ~0xFFF;
  pt[pt_idx] = 0;
  asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
  pmm_free(phys);
}