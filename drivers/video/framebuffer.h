#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

void framebuffer_init(uint32_t address, uint32_t width, uint32_t height,
                      uint32_t pitch, uint8_t bpp);

void framebuffer_clear(uint32_t color);

void framebuffer_putpixel(uint32_t x, uint32_t y, uint32_t color);

void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t color);

uint32_t framebuffer_get_width(void);
uint32_t framebuffer_get_height(void);
uint32_t framebuffer_get_pitch(void);
uint8_t framebuffer_get_bpp(void);
uint32_t framebuffer_get_address(void);

#endif