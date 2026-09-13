/*
 * elf.c - static ELF32/i386 program loader.
 *
 * Segments are mapped into a caller-supplied page directory and written
 * through the physical addresses returned by vmm_phys(), so a program can be
 * loaded into a process that is not the one currently running.
 */

#include "elf.h"
#include "fs.h"
#include "string.h"
#include "vmm.h"
#include <stdint.h>

static int check_ehdr(const elf32_ehdr_t *eh) {
  if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
      eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
    return -1;
  if (eh->e_ident[4] != ELFCLASS32 || eh->e_machine != EM_386)
    return -1;
  return 0;
}

static int map_segment(uint32_t pdir, const elf32_phdr_t *ph) {
  uint32_t vstart = ph->p_vaddr & ~0xFFFu;
  uint32_t vend = (ph->p_vaddr + ph->p_memsz + 0xFFFu) & ~0xFFFu;

  for (uint32_t va = vstart; va < vend; va += 0x1000) {
    if (!vmm_page_present(pdir, va)) {
      if (!vmm_alloc_at(pdir, va))
        return -1;
    }
  }
  return 0;
}

static int copy_to_pdir(uint32_t pdir, uint32_t vaddr, const void *src,
                        uint32_t n) {
  const uint8_t *s = src;
  while (n) {
    uint32_t chunk = 0x1000 - (vaddr & 0xFFF);
    if (chunk > n)
      chunk = n;
    uint32_t phys = vmm_phys(pdir, vaddr);
    if (!phys)
      return -1;
    kmemcpy((void *)(uintptr_t)phys, s, chunk);
    vaddr += chunk;
    s += chunk;
    n -= chunk;
  }
  return 0;
}

static void zero_in_pdir(uint32_t pdir, uint32_t vaddr, uint32_t n) {
  while (n) {
    uint32_t chunk = 0x1000 - (vaddr & 0xFFF);
    if (chunk > n)
      chunk = n;
    uint32_t phys = vmm_phys(pdir, vaddr);
    if (!phys)
      return;
    kmemset((void *)(uintptr_t)phys, 0, chunk);
    vaddr += chunk;
    n -= chunk;
  }
}

int elf_load(uint32_t pdir, const void *image, uint32_t size, uint32_t *entry) {
  const uint8_t *img = (const uint8_t *)image;

  if (size < sizeof(elf32_ehdr_t))
    return -1;
  const elf32_ehdr_t *eh = (const elf32_ehdr_t *)img;
  if (check_ehdr(eh) < 0)
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

    if (map_segment(pdir, ph) < 0)
      return -1;
    if (ph->p_filesz &&
        copy_to_pdir(pdir, ph->p_vaddr, img + ph->p_offset, ph->p_filesz) < 0)
      return -1;
    if (ph->p_memsz > ph->p_filesz)
      zero_in_pdir(pdir, ph->p_vaddr + ph->p_filesz,
                   ph->p_memsz - ph->p_filesz);
  }

  *entry = eh->e_entry;
  return 0;
}

int elf_load_inode(uint32_t pdir, struct inode *ip, uint32_t *entry) {
  elf32_ehdr_t eh;
  if (readi(ip, &eh, 0, sizeof(eh)) != (int)sizeof(eh))
    return -1;
  if (check_ehdr(&eh) < 0)
    return -1;

  for (uint32_t i = 0; i < eh.e_phnum; i++) {
    elf32_phdr_t ph;
    uint32_t phoff = eh.e_phoff + i * eh.e_phentsize;
    if (readi(ip, &ph, phoff, sizeof(ph)) != (int)sizeof(ph))
      return -1;
    if (ph.p_type != PT_LOAD || ph.p_memsz == 0)
      continue;

    if (map_segment(pdir, &ph) < 0)
      return -1;

    uint32_t done = 0;
    while (done < ph.p_filesz) {
      uint32_t va = ph.p_vaddr + done;
      uint32_t chunk = 0x1000 - (va & 0xFFF);
      if (chunk > ph.p_filesz - done)
        chunk = ph.p_filesz - done;
      uint32_t phys = vmm_phys(pdir, va);
      if (!phys ||
          readi(ip, (void *)(uintptr_t)phys, ph.p_offset + done, chunk) !=
              (int)chunk)
        return -1;
      done += chunk;
    }
    if (ph.p_memsz > ph.p_filesz)
      zero_in_pdir(pdir, ph.p_vaddr + ph.p_filesz, ph.p_memsz - ph.p_filesz);
  }

  *entry = eh.e_entry;
  return 0;
}
