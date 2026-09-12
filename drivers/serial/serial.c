// serial.c - UART 16550 driver (COM1, 0x3F8)
#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); // 关中断
    outb(COM1 + 3, 0x80); // 启用 DLAB（设置波特率）
    outb(COM1 + 0, 0x03); // 38400 波特（低字节）
    outb(COM1 + 1, 0x00); // 高字节
    outb(COM1 + 3, 0x03); // 8位, 无校验, 1停止位，关闭 DLAB
    outb(COM1 + 2, 0xC7); // 启用 FIFO
    outb(COM1 + 4, 0x0B); // IRQ 启用，RTS/DSR 置位
}

char serial_getchar(void) {
    while (!(inb(COM1 + 5) & 0x01)); // 等待数据就绪
    return (char)inb(COM1);
}

void serial_putchar(char c) {
    while (!(inb(COM1 + 5) & 0x20)); // 等待发送缓冲区空
    if (c == '\n') {
        outb(COM1, '\r'); // 终端需要 \r\n
        while (!(inb(COM1 + 5) & 0x20));
    }
    outb(COM1, (unsigned char)c);
}
