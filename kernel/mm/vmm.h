#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#define PAGE_PRESENT 0x1 // 页存在
#define PAGE_WRITE 0x2   // 可写
#define PAGE_USER 0x4    // 用户态可访问

void vmm_init();

/*
 * Page directories.  Every process owns one.  The first 32 PDEs (the kernel
 * identity map, 0..128 MiB) are shared by all of them so kernel code, page
 * tables and physical pages stay addressable after a CR3 switch.
 */
uint32_t vmm_kernel_pdir(void);
uint32_t vmm_current_pdir(void);
uint32_t vmm_create_pdir(void);
void vmm_destroy_pdir(uint32_t pdir);
void vmm_copy_pdir(uint32_t dst, uint32_t src);
void vmm_switch_pdir(uint32_t pdir);

/* Allocate a user page at a fixed virtual address in `pdir`. */
uint32_t vmm_alloc_at(uint32_t pdir, uint32_t virt);
/* Physical address backing `virt`, or 0. */
uint32_t vmm_phys(uint32_t pdir, uint32_t virt);
void vmm_free(uint32_t pdir, uint32_t virt);
int vmm_page_present(uint32_t pdir, uint32_t virt);

#endif
