#include <block.h>
#include <log.h>
#include <memory.h>
#include <pci.h>
#include <stdint.h>
#include <virtio_blk.h>
#include <x86_64/io.h>

#define VIRTIO_VENDOR_ID 0x1af4
#define VIRTIO_LEGACY_BLK_DEVICE_ID 0x1001

#define VIRTIO_REG_DEVICE_FEATURES 0x00
#define VIRTIO_REG_DRIVER_FEATURES 0x04
#define VIRTIO_REG_QUEUE_PFN       0x08
#define VIRTIO_REG_QUEUE_NUM       0x0c
#define VIRTIO_REG_QUEUE_SEL       0x0e
#define VIRTIO_REG_QUEUE_NOTIFY    0x10
#define VIRTIO_REG_STATUS          0x12
#define VIRTIO_REG_ISR             0x13
#define VIRTIO_REG_CONFIG          0x14

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FAILED      0x80

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_S_OK  0

#define VIRTIO_DMA_HEADER_OFFSET 0
#define VIRTIO_DMA_DATA_OFFSET   64
#define VIRTIO_DMA_STATUS_OFFSET 576

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtio_blk_outhdr {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_device {
    struct block_device block;
    uint16_t io_base;
    uint16_t queue_size;
    uint64_t queue_phys;
    uint8_t *queue;
    uint64_t used_offset;
    uint16_t last_used_idx;
    uint64_t dma_phys;
    uint8_t *dma;
};

static struct virtio_blk_device virtio_blk0;

static void memory_copy(void *dst, const void *src, uint64_t len) {
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;

    for (uint64_t i = 0; i < len; i++) {
        out[i] = in[i];
    }
}

static void memory_set(void *dst, uint8_t value, uint64_t len) {
    uint8_t *out = (uint8_t *)dst;

    for (uint64_t i = 0; i < len; i++) {
        out[i] = value;
    }
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

static uint16_t port(const struct virtio_blk_device *dev, uint16_t offset) {
    return (uint16_t)(dev->io_base + offset);
}

static volatile uint16_t *avail_flags(struct virtio_blk_device *dev) {
    return (volatile uint16_t *)(dev->queue + sizeof(struct virtq_desc) * dev->queue_size);
}

static volatile uint16_t *avail_idx(struct virtio_blk_device *dev) {
    return avail_flags(dev) + 1;
}

static volatile uint16_t *avail_ring(struct virtio_blk_device *dev) {
    return avail_flags(dev) + 2;
}

static volatile uint16_t *used_flags(struct virtio_blk_device *dev) {
    return (volatile uint16_t *)(dev->queue + dev->used_offset);
}

static volatile uint16_t *used_idx(struct virtio_blk_device *dev) {
    return used_flags(dev) + 1;
}

static volatile struct virtq_used_elem *used_ring(struct virtio_blk_device *dev) {
    return (volatile struct virtq_used_elem *)(used_flags(dev) + 2);
}

static int virtio_blk_transfer(struct virtio_blk_device *dev, uint64_t lba,
                               int write, void *buffer) {
    struct virtq_desc *desc = (struct virtq_desc *)dev->queue;
    struct virtio_blk_outhdr *header = (struct virtio_blk_outhdr *)(dev->dma + VIRTIO_DMA_HEADER_OFFSET);
    uint8_t *data = dev->dma + VIRTIO_DMA_DATA_OFFSET;
    uint8_t *status = dev->dma + VIRTIO_DMA_STATUS_OFFSET;

    if (write) {
        memory_copy(data, buffer, 512);
    } else {
        memory_set(data, 0, 512);
    }

    header->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    header->ioprio = 0;
    header->sector = lba;
    *status = 0xff;

    desc[0].addr = dev->dma_phys + VIRTIO_DMA_HEADER_OFFSET;
    desc[0].len = sizeof(*header);
    desc[0].flags = VIRTQ_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = dev->dma_phys + VIRTIO_DMA_DATA_OFFSET;
    desc[1].len = 512;
    desc[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    desc[1].next = 2;

    desc[2].addr = dev->dma_phys + VIRTIO_DMA_STATUS_OFFSET;
    desc[2].len = 1;
    desc[2].flags = VIRTQ_DESC_F_WRITE;
    desc[2].next = 0;

    uint16_t idx = *avail_idx(dev);
    avail_ring(dev)[idx % dev->queue_size] = 0;
    __sync_synchronize();
    *avail_idx(dev) = (uint16_t)(idx + 1);
    __sync_synchronize();
    outw(port(dev, VIRTIO_REG_QUEUE_NOTIFY), 0);

    for (uint64_t spin = 0; spin < 100000000ULL; spin++) {
        if (*used_idx(dev) != dev->last_used_idx) {
            volatile struct virtq_used_elem *used_elem = &used_ring(dev)[dev->last_used_idx % dev->queue_size];
            uint32_t used_id = used_elem->id;
            dev->last_used_idx++;
            (void)inb(port(dev, VIRTIO_REG_ISR));
            if (used_id != 0 || *status != VIRTIO_BLK_S_OK) {
                return -1;
            }
            if (!write) {
                memory_copy(buffer, data, 512);
            }
            return 0;
        }
    }

    return -1;
}

static int virtio_blk_read(struct block_device *device, uint64_t lba, uint64_t count, void *buffer) {
    struct virtio_blk_device *dev = (struct virtio_blk_device *)device->context;
    uint8_t *out = (uint8_t *)buffer;

    for (uint64_t i = 0; i < count; i++) {
        if (virtio_blk_transfer(dev, lba + i, 0, out + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

static int virtio_blk_write(struct block_device *device, uint64_t lba, uint64_t count, const void *buffer) {
    struct virtio_blk_device *dev = (struct virtio_blk_device *)device->context;
    const uint8_t *in = (const uint8_t *)buffer;

    for (uint64_t i = 0; i < count; i++) {
        if (virtio_blk_transfer(dev, lba + i, 1, (void *)(in + i * 512)) != 0) {
            return -1;
        }
    }
    return 0;
}

static uint64_t virtio_blk_capacity(uint16_t io_base) {
    uint32_t low = inl((uint16_t)(io_base + VIRTIO_REG_CONFIG));
    uint32_t high = inl((uint16_t)(io_base + VIRTIO_REG_CONFIG + 4));
    return ((uint64_t)high << 32) | low;
}

void virtio_blk_init(void) {
    struct pci_device pci;

    if (pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_LEGACY_BLK_DEVICE_ID, &pci) != 0) {
        log_info("virtio-blk legacy PCI device not found\n");
        return;
    }

    uint32_t bar0 = pci_bar(&pci, 0);
    if ((bar0 & 1) == 0) {
        log_warn("virtio-blk BAR0 is not an I/O BAR\n");
        return;
    }

    struct virtio_blk_device *dev = &virtio_blk0;
    memory_set(dev, 0, sizeof(*dev));
    dev->io_base = (uint16_t)(bar0 & ~3U);

    pci_enable_io_bus_master(&pci);

    outb(port(dev, VIRTIO_REG_STATUS), 0);
    outb(port(dev, VIRTIO_REG_STATUS), VIRTIO_STATUS_ACKNOWLEDGE);
    outb(port(dev, VIRTIO_REG_STATUS), VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    outl(port(dev, VIRTIO_REG_DRIVER_FEATURES), 0);

    outw(port(dev, VIRTIO_REG_QUEUE_SEL), 0);
    dev->queue_size = inw(port(dev, VIRTIO_REG_QUEUE_NUM));
    if (dev->queue_size < 3) {
        outb(port(dev, VIRTIO_REG_STATUS), VIRTIO_STATUS_FAILED);
        log_warn("virtio-blk queue too small\n");
        return;
    }

    uint64_t desc_size = sizeof(struct virtq_desc) * dev->queue_size;
    uint64_t avail_size = 2 + 2 + 2 * (uint64_t)dev->queue_size;
    dev->used_offset = align_up(desc_size + avail_size, PAGE_SIZE);
    uint64_t used_size = 2 + 2 + sizeof(struct virtq_used_elem) * (uint64_t)dev->queue_size;
    uint64_t queue_bytes = dev->used_offset + used_size;
    uint64_t queue_pages = align_up(queue_bytes, PAGE_SIZE) / PAGE_SIZE;

    dev->queue_phys = pmm_alloc_contiguous_pages(queue_pages);
    dev->queue = phys_to_virt(dev->queue_phys);
    memory_set(dev->queue, 0, queue_pages * PAGE_SIZE);

    dev->dma_phys = pmm_alloc_page();
    dev->dma = phys_to_virt(dev->dma_phys);
    memory_set(dev->dma, 0, PAGE_SIZE);

    outl(port(dev, VIRTIO_REG_QUEUE_PFN), (uint32_t)(dev->queue_phys / PAGE_SIZE));
    *avail_flags(dev) = 0;
    *avail_idx(dev) = 0;
    *used_flags(dev) = 0;
    dev->last_used_idx = 0;

    uint64_t capacity = virtio_blk_capacity(dev->io_base);
    dev->block.name[0] = 'v';
    dev->block.name[1] = 'd';
    dev->block.name[2] = '0';
    dev->block.name[3] = '\0';
    dev->block.block_size = 512;
    dev->block.block_count = capacity;
    dev->block.writable = 1;
    dev->block.context = dev;
    dev->block.read = virtio_blk_read;
    dev->block.write = virtio_blk_write;

    outb(port(dev, VIRTIO_REG_STATUS),
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    uint8_t probe[512];
    if (virtio_blk_read(&dev->block, 0, 1, probe) != 0) {
        outb(port(dev, VIRTIO_REG_STATUS), VIRTIO_STATUS_FAILED);
        log_warn("virtio-blk probe read failed\n");
        return;
    }

    if (block_register(&dev->block) != 0) {
        log_warn("virtio-blk block registration failed\n");
        return;
    }

    log_info("virtio-blk ready: vd0 sectors=%u io=%x queue=%u\n",
             capacity, (uint64_t)dev->io_base, (uint64_t)dev->queue_size);
}
