#ifndef GDT_H
#define GDT_H

struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

struct gdt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

void gdt_init();
void gdt_set(int i, unsigned int base, unsigned int limit,
             unsigned int access, unsigned int gran);

/* Entry 6 is a user data descriptor whose base is the per-thread TLS block;
 * user space reaches it through USER_TLS (selector 0x33). */
void gdt_set_tls_base(unsigned int base);

#define USER_CS 0x1B
#define USER_DS 0x23
#define USER_TLS 0x33

#endif