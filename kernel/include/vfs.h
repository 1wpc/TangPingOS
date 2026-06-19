#ifndef TANGPINGOS_VFS_H
#define TANGPINGOS_VFS_H

#include <stdint.h>

typedef uint64_t (*vfs_read_fn)(
    const char *path,
    uint64_t offset,
    void *buffer,
    uint64_t buffer_len,
    void *context
);

void vfs_init(void);
int vfs_register_readonly_fs(const char *name, vfs_read_fn read, void *context);
uint64_t vfs_read_file(const char *path, uint64_t offset, void *buffer, uint64_t buffer_len);

#endif
