// kernel/string.c

void kstrcpy(char *dst, const char *src) {
  while (*src) {
    *dst++ = *src++;
  }

  *dst = '\0';
}