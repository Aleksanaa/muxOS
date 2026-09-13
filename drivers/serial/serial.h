#ifndef SERIAL_H
#define SERIAL_H
void serial_init(void);
char serial_getchar(void);
int serial_haschar(void);
void serial_putchar(char c);
#endif
