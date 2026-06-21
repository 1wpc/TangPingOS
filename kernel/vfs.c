#include <console.h>
#include <log.h>
#include <stdint.h>
#include <vfs.h>

#define VFS_MAX_FILESYSTEMS 8
#define VFS_MAX_DIRENTS_PER_FS 64

struct vfs_filesystem {
    char name[VFS_MOUNT_NAME_MAX];
    char path[VFS_MOUNT_PATH_MAX];
    char source[VFS_MOUNT_SOURCE_MAX];
    uint64_t writable;
    vfs_read_fn read;
    vfs_list_fn list;
    vfs_write_fn write;
    vfs_size_fn size;
    vfs_truncate_fn truncate;
    vfs_unlink_fn unlink;
    void *context;
};

static struct vfs_filesystem filesystems[VFS_MAX_FILESYSTEMS];
static uint64_t filesystem_count;

static uint64_t string_len(const char *s) {
    uint64_t len = 0;
    if (s == 0) {
        return 0;
    }
    while (s[len] != '\0') {
        len++;
    }
    return len;
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

static void copy_limited(char *dst, uint64_t dst_len, const char *src) {
    uint64_t i = 0;

    if (dst_len == 0) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_mount_path(char *dst, uint64_t dst_len, const char *src) {
    uint64_t len;

    copy_limited(dst, dst_len, src == 0 || src[0] == '\0' ? "/" : src);
    len = string_len(dst);
    while (len > 1 && dst[len - 1] == '/') {
        dst[--len] = '\0';
    }
}

static int mount_path_matches(const struct vfs_filesystem *fs, const char *path) {
    uint64_t mount_len;

    if (fs == 0 || path == 0 || path[0] != '/') {
        return 0;
    }
    if (strings_equal(fs->path, "/")) {
        return 1;
    }
    mount_len = string_len(fs->path);
    for (uint64_t i = 0; i < mount_len; i++) {
        if (path[i] != fs->path[i]) {
            return 0;
        }
    }
    return path[mount_len] == '\0' || path[mount_len] == '/';
}

static const char *mount_local_path(const struct vfs_filesystem *fs, const char *path) {
    uint64_t mount_len;

    if (strings_equal(fs->path, "/")) {
        return path;
    }
    mount_len = string_len(fs->path);
    if (path[mount_len] == '\0') {
        return "/";
    }
    return path + mount_len;
}

static int mount_parent_entry(const struct vfs_filesystem *fs, const char *path,
                              struct vfs_dirent *out) {
    uint64_t mount_len;
    uint64_t slash = 0;
    uint64_t parent_len;
    const char *name;

    if (fs == 0 || out == 0 || strings_equal(fs->path, "/")) {
        return 0;
    }

    mount_len = string_len(fs->path);
    for (uint64_t i = 1; i < mount_len; i++) {
        if (fs->path[i] == '/') {
            slash = i;
        }
    }

    parent_len = slash == 0 ? 1 : slash;
    if (string_len(path) != parent_len) {
        return 0;
    }
    if (parent_len == 1) {
        if (!strings_equal(path, "/")) {
            return 0;
        }
        name = fs->path + 1;
    } else {
        for (uint64_t i = 0; i < parent_len; i++) {
            if (path[i] != fs->path[i]) {
                return 0;
            }
        }
        name = fs->path + slash + 1;
    }

    copy_limited(out->name, sizeof(out->name), name);
    out->type = VFS_DIRENT_TYPE_DIR;
    out->size = 0;
    return 1;
}

void vfs_init(void) {
    filesystem_count = 0;
    log_info("VFS initialized\n");
}

int vfs_register_readonly_fs(const char *name, vfs_read_fn read, vfs_list_fn list, void *context) {
    return vfs_register_fs_ex(name, read, list, 0, 0, 0, 0, context);
}

int vfs_register_fs(const char *name, vfs_read_fn read, vfs_list_fn list, vfs_write_fn write, void *context) {
    return vfs_register_fs_ex(name, read, list, write, 0, 0, 0, context);
}

int vfs_register_fs_ex(const char *name, vfs_read_fn read, vfs_list_fn list, vfs_write_fn write,
                       vfs_size_fn size, vfs_truncate_fn truncate, vfs_unlink_fn unlink,
                       void *context) {
    return vfs_register_fs_mount(name, "/", name, read, list, write,
                                 size, truncate, unlink, context);
}

int vfs_register_fs_mount(const char *name, const char *path, const char *source,
                          vfs_read_fn read, vfs_list_fn list, vfs_write_fn write,
                          vfs_size_fn size, vfs_truncate_fn truncate, vfs_unlink_fn unlink,
                          void *context) {
    if (read == 0 || filesystem_count >= VFS_MAX_FILESYSTEMS) {
        return -1;
    }

    struct vfs_filesystem *fs = &filesystems[filesystem_count++];
    copy_limited(fs->name, sizeof(fs->name), name);
    copy_mount_path(fs->path, sizeof(fs->path), path);
    copy_limited(fs->source, sizeof(fs->source), source == 0 ? "" : source);
    fs->writable = write == 0 ? 0 : 1;
    fs->read = read;
    fs->list = list;
    fs->write = write;
    fs->size = size;
    fs->truncate = truncate;
    fs->unlink = unlink;
    fs->context = context;

    log_info("VFS mounted %s fs: %s at %s source=%s\n",
             fs->writable == 0 ? "readonly" : "writable",
             fs->name,
             fs->path,
             fs->source);
    return 0;
}

int vfs_mount_info(uint64_t index, struct vfs_mount_info *out) {
    if (out == 0 || index >= filesystem_count) {
        return -1;
    }

    struct vfs_filesystem *fs = &filesystems[index];
    copy_limited(out->name, sizeof(out->name), fs->name);
    copy_limited(out->path, sizeof(out->path), fs->path);
    copy_limited(out->source, sizeof(out->source), fs->source);
    out->writable = fs->writable;
    return 0;
}

int vfs_file_exists(const char *path) {
    uint8_t dummy;

    if (path == 0) {
        return 0;
    }

    return vfs_read_file(path, 0, &dummy, 0) != (uint64_t)-1;
}

int vfs_list_dir(const char *path, uint64_t index, struct vfs_dirent *out) {
    if (path == 0 || out == 0) {
        return -1;
    }

    uint64_t seen = 0;
    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_dirent candidate;
        if (!mount_parent_entry(&filesystems[i], path, &candidate)) {
            continue;
        }
        if (seen == index) {
            *out = candidate;
            return 1;
        }
        seen++;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        const char *local_path;
        if (fs->list == 0) {
            continue;
        }
        if (!mount_path_matches(fs, path)) {
            continue;
        }
        local_path = mount_local_path(fs, path);

        for (uint64_t local_index = 0; local_index < VFS_MAX_DIRENTS_PER_FS; local_index++) {
            struct vfs_dirent candidate;
            int result = fs->list(local_path, local_index, &candidate, fs->context);
            if (result < 0) {
                break;
            }
            if (result == 0) {
                break;
            }
            if (seen == index) {
                *out = candidate;
                return 1;
            }
            seen++;
        }
    }

    return 0;
}

uint64_t vfs_read_file(const char *path, uint64_t offset, void *buffer, uint64_t buffer_len) {
    if (path == 0 || buffer == 0) {
        return (uint64_t)-1;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        if (!mount_path_matches(fs, path)) {
            continue;
        }
        uint64_t result = fs->read(mount_local_path(fs, path), offset, buffer, buffer_len, fs->context);
        if (result != (uint64_t)-1) {
            return result;
        }
    }

    return (uint64_t)-1;
}

uint64_t vfs_write_file(const char *path, uint64_t offset, const void *buffer, uint64_t buffer_len) {
    if (path == 0 || buffer == 0) {
        return (uint64_t)-1;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        if (fs->write == 0) {
            continue;
        }
        if (!mount_path_matches(fs, path)) {
            continue;
        }

        uint64_t result = fs->write(mount_local_path(fs, path), offset, buffer, buffer_len, fs->context);
        if (result != (uint64_t)-1) {
            return result;
        }
    }

    return (uint64_t)-1;
}

uint64_t vfs_write_existing_file(const char *path, uint64_t offset,
                                 const void *buffer, uint64_t buffer_len) {
    uint8_t dummy;

    if (path == 0 || buffer == 0) {
        return (uint64_t)-1;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        if (fs->write == 0) {
            continue;
        }
        if (!mount_path_matches(fs, path)) {
            continue;
        }

        const char *local_path = mount_local_path(fs, path);
        if (fs->read(local_path, 0, &dummy, 0, fs->context) == (uint64_t)-1) {
            continue;
        }

        uint64_t result = fs->write(local_path, offset, buffer, buffer_len, fs->context);
        if (result != (uint64_t)-1) {
            return result;
        }
    }

    return (uint64_t)-1;
}

uint64_t vfs_file_size(const char *path) {
    if (path == 0) {
        return (uint64_t)-1;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        if (fs->size == 0) {
            continue;
        }
        if (!mount_path_matches(fs, path)) {
            continue;
        }

        uint64_t result = fs->size(mount_local_path(fs, path), fs->context);
        if (result != (uint64_t)-1) {
            return result;
        }
    }

    return (uint64_t)-1;
}

int vfs_truncate_file(const char *path, uint64_t size) {
    if (path == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        if (fs->truncate == 0) {
            continue;
        }
        if (!mount_path_matches(fs, path)) {
            continue;
        }

        if (fs->truncate(mount_local_path(fs, path), size, fs->context) == 0) {
            return 0;
        }
    }

    return -1;
}

int vfs_create_file(const char *path) {
    return vfs_truncate_file(path, 0);
}

int vfs_unlink_file(const char *path) {
    if (path == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < filesystem_count; i++) {
        struct vfs_filesystem *fs = &filesystems[i];
        if (fs->unlink == 0) {
            continue;
        }
        if (!mount_path_matches(fs, path)) {
            continue;
        }

        if (fs->unlink(mount_local_path(fs, path), fs->context) == 0) {
            return 0;
        }
    }

    return -1;
}
