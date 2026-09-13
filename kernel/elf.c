/*
 * elf.c - static ELF32/i386 program loader.
 *
 * Adapted from xv6's exec.c (MIT).  Segments are mapped with vmm_alloc_at(),
 * which hands back identity-mapped pages in the kernel's shared address space,
 * so the loader can copy segment bytes in directly.
 */

#include "elf.h"
#include "string.h"
#include "vmm.h"
#include <stdint.h>

int elf_load(const void *image, uint32_t size, uint32_t *entry) {
  const uint8_t *img = (const uint8_t *)image;

  if (size < sizeof(elf32_ehdr_t))
    return -1;
  const elf32_ehdr_t *eh = (const elf32_ehdr_t *)img;
  if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
      eh->e_ident[3] != 'F')
    return -1;
  if (eh->e_ident[4] != ELFCLASS32 || eh->e_machine != EM_386)
    return -1;

  for (uint32_t i = 0; i < eh->e_phnum; i++) {
    uint32_t phoff = eh->e_phoff + i * eh->e_phentsize;
    if (phoff + sizeof(elf32_phdr_t) > size)
      return -1;
    const elf32_phdr_t *ph = (const elf32_phdr_t *)(img + phoff);
    if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
      continue;
    if (ph->p_offset + ph->p_filesz > size)
      return -1;

    uint32_t vstart = ph->p_vaddr & ~0xFFFu;
    uint32_t vend = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;
    for (uint32_t va = vstart; va < vend; va += 0x1000) {
      if (!vmm_page_present(va)) {
        if (!vmm_alloc_at(va))
          return -1;
      }
    }

    kmemcpy((void *)(uintptr_t)ph->p_vaddr, img + ph->p_offset, ph->p_filesz);
    kmemset((void *)(uintptr_t)(ph->p_vaddr + ph->p_filesz), 0,
            ph->p_memsz - ph->p_filesz);
  }

  *entry = eh->e_entry;
  return 0;
}
