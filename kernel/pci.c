#include <block.h>
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
#define XHCI_TRANSFER_RING_PAGES 4ULL
#define XHCI_TRANSFER_RING_TRBS ((XHCI_TRANSFER_RING_PAGES * PAGE_SIZE) / XHCI_TRB_SIZE)
#define XHCI_LINK_TRB_TYPE 6ULL
#define XHCI_NORMAL_TRB_TYPE 1ULL
#define XHCI_SETUP_STAGE_TRB_TYPE 2ULL
#define XHCI_DATA_STAGE_TRB_TYPE 3ULL
#define XHCI_STATUS_STAGE_TRB_TYPE 4ULL
#define XHCI_ENABLE_SLOT_TRB_TYPE 9ULL
#define XHCI_ADDRESS_DEVICE_TRB_TYPE 11ULL
#define XHCI_CONFIGURE_ENDPOINT_TRB_TYPE 12ULL
#define XHCI_TRANSFER_EVENT_TYPE 32ULL
#define XHCI_COMMAND_COMPLETION_EVENT_TYPE 33ULL
#define XHCI_TRB_TYPE_SHIFT 10ULL
#define XHCI_TRB_CYCLE 1ULL
#define XHCI_TRB_IOC (1ULL << 5)
#define XHCI_TRB_IDT (1ULL << 6)
#define XHCI_TRB_DIR_IN (1ULL << 16)
#define XHCI_SETUP_TRT_IN (3ULL << 16)
#define XHCI_LINK_TRB_TOGGLE_CYCLE (1ULL << 1)
#define XHCI_COMPLETION_SUCCESS 1ULL
#define XHCI_EVENT_RING_SEGMENT_PAGES 4ULL
#define XHCI_EVENT_RING_SEGMENT_TRBS ((XHCI_EVENT_RING_SEGMENT_PAGES * PAGE_SIZE) / XHCI_TRB_SIZE)
#define XHCI_RUNTIME_IR0_OFFSET 0x20ULL
#define XHCI_IR_ERSTSZ 0x08ULL
#define XHCI_IR_ERSTBA 0x10ULL
#define XHCI_IR_ERDP 0x18ULL
#define XHCI_ERDP_EHB (1ULL << 3)
#define XHCI_HCCPARAMS1_CSZ (1U << 2)
#define XHCI_PORT_REGS_BASE 0x400ULL
#define XHCI_PORT_REG_STRIDE 0x10ULL
#define XHCI_PORTSC_CCS (1U << 0)
#define XHCI_PORTSC_PED (1U << 1)
#define XHCI_PORTSC_PR  (1U << 4)
#define XHCI_PORTSC_PP  (1U << 9)
#define XHCI_PORTSC_PLS_SHIFT 5ULL
#define XHCI_PORTSC_PLS_MASK (0xfU << XHCI_PORTSC_PLS_SHIFT)
#define XHCI_PORTSC_SPEED_SHIFT 10ULL
#define XHCI_PORTSC_SPEED_MASK (0xfU << XHCI_PORTSC_SPEED_SHIFT)
#define XHCI_PORTSC_CHANGE_MASK ((1U << 17) | (1U << 18) | (1U << 19) | (1U << 20) | \
                                 (1U << 21) | (1U << 22) | (1U << 23) | (1U << 24) | \
                                 (1U << 25) | (1U << 26) | (1U << 27) | (1U << 30) | \
                                 (1U << 31))
#define USB_DESCRIPTOR_TYPE_CONFIGURATION 2ULL
#define USB_DESCRIPTOR_TYPE_INTERFACE 4ULL
#define USB_DESCRIPTOR_TYPE_ENDPOINT 5ULL
#define USB_ENDPOINT_DIRECTION_IN 0x80ULL
#define USB_ENDPOINT_TRANSFER_TYPE_MASK 0x03ULL
#define USB_ENDPOINT_TRANSFER_TYPE_BULK 0x02ULL
#define XHCI_ENDPOINT_TYPE_BULK_OUT 2ULL
#define XHCI_ENDPOINT_TYPE_BULK_IN 6ULL
#define USB_REQUEST_SET_CONFIGURATION 9ULL
#define USB_BOT_CBW_SIGNATURE 0x43425355ULL
#define USB_BOT_CSW_SIGNATURE 0x53425355ULL
#define USB_BOT_CBW_TAG_INQUIRY 0x544f5031ULL
#define USB_BOT_CBW_TAG_READ_CAPACITY 0x544f5032ULL
#define USB_BOT_CBW_TAG_READ10 0x544f5033ULL
#define USB_BOT_CBW_TAG_WRITE10 0x544f5034ULL
#define USB_BOT_INQUIRY_LENGTH 36ULL
#define USB_BOT_READ_CAPACITY_LENGTH 8ULL
#define USB_BOT_READ10_LENGTH 512ULL
#define USB_BOT_WRITE10_LENGTH 512ULL
#define USB_BOT_CBW_LENGTH 31ULL
#define USB_BOT_CSW_LENGTH 13ULL

static struct pci_xhci_info xhci_info;
static volatile uint8_t *xhci_regs;
static uint64_t xhci_bulk_out_next_trb;
static uint64_t xhci_bulk_in_next_trb;
static uint64_t xhci_next_transfer_event;
static uint64_t xhci_bulk_out_cycle;
static uint64_t xhci_bulk_in_cycle;
static uint64_t xhci_transfer_event_cycle;
static uint64_t xhci_bulk_out_wraps;
static uint64_t xhci_bulk_in_wraps;
static uint64_t xhci_transfer_event_wraps;
static uint64_t xhci_bot_ring_ready;
static uint64_t xhci_bot_next_tag;
static struct block_device xhci_usb_storage_block;
static int xhci_usb_storage_registered;

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

static uint64_t xhci_ep0_max_packet_size(uint64_t speed) {
    if (speed == 4) {
        return 512;
    }
    if (speed == 3) {
        return 64;
    }
    return 8;
}

static uint64_t xhci_dci_from_endpoint_address(uint64_t endpoint_address) {
    uint64_t endpoint_number = endpoint_address & 0x0fULL;
    uint64_t direction_in = (endpoint_address & USB_ENDPOINT_DIRECTION_IN) ? 1 : 0;

    if (endpoint_number == 0) {
        return 1;
    }
    return endpoint_number * 2 + direction_in;
}

static uint64_t read_le64(const uint8_t *buffer) {
    uint64_t value = 0;
    for (uint64_t i = 0; i < 8; i++) {
        value |= (uint64_t)buffer[i] << (i * 8);
    }
    return value;
}

static uint32_t read_le32(const uint8_t *buffer) {
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8) |
           ((uint32_t)buffer[2] << 16) |
           ((uint32_t)buffer[3] << 24);
}

static uint32_t read_be32(const uint8_t *buffer) {
    return ((uint32_t)buffer[0] << 24) |
           ((uint32_t)buffer[1] << 16) |
           ((uint32_t)buffer[2] << 8) |
           (uint32_t)buffer[3];
}

static void write_le32(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static void write_be32(uint8_t *buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)value;
}

static void write_be16(uint8_t *buffer, uint16_t value) {
    buffer[0] = (uint8_t)(value >> 8);
    buffer[1] = (uint8_t)value;
}

static int xhci_wait_transfer_event(volatile uint8_t *regs,
                                    uint64_t first_event_index,
                                    uint64_t expected_slot_id,
                                    uint64_t *completion_code,
                                    uint64_t *event_type_out,
                                    uint64_t *event_trb0,
                                    uint64_t *event_trb1,
                                    uint64_t *event_trb2,
                                    uint64_t *event_trb3) {
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    volatile uint32_t *event_ring;

    if (regs == 0 || xhci_info.runtime_offset == 0 ||
        xhci_info.event_ring_phys == 0 ||
        first_event_index >= XHCI_EVENT_RING_SEGMENT_TRBS) {
        return -1;
    }

    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;
    event_ring = (volatile uint32_t *)phys_to_virt(xhci_info.event_ring_phys);

    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        for (uint64_t event_index = first_event_index;
             event_index < XHCI_EVENT_RING_SEGMENT_TRBS;
             event_index++) {
            uint64_t trb_index = event_index * 4;
            uint32_t control = event_ring[trb_index + 3];
            if ((control & XHCI_TRB_CYCLE) == 0) {
                continue;
            }

            uint64_t event_type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fU;
            if (event_type != XHCI_TRANSFER_EVENT_TYPE) {
                continue;
            }

            uint64_t event_slot_id = control >> 24;
            if (event_slot_id != expected_slot_id) {
                continue;
            }

            if (event_trb0 != 0) {
                *event_trb0 = event_ring[trb_index + 0];
            }
            if (event_trb1 != 0) {
                *event_trb1 = event_ring[trb_index + 1];
            }
            if (event_trb2 != 0) {
                *event_trb2 = event_ring[trb_index + 2];
            }
            if (event_trb3 != 0) {
                *event_trb3 = event_ring[trb_index + 3];
            }
            if (event_type_out != 0) {
                *event_type_out = event_type;
            }
            if (completion_code != 0) {
                *completion_code = event_ring[trb_index + 2] >> 24;
            }

            xhci_info.erdp_value = xhci_info.event_ring_phys + XHCI_TRB_SIZE * (event_index + 1);
            mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_value | XHCI_ERDP_EHB);
            xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
            return ((event_ring[trb_index + 2] >> 24) == XHCI_COMPLETION_SUCCESS) ? 0 : -1;
        }
    }

    return -1;
}

static void xhci_mark_transfer_trb_inactive(uint64_t ring_phys,
                                            uint64_t trb_index,
                                            uint64_t inactive_cycle) {
    volatile uint32_t *ring;

    if (ring_phys == 0 || trb_index >= XHCI_TRANSFER_RING_TRBS - 1) {
        return;
    }

    ring = (volatile uint32_t *)phys_to_virt(ring_phys);
    uint64_t base = trb_index * 4;
    ring[base + 0] = 0;
    ring[base + 1] = 0;
    ring[base + 2] = 0;
    ring[base + 3] = inactive_cycle ? XHCI_TRB_CYCLE : 0;
    __sync_synchronize();
}

static void xhci_advance_transfer_ring(uint64_t *next_trb,
                                       uint64_t *producer_cycle,
                                       uint64_t *wraps,
                                       const char *name) {
    if (next_trb == 0 || producer_cycle == 0 || wraps == 0) {
        return;
    }

    (*next_trb)++;
    if (*next_trb < XHCI_TRANSFER_RING_TRBS - 1) {
        return;
    }

    *next_trb = 0;
    *producer_cycle ^= 1;
    (*wraps)++;
    log_info("xHCI %s transfer ring wrapped: cycle=%u wraps=%u\n",
             name,
             *producer_cycle,
             *wraps);
}

static void xhci_advance_transfer_event(volatile uint8_t *regs) {
    if (regs == 0 || xhci_info.runtime_offset == 0) {
        return;
    }

    xhci_next_transfer_event++;
    if (xhci_next_transfer_event >= XHCI_EVENT_RING_SEGMENT_TRBS) {
        xhci_next_transfer_event = 0;
        xhci_transfer_event_cycle ^= 1;
        xhci_transfer_event_wraps++;
        log_info("xHCI transfer event ring wrapped: cycle=%u wraps=%u\n",
                 xhci_transfer_event_cycle,
                 xhci_transfer_event_wraps);
    }
}

static int xhci_wait_next_transfer_event(volatile uint8_t *regs,
                                         uint64_t expected_slot_id,
                                         uint64_t *completion_code,
                                         uint64_t *event_type_out,
                                         uint64_t *event_trb0,
                                         uint64_t *event_trb1,
                                         uint64_t *event_trb2,
                                         uint64_t *event_trb3) {
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    volatile uint32_t *event_ring;

    if (regs == 0 || xhci_info.runtime_offset == 0 ||
        xhci_info.event_ring_phys == 0 ||
        xhci_next_transfer_event >= XHCI_EVENT_RING_SEGMENT_TRBS) {
        return -1;
    }

    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;
    event_ring = (volatile uint32_t *)phys_to_virt(xhci_info.event_ring_phys);

    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        uint64_t trb_index = xhci_next_transfer_event * 4;
        uint32_t control = event_ring[trb_index + 3];
        if ((control & XHCI_TRB_CYCLE) != xhci_transfer_event_cycle) {
            continue;
        }

        uint64_t event_type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fU;
        uint64_t event_slot_id = control >> 24;
        if (event_type != XHCI_TRANSFER_EVENT_TYPE ||
            event_slot_id != expected_slot_id) {
            uint64_t next_event = xhci_next_transfer_event + 1;
            if (next_event >= XHCI_EVENT_RING_SEGMENT_TRBS) {
                next_event = 0;
            }
            xhci_info.erdp_value =
                xhci_info.event_ring_phys + XHCI_TRB_SIZE * next_event;
            mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_value | XHCI_ERDP_EHB);
            xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
            xhci_advance_transfer_event(regs);
            continue;
        }

        if (event_trb0 != 0) {
            *event_trb0 = event_ring[trb_index + 0];
        }
        if (event_trb1 != 0) {
            *event_trb1 = event_ring[trb_index + 1];
        }
        if (event_trb2 != 0) {
            *event_trb2 = event_ring[trb_index + 2];
        }
        if (event_trb3 != 0) {
            *event_trb3 = event_ring[trb_index + 3];
        }
        if (event_type_out != 0) {
            *event_type_out = event_type;
        }
        if (completion_code != 0) {
            *completion_code = event_ring[trb_index + 2] >> 24;
        }

        uint64_t next_event = xhci_next_transfer_event + 1;
        if (next_event >= XHCI_EVENT_RING_SEGMENT_TRBS) {
            next_event = 0;
        }
        xhci_info.erdp_value = xhci_info.event_ring_phys + XHCI_TRB_SIZE * next_event;
        mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_value | XHCI_ERDP_EHB);
        xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
        xhci_advance_transfer_event(regs);
        return ((event_ring[trb_index + 2] >> 24) == XHCI_COMPLETION_SUCCESS) ? 0 : -1;
    }

    return -1;
}

static int xhci_ep0_get_descriptor(volatile uint8_t *regs,
                                   uint64_t ring_trb_index,
                                   uint64_t first_event_index,
                                   uint64_t descriptor_type,
                                   uint64_t length,
                                   uint64_t buffer_phys,
                                   uint64_t *completion_code,
                                   uint64_t *event_type_out,
                                   uint64_t *event_trb0,
                                   uint64_t *event_trb1,
                                   uint64_t *event_trb2,
                                   uint64_t *event_trb3) {
    volatile uint8_t *doorbells;
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    volatile uint32_t *event_ring;
    volatile uint32_t *ep0_ring;
    uint64_t slot_id;
    uint64_t base;

    if (regs == 0 || !xhci_info.address_device_ok ||
        xhci_info.ep0_ring_phys == 0 || xhci_info.address_device_slot_id == 0 ||
        xhci_info.doorbell_offset == 0 || xhci_info.runtime_offset == 0 ||
        buffer_phys == 0 || length == 0 || length > PAGE_SIZE) {
        return -1;
    }
    if (ring_trb_index + 2 >= XHCI_RING_TRBS - 1 ||
        first_event_index >= XHCI_EVENT_RING_SEGMENT_TRBS) {
        return -1;
    }

    slot_id = xhci_info.address_device_slot_id;
    base = ring_trb_index * 4;
    ep0_ring = (volatile uint32_t *)phys_to_virt(xhci_info.ep0_ring_phys);
    event_ring = (volatile uint32_t *)phys_to_virt(xhci_info.event_ring_phys);
    doorbells = regs + xhci_info.doorbell_offset;
    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;

    ep0_ring[base + 0] = (uint32_t)((descriptor_type << 24) | 0x00000680U);
    ep0_ring[base + 1] = (uint32_t)(length << 16);
    ep0_ring[base + 2] = 8;
    ep0_ring[base + 3] = (uint32_t)((XHCI_SETUP_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                    XHCI_SETUP_TRT_IN |
                                    XHCI_TRB_IDT |
                                    XHCI_TRB_CYCLE);

    ep0_ring[base + 4] = (uint32_t)buffer_phys;
    ep0_ring[base + 5] = (uint32_t)(buffer_phys >> 32);
    ep0_ring[base + 6] = (uint32_t)length;
    ep0_ring[base + 7] = (uint32_t)((XHCI_DATA_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                    XHCI_TRB_DIR_IN |
                                    XHCI_TRB_CYCLE);

    ep0_ring[base + 8] = 0;
    ep0_ring[base + 9] = 0;
    ep0_ring[base + 10] = 0;
    ep0_ring[base + 11] = (uint32_t)((XHCI_STATUS_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                     XHCI_TRB_IOC |
                                     XHCI_TRB_CYCLE);
    __sync_synchronize();

    mmio_write32(doorbells, slot_id * 4, 1);

    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        for (uint64_t event_index = first_event_index;
             event_index < XHCI_EVENT_RING_SEGMENT_TRBS;
             event_index++) {
            uint64_t trb_index = event_index * 4;
            uint32_t control = event_ring[trb_index + 3];
            if ((control & XHCI_TRB_CYCLE) == 0) {
                continue;
            }

            uint64_t event_type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fU;
            if (event_type != XHCI_TRANSFER_EVENT_TYPE) {
                continue;
            }

            uint64_t event_slot_id = control >> 24;
            if (event_slot_id != slot_id) {
                continue;
            }

            if (event_trb0 != 0) {
                *event_trb0 = event_ring[trb_index + 0];
            }
            if (event_trb1 != 0) {
                *event_trb1 = event_ring[trb_index + 1];
            }
            if (event_trb2 != 0) {
                *event_trb2 = event_ring[trb_index + 2];
            }
            if (event_trb3 != 0) {
                *event_trb3 = event_ring[trb_index + 3];
            }
            if (event_type_out != 0) {
                *event_type_out = event_type;
            }
            if (completion_code != 0) {
                *completion_code = event_ring[trb_index + 2] >> 24;
            }

            xhci_info.erdp_value = xhci_info.event_ring_phys + XHCI_TRB_SIZE * (event_index + 1);
            mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_value | XHCI_ERDP_EHB);
            xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
            return ((event_ring[trb_index + 2] >> 24) == XHCI_COMPLETION_SUCCESS) ? 0 : -1;
        }
    }

    return -1;
}

static int xhci_ep0_set_configuration(volatile uint8_t *regs,
                                      uint64_t ring_trb_index,
                                      uint64_t first_event_index,
                                      uint64_t configuration_value,
                                      uint64_t *completion_code,
                                      uint64_t *event_type_out,
                                      uint64_t *event_trb0,
                                      uint64_t *event_trb1,
                                      uint64_t *event_trb2,
                                      uint64_t *event_trb3) {
    volatile uint8_t *doorbells;
    volatile uint32_t *ep0_ring;
    uint64_t slot_id;
    uint64_t base;

    if (regs == 0 || !xhci_info.address_device_ok ||
        xhci_info.ep0_ring_phys == 0 || xhci_info.address_device_slot_id == 0 ||
        xhci_info.doorbell_offset == 0) {
        return -1;
    }
    if (ring_trb_index + 1 >= XHCI_RING_TRBS - 1) {
        return -1;
    }

    slot_id = xhci_info.address_device_slot_id;
    base = ring_trb_index * 4;
    ep0_ring = (volatile uint32_t *)phys_to_virt(xhci_info.ep0_ring_phys);
    doorbells = regs + xhci_info.doorbell_offset;

    ep0_ring[base + 0] = (uint32_t)((configuration_value << 16) |
                                    (USB_REQUEST_SET_CONFIGURATION << 8));
    ep0_ring[base + 1] = 0;
    ep0_ring[base + 2] = 8;
    ep0_ring[base + 3] = (uint32_t)((XHCI_SETUP_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                    XHCI_TRB_IDT |
                                    XHCI_TRB_CYCLE);

    ep0_ring[base + 4] = 0;
    ep0_ring[base + 5] = 0;
    ep0_ring[base + 6] = 0;
    ep0_ring[base + 7] = (uint32_t)((XHCI_STATUS_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                    XHCI_TRB_DIR_IN |
                                    XHCI_TRB_IOC |
                                    XHCI_TRB_CYCLE);
    __sync_synchronize();

    mmio_write32(doorbells, slot_id * 4, 1);
    return xhci_wait_transfer_event(regs,
                                    first_event_index,
                                    slot_id,
                                    completion_code,
                                    event_type_out,
                                    event_trb0,
                                    event_trb1,
                                    event_trb2,
                                    event_trb3);
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
    xhci_info.event_ring_phys = pmm_alloc_contiguous_pages(XHCI_EVENT_RING_SEGMENT_PAGES);
    xhci_info.erst_phys = pmm_alloc_page();
    xhci_info.dcbaa_phys = pmm_alloc_page();
    if (xhci_info.command_ring_phys == 0 ||
        xhci_info.event_ring_phys == 0 ||
        xhci_info.erst_phys == 0 ||
        xhci_info.dcbaa_phys == 0) {
        return;
    }

    zero_page_phys(xhci_info.command_ring_phys);
    for (uint64_t i = 0; i < XHCI_EVENT_RING_SEGMENT_PAGES; i++) {
        zero_page_phys(xhci_info.event_ring_phys + i * PAGE_SIZE);
    }
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
    xhci_info.connected_port_count = 0;
    xhci_info.enabled_port_count = 0;
    xhci_info.powered_port_count = 0;
    xhci_info.first_connected_port = 0;
    xhci_info.first_connected_portsc = 0;
    xhci_info.first_connected_speed = 0;
    xhci_info.first_connected_enabled = 0;
    xhci_info.first_connected_powered = 0;
    xhci_info.first_connected_link_state = 0;
    xhci_info.second_connected_port = 0;
    xhci_info.second_connected_portsc = 0;
    xhci_info.second_connected_speed = 0;
    xhci_info.second_connected_enabled = 0;
    xhci_info.second_connected_powered = 0;
    xhci_info.second_connected_link_state = 0;
    xhci_info.port1_portsc = 0;
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
        } else if (xhci_info.second_connected_port == 0) {
            xhci_info.second_connected_port = port;
            xhci_info.second_connected_portsc = portsc;
            xhci_info.second_connected_speed = speed;
            xhci_info.second_connected_enabled = enabled;
            xhci_info.second_connected_powered = powered;
            xhci_info.second_connected_link_state = link_state;
        }
    }
}

static void xhci_reset_first_connected_port(volatile uint8_t *regs) {
    volatile uint8_t *op;
    uint64_t port;
    uint64_t offset;
    uint32_t before;
    uint32_t after = 0;

    if (regs == 0 || !xhci_info.reset_allowed || !xhci_info.controller_started) {
        return;
    }
    if (xhci_info.first_connected_port == 0 || xhci_info.cap_length == 0) {
        return;
    }

    op = regs + xhci_info.cap_length;
    port = xhci_info.first_connected_port;
    offset = XHCI_PORT_REGS_BASE + (port - 1) * XHCI_PORT_REG_STRIDE;
    if (offset + 4 > XHCI_MMIO_MAP_BYTES) {
        return;
    }

    before = mmio_read32(op, offset);
    xhci_info.port_reset_attempted = 1;
    xhci_info.port_reset_port = port;
    xhci_info.port_reset_portsc_before = before;
    mmio_write32(op, offset, (before & ~XHCI_PORTSC_CHANGE_MASK) | XHCI_PORTSC_PR);

    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        after = mmio_read32(op, offset);
        if ((after & XHCI_PORTSC_PR) == 0) {
            break;
        }
    }

    xhci_info.port_reset_portsc_after = after;
    xhci_info.port_reset_enabled = (after & XHCI_PORTSC_PED) ? 1 : 0;
    xhci_info.port_reset_speed = (after & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    xhci_info.port_reset_link_state = (after & XHCI_PORTSC_PLS_MASK) >> XHCI_PORTSC_PLS_SHIFT;
    if ((after & XHCI_PORTSC_PR) == 0 &&
        (after & XHCI_PORTSC_CCS) != 0 &&
        (after & XHCI_PORTSC_PED) != 0) {
        xhci_info.port_reset_ok = 1;
    }
}

static void xhci_address_first_device(volatile uint8_t *regs) {
    volatile uint8_t *doorbells;
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    volatile uint32_t *command_ring;
    volatile uint32_t *event_ring;
    uint32_t *input_context;
    uint32_t *slot_context;
    uint32_t *endpoint0_context;
    uint64_t *dcbaa;
    uint64_t *ep0_ring;
    uint64_t context_size;
    uint64_t slot_id;
    uint64_t port;
    uint64_t speed;
    uint64_t max_packet_size;

    if (regs == 0 || !xhci_info.rings_ready || !xhci_info.enable_slot_ok ||
        !xhci_info.port_reset_ok || xhci_info.enable_slot_id == 0) {
        return;
    }
    if (xhci_info.doorbell_offset == 0 || xhci_info.runtime_offset == 0 ||
        xhci_info.dcbaa_phys == 0) {
        return;
    }

    slot_id = xhci_info.enable_slot_id;
    if (slot_id > xhci_info.max_slots || slot_id >= (PAGE_SIZE / sizeof(uint64_t))) {
        return;
    }

    xhci_info.input_context_phys = pmm_alloc_page();
    xhci_info.output_context_phys = pmm_alloc_page();
    xhci_info.ep0_ring_phys = pmm_alloc_page();
    if (xhci_info.input_context_phys == 0 ||
        xhci_info.output_context_phys == 0 ||
        xhci_info.ep0_ring_phys == 0) {
        return;
    }

    zero_page_phys(xhci_info.input_context_phys);
    zero_page_phys(xhci_info.output_context_phys);
    zero_page_phys(xhci_info.ep0_ring_phys);

    context_size = xhci_info.context_size;
    if (context_size == 0) {
        context_size = 32;
    }
    port = xhci_info.port_reset_port;
    speed = xhci_info.port_reset_speed;
    max_packet_size = xhci_ep0_max_packet_size(speed);
    xhci_info.ep0_max_packet_size = max_packet_size;

    input_context = (uint32_t *)phys_to_virt(xhci_info.input_context_phys);
    slot_context = (uint32_t *)((uint8_t *)input_context + context_size);
    endpoint0_context = (uint32_t *)((uint8_t *)input_context + context_size * 2);
    dcbaa = (uint64_t *)phys_to_virt(xhci_info.dcbaa_phys);
    ep0_ring = (uint64_t *)phys_to_virt(xhci_info.ep0_ring_phys);

    dcbaa[slot_id] = xhci_info.output_context_phys;
    ep0_ring[(XHCI_RING_TRBS - 1) * 2 + 0] = xhci_info.ep0_ring_phys;
    ep0_ring[(XHCI_RING_TRBS - 1) * 2 + 1] =
        (XHCI_LINK_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;

    input_context[1] = 0x3U;
    slot_context[0] = (uint32_t)(((speed & 0xfULL) << 20) | (1ULL << 27));
    slot_context[1] = (uint32_t)((port & 0xffULL) << 16);
    endpoint0_context[1] = (uint32_t)((3ULL << 1) | (4ULL << 3) | (max_packet_size << 16));
    endpoint0_context[2] = (uint32_t)(xhci_info.ep0_ring_phys | XHCI_TRB_CYCLE);
    endpoint0_context[3] = (uint32_t)((xhci_info.ep0_ring_phys | XHCI_TRB_CYCLE) >> 32);
    endpoint0_context[4] = 8;

    doorbells = regs + xhci_info.doorbell_offset;
    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;
    command_ring = (volatile uint32_t *)phys_to_virt(xhci_info.command_ring_phys);
    event_ring = (volatile uint32_t *)phys_to_virt(xhci_info.event_ring_phys);

    command_ring[4] = (uint32_t)xhci_info.input_context_phys;
    command_ring[5] = (uint32_t)(xhci_info.input_context_phys >> 32);
    command_ring[6] = 0;
    command_ring[7] = (uint32_t)((slot_id << 24) |
                                 (XHCI_ADDRESS_DEVICE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                 XHCI_TRB_CYCLE);
    __sync_synchronize();

    xhci_info.address_device_attempted = 1;
    xhci_info.command_doorbell_value = 0;
    mmio_write32(doorbells, 0, (uint32_t)xhci_info.command_doorbell_value);

    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        for (uint64_t event_index = 1; event_index < 8; event_index++) {
            uint64_t trb_index = event_index * 4;
            uint32_t control = event_ring[trb_index + 3];
            if ((control & XHCI_TRB_CYCLE) == 0) {
                continue;
            }

            uint64_t event_type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fU;
            if (event_type != XHCI_COMMAND_COMPLETION_EVENT_TYPE) {
                continue;
            }

            uint64_t completion_code = event_ring[trb_index + 2] >> 24;
            uint64_t event_slot_id = control >> 24;
            xhci_info.address_device_event_trb0 = event_ring[trb_index + 0];
            xhci_info.address_device_event_trb1 = event_ring[trb_index + 1];
            xhci_info.address_device_event_trb2 = event_ring[trb_index + 2];
            xhci_info.address_device_event_trb3 = event_ring[trb_index + 3];
            xhci_info.address_device_event_type = event_type;
            xhci_info.address_device_completion_code = completion_code;
            xhci_info.address_device_slot_id = event_slot_id;
            if (completion_code == XHCI_COMPLETION_SUCCESS && event_slot_id == slot_id) {
                uint32_t *output_slot_context = (uint32_t *)phys_to_virt(xhci_info.output_context_phys);
                xhci_info.address_device_ok = 1;
                xhci_info.output_slot_context0 = output_slot_context[0];
                xhci_info.output_slot_context1 = output_slot_context[1];
                xhci_info.output_slot_context2 = output_slot_context[2];
                xhci_info.output_slot_context3 = output_slot_context[3];
                xhci_info.addressed_device_address = output_slot_context[3] & 0xffU;
                xhci_info.addressed_slot_state = (output_slot_context[3] >> 27) & 0x1fU;
            }
            xhci_info.erdp_after_address = xhci_info.event_ring_phys + XHCI_TRB_SIZE * (event_index + 1);
            mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_after_address | XHCI_ERDP_EHB);
            xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
            return;
        }
    }
}

static void xhci_get_first_device_descriptor(volatile uint8_t *regs) {
    volatile uint8_t *doorbells;
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    volatile uint32_t *event_ring;
    volatile uint32_t *ep0_ring;
    uint8_t *descriptor;
    uint64_t slot_id;

    if (regs == 0 || !xhci_info.address_device_ok ||
        xhci_info.ep0_ring_phys == 0 || xhci_info.address_device_slot_id == 0) {
        return;
    }
    if (xhci_info.doorbell_offset == 0 || xhci_info.runtime_offset == 0) {
        return;
    }

    xhci_info.device_descriptor_phys = pmm_alloc_page();
    if (xhci_info.device_descriptor_phys == 0) {
        return;
    }
    zero_page_phys(xhci_info.device_descriptor_phys);

    slot_id = xhci_info.address_device_slot_id;
    ep0_ring = (volatile uint32_t *)phys_to_virt(xhci_info.ep0_ring_phys);
    event_ring = (volatile uint32_t *)phys_to_virt(xhci_info.event_ring_phys);
    doorbells = regs + xhci_info.doorbell_offset;
    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;

    ep0_ring[0] = 0x01000680U;
    ep0_ring[1] = 18U << 16;
    ep0_ring[2] = 8;
    ep0_ring[3] = (uint32_t)((XHCI_SETUP_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                             XHCI_SETUP_TRT_IN |
                             XHCI_TRB_IDT |
                             XHCI_TRB_CYCLE);

    ep0_ring[4] = (uint32_t)xhci_info.device_descriptor_phys;
    ep0_ring[5] = (uint32_t)(xhci_info.device_descriptor_phys >> 32);
    ep0_ring[6] = 18;
    ep0_ring[7] = (uint32_t)((XHCI_DATA_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                             XHCI_TRB_DIR_IN |
                             XHCI_TRB_CYCLE);

    ep0_ring[8] = 0;
    ep0_ring[9] = 0;
    ep0_ring[10] = 0;
    ep0_ring[11] = (uint32_t)((XHCI_STATUS_STAGE_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                              XHCI_TRB_IOC |
                              XHCI_TRB_CYCLE);
    __sync_synchronize();

    xhci_info.device_descriptor_attempted = 1;
    mmio_write32(doorbells, slot_id * 4, 1);

    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        for (uint64_t event_index = 3; event_index < 16; event_index++) {
            uint64_t trb_index = event_index * 4;
            uint32_t control = event_ring[trb_index + 3];
            if ((control & XHCI_TRB_CYCLE) == 0) {
                continue;
            }

            uint64_t event_type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fU;
            if (event_type != XHCI_TRANSFER_EVENT_TYPE) {
                continue;
            }

            uint64_t completion_code = event_ring[trb_index + 2] >> 24;
            uint64_t event_slot_id = control >> 24;
            xhci_info.device_descriptor_event_trb0 = event_ring[trb_index + 0];
            xhci_info.device_descriptor_event_trb1 = event_ring[trb_index + 1];
            xhci_info.device_descriptor_event_trb2 = event_ring[trb_index + 2];
            xhci_info.device_descriptor_event_trb3 = event_ring[trb_index + 3];
            xhci_info.device_descriptor_event_type = event_type;
            xhci_info.device_descriptor_completion_code = completion_code;
            if (completion_code == XHCI_COMPLETION_SUCCESS && event_slot_id == slot_id) {
                descriptor = (uint8_t *)phys_to_virt(xhci_info.device_descriptor_phys);
                xhci_info.device_descriptor_ok = 1;
                xhci_info.device_descriptor_first8 = read_le64(&descriptor[0]);
                xhci_info.device_descriptor_second8 = read_le64(&descriptor[8]);
                xhci_info.device_descriptor_tail = descriptor[16] | ((uint64_t)descriptor[17] << 8);
                xhci_info.device_descriptor_length = descriptor[0];
                xhci_info.device_descriptor_type = descriptor[1];
                xhci_info.device_usb_version = descriptor[2] | ((uint64_t)descriptor[3] << 8);
                xhci_info.device_class = descriptor[4];
                xhci_info.device_subclass = descriptor[5];
                xhci_info.device_protocol = descriptor[6];
                xhci_info.device_max_packet_raw = descriptor[7];
                xhci_info.device_vendor_id = descriptor[8] | ((uint64_t)descriptor[9] << 8);
                xhci_info.device_product_id = descriptor[10] | ((uint64_t)descriptor[11] << 8);
                xhci_info.device_version = descriptor[12] | ((uint64_t)descriptor[13] << 8);
            }
            xhci_info.erdp_value = xhci_info.event_ring_phys + XHCI_TRB_SIZE * (event_index + 1);
            mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_value | XHCI_ERDP_EHB);
            xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
            return;
        }
    }
}

static void xhci_get_first_configuration_descriptor(volatile uint8_t *regs) {
    uint8_t *descriptor;
    uint64_t total_length;

    if (regs == 0 || !xhci_info.device_descriptor_ok) {
        return;
    }

    xhci_info.configuration_descriptor_phys = pmm_alloc_page();
    if (xhci_info.configuration_descriptor_phys == 0) {
        return;
    }
    zero_page_phys(xhci_info.configuration_descriptor_phys);
    xhci_info.configuration_descriptor_attempted = 1;

    if (xhci_ep0_get_descriptor(regs,
                                3,
                                4,
                                USB_DESCRIPTOR_TYPE_CONFIGURATION,
                                9,
                                xhci_info.configuration_descriptor_phys,
                                &xhci_info.configuration_descriptor_completion_code,
                                &xhci_info.configuration_descriptor_event_type,
                                &xhci_info.configuration_descriptor_event_trb0,
                                &xhci_info.configuration_descriptor_event_trb1,
                                &xhci_info.configuration_descriptor_event_trb2,
                                &xhci_info.configuration_descriptor_event_trb3) != 0) {
        return;
    }

    descriptor = (uint8_t *)phys_to_virt(xhci_info.configuration_descriptor_phys);
    xhci_info.configuration_descriptor_length = descriptor[0];
    xhci_info.configuration_descriptor_type = descriptor[1];
    total_length = descriptor[2] | ((uint64_t)descriptor[3] << 8);
    xhci_info.configuration_total_length = total_length;
    if (xhci_info.configuration_descriptor_length < 9 ||
        xhci_info.configuration_descriptor_type != USB_DESCRIPTOR_TYPE_CONFIGURATION ||
        total_length < 9 || total_length > PAGE_SIZE) {
        return;
    }

    zero_page_phys(xhci_info.configuration_descriptor_phys);
    if (xhci_ep0_get_descriptor(regs,
                                6,
                                5,
                                USB_DESCRIPTOR_TYPE_CONFIGURATION,
                                total_length,
                                xhci_info.configuration_descriptor_phys,
                                &xhci_info.configuration_descriptor_completion_code,
                                &xhci_info.configuration_descriptor_event_type,
                                &xhci_info.configuration_descriptor_event_trb0,
                                &xhci_info.configuration_descriptor_event_trb1,
                                &xhci_info.configuration_descriptor_event_trb2,
                                &xhci_info.configuration_descriptor_event_trb3) != 0) {
        return;
    }

    descriptor = (uint8_t *)phys_to_virt(xhci_info.configuration_descriptor_phys);
    xhci_info.configuration_descriptor_ok = 1;
    xhci_info.configuration_descriptor_first8 = read_le64(&descriptor[0]);
    if (total_length >= 16) {
        xhci_info.configuration_descriptor_second8 = read_le64(&descriptor[8]);
    }
    xhci_info.configuration_descriptor_length = descriptor[0];
    xhci_info.configuration_descriptor_type = descriptor[1];
    xhci_info.configuration_total_length = descriptor[2] | ((uint64_t)descriptor[3] << 8);
    xhci_info.configuration_num_interfaces = descriptor[4];
    xhci_info.configuration_value = descriptor[5];
    xhci_info.configuration_attributes = descriptor[7];
    xhci_info.configuration_max_power = descriptor[8];

    for (uint64_t offset = descriptor[0]; offset + 2 <= total_length;) {
        uint64_t length = descriptor[offset];
        uint64_t type = descriptor[offset + 1];
        if (length == 0 || offset + length > total_length) {
            break;
        }
        if (type == USB_DESCRIPTOR_TYPE_INTERFACE && length >= 9 && !xhci_info.first_interface_seen) {
            xhci_info.first_interface_seen = 1;
            xhci_info.first_interface_number = descriptor[offset + 2];
            xhci_info.first_interface_alternate = descriptor[offset + 3];
            xhci_info.first_interface_endpoint_count = descriptor[offset + 4];
            xhci_info.first_interface_class = descriptor[offset + 5];
            xhci_info.first_interface_subclass = descriptor[offset + 6];
            xhci_info.first_interface_protocol = descriptor[offset + 7];
        } else if (type == USB_DESCRIPTOR_TYPE_ENDPOINT && length >= 7) {
            uint64_t address = descriptor[offset + 2];
            uint64_t attributes = descriptor[offset + 3];
            uint64_t max_packet_size = descriptor[offset + 4] | ((uint64_t)descriptor[offset + 5] << 8);
            uint64_t interval = descriptor[offset + 6];
            uint64_t transfer_type = attributes & USB_ENDPOINT_TRANSFER_TYPE_MASK;

            xhci_info.endpoint_descriptor_count++;
            if (transfer_type == USB_ENDPOINT_TRANSFER_TYPE_BULK) {
                if ((address & USB_ENDPOINT_DIRECTION_IN) != 0) {
                    if (!xhci_info.first_bulk_in_seen) {
                        xhci_info.first_bulk_in_seen = 1;
                        xhci_info.first_bulk_in_address = address;
                        xhci_info.first_bulk_in_attributes = attributes;
                        xhci_info.first_bulk_in_max_packet_size = max_packet_size;
                        xhci_info.first_bulk_in_interval = interval;
                        xhci_info.first_bulk_in_dci = xhci_dci_from_endpoint_address(address);
                    }
                } else if (!xhci_info.first_bulk_out_seen) {
                    xhci_info.first_bulk_out_seen = 1;
                    xhci_info.first_bulk_out_address = address;
                    xhci_info.first_bulk_out_attributes = attributes;
                    xhci_info.first_bulk_out_max_packet_size = max_packet_size;
                    xhci_info.first_bulk_out_interval = interval;
                    xhci_info.first_bulk_out_dci = xhci_dci_from_endpoint_address(address);
                }
            }
        }
        offset += length;
    }
}

static void xhci_configure_first_mass_storage_endpoints(volatile uint8_t *regs) {
    volatile uint8_t *doorbells;
    volatile uint8_t *rt;
    volatile uint8_t *ir0;
    volatile uint32_t *command_ring;
    volatile uint32_t *event_ring;
    uint32_t *input_context;
    uint32_t *slot_context;
    uint32_t *bulk_in_context;
    uint32_t *bulk_out_context;
    uint64_t *bulk_in_ring;
    uint64_t *bulk_out_ring;
    uint64_t context_size;
    uint64_t slot_id;
    uint64_t in_dci;
    uint64_t out_dci;
    uint64_t max_dci;

    if (regs == 0 || !xhci_info.configuration_descriptor_ok ||
        !xhci_info.first_bulk_in_seen || !xhci_info.first_bulk_out_seen ||
        xhci_info.address_device_slot_id == 0 || xhci_info.output_context_phys == 0) {
        return;
    }
    if (xhci_info.doorbell_offset == 0 || xhci_info.runtime_offset == 0 ||
        xhci_info.command_ring_phys == 0 || xhci_info.event_ring_phys == 0) {
        return;
    }

    slot_id = xhci_info.address_device_slot_id;
    in_dci = xhci_info.first_bulk_in_dci;
    out_dci = xhci_info.first_bulk_out_dci;
    if (in_dci == 0 || out_dci == 0 || in_dci >= 32 || out_dci >= 32) {
        return;
    }
    max_dci = in_dci > out_dci ? in_dci : out_dci;

    xhci_info.configure_endpoint_input_context_phys = pmm_alloc_page();
    xhci_info.bulk_in_ring_phys = pmm_alloc_contiguous_pages(XHCI_TRANSFER_RING_PAGES);
    xhci_info.bulk_out_ring_phys = pmm_alloc_contiguous_pages(XHCI_TRANSFER_RING_PAGES);
    if (xhci_info.configure_endpoint_input_context_phys == 0 ||
        xhci_info.bulk_in_ring_phys == 0 ||
        xhci_info.bulk_out_ring_phys == 0) {
        return;
    }

    zero_page_phys(xhci_info.configure_endpoint_input_context_phys);
    for (uint64_t i = 0; i < XHCI_TRANSFER_RING_PAGES; i++) {
        zero_page_phys(xhci_info.bulk_in_ring_phys + i * PAGE_SIZE);
        zero_page_phys(xhci_info.bulk_out_ring_phys + i * PAGE_SIZE);
    }

    context_size = xhci_info.context_size;
    if (context_size == 0) {
        context_size = 32;
    }

    input_context = (uint32_t *)phys_to_virt(xhci_info.configure_endpoint_input_context_phys);
    slot_context = (uint32_t *)((uint8_t *)input_context + context_size);
    bulk_out_context = (uint32_t *)((uint8_t *)input_context + context_size * (out_dci + 1));
    bulk_in_context = (uint32_t *)((uint8_t *)input_context + context_size * (in_dci + 1));
    bulk_in_ring = (uint64_t *)phys_to_virt(xhci_info.bulk_in_ring_phys);
    bulk_out_ring = (uint64_t *)phys_to_virt(xhci_info.bulk_out_ring_phys);

    bulk_in_ring[(XHCI_TRANSFER_RING_TRBS - 1) * 2 + 0] = xhci_info.bulk_in_ring_phys;
    bulk_in_ring[(XHCI_TRANSFER_RING_TRBS - 1) * 2 + 1] =
        (XHCI_LINK_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
    bulk_out_ring[(XHCI_TRANSFER_RING_TRBS - 1) * 2 + 0] = xhci_info.bulk_out_ring_phys;
    bulk_out_ring[(XHCI_TRANSFER_RING_TRBS - 1) * 2 + 1] =
        (XHCI_LINK_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) | XHCI_LINK_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;

    input_context[1] = (uint32_t)((1ULL << 0) | (1ULL << in_dci) | (1ULL << out_dci));
    copy_bytes(slot_context, (const void *)phys_to_virt(xhci_info.output_context_phys), context_size);
    slot_context[0] = (slot_context[0] & ~(0x1fU << 27)) | (uint32_t)((max_dci & 0x1fULL) << 27);

    bulk_in_context[1] = (uint32_t)((3ULL << 1) |
                                    (XHCI_ENDPOINT_TYPE_BULK_IN << 3) |
                                    (xhci_info.first_bulk_in_max_packet_size << 16));
    bulk_in_context[2] = (uint32_t)(xhci_info.bulk_in_ring_phys | XHCI_TRB_CYCLE);
    bulk_in_context[3] = (uint32_t)((xhci_info.bulk_in_ring_phys | XHCI_TRB_CYCLE) >> 32);
    bulk_in_context[4] = (uint32_t)xhci_info.first_bulk_in_max_packet_size;

    bulk_out_context[1] = (uint32_t)((3ULL << 1) |
                                     (XHCI_ENDPOINT_TYPE_BULK_OUT << 3) |
                                     (xhci_info.first_bulk_out_max_packet_size << 16));
    bulk_out_context[2] = (uint32_t)(xhci_info.bulk_out_ring_phys | XHCI_TRB_CYCLE);
    bulk_out_context[3] = (uint32_t)((xhci_info.bulk_out_ring_phys | XHCI_TRB_CYCLE) >> 32);
    bulk_out_context[4] = (uint32_t)xhci_info.first_bulk_out_max_packet_size;

    xhci_info.bulk_in_endpoint_context0 = bulk_in_context[0];
    xhci_info.bulk_in_endpoint_context1 = bulk_in_context[1];
    xhci_info.bulk_in_endpoint_context2 = bulk_in_context[2];
    xhci_info.bulk_in_endpoint_context3 = bulk_in_context[3];
    xhci_info.bulk_in_endpoint_context4 = bulk_in_context[4];
    xhci_info.bulk_out_endpoint_context0 = bulk_out_context[0];
    xhci_info.bulk_out_endpoint_context1 = bulk_out_context[1];
    xhci_info.bulk_out_endpoint_context2 = bulk_out_context[2];
    xhci_info.bulk_out_endpoint_context3 = bulk_out_context[3];
    xhci_info.bulk_out_endpoint_context4 = bulk_out_context[4];

    doorbells = regs + xhci_info.doorbell_offset;
    rt = regs + xhci_info.runtime_offset;
    ir0 = rt + XHCI_RUNTIME_IR0_OFFSET;
    command_ring = (volatile uint32_t *)phys_to_virt(xhci_info.command_ring_phys);
    event_ring = (volatile uint32_t *)phys_to_virt(xhci_info.event_ring_phys);

    command_ring[8] = (uint32_t)xhci_info.configure_endpoint_input_context_phys;
    command_ring[9] = (uint32_t)(xhci_info.configure_endpoint_input_context_phys >> 32);
    command_ring[10] = 0;
    command_ring[11] = (uint32_t)((slot_id << 24) |
                                  (XHCI_CONFIGURE_ENDPOINT_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                  XHCI_TRB_CYCLE);
    __sync_synchronize();

    xhci_info.configure_endpoint_attempted = 1;
    xhci_info.command_doorbell_value = 0;
    mmio_write32(doorbells, 0, (uint32_t)xhci_info.command_doorbell_value);

    for (uint64_t spin = 0; spin < XHCI_POLL_LIMIT; spin++) {
        for (uint64_t event_index = 6; event_index < 16; event_index++) {
            uint64_t trb_index = event_index * 4;
            uint32_t control = event_ring[trb_index + 3];
            if ((control & XHCI_TRB_CYCLE) == 0) {
                continue;
            }

            uint64_t event_type = (control >> XHCI_TRB_TYPE_SHIFT) & 0x3fU;
            if (event_type != XHCI_COMMAND_COMPLETION_EVENT_TYPE) {
                continue;
            }

            uint64_t completion_code = event_ring[trb_index + 2] >> 24;
            uint64_t event_slot_id = control >> 24;
            xhci_info.configure_endpoint_event_trb0 = event_ring[trb_index + 0];
            xhci_info.configure_endpoint_event_trb1 = event_ring[trb_index + 1];
            xhci_info.configure_endpoint_event_trb2 = event_ring[trb_index + 2];
            xhci_info.configure_endpoint_event_trb3 = event_ring[trb_index + 3];
            xhci_info.configure_endpoint_event_type = event_type;
            xhci_info.configure_endpoint_completion_code = completion_code;
            xhci_info.configure_endpoint_slot_id = event_slot_id;
            if (completion_code == XHCI_COMPLETION_SUCCESS && event_slot_id == slot_id) {
                xhci_info.configure_endpoint_ok = 1;
            }
            xhci_info.erdp_value = xhci_info.event_ring_phys + XHCI_TRB_SIZE * (event_index + 1);
            mmio_write64(ir0, XHCI_IR_ERDP, xhci_info.erdp_value | XHCI_ERDP_EHB);
            xhci_info.erdp_value = mmio_read64(ir0, XHCI_IR_ERDP);
            return;
        }
    }
}

static int xhci_bulk_transfer(volatile uint8_t *regs,
                              uint64_t ring_phys,
                              uint64_t ring_trb_index,
                              uint64_t endpoint_dci,
                              uint64_t buffer_phys,
                              uint64_t length,
                              uint64_t first_event_index,
                              uint64_t *completion_code,
                              uint64_t *event_type_out,
                              uint64_t *event_trb0,
                              uint64_t *event_trb1,
                              uint64_t *event_trb2,
                              uint64_t *event_trb3) {
    volatile uint8_t *doorbells;
    volatile uint32_t *ring;
    uint64_t slot_id;
    uint64_t base;

    if (regs == 0 || ring_phys == 0 || buffer_phys == 0 ||
        endpoint_dci == 0 || endpoint_dci >= 32 ||
        xhci_info.address_device_slot_id == 0 || xhci_info.doorbell_offset == 0) {
        return -1;
    }
    if (ring_trb_index >= XHCI_TRANSFER_RING_TRBS - 1 || length == 0 || length > PAGE_SIZE) {
        return -1;
    }

    slot_id = xhci_info.address_device_slot_id;
    base = ring_trb_index * 4;
    ring = (volatile uint32_t *)phys_to_virt(ring_phys);
    doorbells = regs + xhci_info.doorbell_offset;

    ring[base + 0] = (uint32_t)buffer_phys;
    ring[base + 1] = (uint32_t)(buffer_phys >> 32);
    ring[base + 2] = (uint32_t)length;
    ring[base + 3] = (uint32_t)((XHCI_NORMAL_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                XHCI_TRB_IOC |
                                XHCI_TRB_CYCLE);
    __sync_synchronize();

    mmio_write32(doorbells, slot_id * 4, (uint32_t)endpoint_dci);
    return xhci_wait_transfer_event(regs,
                                    first_event_index,
                                    slot_id,
                                    completion_code,
                                    event_type_out,
                                    event_trb0,
                                    event_trb1,
                                    event_trb2,
                                    event_trb3);
}

static int xhci_bulk_transfer_dynamic(volatile uint8_t *regs,
                                      uint64_t ring_phys,
                                      uint64_t *next_trb,
                                      uint64_t *producer_cycle,
                                      uint64_t *wraps,
                                      const char *name,
                                      uint64_t endpoint_dci,
                                      uint64_t buffer_phys,
                                      uint64_t length,
                                      uint64_t *completion_code,
                                      uint64_t *event_type_out,
                                      uint64_t *event_trb0,
                                      uint64_t *event_trb1,
                                      uint64_t *event_trb2,
                                      uint64_t *event_trb3) {
    volatile uint8_t *doorbells;
    volatile uint32_t *ring;
    uint64_t slot_id;
    uint64_t base;

    if (regs == 0 || ring_phys == 0 || next_trb == 0 ||
        producer_cycle == 0 || buffer_phys == 0 ||
        endpoint_dci == 0 || endpoint_dci >= 32 ||
        xhci_info.address_device_slot_id == 0 || xhci_info.doorbell_offset == 0 ||
        *next_trb >= XHCI_TRANSFER_RING_TRBS - 1 ||
        length == 0 || length > PAGE_SIZE) {
        return -1;
    }

    slot_id = xhci_info.address_device_slot_id;
    base = *next_trb * 4;
    ring = (volatile uint32_t *)phys_to_virt(ring_phys);
    doorbells = regs + xhci_info.doorbell_offset;

    if (*next_trb + 1 < XHCI_TRANSFER_RING_TRBS - 1) {
        xhci_mark_transfer_trb_inactive(ring_phys, *next_trb + 1, *producer_cycle ? 0 : 1);
    } else {
        uint64_t link_base = (XHCI_TRANSFER_RING_TRBS - 1) * 4;
        ring[link_base + 0] = (uint32_t)ring_phys;
        ring[link_base + 1] = (uint32_t)(ring_phys >> 32);
        ring[link_base + 2] = 0;
        ring[link_base + 3] = (uint32_t)((XHCI_LINK_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                        XHCI_LINK_TRB_TOGGLE_CYCLE |
                                        (*producer_cycle ? XHCI_TRB_CYCLE : 0));
        xhci_mark_transfer_trb_inactive(ring_phys, 0, *producer_cycle);
    }

    ring[base + 0] = (uint32_t)buffer_phys;
    ring[base + 1] = (uint32_t)(buffer_phys >> 32);
    ring[base + 2] = (uint32_t)length;
    ring[base + 3] = (uint32_t)((XHCI_NORMAL_TRB_TYPE << XHCI_TRB_TYPE_SHIFT) |
                                XHCI_TRB_IOC |
                                (*producer_cycle ? XHCI_TRB_CYCLE : 0));
    __sync_synchronize();

    mmio_write32(doorbells, slot_id * 4, (uint32_t)endpoint_dci);
    if (xhci_wait_next_transfer_event(regs,
                                      slot_id,
                                      completion_code,
                                      event_type_out,
                                      event_trb0,
                                      event_trb1,
                                      event_trb2,
                                      event_trb3) != 0) {
        return -1;
    }

    xhci_advance_transfer_ring(next_trb, producer_cycle, wraps, name);
    return 0;
}

struct xhci_bot_result {
    uint64_t cbw_completion_code;
    uint64_t cbw_event_type;
    uint64_t data_completion_code;
    uint64_t data_event_type;
    uint64_t csw_completion_code;
    uint64_t csw_event_type;
    uint64_t csw_signature;
    uint64_t csw_tag;
    uint64_t csw_residue;
    uint64_t csw_status;
};

static int xhci_bot_scsi_command(volatile uint8_t *regs,
                                 uint64_t tag,
                                 const uint8_t *cdb,
                                 uint64_t cdb_length,
                                 uint64_t data_phys,
                                 uint64_t data_length,
                                 uint64_t data_in,
                                 uint64_t out_trb_index,
                                 uint64_t in_trb_index,
                                 uint64_t first_event_index,
                                 struct xhci_bot_result *result) {
    uint8_t *cbw;
    uint8_t *csw;

    if (regs == 0 || cdb == 0 || result == 0 ||
        cdb_length == 0 || cdb_length > 16 ||
        xhci_info.bot_cbw_phys == 0 || xhci_info.bot_csw_phys == 0 ||
        xhci_info.bulk_in_ring_phys == 0 || xhci_info.bulk_out_ring_phys == 0 ||
        xhci_info.first_bulk_in_dci == 0 || xhci_info.first_bulk_out_dci == 0 ||
        data_phys == 0 || data_length == 0 || data_length > PAGE_SIZE ||
        first_event_index + 2 >= XHCI_EVENT_RING_SEGMENT_TRBS) {
        return -1;
    }

    zero_page_phys(xhci_info.bot_cbw_phys);
    zero_page_phys(xhci_info.bot_csw_phys);
    zero_bytes(result, sizeof(*result));

    cbw = (uint8_t *)phys_to_virt(xhci_info.bot_cbw_phys);
    write_le32(&cbw[0], (uint32_t)USB_BOT_CBW_SIGNATURE);
    write_le32(&cbw[4], (uint32_t)tag);
    write_le32(&cbw[8], (uint32_t)data_length);
    cbw[12] = data_in ? 0x80 : 0x00;
    cbw[13] = 0;
    cbw[14] = (uint8_t)cdb_length;
    copy_bytes(&cbw[15], cdb, cdb_length);

    if (xhci_bulk_transfer(regs,
                           xhci_info.bulk_out_ring_phys,
                           out_trb_index,
                           xhci_info.first_bulk_out_dci,
                           xhci_info.bot_cbw_phys,
                           USB_BOT_CBW_LENGTH,
                           first_event_index,
                           &result->cbw_completion_code,
                           &result->cbw_event_type,
                           0,
                           0,
                           0,
                           0) != 0) {
        return -1;
    }

    if (xhci_bulk_transfer(regs,
                           data_in ? xhci_info.bulk_in_ring_phys : xhci_info.bulk_out_ring_phys,
                           data_in ? in_trb_index : out_trb_index + 1,
                           data_in ? xhci_info.first_bulk_in_dci : xhci_info.first_bulk_out_dci,
                           data_phys,
                           data_length,
                           first_event_index + 1,
                           &result->data_completion_code,
                           &result->data_event_type,
                           0,
                           0,
                           0,
                           0) != 0) {
        return -1;
    }

    if (xhci_bulk_transfer(regs,
                           xhci_info.bulk_in_ring_phys,
                           data_in ? in_trb_index + 1 : in_trb_index,
                           xhci_info.first_bulk_in_dci,
                           xhci_info.bot_csw_phys,
                           USB_BOT_CSW_LENGTH,
                           first_event_index + 2,
                           &result->csw_completion_code,
                           &result->csw_event_type,
                           0,
                           0,
                           0,
                           0) != 0) {
        return -1;
    }

    csw = (uint8_t *)phys_to_virt(xhci_info.bot_csw_phys);
    result->csw_signature = read_le32(&csw[0]);
    result->csw_tag = read_le32(&csw[4]);
    result->csw_residue = read_le32(&csw[8]);
    result->csw_status = csw[12];

    if (result->csw_signature != USB_BOT_CSW_SIGNATURE ||
        result->csw_tag != tag ||
        result->csw_status != 0) {
        return -1;
    }

    return 0;
}

static int xhci_bot_scsi_command_dynamic(volatile uint8_t *regs,
                                         uint64_t tag,
                                         const uint8_t *cdb,
                                         uint64_t cdb_length,
                                         uint64_t data_phys,
                                         uint64_t data_length,
                                         uint64_t data_in,
                                         struct xhci_bot_result *result) {
    uint8_t *cbw;
    uint8_t *csw;

    if (regs == 0 || cdb == 0 || result == 0 ||
        cdb_length == 0 || cdb_length > 16 ||
        xhci_info.bot_cbw_phys == 0 || xhci_info.bot_csw_phys == 0 ||
        xhci_info.bulk_in_ring_phys == 0 || xhci_info.bulk_out_ring_phys == 0 ||
        xhci_info.first_bulk_in_dci == 0 || xhci_info.first_bulk_out_dci == 0 ||
        data_phys == 0 || data_length == 0 || data_length > PAGE_SIZE ||
        !xhci_bot_ring_ready) {
        return -1;
    }

    zero_page_phys(xhci_info.bot_cbw_phys);
    zero_page_phys(xhci_info.bot_csw_phys);
    zero_bytes(result, sizeof(*result));

    cbw = (uint8_t *)phys_to_virt(xhci_info.bot_cbw_phys);
    write_le32(&cbw[0], (uint32_t)USB_BOT_CBW_SIGNATURE);
    write_le32(&cbw[4], (uint32_t)tag);
    write_le32(&cbw[8], (uint32_t)data_length);
    cbw[12] = data_in ? 0x80 : 0x00;
    cbw[13] = 0;
    cbw[14] = (uint8_t)cdb_length;
    copy_bytes(&cbw[15], cdb, cdb_length);

    if (xhci_bulk_transfer_dynamic(regs,
                                   xhci_info.bulk_out_ring_phys,
                                   &xhci_bulk_out_next_trb,
                                   &xhci_bulk_out_cycle,
                                   &xhci_bulk_out_wraps,
                                   "bulk-out",
                                   xhci_info.first_bulk_out_dci,
                                   xhci_info.bot_cbw_phys,
                                   USB_BOT_CBW_LENGTH,
                                   &result->cbw_completion_code,
                                   &result->cbw_event_type,
                                   0,
                                   0,
                                   0,
                                   0) != 0) {
        return -1;
    }

    if (xhci_bulk_transfer_dynamic(regs,
                                   data_in ? xhci_info.bulk_in_ring_phys : xhci_info.bulk_out_ring_phys,
                                   data_in ? &xhci_bulk_in_next_trb : &xhci_bulk_out_next_trb,
                                   data_in ? &xhci_bulk_in_cycle : &xhci_bulk_out_cycle,
                                   data_in ? &xhci_bulk_in_wraps : &xhci_bulk_out_wraps,
                                   data_in ? "bulk-in" : "bulk-out",
                                   data_in ? xhci_info.first_bulk_in_dci : xhci_info.first_bulk_out_dci,
                                   data_phys,
                                   data_length,
                                   &result->data_completion_code,
                                   &result->data_event_type,
                                   0,
                                   0,
                                   0,
                                   0) != 0) {
        return -1;
    }

    if (xhci_bulk_transfer_dynamic(regs,
                                   xhci_info.bulk_in_ring_phys,
                                   &xhci_bulk_in_next_trb,
                                   &xhci_bulk_in_cycle,
                                   &xhci_bulk_in_wraps,
                                   "bulk-in",
                                   xhci_info.first_bulk_in_dci,
                                   xhci_info.bot_csw_phys,
                                   USB_BOT_CSW_LENGTH,
                                   &result->csw_completion_code,
                                   &result->csw_event_type,
                                   0,
                                   0,
                                   0,
                                   0) != 0) {
        return -1;
    }

    csw = (uint8_t *)phys_to_virt(xhci_info.bot_csw_phys);
    result->csw_signature = read_le32(&csw[0]);
    result->csw_tag = read_le32(&csw[4]);
    result->csw_residue = read_le32(&csw[8]);
    result->csw_status = csw[12];

    if (result->csw_signature != USB_BOT_CSW_SIGNATURE ||
        result->csw_tag != tag ||
        result->csw_status != 0) {
        return -1;
    }

    return 0;
}

static void xhci_set_configuration_and_probe_storage(volatile uint8_t *regs) {
    uint8_t inquiry_cdb[6];
    uint8_t read_capacity_cdb[10];
    uint8_t read10_cdb[10];
    struct xhci_bot_result result;
    uint8_t *data;

    if (regs == 0 || !xhci_info.configure_endpoint_ok ||
        xhci_info.configuration_value == 0 ||
        xhci_info.bulk_in_ring_phys == 0 || xhci_info.bulk_out_ring_phys == 0 ||
        xhci_info.first_bulk_in_dci == 0 || xhci_info.first_bulk_out_dci == 0) {
        return;
    }

    xhci_info.set_configuration_value = xhci_info.configuration_value;
    xhci_info.set_configuration_attempted = 1;
    if (xhci_ep0_set_configuration(regs,
                                   9,
                                   7,
                                   xhci_info.set_configuration_value,
                                   &xhci_info.set_configuration_completion_code,
                                   &xhci_info.set_configuration_event_type,
                                   &xhci_info.set_configuration_event_trb0,
                                   &xhci_info.set_configuration_event_trb1,
                                   &xhci_info.set_configuration_event_trb2,
                                   &xhci_info.set_configuration_event_trb3) != 0) {
        return;
    }
    xhci_info.set_configuration_ok = 1;

    xhci_info.bot_cbw_phys = pmm_alloc_page();
    xhci_info.bot_inquiry_data_phys = pmm_alloc_page();
    xhci_info.bot_csw_phys = pmm_alloc_page();
    if (xhci_info.bot_cbw_phys == 0 ||
        xhci_info.bot_inquiry_data_phys == 0 ||
        xhci_info.bot_csw_phys == 0) {
        return;
    }
    zero_page_phys(xhci_info.bot_cbw_phys);
    zero_page_phys(xhci_info.bot_inquiry_data_phys);
    zero_page_phys(xhci_info.bot_csw_phys);

    xhci_info.bot_inquiry_attempted = 1;
    xhci_info.bot_inquiry_tag = USB_BOT_CBW_TAG_INQUIRY;
    zero_bytes(inquiry_cdb, sizeof(inquiry_cdb));
    inquiry_cdb[0] = 0x12;
    inquiry_cdb[4] = (uint8_t)USB_BOT_INQUIRY_LENGTH;
    if (xhci_bot_scsi_command(regs,
                              xhci_info.bot_inquiry_tag,
                              inquiry_cdb,
                              sizeof(inquiry_cdb),
                              xhci_info.bot_inquiry_data_phys,
                              USB_BOT_INQUIRY_LENGTH,
                              1,
                              0,
                              0,
                              8,
                              &result) != 0) {
        return;
    }

    data = (uint8_t *)phys_to_virt(xhci_info.bot_inquiry_data_phys);
    xhci_info.bot_cbw_completion_code = result.cbw_completion_code;
    xhci_info.bot_cbw_event_type = result.cbw_event_type;
    xhci_info.bot_data_completion_code = result.data_completion_code;
    xhci_info.bot_data_event_type = result.data_event_type;
    xhci_info.bot_csw_completion_code = result.csw_completion_code;
    xhci_info.bot_csw_event_type = result.csw_event_type;
    xhci_info.bot_csw_signature = result.csw_signature;
    xhci_info.bot_csw_tag = result.csw_tag;
    xhci_info.bot_csw_residue = result.csw_residue;
    xhci_info.bot_csw_status = result.csw_status;
    xhci_info.inquiry_peripheral = data[0];
    xhci_info.inquiry_removable = data[1];
    xhci_info.inquiry_version = data[2];
    xhci_info.inquiry_response_format = data[3];
    xhci_info.inquiry_additional_length = data[4];
    xhci_info.inquiry_vendor_first8 = read_le64(&data[8]);
    xhci_info.inquiry_product_first8 = read_le64(&data[16]);
    xhci_info.inquiry_product_second8 = read_le64(&data[24]);
    xhci_info.inquiry_revision = read_le32(&data[32]);
    xhci_info.bot_inquiry_ok = 1;

    xhci_info.bot_capacity_data_phys = pmm_alloc_page();
    if (xhci_info.bot_capacity_data_phys == 0) {
        return;
    }
    zero_page_phys(xhci_info.bot_capacity_data_phys);
    zero_bytes(read_capacity_cdb, sizeof(read_capacity_cdb));
    read_capacity_cdb[0] = 0x25;
    xhci_info.bot_capacity_attempted = 1;
    xhci_info.bot_capacity_tag = USB_BOT_CBW_TAG_READ_CAPACITY;
    if (xhci_bot_scsi_command(regs,
                              xhci_info.bot_capacity_tag,
                              read_capacity_cdb,
                              sizeof(read_capacity_cdb),
                              xhci_info.bot_capacity_data_phys,
                              USB_BOT_READ_CAPACITY_LENGTH,
                              1,
                              1,
                              2,
                              11,
                              &result) != 0) {
        xhci_info.bot_capacity_cbw_completion_code = result.cbw_completion_code;
        xhci_info.bot_capacity_data_completion_code = result.data_completion_code;
        xhci_info.bot_capacity_csw_completion_code = result.csw_completion_code;
        xhci_info.bot_capacity_csw_signature = result.csw_signature;
        xhci_info.bot_capacity_csw_tag = result.csw_tag;
        xhci_info.bot_capacity_csw_residue = result.csw_residue;
        xhci_info.bot_capacity_csw_status = result.csw_status;
        return;
    }

    data = (uint8_t *)phys_to_virt(xhci_info.bot_capacity_data_phys);
    xhci_info.bot_capacity_cbw_completion_code = result.cbw_completion_code;
    xhci_info.bot_capacity_data_completion_code = result.data_completion_code;
    xhci_info.bot_capacity_csw_completion_code = result.csw_completion_code;
    xhci_info.bot_capacity_csw_signature = result.csw_signature;
    xhci_info.bot_capacity_csw_tag = result.csw_tag;
    xhci_info.bot_capacity_csw_residue = result.csw_residue;
    xhci_info.bot_capacity_csw_status = result.csw_status;
    xhci_info.bot_capacity_last_lba = read_be32(&data[0]);
    xhci_info.bot_capacity_block_size = read_be32(&data[4]);
    xhci_info.bot_capacity_block_count = xhci_info.bot_capacity_last_lba + 1;
    xhci_info.bot_capacity_ok = 1;

    if (xhci_info.bot_capacity_block_size == 0 ||
        xhci_info.bot_capacity_block_size > PAGE_SIZE) {
        return;
    }

    xhci_info.bot_read10_data_phys = pmm_alloc_page();
    if (xhci_info.bot_read10_data_phys == 0) {
        return;
    }
    zero_page_phys(xhci_info.bot_read10_data_phys);
    zero_bytes(read10_cdb, sizeof(read10_cdb));
    read10_cdb[0] = 0x28;
    write_be32(&read10_cdb[2], 0);
    write_be16(&read10_cdb[7], 1);
    xhci_info.bot_read10_attempted = 1;
    xhci_info.bot_read10_tag = USB_BOT_CBW_TAG_READ10;
    xhci_info.bot_read10_lba = 0;
    xhci_info.bot_read10_bytes = xhci_info.bot_capacity_block_size;
    if (xhci_bot_scsi_command(regs,
                              xhci_info.bot_read10_tag,
                              read10_cdb,
                              sizeof(read10_cdb),
                              xhci_info.bot_read10_data_phys,
                              xhci_info.bot_read10_bytes,
                              1,
                              2,
                              4,
                              14,
                              &result) != 0) {
        xhci_info.bot_read10_cbw_completion_code = result.cbw_completion_code;
        xhci_info.bot_read10_data_completion_code = result.data_completion_code;
        xhci_info.bot_read10_csw_completion_code = result.csw_completion_code;
        xhci_info.bot_read10_csw_signature = result.csw_signature;
        xhci_info.bot_read10_csw_tag = result.csw_tag;
        xhci_info.bot_read10_csw_residue = result.csw_residue;
        xhci_info.bot_read10_csw_status = result.csw_status;
        return;
    }

    data = (uint8_t *)phys_to_virt(xhci_info.bot_read10_data_phys);
    xhci_info.bot_read10_cbw_completion_code = result.cbw_completion_code;
    xhci_info.bot_read10_data_completion_code = result.data_completion_code;
    xhci_info.bot_read10_csw_completion_code = result.csw_completion_code;
    xhci_info.bot_read10_csw_signature = result.csw_signature;
    xhci_info.bot_read10_csw_tag = result.csw_tag;
    xhci_info.bot_read10_csw_residue = result.csw_residue;
    xhci_info.bot_read10_csw_status = result.csw_status;
    xhci_info.bot_read10_first8 = read_le64(&data[0]);
    xhci_info.bot_read10_second8 = read_le64(&data[8]);
    xhci_info.bot_read10_ok = 1;
    xhci_bulk_out_next_trb = 3;
    xhci_bulk_in_next_trb = 6;
    xhci_next_transfer_event = 17;
    xhci_bulk_out_cycle = 1;
    xhci_bulk_in_cycle = 1;
    xhci_transfer_event_cycle = 1;
    xhci_bulk_out_wraps = 0;
    xhci_bulk_in_wraps = 0;
    xhci_transfer_event_wraps = 0;
    xhci_bot_ring_ready = 1;
    xhci_bot_next_tag = 0x544f5100ULL;
}

static int xhci_usb_storage_read_sector(uint64_t lba, void *buffer) {
    uint8_t read10_cdb[10];
    struct xhci_bot_result result;
    uint64_t tag;
    uint8_t *data;

    if (xhci_regs == 0 || buffer == 0 || !xhci_info.bot_capacity_ok ||
        xhci_info.bot_capacity_block_size != 512 ||
        xhci_info.bot_read10_data_phys == 0 ||
        !xhci_bot_ring_ready || xhci_bot_next_tag == 0) {
        return -1;
    }
    if (lba > xhci_info.bot_capacity_last_lba) {
        return -1;
    }

    zero_page_phys(xhci_info.bot_read10_data_phys);
    zero_bytes(read10_cdb, sizeof(read10_cdb));
    read10_cdb[0] = 0x28;
    write_be32(&read10_cdb[2], (uint32_t)lba);
    write_be16(&read10_cdb[7], 1);

    tag = xhci_bot_next_tag++;
    xhci_info.bot_read10_attempted = 1;
    xhci_info.bot_read10_tag = tag;
    xhci_info.bot_read10_lba = lba;
    xhci_info.bot_read10_bytes = 512;
    if (xhci_bot_scsi_command_dynamic(xhci_regs,
                                      tag,
                                      read10_cdb,
                                      sizeof(read10_cdb),
                                      xhci_info.bot_read10_data_phys,
                                      512,
                                      1,
                                      &result) != 0) {
        xhci_info.bot_read10_cbw_completion_code = result.cbw_completion_code;
        xhci_info.bot_read10_data_completion_code = result.data_completion_code;
        xhci_info.bot_read10_csw_completion_code = result.csw_completion_code;
        xhci_info.bot_read10_csw_signature = result.csw_signature;
        xhci_info.bot_read10_csw_tag = result.csw_tag;
        xhci_info.bot_read10_csw_residue = result.csw_residue;
        xhci_info.bot_read10_csw_status = result.csw_status;
        xhci_info.bot_read10_ok = 0;
        return -1;
    }

    data = (uint8_t *)phys_to_virt(xhci_info.bot_read10_data_phys);
    copy_bytes(buffer, data, 512);
    xhci_info.bot_read10_cbw_completion_code = result.cbw_completion_code;
    xhci_info.bot_read10_data_completion_code = result.data_completion_code;
    xhci_info.bot_read10_csw_completion_code = result.csw_completion_code;
    xhci_info.bot_read10_csw_signature = result.csw_signature;
    xhci_info.bot_read10_csw_tag = result.csw_tag;
    xhci_info.bot_read10_csw_residue = result.csw_residue;
    xhci_info.bot_read10_csw_status = result.csw_status;
    xhci_info.bot_read10_first8 = read_le64(&data[0]);
    xhci_info.bot_read10_second8 = read_le64(&data[8]);
    xhci_info.bot_read10_ok = 1;
    return 0;
}

static int xhci_usb_storage_write_sector(uint64_t lba, const void *buffer) {
    uint8_t write10_cdb[10];
    struct xhci_bot_result result;
    uint64_t tag;
    uint8_t *data;

    if (xhci_regs == 0 || buffer == 0 || !xhci_info.bot_capacity_ok ||
        xhci_info.bot_capacity_block_size != 512 ||
        xhci_info.bot_read10_data_phys == 0 ||
        !xhci_bot_ring_ready || xhci_bot_next_tag == 0) {
        return -1;
    }
    if (lba > xhci_info.bot_capacity_last_lba) {
        return -1;
    }

    data = (uint8_t *)phys_to_virt(xhci_info.bot_read10_data_phys);
    copy_bytes(data, buffer, 512);
    zero_bytes(write10_cdb, sizeof(write10_cdb));
    write10_cdb[0] = 0x2a;
    write_be32(&write10_cdb[2], (uint32_t)lba);
    write_be16(&write10_cdb[7], 1);

    tag = xhci_bot_next_tag++;
    xhci_info.bot_write10_attempted = 1;
    xhci_info.bot_write10_tag = tag;
    xhci_info.bot_write10_lba = lba;
    xhci_info.bot_write10_bytes = 512;
    xhci_info.bot_write10_data_phys = xhci_info.bot_read10_data_phys;
    if (xhci_bot_scsi_command_dynamic(xhci_regs,
                                      tag,
                                      write10_cdb,
                                      sizeof(write10_cdb),
                                      xhci_info.bot_write10_data_phys,
                                      512,
                                      0,
                                      &result) != 0) {
        xhci_info.bot_write10_cbw_completion_code = result.cbw_completion_code;
        xhci_info.bot_write10_data_completion_code = result.data_completion_code;
        xhci_info.bot_write10_csw_completion_code = result.csw_completion_code;
        xhci_info.bot_write10_csw_signature = result.csw_signature;
        xhci_info.bot_write10_csw_tag = result.csw_tag;
        xhci_info.bot_write10_csw_residue = result.csw_residue;
        xhci_info.bot_write10_csw_status = result.csw_status;
        xhci_info.bot_write10_first8 = read_le64(&data[0]);
        xhci_info.bot_write10_second8 = read_le64(&data[8]);
        xhci_info.bot_write10_ok = 0;
        return -1;
    }

    xhci_info.bot_write10_cbw_completion_code = XHCI_COMPLETION_SUCCESS;
    xhci_info.bot_write10_data_completion_code = XHCI_COMPLETION_SUCCESS;
    xhci_info.bot_write10_csw_completion_code = XHCI_COMPLETION_SUCCESS;
    xhci_info.bot_write10_csw_signature = USB_BOT_CSW_SIGNATURE;
    xhci_info.bot_write10_csw_tag = tag;
    xhci_info.bot_write10_csw_residue = 0;
    xhci_info.bot_write10_csw_status = 0;
    xhci_info.bot_write10_first8 = read_le64(&data[0]);
    xhci_info.bot_write10_second8 = read_le64(&data[8]);
    xhci_info.bot_write10_ok = 1;
    return 0;
}

static int xhci_usb_storage_read(struct block_device *device, uint64_t lba,
                                 uint64_t count, void *buffer) {
    uint8_t *out = (uint8_t *)buffer;

    (void)device;
    if (out == 0 || count == 0) {
        return -1;
    }
    for (uint64_t i = 0; i < count; i++) {
        if (xhci_usb_storage_read_sector(lba + i, out + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

static int xhci_usb_storage_write(struct block_device *device, uint64_t lba,
                                  uint64_t count, const void *buffer) {
    const uint8_t *in = (const uint8_t *)buffer;

    (void)device;
    if (in == 0 || count == 0) {
        return -1;
    }
    for (uint64_t i = 0; i < count; i++) {
        if (xhci_usb_storage_write_sector(lba + i, in + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

static void fill_usb_write10_test_pattern(uint8_t *buffer, uint64_t lba) {
    static const char marker[] = "TPOSUSBW";

    for (uint64_t i = 0; i < 512; i++) {
        buffer[i] = (uint8_t)(0xa0U ^ (uint8_t)i);
    }
    for (uint64_t i = 0; i < sizeof(marker) - 1; i++) {
        buffer[i] = (uint8_t)marker[i];
    }
    write_le32(&buffer[8], (uint32_t)lba);
    write_le32(&buffer[12], 0x10203040U);
}

static int usb_storage_write10_self_test(uint64_t scratch_lba) {
    uint8_t expected[512];
    uint8_t actual[512];

    fill_usb_write10_test_pattern(expected, scratch_lba);
    if (xhci_usb_storage_write_sector(scratch_lba, expected) != 0) {
        return -1;
    }
    if (xhci_usb_storage_read_sector(scratch_lba, actual) != 0) {
        return -1;
    }
    for (uint64_t i = 0; i < sizeof(expected); i++) {
        if (actual[i] != expected[i]) {
            return -1;
        }
    }
    xhci_info.bot_write10_verified = 1;
    xhci_info.bot_write10_first8 = read_le64(&actual[0]);
    xhci_info.bot_write10_second8 = read_le64(&actual[8]);
    return 0;
}

int pci_register_usb_storage_block_device(void) {
    uint64_t usb_storage_writable = 0;
    uint64_t scratch_lba;

    if (xhci_usb_storage_registered) {
        return 0;
    }
    if (!xhci_info.bot_capacity_ok || !xhci_info.bot_read10_ok ||
        xhci_info.bot_capacity_block_size != 512 ||
        xhci_info.bot_capacity_block_count == 0 ||
        xhci_regs == 0) {
        return -1;
    }

    scratch_lba = xhci_info.bot_capacity_last_lba;
    if (scratch_lba > 0 && usb_storage_write10_self_test(scratch_lba) == 0) {
        usb_storage_writable = 1;
        log_info("USB BOT write10 self-test: ok=1 lba=%u first=%x/%x\n",
                 scratch_lba,
                 xhci_info.bot_write10_first8,
                 xhci_info.bot_write10_second8);
    } else {
        log_warn("USB BOT write10 self-test failed; usb0 remains readonly\n");
    }

    xhci_usb_storage_block.name[0] = 'u';
    xhci_usb_storage_block.name[1] = 's';
    xhci_usb_storage_block.name[2] = 'b';
    xhci_usb_storage_block.name[3] = '0';
    xhci_usb_storage_block.name[4] = '\0';
    xhci_usb_storage_block.block_size = 512;
    xhci_usb_storage_block.block_count = xhci_info.bot_capacity_block_count;
    xhci_usb_storage_block.writable = usb_storage_writable;
    xhci_usb_storage_block.context = 0;
    xhci_usb_storage_block.read = xhci_usb_storage_read;
    xhci_usb_storage_block.write = usb_storage_writable ? xhci_usb_storage_write : 0;

    if (block_register(&xhci_usb_storage_block) != 0) {
        log_warn("USB storage block registration failed\n");
        return -1;
    }

    xhci_usb_storage_registered = 1;
    log_info("USB storage block ready: usb0 blocks=%u block_size=512 writable=%u\n",
             xhci_usb_storage_block.block_count,
             xhci_usb_storage_block.writable);
    return 0;
}

static void pci_scan_xhci(void) {
    struct pci_device device;

    zero_bytes(&xhci_info, sizeof(xhci_info));
    xhci_regs = 0;
    xhci_bulk_out_next_trb = 0;
    xhci_bulk_in_next_trb = 0;
    xhci_next_transfer_event = 0;
    xhci_bulk_out_cycle = 0;
    xhci_bulk_in_cycle = 0;
    xhci_transfer_event_cycle = 0;
    xhci_bulk_out_wraps = 0;
    xhci_bulk_in_wraps = 0;
    xhci_transfer_event_wraps = 0;
    xhci_bot_ring_ready = 0;
    xhci_bot_next_tag = 0;
    xhci_usb_storage_registered = 0;
    zero_bytes(&xhci_usb_storage_block, sizeof(xhci_usb_storage_block));
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
                            xhci_regs = regs;
                            xhci_info.mmio_mapped = 1;
                            capreg = *(volatile uint32_t *)(regs + 0x00);
                            xhci_info.cap_length = capreg & 0xffU;
                            xhci_info.hci_version = (capreg >> 16) & 0xffffU;
                            hcsparams1 = *(volatile uint32_t *)(regs + 0x04);
                            uint32_t hcsparams2 = *(volatile uint32_t *)(regs + 0x08);
                            uint32_t hccparams1 = *(volatile uint32_t *)(regs + 0x10);
                            xhci_info.context_size = (hccparams1 & XHCI_HCCPARAMS1_CSZ) ? 64 : 32;
                            xhci_info.max_slots = hcsparams1 & 0xffU;
                            xhci_info.max_interrupters = (hcsparams1 >> 8) & 0x7ffU;
                            xhci_info.max_ports = (hcsparams1 >> 24) & 0xffU;
                            xhci_info.scratchpad_count =
                                ((uint64_t)((hcsparams2 >> 21) & 0x1fU) << 5) |
                                (uint64_t)(hcsparams2 & 0x1fU);
                            xhci_info.doorbell_offset = *(volatile uint32_t *)(regs + 0x14) & ~0x3U;
                            xhci_info.runtime_offset = *(volatile uint32_t *)(regs + 0x18) & ~0x1fU;
                            xhci_scan_extended_caps(regs, hccparams1);
                            xhci_probe_operational(&device, regs);
                            xhci_configure_rings(regs);
                            xhci_enable_first_slot(regs);
                            xhci_scan_root_ports(regs);
                            xhci_reset_first_connected_port(regs);
                            xhci_scan_root_ports(regs);
                            xhci_address_first_device(regs);
                            xhci_get_first_device_descriptor(regs);
                            xhci_get_first_configuration_descriptor(regs);
                            xhci_configure_first_mass_storage_endpoints(regs);
                            xhci_set_configuration_and_probe_storage(regs);
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
                            log_info("xHCI ports: scanned=%u connected=%u enabled=%u powered=%u first=%u portsc=%x speed=%u pls=%u second=%u portsc=%x speed=%u pls=%u port1=%x\n",
                                     xhci_info.port_scan_limit,
                                     xhci_info.connected_port_count,
                                     xhci_info.enabled_port_count,
                                     xhci_info.powered_port_count,
                                     xhci_info.first_connected_port,
                                     xhci_info.first_connected_portsc,
                                     xhci_info.first_connected_speed,
                                     xhci_info.first_connected_link_state,
                                     xhci_info.second_connected_port,
                                     xhci_info.second_connected_portsc,
                                     xhci_info.second_connected_speed,
                                     xhci_info.second_connected_link_state,
                                     xhci_info.port1_portsc);
                        }
                        if (xhci_info.port_reset_attempted) {
                            log_info("xHCI port reset: ok=%u port=%u before=%x after=%x enabled=%u speed=%u pls=%u\n",
                                     xhci_info.port_reset_ok,
                                     xhci_info.port_reset_port,
                                     xhci_info.port_reset_portsc_before,
                                     xhci_info.port_reset_portsc_after,
                                     xhci_info.port_reset_enabled,
                                     xhci_info.port_reset_speed,
                                     xhci_info.port_reset_link_state);
                        }
                        if (xhci_info.address_device_attempted) {
                            log_info("xHCI address device: ok=%u slot=%u cc=%u type=%u input=%x output=%x ep0=%x mps=%u addr=%u state=%u erdp=%x\n",
                                     xhci_info.address_device_ok,
                                     xhci_info.address_device_slot_id,
                                     xhci_info.address_device_completion_code,
                                     xhci_info.address_device_event_type,
                                     xhci_info.input_context_phys,
                                     xhci_info.output_context_phys,
                                     xhci_info.ep0_ring_phys,
                                     xhci_info.ep0_max_packet_size,
                                     xhci_info.addressed_device_address,
                                     xhci_info.addressed_slot_state,
                                     xhci_info.erdp_value);
                        }
                        if (xhci_info.device_descriptor_attempted) {
                            log_info("xHCI device descriptor: ok=%u cc=%u type=%u desc=%x first=%x second=%x tail=%x vendor=%x product=%x class=%u subclass=%u proto=%u\n",
                                     xhci_info.device_descriptor_ok,
                                     xhci_info.device_descriptor_completion_code,
                                     xhci_info.device_descriptor_event_type,
                                     xhci_info.device_descriptor_phys,
                                     xhci_info.device_descriptor_first8,
                                     xhci_info.device_descriptor_second8,
                                     xhci_info.device_descriptor_tail,
                                     xhci_info.device_vendor_id,
                                     xhci_info.device_product_id,
                                     xhci_info.device_class,
                                     xhci_info.device_subclass,
                                     xhci_info.device_protocol);
                        }
                        if (xhci_info.configuration_descriptor_attempted) {
                            log_info("xHCI configuration descriptor: ok=%u cc=%u type=%u cfg=%x total=%u interfaces=%u first_if_seen=%u class=%u subclass=%u proto=%u endpoints=%u bulk_in=%u addr=%u bulk_out=%u addr=%u\n",
                                     xhci_info.configuration_descriptor_ok,
                                     xhci_info.configuration_descriptor_completion_code,
                                     xhci_info.configuration_descriptor_event_type,
                                     xhci_info.configuration_descriptor_phys,
                                     xhci_info.configuration_total_length,
                                     xhci_info.configuration_num_interfaces,
                                     xhci_info.first_interface_seen,
                                     xhci_info.first_interface_class,
                                     xhci_info.first_interface_subclass,
                                     xhci_info.first_interface_protocol,
                                     xhci_info.endpoint_descriptor_count,
                                     xhci_info.first_bulk_in_seen,
                                     xhci_info.first_bulk_in_address,
                                     xhci_info.first_bulk_out_seen,
                                     xhci_info.first_bulk_out_address);
                        }
                        if (xhci_info.configure_endpoint_attempted) {
                            log_info("xHCI configure endpoint: ok=%u slot=%u cc=%u type=%u input=%x bulk_in_ring=%x dci=%u bulk_out_ring=%x dci=%u in_ctx=%x/%x/%x/%x/%x out_ctx=%x/%x/%x/%x/%x\n",
                                     xhci_info.configure_endpoint_ok,
                                     xhci_info.configure_endpoint_slot_id,
                                     xhci_info.configure_endpoint_completion_code,
                                     xhci_info.configure_endpoint_event_type,
                                     xhci_info.configure_endpoint_input_context_phys,
                                     xhci_info.bulk_in_ring_phys,
                                     xhci_info.first_bulk_in_dci,
                                     xhci_info.bulk_out_ring_phys,
                                     xhci_info.first_bulk_out_dci,
                                     xhci_info.bulk_in_endpoint_context0,
                                     xhci_info.bulk_in_endpoint_context1,
                                     xhci_info.bulk_in_endpoint_context2,
                                     xhci_info.bulk_in_endpoint_context3,
                                     xhci_info.bulk_in_endpoint_context4,
                                     xhci_info.bulk_out_endpoint_context0,
                                     xhci_info.bulk_out_endpoint_context1,
                                     xhci_info.bulk_out_endpoint_context2,
                                     xhci_info.bulk_out_endpoint_context3,
                                     xhci_info.bulk_out_endpoint_context4);
                        }
                        if (xhci_info.set_configuration_attempted) {
                            log_info("xHCI set configuration: ok=%u value=%u cc=%u type=%u trb=%x/%x/%x/%x\n",
                                     xhci_info.set_configuration_ok,
                                     xhci_info.set_configuration_value,
                                     xhci_info.set_configuration_completion_code,
                                     xhci_info.set_configuration_event_type,
                                     xhci_info.set_configuration_event_trb0,
                                     xhci_info.set_configuration_event_trb1,
                                     xhci_info.set_configuration_event_trb2,
                                     xhci_info.set_configuration_event_trb3);
                        }
                        if (xhci_info.bot_inquiry_attempted) {
                            log_info("USB BOT inquiry: ok=%u tag=%x cbw_cc=%u data_cc=%u csw_cc=%u csw=%x/%x residue=%u status=%u vendor=%x product=%x/%x revision=%x\n",
                                     xhci_info.bot_inquiry_ok,
                                     xhci_info.bot_inquiry_tag,
                                     xhci_info.bot_cbw_completion_code,
                                     xhci_info.bot_data_completion_code,
                                     xhci_info.bot_csw_completion_code,
                                     xhci_info.bot_csw_signature,
                                     xhci_info.bot_csw_tag,
                                     xhci_info.bot_csw_residue,
                                     xhci_info.bot_csw_status,
                                     xhci_info.inquiry_vendor_first8,
                                     xhci_info.inquiry_product_first8,
                                     xhci_info.inquiry_product_second8,
                                     xhci_info.inquiry_revision);
                        }
                        if (xhci_info.bot_capacity_attempted) {
                            log_info("USB BOT read capacity: ok=%u tag=%x cbw_cc=%u data_cc=%u csw_cc=%u csw=%x/%x residue=%u status=%u last_lba=%u block_size=%u block_count=%u\n",
                                     xhci_info.bot_capacity_ok,
                                     xhci_info.bot_capacity_tag,
                                     xhci_info.bot_capacity_cbw_completion_code,
                                     xhci_info.bot_capacity_data_completion_code,
                                     xhci_info.bot_capacity_csw_completion_code,
                                     xhci_info.bot_capacity_csw_signature,
                                     xhci_info.bot_capacity_csw_tag,
                                     xhci_info.bot_capacity_csw_residue,
                                     xhci_info.bot_capacity_csw_status,
                                     xhci_info.bot_capacity_last_lba,
                                     xhci_info.bot_capacity_block_size,
                                     xhci_info.bot_capacity_block_count);
                        }
                        if (xhci_info.bot_read10_attempted) {
                            log_info("USB BOT read10: ok=%u tag=%x lba=%u bytes=%u cbw_cc=%u data_cc=%u csw_cc=%u csw=%x/%x residue=%u status=%u first=%x/%x\n",
                                     xhci_info.bot_read10_ok,
                                     xhci_info.bot_read10_tag,
                                     xhci_info.bot_read10_lba,
                                     xhci_info.bot_read10_bytes,
                                     xhci_info.bot_read10_cbw_completion_code,
                                     xhci_info.bot_read10_data_completion_code,
                                     xhci_info.bot_read10_csw_completion_code,
                                     xhci_info.bot_read10_csw_signature,
                                     xhci_info.bot_read10_csw_tag,
                                     xhci_info.bot_read10_csw_residue,
                                     xhci_info.bot_read10_csw_status,
                                     xhci_info.bot_read10_first8,
                                     xhci_info.bot_read10_second8);
                        }
                        if (xhci_info.bot_write10_attempted) {
                            log_info("USB BOT write10: ok=%u verified=%u tag=%x lba=%u bytes=%u cbw_cc=%u data_cc=%u csw_cc=%u csw=%x/%x residue=%u status=%u first=%x/%x\n",
                                     xhci_info.bot_write10_ok,
                                     xhci_info.bot_write10_verified,
                                     xhci_info.bot_write10_tag,
                                     xhci_info.bot_write10_lba,
                                     xhci_info.bot_write10_bytes,
                                     xhci_info.bot_write10_cbw_completion_code,
                                     xhci_info.bot_write10_data_completion_code,
                                     xhci_info.bot_write10_csw_completion_code,
                                     xhci_info.bot_write10_csw_signature,
                                     xhci_info.bot_write10_csw_tag,
                                     xhci_info.bot_write10_csw_residue,
                                     xhci_info.bot_write10_csw_status,
                                     xhci_info.bot_write10_first8,
                                     xhci_info.bot_write10_second8);
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
