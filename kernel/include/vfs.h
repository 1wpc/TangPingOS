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

typedef uint64_t (*vfs_write_fn)(
    const char *path,
    uint64_t offset,
    const void *buffer,
    uint64_t buffer_len,
    void *context
);

typedef uint64_t (*vfs_size_fn)(
    const char *path,
    void *context
);

typedef int (*vfs_truncate_fn)(
    const char *path,
    uint64_t size,
    void *context
);

typedef int (*vfs_unlink_fn)(
    const char *path,
    void *context
);

void vfs_init(void);
int vfs_register_readonly_fs(const char *name, vfs_read_fn read, vfs_list_fn list, void *context);
int vfs_register_fs(const char *name, vfs_read_fn read, vfs_list_fn list, vfs_write_fn write, void *context);
int vfs_register_fs_ex(const char *name, vfs_read_fn read, vfs_list_fn list, vfs_write_fn write,
                       vfs_size_fn size, vfs_truncate_fn truncate, vfs_unlink_fn unlink,
                       void *context);
int vfs_file_exists(const char *path);
int vfs_list_dir(const char *path, uint64_t index, struct vfs_dirent *out);
uint64_t vfs_read_file(const char *path, uint64_t offset, void *buffer, uint64_t buffer_len);
uint64_t vfs_write_file(const char *path, uint64_t offset, const void *buffer, uint64_t buffer_len);
uint64_t vfs_write_existing_file(const char *path, uint64_t offset, const void *buffer, uint64_t buffer_len);
uint64_t vfs_file_size(const char *path);
int vfs_truncate_file(const char *path, uint64_t size);
int vfs_create_file(const char *path);
int vfs_unlink_file(const char *path);

#endif
