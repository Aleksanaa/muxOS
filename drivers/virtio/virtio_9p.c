/*
 * virtio_9p.c - virtio-9p host filesystem passthrough.
 *
 * Drives QEMU's `-virtfs local,...` device using the legacy (0.9.5)
 * virtio-pci interface: the ring layout is the one both QEMU and the virtio
 * spec compute for legacy PCI, so the device and driver agree without any
 * modern capability negotiation.
 *
 * Transport:
 *   PCI BAR0 (I/O) -> legacy virtio header, device config at +0x14
 *   one virtqueue, polled (device notifications suppressed)
 *   9P2000.u request/response in contiguous buffers of V9P_MSIZE_DEFAULT
 */

#include "9p.h"
#include "io.h"
#include "pci.h"
#include "pmm.h"
#include "string.h"
#include "vga.h"

/* --- legacy virtio-pci header (I/O BAR offsets) ------------------------ */
#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_NUM 0x0c
#define VIRTIO_PCI_QUEUE_SEL 0x0e
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_ISR 0x13
#define VIRTIO_PCI_CONFIG 0x14

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4

/* virtio device IDs for 9p (legacy id, and modern id 0x1040 + 9). */
#define VIRTIO_ID_9P 9
#define PCI_VENDOR_VIRTIO 0x1AF4

/* --- split virtqueue --------------------------------------------------- */
struct vring_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed));

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2
#define VRING_AVAIL_F_NO_INTERRUPT 1

struct vring_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

struct vring_used {
  uint16_t flags;
  uint16_t idx;
  struct vring_used_elem ring[];
} __attribute__((packed));

static uint16_t io_base;
static struct vring_desc *vq_desc;
static struct vring_avail *vq_avail;
static struct vring_used *vq_used;
static uint16_t vq_num;
static uint16_t vq_last_used;

/* Negotiated 9P message size.  QEMU warns when this is <= 8192; the buffers
 * below are sized to this and must be physically contiguous. */
#define V9P_MSIZE_DEFAULT 32768u
static uchar *v9p_req;
static uchar *v9p_resp;
static uint32_t v9p_msize = V9P_MSIZE_DEFAULT;
static char v9p_tag[32];
static char v9p_err[80];
static int v9p_ready;

#define VRING_ALIGN 4096u

static void mb(void) { asm volatile("" ::: "memory"); }

static int v9p_fail(const char *msg) {
  print("[9P] ", 0x0C);
  print(msg, 0x0C);
  print("\n", 0x0C);
  return 0;
}

/* Read the device's mount tag (2-byte length followed by the tag bytes). */
static void read_tag(void) {
  int i;
  uint16_t len = inw(io_base + VIRTIO_PCI_CONFIG);
  if (len > 31)
    len = 31;
  for (i = 0; i < len; i++)
    v9p_tag[i] = (char)inb(io_base + VIRTIO_PCI_CONFIG + 2 + i);
  v9p_tag[len] = 0;
}

static int setup_queue(uint16_t num) {
  uint32_t desc_sz = (uint32_t)num * sizeof(struct vring_desc);
  uint32_t avail_sz = 4 + 2 * (uint32_t)num; /* flags + idx + ring[num] */
  uint32_t used_off =
      (desc_sz + avail_sz + VRING_ALIGN - 1) & ~(VRING_ALIGN - 1);
  uint32_t used_sz = 4 + 8 * (uint32_t)num;
  uint32_t total = used_off + used_sz;
  uint32_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
  uint32_t mem = pmm_alloc_contig(pages);
  if (!mem)
    return -1;

  kmemset((void *)(uintptr_t)mem, 0, pages * PAGE_SIZE);
  vq_desc = (struct vring_desc *)(uintptr_t)mem;
  vq_avail = (struct vring_avail *)(uintptr_t)(mem + desc_sz);
  vq_used = (struct vring_used *)(uintptr_t)(mem + used_off);
  vq_num = num;
  vq_last_used = 0;
  vq_avail->flags = VRING_AVAIL_F_NO_INTERRUPT;

  outl(io_base + VIRTIO_PCI_QUEUE_PFN, mem >> 12);
  return 0;
}

int v9p_init(void) {
  struct pci_dev dev;

  if (!pci_find(PCI_VENDOR_VIRTIO,
                (uint16_t)(0x1000 + VIRTIO_ID_9P), &dev) &&
      !pci_find(PCI_VENDOR_VIRTIO,
                (uint16_t)(0x1040 + VIRTIO_ID_9P), &dev)) {
    print("[9P] no virtio-9p device\n", 0x0C);
    return 0;
  }

  print("[9P] found virtio-9p at ", 0x0B);
  print_hex(((uint32_t)dev.bus << 16) | ((uint32_t)dev.slot << 8) | dev.func);
  print("\n", 0x0B);

  pci_enable_busmaster(&dev);

  if (!(dev.bar[0] & 1))
    return v9p_fail("BAR0 is not an I/O BAR");
  io_base = (uint16_t)(dev.bar[0] & ~0x3u);

  /* Legacy handshake: reset, ACK, DRIVER. */
  outb(io_base + VIRTIO_PCI_STATUS, 0);
  outb(io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
  outb(io_base + VIRTIO_PCI_STATUS,
       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  /* Accept no optional features: the ring features we do not implement and
   * VIRTIO_F_VERSION_1 is a modern-only bit. */
  outl(io_base + VIRTIO_PCI_GUEST_FEATURES, 0);

  read_tag();
  print("[9P] tag: ", 0x0B);
  print(v9p_tag, 0x0B);
  print("\n", 0x0B);

  outw(io_base + VIRTIO_PCI_QUEUE_SEL, 0);
  uint16_t num = inw(io_base + VIRTIO_PCI_QUEUE_NUM);
  if (num == 0)
    return v9p_fail("queue 0 has size 0");
  if (num > 512)
    num = 512;

  if (setup_queue(num) < 0)
    return v9p_fail("no contiguous memory for ring");

  {
    uint32_t mbytes = v9p_msize;
    uint32_t reqmem = pmm_alloc_contig(mbytes / PAGE_SIZE);
    uint32_t respmem = pmm_alloc_contig(mbytes / PAGE_SIZE);
    if (!reqmem || !respmem)
      return v9p_fail("no memory for 9p buffers");
    v9p_req = (uchar *)(uintptr_t)reqmem;
    v9p_resp = (uchar *)(uintptr_t)respmem;
    kmemset(v9p_req, 0, mbytes);
    kmemset(v9p_resp, 0, mbytes);
  }

  outb(io_base + VIRTIO_PCI_STATUS,
       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
           VIRTIO_STATUS_DRIVER_OK);

  v9p_ready = 1;
  print("[OK] virtio-9p queue ready\n", 0x0A);
  return 1;
}

int v9p_rpc(Fcall *tx, Fcall *rx) {
  uint n, rlen;

  if (!v9p_ready)
    return -1;

  tx->tag = 0;
  n = sizeS2M(tx);
  if (n == 0 || n > v9p_msize)
    return -1;
  if (convS2M(tx, v9p_req, v9p_msize) != n)
    return -1;

  /* Descriptor 0: request (device reads it). */
  vq_desc[0].addr = (uint64_t)(uintptr_t)v9p_req;
  vq_desc[0].len = n;
  vq_desc[0].flags = VRING_DESC_F_NEXT;
  vq_desc[0].next = 1;
  /* Descriptor 1: response (device writes it). */
  vq_desc[1].addr = (uint64_t)(uintptr_t)v9p_resp;
  vq_desc[1].len = v9p_msize;
  vq_desc[1].flags = VRING_DESC_F_WRITE;
  vq_desc[1].next = 0;

  vq_avail->ring[vq_avail->idx % vq_num] = 0;
  mb();
  vq_avail->idx++;
  mb();
  outw(io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

  while (vq_used->idx == vq_last_used)
    mb();
  mb();

  rlen = vq_used->ring[vq_last_used % vq_num].len;
  vq_last_used++;
  (void)inb(io_base + VIRTIO_PCI_ISR);

  if (rlen < 9)
    return -1;

  /* 9P2000.u extends Rerror with a trailing 4-byte errcode, which the
   * stock convM2S rejects; extract the message by hand. */
  if (v9p_resp[4] == Rerror) {
    uint16_t elen = (uint16_t)(v9p_resp[7] | (v9p_resp[8] << 8));
    if (9 + (int)elen > (int)rlen)
      elen = (uint16_t)(rlen - 9);
    if (elen > sizeof(v9p_err) - 1)
      elen = sizeof(v9p_err) - 1;
    kmemcpy(v9p_err, v9p_resp + 9, elen);
    v9p_err[elen] = 0;
    return -1;
  }

  if (convM2S(v9p_resp, rlen, rx) == 0)
    return -1;
  if (rx->type != tx->type + 1)
    return -1;
  return 0;
}

const char *v9p_errstr(void) { return v9p_err[0] ? v9p_err : "protocol error"; }
const char *v9p_get_tag(void) { return v9p_tag; }
uint32_t v9p_get_msize(void) { return v9p_msize; }
void v9p_set_msize(uint32_t m) {
  if (m && m <= V9P_MSIZE_DEFAULT)
    v9p_msize = m;
}
