#include <block.h>
#include <log.h>
#include <partition.h>
#include <stdint.h>

#define MBR_SIGNATURE_OFFSET 510
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_PRIMARY_PARTITIONS 4
#define PARTITION_MAX_DEVICES 16

struct partition_context {
    uint64_t parent_index;
    uint64_t start_lba;
};

static struct block_device partition_devices[PARTITION_MAX_DEVICES];
static struct partition_context partition_contexts[PARTITION_MAX_DEVICES];
static uint64_t partition_count;

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
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

static uint64_t string_length(const char *s) {
    uint64_t len = 0;

    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void make_partition_name(char *out, uint64_t out_len, const char *parent, uint64_t number) {
    copy_cstr_limited(out, out_len, parent);
    uint64_t len = string_length(out);

    if (len + 2 >= out_len) {
        return;
    }
    out[len++] = 'p';
    out[len++] = (char)('0' + number);
    out[len] = '\0';
}

static int partition_read(struct block_device *device, uint64_t lba, uint64_t count, void *buffer) {
    struct partition_context *ctx = (struct partition_context *)device->context;
    return block_read(ctx->parent_index, ctx->start_lba + lba, count, buffer);
}

static int partition_write(struct block_device *device, uint64_t lba, uint64_t count, const void *buffer) {
    struct partition_context *ctx = (struct partition_context *)device->context;
    return block_write(ctx->parent_index, ctx->start_lba + lba, count, buffer);
}

static void register_mbr_partition(uint64_t parent_index, const struct block_device_info *parent,
                                   uint64_t number, uint8_t type, uint32_t start_lba,
                                   uint32_t sector_count) {
    if (partition_count >= PARTITION_MAX_DEVICES || type == 0 || sector_count == 0) {
        return;
    }
    if ((uint64_t)start_lba >= parent->block_count ||
        (uint64_t)sector_count > parent->block_count - (uint64_t)start_lba) {
        log_warn("MBR partition outside disk: parent=%s part=%u start=%u count=%u\n",
                 parent->name, number, (uint64_t)start_lba, (uint64_t)sector_count);
        return;
    }

    struct partition_context *ctx = &partition_contexts[partition_count];
    struct block_device *device = &partition_devices[partition_count];
    ctx->parent_index = parent_index;
    ctx->start_lba = start_lba;

    make_partition_name(device->name, sizeof(device->name), parent->name, number);
    device->block_size = parent->block_size;
    device->block_count = sector_count;
    device->writable = parent->writable;
    device->context = ctx;
    device->read = partition_read;
    device->write = parent->writable ? partition_write : 0;

    if (block_register(device) == 0) {
        partition_count++;
        log_info("MBR partition registered: %s start=%u sectors=%u type=%x\n",
                 device->name, (uint64_t)start_lba, (uint64_t)sector_count, (uint64_t)type);
    }
}

static void scan_mbr(uint64_t parent_index) {
    uint8_t sector[512];
    struct block_device_info parent;

    if (block_get_info(parent_index, &parent) != 0 || parent.block_size != 512) {
        return;
    }
    if (block_read(parent_index, 0, 1, sector) != 0) {
        return;
    }
    if (sector[MBR_SIGNATURE_OFFSET] != 0x55 || sector[MBR_SIGNATURE_OFFSET + 1] != 0xaa) {
        return;
    }

    for (uint64_t i = 0; i < MBR_PRIMARY_PARTITIONS; i++) {
        const uint8_t *entry = sector + MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE;
        uint8_t type = entry[4];
        uint32_t start_lba = read_le32(entry + 8);
        uint32_t sector_count = read_le32(entry + 12);
        register_mbr_partition(parent_index, &parent, i + 1, type, start_lba, sector_count);
    }
}

void partition_init(void) {
    uint64_t initial_count = block_device_count();

    for (uint64_t i = 0; i < initial_count; i++) {
        scan_mbr(i);
    }
}
