#include <console.h>
#include <devfs.h>
#include <log.h>
#include <stdint.h>
#include <tty.h>
#include <vfs.h>

static int chars_equal(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static void copy_dirent_name(char *dst, const char *src) {
    uint64_t i = 0;
    while (i + 1 < VFS_DIRENT_NAME_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint64_t devfs_read(const char *path, uint64_t offset,
                           void *buffer, uint64_t buffer_len, void *context) {
    (void)offset;
    (void)buffer;
    (void)buffer_len;
    (void)context;

    if (tty_is_path(path) || chars_equal(path, "/tty")) {
        return 0;
    }

    return (uint64_t)-1;
}

static int devfs_list(const char *path, uint64_t index, struct vfs_dirent *out, void *context) {
    (void)context;

    if (out == 0) {
        return -1;
    }

    if (chars_equal(path, "/")) {
        if (index > 0) {
            return 0;
        }
        copy_dirent_name(out->name, "tty");
        out->type = VFS_DIRENT_TYPE_DEVICE;
        out->size = 0;
        return 1;
    }

    return -1;
}

void devfs_init(void) {
    if (vfs_register_fs_mount("devfs", "/dev", "kernel-devices",
                              devfs_read, devfs_list, 0, 0, 0, 0, 0) != 0) {
        log_error("devfs: failed to register VFS backend\n");
        return;
    }
}
