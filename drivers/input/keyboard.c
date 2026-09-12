#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "vga.h"
char kb_buf[KB_BUFFER_SIZE];
volatile int kb_head = 0;
volatile int kb_tail = 0;
void kb_buf_push(char c);
// Scancode set 1: index = scancode, value = ASCII (0 = no printable char)
static const char scancode_map[128] = {
    /*00*/ 0,
    /*01*/ 0, // ESC
    /*02*/ '1',  '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    /*0E*/ '\b',
    /*0F*/ '\t',
    /*10*/ 'q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    /*1C*/ '\n',
    /*1D*/ 0, // left ctrl
    /*1E*/ 'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    /*2A*/ 0, // left shift
    /*2B*/ '\\',
    /*2C*/ 'z',  'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    /*36*/ 0, // right shift
    /*37*/ '*',
    /*38*/ 0, // left alt
    /*39*/ ' ',
};

static const char shift_map[128] = {
    /*00*/ 0,
    /*01*/ 0, // ESC
    /*02*/ '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
    /*0E*/ '\b',
    /*0F*/ '\t',
    /*10*/ 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    /*1C*/ '\n',
    /*1D*/ 0, // left ctrl
    /*1E*/ 'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    /*2A*/ 0, // left shift
    /*2B*/ '|',
    /*2C*/ 'Z',  'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    /*36*/ 0, // right shift
    /*37*/ '*',
    /*38*/ 0, // left alt
    /*39*/ ' ',
};

static volatile int shift = 0;

__attribute__((interrupt)) void
keyboard_handler(struct interrupt_frame *frame) {
  uint8_t scancode = inb(0x60);
  if (scancode & 0x80) { // 按下
    // key release: check if shift released
    uint8_t sc = scancode & 0x7F;
    if (sc == 0x2A || sc == 0x36)
      shift = 0;
  } else {
    // key press
    if (scancode == 0x2A || scancode == 0x36) {
      shift = 1;
      pic_eoi(1);
      return;
    }
    const char *map = shift ? shift_map : scancode_map;
    char c = (scancode < 128) ? map[scancode] : 0;
    if (c)
      kb_buf_push(c);
  }
  pic_eoi(1);
}

void keyboard_init(void) {
  // 解除 PIC1 的 IRQ1 屏蔽（清 bit1）
  uint8_t mask = inb(0x21);
  outb(0x21, mask & ~0x02);
  print("[OK] Keyboard init\n", 0);
}

void kb_buf_push(char c) {
  int next = (kb_head + 1) % KB_BUFFER_SIZE;
  if (next != kb_tail) {
    kb_buf[kb_head] = c;
    kb_head = next;
  }
}

char kb_getchar() {
  while (kb_head == kb_tail) {
  };
  char c = kb_buf[kb_tail];
  kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
  return c;
}