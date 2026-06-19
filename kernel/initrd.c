#include <console.h>
#include <initrd.h>
#include <stddef.h>
#include <stdint.h>
#include <vfs.h>

#define INITRD_MAX_FILES 32
#define INITRD_NAME_MAX 128
#define TAR_BLOCK_SIZE 512

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed));

struct initrd_file {
    char name[INITRD_NAME_MAX];
    const uint8_t *data;
    uint64_t size;
};

static struct initrd_file files[INITRD_MAX_FILES];
static uint64_t file_count;

static uint64_t initrd_read_file(
    const char *path,
    uint64_t offset,
    void *buffer,
    uint64_t buffer_len,
    void *context
);

static int chars_equal(const char *a, const char *b) {
    uint64_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

static int ends_with(const char *s, const char *suffix) {
    uint64_t s_len = 0;
    uint64_t suffix_len = 0;

    while (s != NULL && s[s_len] != '\0') {
        s_len++;
    }
    while (suffix[suffix_len] != '\0') {
        suffix_len++;
    }

    if (s == NULL || s_len < suffix_len) {
        return 0;
    }

    return chars_equal(s + s_len - suffix_len, suffix);
}

static uint64_t parse_octal(const char *value, uint64_t max_len) {
    uint64_t result = 0;

    for (uint64_t i = 0; i < max_len; i++) {
        char c = value[i];
        if (c == '\0' || c == ' ') {
            break;
        }
        if (c < '0' || c > '7') {
            break;
        }
        result = (result << 3) + (uint64_t)(c - '0');
    }

    return result;
}

static int is_zero_block(const uint8_t *block) {
    for (uint64_t i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return 0;
        }
    }

    return 1;
}

static void copy_tar_name(char *dst, const struct tar_header *header) {
    uint64_t out = 0;

    if (header->prefix[0] != '\0') {
        for (uint64_t i = 0; i < sizeof(header->prefix) && header->prefix[i] != '\0'; i++) {
            if (out + 1 >= INITRD_NAME_MAX) {
                break;
            }
            dst[out++] = header->prefix[i];
        }
        if (out + 1 < INITRD_NAME_MAX) {
            dst[out++] = '/';
        }
    }

    for (uint64_t i = 0; i < sizeof(header->name) && header->name[i] != '\0'; i++) {
        if (out + 1 >= INITRD_NAME_MAX) {
            break;
        }
        dst[out++] = header->name[i];
    }

    dst[out] = '\0';
}

static const char *normalize_path(const char *path) {
    while (*path == '/') {
        path++;
    }
    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    return path;
}

static void copy_user_path(char *dst, const char *path) {
    const char *normalized = normalize_path(path);
    uint64_t i = 0;

    while (i + 1 < INITRD_NAME_MAX && normalized[i] != '\0') {
        dst[i] = normalized[i];
        i++;
    }
    dst[i] = '\0';
}

static struct limine_file *find_initrd_module(struct limine_module_response *modules) {
    if (modules == NULL) {
        return NULL;
    }

    for (uint64_t i = 0; i < modules->module_count; i++) {
        struct limine_file *module = modules->modules[i];
        const char *string = module->string;
        const char *path = module->path;

        if ((string != NULL && chars_equal(string, "initrd")) ||
            (path != NULL && ends_with(path, "/initrd.tar"))) {
            return module;
        }
    }

    return NULL;
}

void initrd_init(struct limine_module_response *modules) {
    struct limine_file *module = find_initrd_module(modules);
    file_count = 0;

    if (module == NULL) {
        console_write("initrd: module not found\n");
        return;
    }

    console_printf("initrd module: path=%s size=%u\n", module->path, module->size);

    const uint8_t *cursor = (const uint8_t *)module->address;
    const uint8_t *end = cursor + module->size;

    while (cursor + TAR_BLOCK_SIZE <= end && !is_zero_block(cursor)) {
        const struct tar_header *header = (const struct tar_header *)cursor;
        uint64_t size = parse_octal(header->size, sizeof(header->size));
        const uint8_t *data = cursor + TAR_BLOCK_SIZE;
        uint64_t record_size = TAR_BLOCK_SIZE + ((size + TAR_BLOCK_SIZE - 1) & ~(TAR_BLOCK_SIZE - 1));

        if (data + size > end || cursor + record_size > end) {
            console_write("initrd: truncated tar entry\n");
            break;
        }

        if ((header->typeflag == '\0' || header->typeflag == '0') && file_count < INITRD_MAX_FILES) {
            struct initrd_file *file = &files[file_count++];
            copy_tar_name(file->name, header);
            file->data = data;
            file->size = size;
            console_printf("initrd file: %s size=%u\n", file->name, file->size);
        }

        cursor += record_size;
    }

    if (file_count > 0 && vfs_register_readonly_fs("initrd", initrd_read_file, NULL) != 0) {
        console_write("initrd: failed to register VFS backend\n");
    }
}

static uint64_t initrd_read_file(
    const char *path,
    uint64_t offset,
    void *buffer,
    uint64_t buffer_len,
    void *context
) {
    char normalized[INITRD_NAME_MAX];
    uint8_t *out = (uint8_t *)buffer;
    (void)context;

    if (path == NULL || buffer == NULL) {
        return (uint64_t)-1;
    }

    copy_user_path(normalized, path);

    for (uint64_t i = 0; i < file_count; i++) {
        struct initrd_file *file = &files[i];
        if (!chars_equal(normalized, file->name)) {
            continue;
        }

        if (offset >= file->size) {
            return 0;
        }

        uint64_t remaining = file->size - offset;
        uint64_t to_copy = remaining < buffer_len ? remaining : buffer_len;
        for (uint64_t j = 0; j < to_copy; j++) {
            out[j] = file->data[offset + j];
        }

        return to_copy;
    }

    return (uint64_t)-1;
}
