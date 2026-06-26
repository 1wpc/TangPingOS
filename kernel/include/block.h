#ifndef TANGPINGOS_BLOCK_H
#define TANGPINGOS_BLOCK_H

#include <stdint.h>

#define BLOCK_DEVICE_NAME_MAX 32
#define BLOCK_MAX_DEVICES 16

struct block_device_info {
    char name[BLOCK_DEVICE_NAME_MAX];
    uint64_t block_size;
    uint64_t block_count;
    uint64_t writable;
};

struct block_device;

typedef int (*block_read_fn)(struct block_device *device, uint64_t lba, uint64_t count, void *buffer);
typedef int (*block_write_fn)(struct block_device *device, uint64_t lba, uint64_t count, const void *buffer);

struct block_device {
    char name[BLOCK_DEVICE_NAME_MAX];
    uint64_t block_size;
    uint64_t block_count;
    uint64_t writable;
    void *context;
    block_read_fn read;
    block_write_fn write;
};

void block_init(void);
int block_register(struct block_device *device);
uint64_t block_device_count(void);
int block_get_info(uint64_t index, struct block_device_info *out);
int block_find_by_name(const char *name, uint64_t *index_out);
int block_read(uint64_t index, uint64_t lba, uint64_t count, void *buffer);
int block_write(uint64_t index, uint64_t lba, uint64_t count, const void *buffer);

#endif
