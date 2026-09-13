// kernel/string.c

#include "string.h"
#include <stdint.h>

void kstrcpy(char *dst, const char *src) {
  while (*src) {
    *dst++ = *src++;
  }

  *dst = '\0';
}

void *kmemset(void *dst, int c, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  while (n--)
    *d++ = (uint8_t)c;
  return dst;
}

void *kmemcpy(void *dst, const void *src, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (n--)
    *d++ = *s++;
  return dst;
}

void *kmemmove(void *dst, const void *src, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (d < s) {
    while (n--)
      *d++ = *s++;
  } else if (d > s) {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }
  return dst;
}

uint32_t kstrlen(const char *s) {
  uint32_t n = 0;
  while (s[n])
    n++;
  return n;
}

int kstrcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, uint32_t n) {
  while (n && *a && *a == *b) {
    a++;
    b++;
    n--;
  }
  if (n == 0)
    return 0;
  return (unsigned char)*a - (unsigned char)*b;
}
