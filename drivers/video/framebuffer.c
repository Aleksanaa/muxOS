#include "framebuffer.h"

static uint8_t *framebuffer = 0;

static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint8_t fb_bpp = 0;

void framebuffer_init(uint32_t address, uint32_t width, uint32_t height,
                      uint32_t pitch, uint8_t bpp) {
  framebuffer = (uint8_t *)address;

  fb_width = width;
  fb_height = height;
  fb_pitch = pitch;
  fb_bpp = bpp;
}

void framebuffer_putpixel(uint32_t x, uint32_t y, uint32_t color) {
  if (framebuffer == 0)
    return;

  if (x >= fb_width || y >= fb_height)
    return;

  uint32_t bytes_per_pixel = fb_bpp / 8;

  uint32_t offset = y * fb_pitch + x * bytes_per_pixel;

  if (fb_bpp == 32) {
    *(uint32_t *)(framebuffer + offset) = color;
  } else if (fb_bpp == 24) {
    framebuffer[offset + 0] = color & 0xFF;
    framebuffer[offset + 1] = (color >> 8) & 0xFF;
    framebuffer[offset + 2] = (color >> 16) & 0xFF;
  }
}

void framebuffer_clear(uint32_t color) {
  if (framebuffer == 0)
    return;

  for (uint32_t y = 0; y < fb_height; y++) {
    for (uint32_t x = 0; x < fb_width; x++) {
      framebuffer_putpixel(x, y, color);
    }
  }
}

void framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t color) {
  if (framebuffer == 0)
    return;

  for (uint32_t iy = 0; iy < height; iy++) {
    for (uint32_t ix = 0; ix < width; ix++) {
      framebuffer_putpixel(x + ix, y + iy, color);
    }
  }
}

uint32_t framebuffer_get_width(void) { return fb_width; }

uint32_t framebuffer_get_height(void) { return fb_height; }

uint32_t framebuffer_get_pitch(void) { return fb_pitch; }

uint8_t framebuffer_get_bpp(void) { return fb_bpp; }

uint32_t framebuffer_get_address(void) { return (uint32_t)framebuffer; }