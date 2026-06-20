#include <log.h>
#include <stdint.h>
#include <vfs.h>

#define RAMFS_MAX_FILES 16
#define RAMFS_NAME_MAX 64
#define RAMFS_FILE_MAX_SIZE 1024

struct ramfs_file {
    int in_use;
    char name[RAMFS_NAME_MAX];
    uint8_t data[RAMFS_FILE_MAX_SIZE];
    uint64_t size;
};

static struct ramfs_file files[RAMFS_MAX_FILES];

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

static int ramfs_path_valid(const char *path) {
    if (path == 0 || path[0] != '/' || path[1] == '\0') {
        return 0;
    }

    for (uint64_t i = 1; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            return 0;
        }
        if (i + 1 >= RAMFS_NAME_MAX) {
            return 0;
        }
    }

    return 1;
}

static void copy_path(char *dst, const char *src) {
    uint64_t i = 0;
    while (i + 1 < RAMFS_NAME_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void copy_dirent_name(char *dst, const char *path) {
    uint64_t src = path[0] == '/' ? 1 : 0;
    uint64_t dst_index = 0;

    while (dst_index + 1 < VFS_DIRENT_NAME_MAX && path[src] != '\0') {
        dst[dst_index++] = path[src++];
    }
    dst[dst_index] = '\0';
}

static void copy_bytes(void *dst, const void *src, uint64_t size) {
    uint8_t *dst_bytes = dst;
    const uint8_t *src_bytes = src;

    for (uint64_t i = 0; i < size; i++) {
        dst_bytes[i] = src_bytes[i];
    }
}

static void zero_bytes(void *dst, uint64_t size) {
    uint8_t *dst_bytes = dst;
    for (uint64_t i = 0; i < size; i++) {
        dst_bytes[i] = 0;
    }
}

static struct ramfs_file *find_file(const char *path) {
    for (uint64_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (files[i].in_use && chars_equal(files[i].name, path)) {
            return &files[i];
        }
    }

    return 0;
}

static struct ramfs_file *create_file(const char *path) {
    for (uint64_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].in_use) {
            files[i].in_use = 1;
            files[i].size = 0;
            copy_path(files[i].name, path);
            log_debug("ramfs created: %s\n", path);
            return &files[i];
        }
    }

    return 0;
}

static uint64_t ramfs_read(const char *path, uint64_t offset,
                           void *buffer, uint64_t buffer_len, void *context) {
    (void)context;

    struct ramfs_file *file = find_file(path);
    if (file == 0) {
        return (uint64_t)-1;
    }
    if (offset >= file->size) {
        return 0;
    }

    uint64_t available = file->size - offset;
    uint64_t to_copy = buffer_len < available ? buffer_len : available;
    copy_bytes(buffer, file->data + offset, to_copy);
    return to_copy;
}

static int ramfs_list(const char *path, uint64_t index, struct vfs_dirent *out, void *context) {
    (void)context;

    if (out == 0 || !chars_equal(path, "/")) {
        return -1;
    }

    uint64_t seen = 0;
    for (uint64_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].in_use) {
            continue;
        }
        if (seen == index) {
            copy_dirent_name(out->name, files[i].name);
            out->type = VFS_DIRENT_TYPE_FILE;
            out->size = files[i].size;
            return 1;
        }
        seen++;
    }

    return 0;
}

static uint64_t ramfs_write(const char *path, uint64_t offset,
                            const void *buffer, uint64_t buffer_len, void *context) {
    (void)context;

    if (!ramfs_path_valid(path) || offset > RAMFS_FILE_MAX_SIZE ||
        buffer_len > RAMFS_FILE_MAX_SIZE - offset) {
        return (uint64_t)-1;
    }

    struct ramfs_file *file = find_file(path);
    if (file == 0) {
        file = create_file(path);
        if (file == 0) {
            return (uint64_t)-1;
        }
    }

    if (offset > file->size) {
        zero_bytes(file->data + file->size, offset - file->size);
    }
    copy_bytes(file->data + offset, buffer, buffer_len);
    if (offset + buffer_len > file->size) {
        file->size = offset + buffer_len;
    }

    return buffer_len;
}

static uint64_t ramfs_size(const char *path, void *context) {
    (void)context;

    struct ramfs_file *file = find_file(path);
    if (file == 0) {
        return (uint64_t)-1;
    }

    return file->size;
}

static int ramfs_truncate(const char *path, uint64_t size, void *context) {
    (void)context;

    if (!ramfs_path_valid(path) || size > RAMFS_FILE_MAX_SIZE) {
        return -1;
    }

    struct ramfs_file *file = find_file(path);
    if (file == 0) {
        file = create_file(path);
        if (file == 0) {
            return -1;
        }
    }

    if (size > file->size) {
        zero_bytes(file->data + file->size, size - file->size);
    }
    file->size = size;
    return 0;
}

static int ramfs_unlink(const char *path, void *context) {
    (void)context;

    if (!ramfs_path_valid(path)) {
        return -1;
    }

    struct ramfs_file *file = find_file(path);
    if (file == 0) {
        return -1;
    }

    file->in_use = 0;
    file->size = 0;
    file->name[0] = '\0';
    return 0;
}

void ramfs_init(void) {
    for (uint64_t i = 0; i < RAMFS_MAX_FILES; i++) {
        files[i].in_use = 0;
        files[i].size = 0;
        files[i].name[0] = '\0';
    }

    if (vfs_register_fs_ex("ramfs", ramfs_read, ramfs_list, ramfs_write,
                           ramfs_size, ramfs_truncate, ramfs_unlink, 0) != 0) {
        log_error("ramfs: failed to register VFS backend\n");
        return;
    }
}
