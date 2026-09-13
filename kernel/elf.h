#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* Minimal ELF32 definitions, enough to load static i386 executables. */
typedef struct {
  uint8_t e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
  uint32_t p_type;
  uint32_t p_offset;
  uint32_t p_vaddr;
  uint32_t p_paddr;
  uint32_t p_filesz;
  uint32_t p_memsz;
  uint32_t p_flags;
  uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

#define ELFCLASS32 1
#define EM_386 3
#define PT_LOAD 1

struct inode;

/* Map every PT_LOAD segment and return the entry point.  Returns 0 on
 * success, -1 if the image is malformed or a page cannot be mapped. */
int elf_load(const void *image, uint32_t size, uint32_t *entry);

/* Same, but reads the image from an open inode, so a multi-megabyte program
 * never has to be staged in a kernel buffer. */
int elf_load_inode(struct inode *ip, uint32_t *entry);

#endif
