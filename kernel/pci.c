#include <log.h>
#include <memory.h>
#include <pci.h>
#include <stdint.h>
#include <x86_64/io.h>

#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA    0xcfc
#define PCI_VENDOR_INVALID 0xffff
#define PCI_CLASS_SERIAL_BUS 0x0c
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30
#define PCI_VENDOR_QEMU 0x1b36
#define PCI_DEVICE_QEMU_XHCI 0x000d

#define PCI_COMMAND_IO_SPACE     0x0001
#define PCI_COMMAND_MEMORY_SPACE 0x0002
#define PCI_COMMAND_BUS_MASTER   0x0004

#define XHCI_USBCMD 0x00
#define XHCI_USBSTS 0x04
#define XHCI_PAGESIZE 0x08
#define XHCI_USBCMD_RS    (1U << 0)
#define XHCI_USBCMD_HCRST (1U << 1)
#define XHCI_USBSTS_HCH   (1U << 0)
#define XHCI_USBSTS_CNR   (1U << 11)
#define XHCI_POLL_LIMIT 1000000ULL

static struct pci_xhci_info xhci_info;

static void zero_bytes(void *ptr, uint64_t len) {
    uint8_t *out = ptr;
    for (uint64_t i = 0; i < len; i++) {
        out[i] = 0;
    }
}

static void copy_bytes(void *dst, const void *src, uint64_t len) {
    uint8_t *out = dst;
    const uint8_t *in = src;
    for (uint64_t i = 0; i < len; i++) {
        out[i] = in[i];
    }
}

static uint32_t mmio_read32(volatile uint8_t *base, uint64_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

static void mmio_write32(volatile uint8_t *base, uint64_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
    __sync_synchronize();
}

static int mmio_wait32_set(volatile uint8_t *base, uint64_t offset, uint32_t mask) {
    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        if ((mmio_read32(base, offset) & mask) == mask) {
            return 0;
        }
    }
    return -1;
}

static int mmio_wait32_clear(volatile uint8_t *base, uint64_t offset, uint32_t mask) {
    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        if ((mmio_read32(base, offset) & mask) == 0) {
            return 0;
        }
    }
    return -1;
}

static uint32_t pci_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return 0x80000000U |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           (offset & 0xfc);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, function, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_read_config32(bus, slot, function, offset);
    return (uint16_t)(value >> ((offset & 2) * 8));
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t current = pci_read_config32(bus, slot, function, offset);
    uint32_t shift = (offset & 2) * 8;
    current &= ~(0xffffU << shift);
    current |= (uint32_t)value << shift;
    pci_write_config32(bus, slot, function, offset, current);
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_read_config32(bus, slot, function, offset);
    return (uint8_t)(value >> ((offset & 3) * 8));
}

static int pci_read_device(uint8_t bus, uint8_t slot, uint8_t function, struct pci_device *out) {
    uint32_t id = pci_read_config32(bus, slot, function, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xffff);

    if (vendor == PCI_VENDOR_INVALID) {
        return -1;
    }

    uint32_t class = pci_read_config32(bus, slot, function, 0x08);
    out->bus = bus;
    out->slot = slot;
    out->function = function;
    out->vendor_id = vendor;
    out->device_id = (uint16_t)(id >> 16);
    out->class_code = (uint8_t)(class >> 24);
    out->subclass = (uint8_t)(class >> 16);
    out->prog_if = (uint8_t)(class >> 8);
    out->revision = (uint8_t)class;
    return 0;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device *out) {
    struct pci_device device;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                if (pci_read_device((uint8_t)bus, slot, function, &device) != 0) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                if (device.vendor_id == vendor_id && device.device_id == device_id) {
                    *out = device;
                    return 0;
                }
                if (function == 0 && (pci_read_config8((uint8_t)bus, slot, function, 0x0e) & 0x80) == 0) {
                    break;
                }
            }
        }
    }

    return -1;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, struct pci_device *out) {
    struct pci_device device;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                if (pci_read_device((uint8_t)bus, slot, function, &device) != 0) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                if (device.class_code == class_code &&
                    device.subclass == subclass &&
                    device.prog_if == prog_if) {
                    if (out != 0) {
                        *out = device;
                    }
                    return 0;
                }
                if (function == 0 && (pci_read_config8((uint8_t)bus, slot, function, 0x0e) & 0x80) == 0) {
                    break;
                }
            }
        }
    }

    return -1;
}

uint32_t pci_bar(const struct pci_device *device, uint8_t bar_index) {
    if (device == 0 || bar_index >= 6) {
        return 0;
    }
    return pci_read_config32(device->bus, device->slot, device->function, (uint8_t)(0x10 + bar_index * 4));
}

static void xhci_probe_operational(const struct pci_device *device, volatile uint8_t *regs) {
    volatile uint8_t *op;

    if (regs == 0 || xhci_info.cap_length == 0) {
        return;
    }

    op = regs + xhci_info.cap_length;
    xhci_info.op_regs_ready = 1;
    xhci_info.op_usbcmd_before = mmio_read32(op, XHCI_USBCMD);
    xhci_info.op_usbsts_before = mmio_read32(op, XHCI_USBSTS);
    xhci_info.op_pagesize = mmio_read32(op, XHCI_PAGESIZE);

    xhci_info.reset_allowed = (device->vendor_id == PCI_VENDOR_QEMU &&
                               device->device_id == PCI_DEVICE_QEMU_XHCI) ? 1 : 0;
    xhci_info.op_usbcmd_after = xhci_info.op_usbcmd_before;
    xhci_info.op_usbsts_after = xhci_info.op_usbsts_before;
    if (!xhci_info.reset_allowed) {
        return;
    }

    xhci_info.reset_attempted = 1;
    if ((xhci_info.op_usbcmd_before & XHCI_USBCMD_RS) != 0) {
        mmio_write32(op, XHCI_USBCMD, (uint32_t)xhci_info.op_usbcmd_before & ~XHCI_USBCMD_RS);
    }
    if (mmio_wait32_set(op, XHCI_USBSTS, XHCI_USBSTS_HCH) != 0) {
        xhci_info.op_usbcmd_after = mmio_read32(op, XHCI_USBCMD);
        xhci_info.op_usbsts_after = mmio_read32(op, XHCI_USBSTS);
        return;
    }
    xhci_info.halt_ok = 1;

    mmio_write32(op, XHCI_USBCMD, mmio_read32(op, XHCI_USBCMD) | XHCI_USBCMD_HCRST);
    if (mmio_wait32_clear(op, XHCI_USBCMD, XHCI_USBCMD_HCRST) == 0) {
        xhci_info.reset_ok = 1;
    }
    if (mmio_wait32_clear(op, XHCI_USBSTS, XHCI_USBSTS_CNR) == 0) {
        xhci_info.ready_ok = 1;
    }

    xhci_info.op_usbcmd_after = mmio_read32(op, XHCI_USBCMD);
    xhci_info.op_usbsts_after = mmio_read32(op, XHCI_USBSTS);
}

static void pci_scan_xhci(void) {
    struct pci_device device;

    zero_bytes(&xhci_info, sizeof(xhci_info));
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                if (pci_read_device((uint8_t)bus, slot, function, &device) != 0) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                if (device.class_code == PCI_CLASS_SERIAL_BUS &&
                    device.subclass == PCI_SUBCLASS_USB &&
                    device.prog_if == PCI_PROGIF_XHCI) {
                    uint32_t bar0 = pci_bar(&device, 0);
                    uint32_t bar1 = pci_bar(&device, 1);
                    uint64_t mmio_base = bar0 & 0xfffffff0U;
                    int is_64bit = (bar0 & 0x6U) == 0x4U;

                    if (is_64bit) {
                        mmio_base |= (uint64_t)bar1 << 32;
                    }
                    xhci_info.count++;
                    if (!xhci_info.found) {
                        volatile uint8_t *regs = 0;
                        pci_enable_mmio_bus_master(&device);
                        xhci_info.found = 1;
                        xhci_info.bus = device.bus;
                        xhci_info.slot = device.slot;
                        xhci_info.function = device.function;
                        xhci_info.vendor_id = device.vendor_id;
                        xhci_info.device_id = device.device_id;
                        xhci_info.revision = device.revision;
                        xhci_info.irq_line = pci_read_config8(device.bus, device.slot, device.function, 0x3c);
                        xhci_info.bar0_raw = bar0;
                        xhci_info.bar0_mmio_base = mmio_base;
                        xhci_info.bar0_is_64bit = is_64bit ? 1 : 0;
                        regs = (volatile uint8_t *)mmio_map_region(mmio_base, 0x1000);
                        if (regs != 0) {
                            uint32_t capreg;
                            uint32_t hcsparams1;
                            xhci_info.mmio_mapped = 1;
                            capreg = *(volatile uint32_t *)(regs + 0x00);
                            xhci_info.cap_length = capreg & 0xffU;
                            xhci_info.hci_version = (capreg >> 16) & 0xffffU;
                            hcsparams1 = *(volatile uint32_t *)(regs + 0x04);
                            xhci_info.max_slots = hcsparams1 & 0xffU;
                            xhci_info.max_interrupters = (hcsparams1 >> 8) & 0x7ffU;
                            xhci_info.max_ports = (hcsparams1 >> 24) & 0xffU;
                            xhci_info.doorbell_offset = *(volatile uint32_t *)(regs + 0x14) & ~0x3U;
                            xhci_info.runtime_offset = *(volatile uint32_t *)(regs + 0x18) & ~0x1fU;
                            xhci_probe_operational(&device, regs);
                        }
                    }
                    log_info("xHCI controller found: pci=%u:%u.%u vendor=%x device=%x mmio=%x irq=%u\n",
                             (uint64_t)device.bus,
                             (uint64_t)device.slot,
                             (uint64_t)device.function,
                             (uint64_t)device.vendor_id,
                             (uint64_t)device.device_id,
                             mmio_base,
                             (uint64_t)pci_read_config8(device.bus, device.slot, device.function, 0x3c));
                    if (xhci_info.found && xhci_info.mmio_mapped &&
                        xhci_info.bus == device.bus &&
                        xhci_info.slot == device.slot &&
                        xhci_info.function == device.function) {
                        log_info("xHCI caps: caplen=%u version=%x slots=%u intrs=%u ports=%u dboff=%x rtsoff=%x\n",
                                 xhci_info.cap_length,
                                 xhci_info.hci_version,
                                 xhci_info.max_slots,
                                 xhci_info.max_interrupters,
                                 xhci_info.max_ports,
                                 xhci_info.doorbell_offset,
                                 xhci_info.runtime_offset);
                        if (xhci_info.op_regs_ready) {
                            log_info("xHCI op: pagesize=%x usbcmd=%x->%x usbsts=%x->%x reset=%u halt=%u ready=%u\n",
                                     xhci_info.op_pagesize,
                                     xhci_info.op_usbcmd_before,
                                     xhci_info.op_usbcmd_after,
                                     xhci_info.op_usbsts_before,
                                     xhci_info.op_usbsts_after,
                                     xhci_info.reset_ok,
                                     xhci_info.halt_ok,
                                     xhci_info.ready_ok);
                        }
                    }
                }
                if (function == 0 && (pci_read_config8((uint8_t)bus, slot, function, 0x0e) & 0x80) == 0) {
                    break;
                }
            }
        }
    }
    if (!xhci_info.found) {
        log_info("xHCI controller not found\n");
    }
}

void pci_get_xhci_info(struct pci_xhci_info *out) {
    if (out == 0) {
        return;
    }
    copy_bytes(out, &xhci_info, sizeof(*out));
}

void pci_enable_io_bus_master(const struct pci_device *device) {
    if (device == 0) {
        return;
    }

    uint16_t command = pci_read_config16(device->bus, device->slot, device->function, 0x04);
    command |= PCI_COMMAND_IO_SPACE;
    command |= PCI_COMMAND_BUS_MASTER;
    pci_write_config16(device->bus, device->slot, device->function, 0x04, command);
}

void pci_enable_mmio_bus_master(const struct pci_device *device) {
    if (device == 0) {
        return;
    }

    uint16_t command = pci_read_config16(device->bus, device->slot, device->function, 0x04);
    command |= PCI_COMMAND_MEMORY_SPACE;
    command |= PCI_COMMAND_BUS_MASTER;
    pci_write_config16(device->bus, device->slot, device->function, 0x04, command);
}

void pci_init(void) {
    log_info("PCI config-space access initialized\n");
    pci_scan_xhci();
}
