#include <block.h>
#include <log.h>
#include <stdint.h>

#define RAMDISK_BLOCK_SIZE 512
#define RAMDISK_BLOCK_COUNT 64

static struct block_device *devices[BLOCK_MAX_DEVICES];
static uint64_t device_count;
static uint8_t ramdisk[RAMDISK_BLOCK_SIZE * RAMDISK_BLOCK_COUNT];

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

static void copy_cstr_limited(char *dst, uint64_t dst_len, const char *src) {
    uint64_t i = 0;

    if (dst_len == 0) {
        return;
    }

    while (src[i] != '\0' && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int strings_equal(const char *a, const char *b) {
    uint64_t i = 0;

    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int block_range_valid(struct block_device *device, uint64_t lba, uint64_t count) {
    if (device == 0 || count == 0 || lba >= device->block_count) {
        return 0;
    }
    if (count > device->block_count - lba) {
        return 0;
    }
    return 1;
}

static int ramdisk_read(struct block_device *device, uint64_t lba, uint64_t count, void *buffer) {
    if (!block_range_valid(device, lba, count) || buffer == 0) {
        return -1;
    }

    memory_copy(buffer, &ramdisk[lba * RAMDISK_BLOCK_SIZE], count * RAMDISK_BLOCK_SIZE);
    return 0;
}

static int ramdisk_write(struct block_device *device, uint64_t lba, uint64_t count, const void *buffer) {
    if (!block_range_valid(device, lba, count) || buffer == 0) {
        return -1;
    }

    memory_copy(&ramdisk[lba * RAMDISK_BLOCK_SIZE], buffer, count * RAMDISK_BLOCK_SIZE);
    return 0;
}

static struct block_device ramdisk_device = {
    .name = "ramblk0",
    .block_size = RAMDISK_BLOCK_SIZE,
    .block_count = RAMDISK_BLOCK_COUNT,
    .writable = 1,
    .context = 0,
    .read = ramdisk_read,
    .write = ramdisk_write,
};

int block_register(struct block_device *device) {
    if (device == 0 || device->block_size == 0 || device->block_count == 0 ||
        device->read == 0 || device_count >= BLOCK_MAX_DEVICES) {
        return -1;
    }

    devices[device_count++] = device;
    log_info("block device registered: %s blocks=%u block_size=%u writable=%u\n",
             device->name, device->block_count, device->block_size, device->writable);
    return 0;
}

uint64_t block_device_count(void) {
    return device_count;
}

int block_get_info(uint64_t index, struct block_device_info *out) {
    if (index >= device_count || out == 0) {
        return -1;
    }

    struct block_device *device = devices[index];
    copy_cstr_limited(out->name, sizeof(out->name), device->name);
    out->block_size = device->block_size;
    out->block_count = device->block_count;
    out->writable = device->writable;
    return 0;
}

int block_find_by_name(const char *name, uint64_t *index_out) {
    if (name == 0 || index_out == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < device_count; i++) {
        if (devices[i] != 0 && strings_equal(devices[i]->name, name)) {
            *index_out = i;
            return 0;
        }
    }

    return -1;
}

int block_read(uint64_t index, uint64_t lba, uint64_t count, void *buffer) {
    if (index >= device_count || buffer == 0) {
        return -1;
    }

    struct block_device *device = devices[index];
    if (!block_range_valid(device, lba, count)) {
        return -1;
    }

    return device->read(device, lba, count, buffer);
}

int block_write(uint64_t index, uint64_t lba, uint64_t count, const void *buffer) {
    if (index >= device_count || buffer == 0) {
        return -1;
    }

    struct block_device *device = devices[index];
    if (!device->writable || device->write == 0 || !block_range_valid(device, lba, count)) {
        return -1;
    }

    return device->write(device, lba, count, buffer);
}

void block_init(void) {
    static const char label[] = "TangPingOS ram block device\n";

    memory_set(ramdisk, 0, sizeof(ramdisk));
    memory_copy(ramdisk, label, sizeof(label) - 1);
    for (uint64_t lba = 1; lba < RAMDISK_BLOCK_COUNT; lba++) {
        uint8_t *sector = &ramdisk[lba * RAMDISK_BLOCK_SIZE];
        sector[0] = 'T';
        sector[1] = 'P';
        sector[2] = 'O';
        sector[3] = 'S';
        sector[4] = (uint8_t)lba;
    }

    if (block_register(&ramdisk_device) != 0) {
        log_error("failed to register ram block device\n");
    }
}
