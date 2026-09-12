#pragma once
#include <stdint.h>

#define KB_BUFFER_SIZE 128

extern volatile int kb_head;
extern volatile int kb_tail;

struct interrupt_frame {
    uint32_t ip, cs, flags;
};

__attribute__((interrupt))
void keyboard_handler(struct interrupt_frame *frame);
void kb_buf_push(char c);
void keyboard_init(void);
char kb_getchar();