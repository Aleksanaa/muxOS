/*
 * pci.c - minimal PCI configuration-space access.
 *
 * Only what the legacy virtio transport needs: read/write config words and
 * scan for a device.  Uses the classic 0xCF8/0xCFC mechanism, which covers
 * the QEMU i440fx/q35 machines this kernel boots on.
 */

#include "pci.h"
#include "io.h"

static uint32_t pci_addr(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t off) {
  return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
         ((uint32_t)func << 8) | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
  outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, off));
  return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
  uint32_t v = pci_read32(bus, slot, func, off & ~3u);
  return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
  uint32_t v = pci_read32(bus, slot, func, off & ~3u);
  return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint32_t val) {
  outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, off));
  outl(PCI_CONFIG_DATA, val);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint16_t val) {
  uint32_t v = pci_read32(bus, slot, func, off & ~3u);
  uint32_t shift = (off & 2) * 8;
  v &= ~(0xFFFFu << shift);
  v |= (uint32_t)val << shift;
  pci_write32(bus, slot, func, off & ~3u, v);
}

void pci_enable_busmaster(struct pci_dev *d) {
  uint16_t cmd = pci_read16(d->bus, d->slot, d->func, PCI_COMMAND);
  cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_BUSMASTER;
  pci_write16(d->bus, d->slot, d->func, PCI_COMMAND, cmd);
}

static void pci_fill(uint8_t bus, uint8_t slot, uint8_t func,
                     struct pci_dev *out) {
  out->bus = bus;
  out->slot = slot;
  out->func = func;
  out->vendor = pci_read16(bus, slot, func, PCI_VENDOR_ID);
  out->device = pci_read16(bus, slot, func, PCI_DEVICE_ID);
  out->class = pci_read8(bus, slot, func, PCI_CLASS);
  out->subclass = pci_read8(bus, slot, func, PCI_SUBCLASS);
  out->progif = pci_read8(bus, slot, func, PCI_PROG_IF);
  out->header_type = pci_read8(bus, slot, func, PCI_HEADER_TYPE);
  for (int i = 0; i < 6; i++)
    out->bar[i] = pci_read32(bus, slot, func, PCI_BAR0 + i * 4);
}

int pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out) {
  for (int bus = 0; bus < 256; bus++) {
    for (int slot = 0; slot < 32; slot++) {
      uint16_t v = pci_read16((uint8_t)bus, (uint8_t)slot, 0, PCI_VENDOR_ID);
      if (v == 0xFFFF)
        continue;
      uint8_t hdr = pci_read8((uint8_t)bus, (uint8_t)slot, 0, PCI_HEADER_TYPE);
      int nfunc = (hdr & 0x80) ? 8 : 1;
      for (int func = 0; func < nfunc; func++) {
        uint16_t vv = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                                 PCI_VENDOR_ID);
        if (vv != vendor)
          continue;
        uint16_t dd = pci_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                                 PCI_DEVICE_ID);
        if (dd != device)
          continue;
        pci_fill((uint8_t)bus, (uint8_t)slot, (uint8_t)func, out);
        return 1;
      }
    }
  }
  return 0;
}
