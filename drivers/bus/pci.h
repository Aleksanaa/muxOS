#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* PCI configuration space, accessed through the 0xCF8/0xCFC I/O ports. */
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_REVISION 0x08
#define PCI_PROG_IF 0x09
#define PCI_SUBCLASS 0x0A
#define PCI_CLASS 0x0B
#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0 0x10

#define PCI_COMMAND_IO 0x0001
#define PCI_COMMAND_MEM 0x0002
#define PCI_COMMAND_BUSMASTER 0x0004

struct pci_dev {
  uint8_t bus, slot, func;
  uint16_t vendor, device;
  uint8_t class, subclass, progif, header_type;
  uint32_t bar[6];
};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint32_t val);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint16_t val);

/* Fill `out` for the first device matching vendor/device.  Returns 1 on hit. */
int pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out);

/* Enable I/O space and bus-mastering for a device. */
void pci_enable_busmaster(struct pci_dev *d);

#endif
