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
#define XHCI_CRCR 0x18
#define XHCI_DCBAAP 0x30
#define XHCI_CONFIG 0x38
#define XHCI_USBCMD_RS    (1U << 0)
#define XHCI_USBCMD_HCRST (1U << 1)
#define XHCI_USBSTS_HCH   (1U << 0)
#define XHCI_USBSTS_CNR   (1U << 11)
#define XHCI_CRCR_RCS     (1ULL << 0)
#define XHCI_POLL_LIMIT 1000000ULL
#define XHCI_EXT_CAP_USB_LEGACY 0x01
#define XHCI_EXT_CAP_BIOS_OWNED (1U << 16)
#define XHCI_EXT_CAP_OS_OWNED   (1U << 24)
#define XHCI_EXT_CAP_SCAN_LIMIT 64ULL
#define XHCI_MMIO_MAP_BYTES 0x4000ULL
#define XHCI_TRB_SIZE 16ULL
#define XHCI_RING_TRBS (PAGE_SIZE / XHCI_TRB_SIZE)
#define XHCI_LINK_TRB_TYPE 6ULL
#define XHCI_ENABLE_SLOT_TRB_TYPE 9ULL
#define XHCI_COMMAND_COMPLETION_EVENT_TYPE 33ULL
#define XHCI_TRB_TYPE_SHIFT 10ULL
#define XHCI_TRB_CYCLE 1ULL
#define XHCI_LINK_TRB_TOGGLE_CYCLE (1ULL << 1)
#define XHCI_COMPLETION_SUCCESS 1ULL
#define XHCI_EVENT_RING_SEGMENT_TRBS 128ULL
#define XHCI_RUNTIME_IR0_OFFSET 0x20ULL
#define XHCI_IR_ERSTSZ 0x08ULL
#define XHCI_IR_ERSTBA 0x10ULL
#define XHCI_IR_ERDP 0x18ULL
#define XHCI_ERDP_EHB (1ULL << 3)
#define XHCI_PORT_REGS_BASE 0x400ULL
#define XHCI_PORT_REG_STRIDE 0x10ULL
#define XHCI_PORTSC_CCS (1U << 0)
#define XHCI_PORTSC_PED (1U << 1)
#define XHCI_PORTSC_PP  (1U << 9)
#define XHCI_PORTSC_PLS_SHIFT 5ULL
#define XHCI_PORTSC_PLS_MASK (0xfU << XHCI_PORTSC_PLS_SHIFT)
#define XHCI_PORTSC_SPEED_SHIFT 10ULL
#define XHCI_PORTSC_SPEED_MASK (0xfU << XHCI_PORTSC_SPEED_SHIFT)

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

static uint64_t mmio_read64(volatile uint8_t *base, uint64_t offset) {
    uint64_t low = mmio_read32(base, offset);
    uint64_t high = mmio_read32(base, offset + 4);
    return low | (high << 32);
}

static void mmio_write64(volatile uint8_t *base, uint64_t offset, uint64_t value) {
    mmio_write32(base, offset, (uint32_t)value);
    mmio_write32(base, offset + 4, (uint32_t)(value >> 32));
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

static int mmio_wait32_field_clear(volatile uint8_t *base, uint64_t offset, uint32_t mask) {
    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        if ((mmio_read32(base, offset) & mask) == 0) {
            return 0;
        }
    }
    return -1;
}

static void zero_page_phys(uint64_t phys) {
    uint8_t *page = phys_to_virt(phys);
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        page[i] = 0;
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

static void xhci_scan_extended_caps(volatile uint8_t *regs, uint32_t hccparams1) {
    uint64_t offset = ((uint64_t)(hccparams1 >> 16) & 0xffffULL) * 4ULL;

    xhci_info.ext_cap_offset = offset;
    if (offset == 0 || offset + 4 > XHCI_MMIO_MAP_BYTES) {
        return;
    }

    for (uint64_t guard = 0; guard < XHCI_EXT_CAP_SCAN_LIMIT; guard++) {
        uint32_t cap = mmio_read32(regs, offset);
        uint32_t cap_id = cap & 0xffU;
        uint32_t next = (cap >> 8) & 0xffU;

        if (cap_id == 0) {
            break;
        }
        xhci_info.ext_cap_count++;

        if (cap_id == XHCI_EXT_CAP_USB_LEGACY && xhci_info.legacy_cap_offset == 0) {
            uint32_t before = cap;
            xhci_info.legacy_cap_offset = offset;
            xhci_info.legacy_bios_owned_before = (before & XHCI_EXT_CAP_BIOS_OWNED) ? 1 : 0;
            xhci_info.legacy_os_owned_before = (before & XHCI_EXT_CAP_OS_OWNED) ? 1 : 0;
            xhci_info.handoff_attempted = 1;

            mmio_write32(regs, offset, before | XHCI_EXT_CAP_OS_OWNED);
            (void)mmio_wait32_field_clear(regs, offset, XHCI_EXT_CAP_BIOS_OWNED);

            uint32_t after = mmio_read32(regs, offset);
            xhci_info.legacy_bios_owned_after = (after & XHCI_EXT_CAP_BIOS_OWNED) ? 1 : 0;
            xhci_info.legacy_os_owned_after = (after & XHCI_EXT_CAP_OS_OWNED) ? 1 : 0;
            if (xhci_info.legacy_os_owned_after && !xhci_info.legacy_bios_owned_after) {
                xhci_info.handoff_ok = 1;
            }
        }

        if (next == 0) {
            break;
        }
        offset += (uint64_t)next * 4ULL;
        if (offset + 4 > XHCI_MMIO_MAP_BYTES) {
            break;
        }
    }
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

static void xhci_configure_rings(volatile uint8_t *regs) {
    volatile uint8_t *op;
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    uint64_t *command_ring;
    uint64_t *dcbaa;
    uint64_t *erst;
    uint64_t scratchpad_count;

    if (regs == 0 || xhci_info.cap_length == 0 || xhci_info.runtime_offset == 0) {
        return;
    }
    if (!xhci_info.halt_ok || !xhci_info.ready_ok) {
        return;
    }

    xhci_info.command_ring_phys = pmm_alloc_page();
    xhci_info.event_ring_phys = pmm_alloc_page();
    xhci_info.erst_phys = pmm_alloc_page();
    xhci_info.dcbaa_phys = pmm_alloc_page();
    if (xhci_info.command_ring_phys == 0 ||
        xhci_info.event_ring_phys == 0 ||
        xhci_info.erst_phys == 0 ||
        xhci_info.dcbaa_phys == 0) {
        return;
    }

    zero_page_phys(xhci_info.command_ring_phys);
    zero_page_phys(xhci_info.event_ring_phys);
    zero_page_phys(xhci_info.erst_phys);
    zero_page_phys(xhci_info.dcbaa_phys);

    command_ring = phys_to_virt(xhci_info.command_ring_phys);
    command_ring[(XHCI_RING_TRBS - 1) * 2 + 0] = xhci_info.command_ring_phys;
    command_ring[(XHCI_RING_TRBS - 1) * 2 + 1] =
        (XHCI_LINK_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TOGGLE_CYCLE | 1ULL;

    scratchpad_count = xhci_info.scratchpad_count;
    if (scratchpad_count > (PAGE_SIZE / sizeof(uint64_t))) {
        return;
    }
    dcbaa = phys_to_virt(xhci_info.dcbaa_phys);
    if (scratchpad_count > 0) {
        xhci_info.scratchpad_array_phys = pmm_alloc_page();
        if (xhci_info.scratchpad_array_phys == 0) {
            return;
        }
        zero_page_phys(xhci_info.scratchpad_array_phys);
        uint64_t *scratchpad_array = phys_to_virt(xhci_info.scratchpad_array_phys);
        for (uint64_t i = 0; i < scratchpad_count && i < (PAGE_SIZE / sizeof(uint64_t)); i++) {
            uint64_t scratchpad_phys = pmm_alloc_page();
            if (scratchpad_phys == 0) {
                return;
            }
            zero_page_phys(scratchpad_phys);
            scratchpad_array[i] = scratchpad_phys;
        }
        dcbaa[0] = xhci_info.scratchpad_array_phys;
    }

    erst = phys_to_virt(xhci_info.erst_phys);
    erst[0] = xhci_info.event_ring_phys;
    erst[1] = XHCI_EVENT_RING_SEGMENT_TRBS;

    op = regs + xhci_info.cap_length;
    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;

    mmio_write64(op, XHCI_DCBAAP, xhci_info.dcbaa_phys);
    mmio_write64(op, XHCI_CRCR, xhci_info.command_ring_phys | XHCI_CRCR_RCS);
    mmio_write32(ir0, XHCI_IR_ERSTSZ, 1);
    mmio_write64(ir0, XHCI_IR_ERSTBA, xhci_info.erst_phys);
    mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.event_ring_phys);

    xhci_info.dcbaap_value = mmio_read64(op, XHCI_DCBAAP);
    xhci_info.crcr_value = mmio_read64(op, XHCI_CRCR);
    xhci_info.erstsz_value = mmio_read32(ir0, XHCI_IR_ERSTSZ);
    xhci_info.erstba_value = mmio_read64(ir0, XHCI_IR_ERSTBA);
    xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
    xhci_info.rings_ready = 1;
}

static void xhci_enable_first_slot(volatile uint8_t *regs) {
    volatile uint8_t *op;
    volatile uint8_t *doorbells;
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    volatile uint32_t *command_ring;
    volatile uint32_t *event_ring;
    uint32_t max_slots_enabled;

    if (regs == 0 || !xhci_info.rings_ready || !xhci_info.reset_allowed) {
        return;
    }
    if (xhci_info.doorbell_offset == 0 || xhci_info.runtime_offset == 0) {
        return;
    }

    op = regs + xhci_info.cap_length;
    doorbells = regs + xhci_info.doorbell_offset;
    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;

    max_slots_enabled = (uint32_t)xhci_info.max_slots;
    if (max_slots_enabled > 0xffU) {
        max_slots_enabled = 0xffU;
    }
    mmio_write32(op, XHCI_CONFIG, max_slots_enabled);
    xhci_info.config_value = mmio_read32(op, XHCI_CONFIG);

    mmio_write32(op, XHCI_USBCMD, mmio_read32(op, XHCI_USBCMD) | XHCI_USBCMD_RS);
    if (mmio_wait32_clear(op, XHCI_USBSTS, XHCI_USBSTS_HCH) != 0) {
        xhci_info.usbcmd_run = mmio_read32(op, XHCI_USBCMD);
        xhci_info.usbsts_run = mmio_read32(op, XHCI_USBSTS);
        return;
    }
    xhci_info.controller_started = 1;
    xhci_info.usbcmd_run = mmio_read32(op, XHCI_USBCMD);
    xhci_info.usbsts_run = mmio_read32(op, XHCI_USBSTS);

    command_ring = (volatile uint32_t *)phys_to_virt(xhci_info.command_ring_phys);
    command_ring[0] = 0;
    command_ring[1] = 0;
    command_ring[2] = 0;
    command_ring[3] = (uint32_t)((XHCI_ENABLE_SLOT_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE);
    __sync_synchronize();

    xhci_info.enable_slot_attempted = 1;
    xhci_info.command_doorbell_value = 0;
    mmio_write32(doorbells, 0, (uint32_t)xhci_info.command_doorbell_value);

    event_ring = (volatile uint32_t *)phys_to_virt(xhci_info.event_ring_phys);
    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        uint32_t control = event_ring[3];
        if ((control & XHCI_TRB_CYCLE) == 0) {
            continue;
        }

        uint64_t event_type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fU;
        uint64_t completion_code = event_ring[2] >> 24;
        uint64_t slot_id = control >> 24;
        xhci_info.enable_slot_event_trb0 = event_ring[0];
        xhci_info.enable_slot_event_trb1 = event_ring[1];
        xhci_info.enable_slot_event_trb2 = event_ring[2];
        xhci_info.enable_slot_event_trb3 = event_ring[3];
        xhci_info.enable_slot_event_type = event_type;
        xhci_info.enable_slot_completion_code = completion_code;
        xhci_info.enable_slot_id = slot_id;
        if (event_type == XHCI_COMMAND_COMPLETION_EVENT_TYPE &&
            completion_code == XHCI_COMPLETION_SUCCESS &&
            slot_id != 0) {
            xhci_info.enable_slot_ok = 1;
        }
        xhci_info.erdp_after_command = xhci_info.event_ring_phys + XHCI_TRB_SIZE;
        mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_after_command | XHCI_ERDP_EHB);
        xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
        return;
    }
}

static void xhci_scan_root_ports(volatile uint8_t *regs) {
    volatile uint8_t *op;
    uint64_t limit;

    if (regs == 0 || !xhci_info.op_regs_ready || xhci_info.cap_length == 0) {
        return;
    }

    op = regs + xhci_info.cap_length;
    limit = xhci_info.max_ports;
    if (limit > 64) {
        limit = 64;
    }

    xhci_info.port_scan_done = 1;
    xhci_info.port_scan_limit = limit;
    for (uint64_t port = 1; port <= limit; port++) {
        uint64_t offset = XHCI_PORT_REGS_BASE + (port - 1) * XHCI_PORT_REG_STRIDE;
        if (offset + 4 > XHCI_MMIO_MAP_BYTES) {
            break;
        }

        uint32_t portsc = mmio_read32(op, offset);
        uint64_t speed = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        uint64_t link_state = (portsc & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
        uint64_t enabled = (portsc & XHCI_PORTSC_PED) ? 1 : 0;
        uint64_t powered = (portsc & XHCI_PORTSC_PP) ? 1 : 0;

        if (port == 1) {
            xhci_info.port1_portsc = portsc;
        }
        if (enabled) {
            xhci_info.enabled_port_count++;
        }
        if (powered) {
            xhci_info.powered_port_count++;
        }
        if ((portsc & XHCI_PORTSC_CCS) == 0) {
            continue;
        }

        xhci_info.connected_port_count++;
        if (xhci_info.first_connected_port == 0) {
            xhci_info.first_connected_port = port;
            xhci_info.first_connected_portsc = portsc;
            xhci_info.first_connected_speed = speed;
            xhci_info.first_connected_enabled = enabled;
            xhci_info.first_connected_powered = powered;
            xhci_info.first_connected_link_state = link_state;
        }
    }
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
                        regs = (volatile uint8_t *)mmio_map_region(mmio_base, XHCI_MMIO_MAP_BYTES);
                        if (regs != 0) {
                            uint32_t capreg;
                            uint32_t hcsparams1;
                            xhci_info.mmio_mapped = 1;
                            capreg = *(volatile uint32_t *)(regs + 0x00);
                            xhci_info.cap_length = capreg & 0xffU;
                            xhci_info.hci_version = (capreg >> 16) & 0xffffU;
                            hcsparams1 = *(volatile uint32_t *)(regs + 0x04);
                            uint32_t hcsparams2 = *(volatile uint32_t *)(regs + 0x08);
                            xhci_info.max_slots = hcsparams1 & 0xffU;
                            xhci_info.max_interrupters = (hcsparams1 >> 8) & 0x7ffU;
                            xhci_info.max_ports = (hcsparams1 >> 24) & 0xffU;
                            xhci_info.scratchpad_count =
                                ((uint64_t)((hcsparams2 >> 21) & 0x1fU) << 5) |
                                (uint64_t)(hcsparams2 & 0x1fU);
                            xhci_info.doorbell_offset = *(volatile uint32_t *)(regs + 0x14) & ~0x3U;
                            xhci_info.runtime_offset = *(volatile uint32_t *)(regs + 0x18) & ~0x1fU;
                            xhci_scan_extended_caps(regs, hcsparams1);
                            xhci_probe_operational(&device, regs);
                            xhci_configure_rings(regs);
                            xhci_enable_first_slot(regs);
                            xhci_scan_root_ports(regs);
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
                        log_info("xHCI extcaps: offset=%x count=%u legacy=%x handoff=%u/%u bios=%u->%u os=%u->%u\n",
                                 xhci_info.ext_cap_offset,
                                 xhci_info.ext_cap_count,
                                 xhci_info.legacy_cap_offset,
                                 xhci_info.handoff_attempted,
                                 xhci_info.handoff_ok,
                                 xhci_info.legacy_bios_owned_before,
                                 xhci_info.legacy_bios_owned_after,
                                 xhci_info.legacy_os_owned_before,
                                 xhci_info.legacy_os_owned_after);
                        if (xhci_info.rings_ready) {
                            log_info("xHCI rings: cmd=%x event=%x erst=%x dcbaa=%x scratchpads=%u crcr=%x erdp=%x\n",
                                     xhci_info.command_ring_phys,
                                     xhci_info.event_ring_phys,
                                     xhci_info.erst_phys,
                                     xhci_info.dcbaa_phys,
                                     xhci_info.scratchpad_count,
                                     xhci_info.crcr_value,
                                     xhci_info.erdp_value);
                        }
                        if (xhci_info.enable_slot_attempted) {
                            log_info("xHCI enable slot: ok=%u slot=%u cc=%u type=%u usbcmd=%x usbsts=%x erdp=%x\n",
                                     xhci_info.enable_slot_ok,
                                     xhci_info.enable_slot_id,
                                     xhci_info.enable_slot_completion_code,
                                     xhci_info.enable_slot_event_type,
                                     xhci_info.usbcmd_run,
                                     xhci_info.usbsts_run,
                                     xhci_info.erdp_value);
                        }
                        if (xhci_info.port_scan_done) {
                            log_info("xHCI ports: scanned=%u connected=%u enabled=%u powered=%u first=%u portsc=%x speed=%u pls=%u port1=%x\n",
                                     xhci_info.port_scan_limit,
                                     xhci_info.connected_port_count,
                                     xhci_info.enabled_port_count,
                                     xhci_info.powered_port_count,
                                     xhci_info.first_connected_port,
                                     xhci_info.first_connected_portsc,
                                     xhci_info.first_connected_speed,
                                     xhci_info.first_connected_link_state,
                                     xhci_info.port1_portsc);
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
