#include <console.h>
#include <stdint.h>
#include <vfs.h>

#define VFS_MAX_FILESYSTEMS 8
#define VFS_NAME_MAX 32

struct vfs_filesystem {
    char name[VFS_NAME_MAX];
    vfs_read_fn read;
    void *context;
};

static struct vfs_filesystem filesystems[VFS_MAX_FILESYSTEMS];
static uint64_t filesystem_count;

static void copy_name(char *dst, const char *src) {
    uint64_t i = 0;

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < VFS_NAME_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void vfs_init(void) {
    filesystem_count = 0;
    console_write("VFS initialized\n");
}

int vfs_register_readonly_fs(const char *name, vfs_read_fn read, void *context) {
    if (read == 0 || filesystem_count >= VFS_MAX_FILESYSTEMS) {
        return -1;
    }

    struct vfs_filesystem *fs = &filesystems[filesystem_count++];
    copy_name(fs->name, name);
    fs->read = read;
    fs->context = context;

    console_printf("VFS mounted readonly fs: %s\n", fs->name);
    return 0;
}

uint64_t vfs_read_file(const char *path, uint64_t offset, void *buffer, uint64_t buffer_len) {
    if (path == 0 || buffer == 0) {
        return (uint64_t)-1;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        uint64_t result = fs->read(path, offset, buffer, buffer_len, fs->context);
        if (result != (uint64_t)-1) {
            return result;
        }
    }

    return (uint64_t)-1;
}
