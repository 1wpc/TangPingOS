#include <log.h>
#include <pci.h>
#include <stdint.h>
#include <x86_64/io.h>

#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA    0xcfc
#define PCI_VENDOR_INVALID 0xffff
#define PCI_CLASS_SERIAL_BUS 0x0c
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30

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
                    }
                    log_info("xHCI controller found: pci=%u:%u.%u vendor=%x device=%x mmio=%x irq=%u\n",
                             (uint64_t)device.bus,
                             (uint64_t)device.slot,
                             (uint64_t)device.function,
                             (uint64_t)device.vendor_id,
                             (uint64_t)device.device_id,
                             mmio_base,
                             (uint64_t)pci_read_config8(device.bus, device.slot, device.function, 0x3c));
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
    command |= 0x0001;
    command |= 0x0004;
    pci_write_config16(device->bus, device->slot, device->function, 0x04, command);
}

void pci_init(void) {
    log_info("PCI config-space access initialized\n");
    pci_scan_xhci();
}
