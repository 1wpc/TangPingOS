#ifndef TANGPINGOS_VFS_H
#define TANGPINGOS_VFS_H

#include <stdint.h>

#define VFS_DIRENT_NAME_MAX 64
#define VFS_DIRENT_TYPE_FILE 1
#define VFS_DIRENT_TYPE_DIR 2
#define VFS_DIRENT_TYPE_DEVICE 3

struct vfs_dirent {
    char name[VFS_DIRENT_NAME_MAX];
    uint64_t type;
    uint64_t size;
};

typedef uint64_t (*vfs_read_fn)(
    const char *path,
    uint64_t offset,
    void *buffer,
    uint64_t buffer_len,
    void *context
);

typedef int (*vfs_list_fn)(
    const char *path,
    uint64_t index,
    struct vfs_dirent *out,
    void *context
);

void vfs_init(void);
int vfs_register_readonly_fs(const char *name, vfs_read_fn read, vfs_list_fn list, void *context);
int vfs_file_exists(const char *path);
int vfs_list_dir(const char *path, uint64_t index, struct vfs_dirent *out);
uint64_t vfs_read_file(const char *path, uint64_t offset, void *buffer, uint64_t buffer_len);

#endif
