#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stdint.h>

/* k-prefixed to avoid colliding with the user C library, which is linked
 * into the same kernel ELF image by linker.ld. */
void kstrcpy(char *dst, const char *src);

void *kmemset(void *dst, int c, uint32_t n);
void *kmemcpy(void *dst, const void *src, uint32_t n);
void *kmemmove(void *dst, const void *src, uint32_t n);

uint32_t kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, uint32_t n);

#endif
