#include "tpos.h"

#define SHELL_LINE_MAX 128
#define SHELL_PATH_MAX 96

#define sys_write tpos_write
#define sys_getpid tpos_getpid
#define sys_yield tpos_yield
#define sys_brk tpos_brk
#define sys_open_flags tpos_open_flags
#define sys_open tpos_open
#define sys_read tpos_read
#define sys_close tpos_close
#define sys_getdents tpos_getdents
#define sys_dup2 tpos_dup2
#define sys_lseek tpos_lseek
#define sys_unlink tpos_unlink
#define sys_spawn tpos_spawn
#define sys_task_info tpos_task_info
#define sys_meminfo tpos_meminfo
#define sys_system_info tpos_system_info
#define sys_uptime tpos_uptime
#define sys_block_info tpos_block_info
#define sys_block_read tpos_block_read
#define sys_block_write tpos_block_write
#define sys_mount_info tpos_mount_info
#define sys_sbrk tpos_sbrk
#define sys_exit tpos_exit
#define u64_to_decimal tpos_u64_to_decimal
#define u64_to_hex tpos_u64_to_hex
#define copy_string tpos_strcpy
#define write_literal tpos_write_literal
#define string_length tpos_strlen
#define write_cstr tpos_write_cstr
#define write_cstr_limited tpos_write_cstr_limited
#define write_u64_decimal tpos_write_u64_decimal
#define write_u64_hex tpos_write_u64_hex
#define write_hex_byte tpos_write_hex_byte

static void write_dirent_name(const struct dirent *dirent) {
    uint64_t len = 0;
    while (len < DIRENT_NAME_MAX && dirent->name[len] != '\0') {
        len++;
    }
    sys_write(STDOUT_FD, dirent->name, len);
}

static int cmd_ls(const char *path) {
    static const char prefix[] = "$ ls ";
    static const char newline[] = "\n";
    static const char entry_prefix[] = "  ";
    static const char fail[] = "ls: failed\n";
    struct dirent dirent;

    write_literal(prefix, sizeof(prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents(path, i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            return -1;
        }

        write_literal(entry_prefix, sizeof(entry_prefix) - 1);
        write_dirent_name(&dirent);
        write_literal(newline, sizeof(newline) - 1);
    }

    return 0;
}

static int cmd_cat(const char *path, char *buffer, uint64_t buffer_len) {
    static const char prefix[] = "$ cat ";
    static const char newline[] = "\n";
    static const char fail[] = "cat: failed\n";

    write_literal(prefix, sizeof(prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);

    uint64_t fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        return -1;
    }

    for (;;) {
        uint64_t read_len = sys_read(fd, buffer, buffer_len);
        if (read_len == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            return -1;
        }
        if (read_len == 0) {
            break;
        }
        sys_write(STDOUT_FD, buffer, read_len);
        if (read_len < buffer_len) {
            break;
        }
    }

    sys_close(fd);
    write_literal(newline, sizeof(newline) - 1);
    return 0;
}

static void run_startup_self_test(char *heap) {
    static const char heap_message[] = "shell.elf: heap buffer works\n";
    static const char usercopy_ok[] = "shell.elf: bad user pointer rejected\n";
    static const char usercopy_fail[] = "shell.elf: bad user pointer was accepted\n";

    uint64_t heap_len = copy_string(heap, heap_message);
    sys_write(STDOUT_FD, heap, heap_len);

    uint64_t fd = sys_open("/hello.txt");
    if (fd == (uint64_t)-1) {
        sys_exit(10);
    }

    uint64_t bad_read = sys_read(fd, (void *)0xffff800000000000ULL, 1);
    if (bad_read == (uint64_t)-1) {
        write_literal(usercopy_ok, sizeof(usercopy_ok) - 1);
    } else {
        write_literal(usercopy_fail, sizeof(usercopy_fail) - 1);
        sys_exit(9);
    }

    sys_close(fd);
}

static int strings_equal(const char *a, const char *b) {
    uint64_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int starts_with(const char *s, const char *prefix) {
    uint64_t i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ') {
        s++;
    }
    return s;
}

static const char *find_space(const char *s) {
    while (*s != '\0' && *s != ' ') {
        s++;
    }
    return s;
}

static int parse_u64(const char *s, uint64_t *out) {
    uint64_t value = 0;
    uint64_t i = 0;

    if (s[0] == '\0') {
        return -1;
    }

    while (s[i] != '\0') {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        value = value * 10 + (uint64_t)(s[i] - '0');
        i++;
    }

    *out = value;
    return 0;
}

static uint64_t copy_token(char *dst, uint64_t dst_len, const char *start, const char *end) {
    uint64_t len = 0;
    while (start + len < end && len + 1 < dst_len) {
        dst[len] = start[len];
        len++;
    }
    dst[len] = '\0';
    return len;
}

static int copy_cstr_limited(char *dst, uint64_t dst_len, const char *src) {
    uint64_t i = 0;
    if (dst_len == 0) {
        return -1;
    }
    while (src[i] != '\0' && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    if (src[i] != '\0') {
        dst[0] = '\0';
        return -1;
    }
    dst[i] = '\0';
    return 0;
}

static uint64_t path_length(const char *path) {
    return string_length(path);
}

static void path_pop_component(char *path) {
    uint64_t len = path_length(path);
    if (len <= 1) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }

    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    while (len > 1 && path[len - 1] != '/') {
        path[--len] = '\0';
    }
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
    }
}

static int path_append_component(char *path, uint64_t path_len,
                                 const char *component, uint64_t component_len) {
    uint64_t len = path_length(path);
    uint64_t need_slash = len > 1 ? 1 : 0;

    if (component_len == 0 || len + need_slash + component_len >= path_len) {
        return -1;
    }
    if (need_slash) {
        path[len++] = '/';
    }
    for (uint64_t i = 0; i < component_len; i++) {
        path[len++] = component[i];
    }
    path[len] = '\0';
    return 0;
}

static int resolve_path(const char *cwd, const char *input, char *out, uint64_t out_len) {
    const char *p = input;

    if (out_len < 2) {
        return -1;
    }

    if (p[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        while (*p == '/') {
            p++;
        }
    } else {
        if (copy_cstr_limited(out, out_len, cwd) != 0) {
            return -1;
        }
    }

    while (*p != '\0') {
        const char *start;
        uint64_t len = 0;

        while (*p == '/') {
            p++;
        }
        start = p;
        while (p[len] != '\0' && p[len] != '/') {
            len++;
        }

        if (len == 0) {
            break;
        }
        if (len == 1 && start[0] == '.') {
            /* no-op */
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            path_pop_component(out);
        } else if (path_append_component(out, out_len, start, len) != 0) {
            return -1;
        }

        p = start + len;
    }

    return 0;
}

static int split_parent_base(const char *path, char *parent, uint64_t parent_len,
                             char *base, uint64_t base_len) {
    uint64_t len = path_length(path);
    uint64_t slash = 0;

    if (len <= 1) {
        return -1;
    }

    for (uint64_t i = 1; i < len; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }

    if (slash == 0) {
        if (copy_cstr_limited(parent, parent_len, "/") != 0 ||
            copy_cstr_limited(base, base_len, path + 1) != 0) {
            return -1;
        }
        return 0;
    }

    if (slash + 1 >= len || slash + 1 >= parent_len) {
        return -1;
    }
    for (uint64_t i = 0; i < slash; i++) {
        parent[i] = path[i];
    }
    parent[slash] = '\0';
    return copy_cstr_limited(base, base_len, path + slash + 1);
}

static int find_dirent_for_path(const char *path, struct dirent *out) {
    char parent[SHELL_PATH_MAX];
    char base[DIRENT_NAME_MAX];
    struct dirent dirent;

    if (strings_equal(path, "/")) {
        copy_cstr_limited(out->name, sizeof(out->name), "/");
        out->type = DIRENT_TYPE_DIR;
        out->size = 0;
        return 0;
    }

    if (split_parent_base(path, parent, sizeof(parent), base, sizeof(base)) != 0) {
        return -1;
    }

    for (uint64_t i = 0; i < 32; i++) {
        uint64_t result = sys_getdents(parent, i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            return -1;
        }
        if (strings_equal(dirent.name, base)) {
            *out = dirent;
            return 0;
        }
    }

    return -1;
}

static int path_is_dir(const char *path) {
    struct dirent dirent;
    if (strings_equal(path, "/")) {
        return 1;
    }
    if (find_dirent_for_path(path, &dirent) != 0) {
        return 0;
    }
    return dirent.type == DIRENT_TYPE_DIR;
}

static int write_file_text(const char *path, const char *text, uint64_t flags);

static void run_dup2_stdin_self_test(char *buffer, uint64_t buffer_len) {
    static const char dup2_ok[] = "shell.elf: dup2(file, stdin) ok\n";
    static const char dup2_offset_ok[] = "shell.elf: dup2 shared offset ok\n";
    static const char dup2_fail[] = "shell.elf: dup2(file, stdin) failed\n";

    if (buffer_len < 6) {
        sys_exit(14);
    }

    uint64_t fd = sys_open("/hello.txt");
    if (fd == (uint64_t)-1) {
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_exit(14);
    }
    if (sys_dup2(fd, STDIN_FD) != STDIN_FD) {
        sys_close(fd);
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_exit(14);
    }

    uint64_t read_len = sys_read(STDIN_FD, buffer, 5);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len == 5 && starts_with(buffer, "Hello")) {
        write_literal(dup2_ok, sizeof(dup2_ok) - 1);
    } else {
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_close(fd);
        sys_exit(14);
    }

    read_len = sys_read(fd, buffer, 1);
    if (read_len == 1 && buffer[0] == ' ') {
        write_literal(dup2_offset_ok, sizeof(dup2_offset_ok) - 1);
    } else {
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_close(fd);
        sys_exit(14);
    }

    sys_close(fd);
}

static void run_ramfs_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: ramfs open/write/read ok\n";
    static const char fd_ok[] = "shell.elf: ramfs append ok\n";
    static const char fail[] = "shell.elf: ramfs write/read failed\n";
    static const char path[] = "/ram-note.txt";
    static const char content[] = "TangPingOS ramfs is writable.\n";
    static const char append[] = "fd write works.\n";

    uint64_t len = sizeof(content) - 1;
    uint64_t append_len = sizeof(append) - 1;
    if (buffer_len < len + append_len + 1) {
        sys_exit(16);
    }
    uint64_t fd = sys_open_flags(path, OPEN_CREATE | OPEN_TRUNC);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    if (sys_write(fd, content, len) != len) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    sys_close(fd);

    fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    uint64_t read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len == len && starts_with(buffer, content)) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }

    fd = sys_open_flags(path, OPEN_APPEND);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    if (sys_write(fd, append, append_len) != append_len) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    sys_close(fd);

    fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len == len + append_len && starts_with(buffer, content)) {
        write_literal(fd_ok, sizeof(fd_ok) - 1);
        return;
    }

    write_literal(fail, sizeof(fail) - 1);
    sys_exit(16);
}

static void run_lseek_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: lseek ok\n";
    static const char fail[] = "shell.elf: lseek failed\n";
    static const char path[] = "/seek-note.txt";
    static const char initial[] = "abcdef";
    static const char patch[] = "XY";
    static const char expected[] = "abXYef";
    static const char tail[] = "ef";

    if (buffer_len < sizeof(expected)) {
        sys_exit(17);
    }

    uint64_t fd = sys_open_flags(path, OPEN_CREATE | OPEN_TRUNC);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_write(fd, initial, sizeof(initial) - 1) != sizeof(initial) - 1) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_lseek(fd, 2, SEEK_SET) != 2) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_write(fd, patch, sizeof(patch) - 1) != sizeof(patch) - 1) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_lseek(fd, 0, SEEK_SET) != 0) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    uint64_t read_len = sys_read(fd, buffer, buffer_len - 1);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len != sizeof(expected) - 1 || !starts_with(buffer, expected)) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }

    if (sys_lseek(fd, -2, SEEK_END) != sizeof(expected) - 1 - 2) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    sys_close(fd);
    if (read_len == sizeof(tail) - 1 && starts_with(buffer, tail)) {
        write_literal(ok, sizeof(ok) - 1);
        return;
    }

    write_literal(fail, sizeof(fail) - 1);
    sys_exit(17);
}

static void run_unlink_self_test(void) {
    static const char ok[] = "shell.elf: unlink ok\n";
    static const char fail[] = "shell.elf: unlink failed\n";
    static const char path[] = "/delete-me.txt";
    static const char content[] = "temporary";

    if (write_file_text(path, content, OPEN_CREATE | OPEN_TRUNC) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(19);
    }
    if (sys_unlink(path) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(19);
    }
    if (sys_open(path) == (uint64_t)-1) {
        write_literal(ok, sizeof(ok) - 1);
        return;
    }

    write_literal(fail, sizeof(fail) - 1);
    sys_exit(19);
}

static int read_file_equals(const char *path, char *buffer, uint64_t buffer_len,
                            const char *expected) {
    uint64_t fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        return 0;
    }

    uint64_t read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len == (uint64_t)-1 || read_len + 1 > buffer_len) {
        return 0;
    }
    buffer[read_len] = '\0';

    return read_len == string_length(expected) && strings_equal(buffer, expected);
}

static void read_line(uint64_t input_fd, char *line, uint64_t max_len) {
    uint64_t len = 0;

    for (;;) {
        char c;
        uint64_t read = sys_read(input_fd, &c, 1);
        if (read == 0) {
            sys_yield();
            continue;
        }
        if (read == (uint64_t)-1) {
            line[0] = '\0';
            return;
        }

        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            line[len] = '\0';
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
            }
            continue;
        }
        if (len + 1 < max_len) {
            line[len++] = c;
        }
    }
}

static int write_file_text(const char *path, const char *text, uint64_t flags) {
    uint64_t len = string_length(text);
    uint64_t fd = sys_open_flags(path, flags);
    if (fd == (uint64_t)-1) {
        return -1;
    }
    if (sys_write(fd, text, len) != len) {
        sys_close(fd);
        return -1;
    }
    sys_close(fd);
    return 0;
}

static int copy_file_data(const char *src, const char *dst, char *buffer, uint64_t buffer_len) {
    uint64_t in_fd = sys_open(src);
    if (in_fd == (uint64_t)-1) {
        return -1;
    }

    uint64_t out_fd = sys_open_flags(dst, OPEN_CREATE | OPEN_TRUNC);
    if (out_fd == (uint64_t)-1) {
        sys_close(in_fd);
        return -1;
    }

    for (;;) {
        uint64_t read_len = sys_read(in_fd, buffer, buffer_len);
        if (read_len == (uint64_t)-1) {
            sys_close(in_fd);
            sys_close(out_fd);
            return -1;
        }
        if (read_len == 0) {
            break;
        }
        if (sys_write(out_fd, buffer, read_len) != read_len) {
            sys_close(in_fd);
            sys_close(out_fd);
            return -1;
        }
    }

    sys_close(in_fd);
    sys_close(out_fd);
    return 0;
}

static void write_dirent_type(uint64_t type) {
    static const char file[] = "file";
    static const char dir[] = "dir";
    static const char device[] = "device";
    static const char unknown[] = "unknown";

    if (type == DIRENT_TYPE_FILE) {
        write_literal(file, sizeof(file) - 1);
    } else if (type == DIRENT_TYPE_DIR) {
        write_literal(dir, sizeof(dir) - 1);
    } else if (type == DIRENT_TYPE_DEVICE) {
        write_literal(device, sizeof(device) - 1);
    } else {
        write_literal(unknown, sizeof(unknown) - 1);
    }
}

static void cmd_stat(const char *path) {
    static const char fail[] = "stat: failed\n";
    static const char path_prefix[] = "path: ";
    static const char type_prefix[] = "type: ";
    static const char size_prefix[] = "size: ";
    static const char newline[] = "\n";
    struct dirent dirent;

    if (find_dirent_for_path(path, &dirent) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal(path_prefix, sizeof(path_prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);
    write_literal(type_prefix, sizeof(type_prefix) - 1);
    write_dirent_type(dirent.type);
    write_literal(newline, sizeof(newline) - 1);
    write_literal(size_prefix, sizeof(size_prefix) - 1);
    write_u64_decimal(dirent.size);
    write_literal(newline, sizeof(newline) - 1);
}

static void cmd_cp(const char *src, const char *dst, char *buffer, uint64_t buffer_len) {
    static const char ok[] = "cp: ok\n";
    static const char fail[] = "cp: failed\n";

    if (copy_file_data(src, dst, buffer, buffer_len) == 0) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void cmd_mv(const char *src, const char *dst, char *buffer, uint64_t buffer_len) {
    static const char ok[] = "mv: ok\n";
    static const char fail[] = "mv: failed\n";

    if (copy_file_data(src, dst, buffer, buffer_len) == 0 && sys_unlink(src) == 0) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void cmd_hexdump(const char *path, char *buffer, uint64_t buffer_len) {
    static const char fail[] = "hexdump: failed\n";
    uint64_t fd = sys_open(path);
    uint64_t offset = 0;

    if (fd == (uint64_t)-1 || buffer_len < 16) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    for (;;) {
        uint64_t read_len = sys_read(fd, buffer, 16);
        if (read_len == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            return;
        }
        if (read_len == 0) {
            break;
        }

        write_u64_hex(offset);
        write_literal(": ", 2);
        for (uint64_t i = 0; i < read_len; i++) {
            write_hex_byte((uint8_t)buffer[i]);
            write_literal(" ", 1);
        }
        write_literal("\n", 1);
        offset += read_len;
    }

    sys_close(fd);
}

static void cmd_edit(const char *path, char *buffer, uint64_t buffer_len) {
    static const char start[] = "edit: enter lines, .save to finish\n";
    static const char prompt[] = "> ";
    static const char ok[] = "edit: saved\n";
    static const char fail[] = "edit: failed\n";

    if (buffer_len < SHELL_LINE_MAX) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    uint64_t fd = sys_open_flags(path, OPEN_CREATE | OPEN_TRUNC);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal(start, sizeof(start) - 1);
    for (;;) {
        write_literal(prompt, sizeof(prompt) - 1);
        read_line(STDIN_FD, buffer, SHELL_LINE_MAX);
        if (strings_equal(buffer, ".save")) {
            sys_close(fd);
            write_literal(ok, sizeof(ok) - 1);
            return;
        }

        uint64_t len = string_length(buffer);
        if (sys_write(fd, buffer, len) != len || sys_write(fd, "\n", 1) != 1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            return;
        }
    }
}

static const char *task_state_name(uint64_t state) {
    switch (state) {
        case 0:
            return "runnable";
        case 1:
            return "sleeping";
        case 2:
            return "waiting-input";
        case 3:
            return "stopped";
        default:
            return "unknown";
    }
}

static void write_pages_as_kib(uint64_t pages, uint64_t page_size) {
    write_u64_decimal((pages * page_size) / 1024);
    write_literal(" KiB", 4);
}

static void cmd_ps(void) {
    static const char header[] = "pid state switches exit name\n";
    static const char fail[] = "ps: failed\n";
    struct task_info info;

    write_literal(header, sizeof(header) - 1);
    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_task_info(i, &info);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            return;
        }

        write_u64_decimal(info.pid);
        write_literal(" ", 1);
        write_cstr(task_state_name(info.state));
        write_literal(" ", 1);
        write_u64_decimal(info.switches);
        write_literal(" ", 1);
        write_u64_decimal(info.exit_status);
        write_literal(" ", 1);
        write_cstr_limited(info.name, sizeof(info.name));
        write_literal("\n", 1);
    }
}

static void cmd_mem(void) {
    static const char fail[] = "mem: failed\n";
    struct meminfo info;

    if (sys_meminfo(&info) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal("page size: ", 11);
    write_u64_decimal(info.page_size);
    write_literal(" bytes\n", 7);
    write_literal("total: ", 7);
    write_u64_decimal(info.total_pages);
    write_literal(" pages, ", 8);
    write_pages_as_kib(info.total_pages, info.page_size);
    write_literal("\nused: ", 7);
    write_u64_decimal(info.used_pages);
    write_literal(" pages, ", 8);
    write_pages_as_kib(info.used_pages, info.page_size);
    write_literal("\nfree: ", 7);
    write_u64_decimal(info.free_pages);
    write_literal(" pages, ", 8);
    write_pages_as_kib(info.free_pages, info.page_size);
    write_literal("\n", 1);
}

static void cmd_uptime(void) {
    struct system_info info;
    uint64_t ticks = sys_uptime();
    uint64_t hz = 100;

    if (sys_system_info(&info) == 0 && info.timer_hz != 0) {
        hz = info.timer_hz;
    }

    write_literal("ticks: ", 7);
    write_u64_decimal(ticks);
    write_literal("\nseconds: ", 10);
    write_u64_decimal(ticks / hz);
    write_literal("\n", 1);
}

static void cmd_sysinfo(void) {
    static const char fail[] = "sysinfo: failed\n";
    struct system_info info;

    if (sys_system_info(&info) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal("cpu: ", 5);
    write_cstr_limited(info.cpu_name, sizeof(info.cpu_name));
    write_literal("\nframebuffer: ", 14);
    write_u64_decimal(info.framebuffer_width);
    write_literal("x", 1);
    write_u64_decimal(info.framebuffer_height);
    write_literal(" pitch ", 7);
    write_u64_decimal(info.framebuffer_pitch);
    write_literal(" bpp ", 5);
    write_u64_decimal(info.framebuffer_bpp);
    write_literal("\nmemmap entries: ", 17);
    write_u64_decimal(info.memmap_entries);
    write_literal("\ntimer hz: ", 11);
    write_u64_decimal(info.timer_hz);
    write_literal("\nticks: ", 8);
    write_u64_decimal(info.ticks);
    write_literal("\nmemory total/free/used pages: ", 31);
    write_u64_decimal(info.total_pages);
    write_literal("/", 1);
    write_u64_decimal(info.free_pages);
    write_literal("/", 1);
    write_u64_decimal(info.used_pages);
    write_literal("\n", 1);
}

static void write_buffer_hex(const char *buffer, uint64_t len) {
    uint64_t offset = 0;

    while (offset < len) {
        uint64_t line_len = len - offset;
        if (line_len > 16) {
            line_len = 16;
        }

        write_u64_hex(offset);
        write_literal(": ", 2);
        for (uint64_t i = 0; i < line_len; i++) {
            write_hex_byte((uint8_t)buffer[offset + i]);
            write_literal(" ", 1);
        }
        write_literal("\n", 1);
        offset += line_len;
    }
}

static void clear_buffer(char *buffer, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        buffer[i] = 0;
    }
}

static void cmd_lsblk(void) {
    static const char header[] = "id name block_size blocks writable\n";
    struct block_device_info info;

    write_literal(header, sizeof(header) - 1);
    for (uint64_t i = 0; i < 8; i++) {
        uint64_t result = sys_block_info(i, &info);
        if (result == (uint64_t)-1) {
            break;
        }

        write_u64_decimal(i);
        write_literal(" ", 1);
        write_cstr_limited(info.name, sizeof(info.name));
        write_literal(" ", 1);
        write_u64_decimal(info.block_size);
        write_literal(" ", 1);
        write_u64_decimal(info.block_count);
        write_literal(" ", 1);
        write_u64_decimal(info.writable);
        write_literal("\n", 1);
    }
}

static void cmd_mounts(void) {
    static const char header[] = "id path fs source writable\n";
    struct mount_info info;

    write_literal(header, sizeof(header) - 1);
    for (uint64_t i = 0; i < 8; i++) {
        uint64_t result = sys_mount_info(i, &info);
        if (result == (uint64_t)-1) {
            break;
        }

        write_u64_decimal(i);
        write_literal(" ", 1);
        write_cstr_limited(info.path, sizeof(info.path));
        write_literal(" ", 1);
        write_cstr_limited(info.name, sizeof(info.name));
        write_literal(" ", 1);
        write_cstr_limited(info.source, sizeof(info.source));
        write_literal(" ", 1);
        write_u64_decimal(info.writable);
        write_literal("\n", 1);
    }
}

static void cmd_blkread(uint64_t device, uint64_t lba, char *buffer, uint64_t buffer_len) {
    static const char fail[] = "blkread: failed\n";

    if (buffer_len < 512 || sys_block_read(device, lba, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_buffer_hex(buffer, 512);
}

static void cmd_blkwrite(uint64_t device, uint64_t lba, const char *text,
                         char *buffer, uint64_t buffer_len) {
    static const char ok[] = "blkwrite: ok\n";
    static const char fail[] = "blkwrite: failed\n";
    uint64_t text_len = string_length(text);

    if (buffer_len < 512 || text_len >= 512) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    clear_buffer(buffer, 512);
    for (uint64_t i = 0; i < text_len; i++) {
        buffer[i] = text[i];
    }

    if (sys_block_write(device, lba, buffer, 1) == 1) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void cmd_run(const char *path, const char *args) {
    static const char ok[] = "run: started\n";
    static const char fail[] = "run: failed\n";

    if (sys_spawn(path, args) == 0) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void run_shell_command(const char *line, char *cwd, char *buffer, uint64_t buffer_len) {
    static const char help[] =
        "commands: help, pwd, cd, ls, cat, echo, clear, touch, write, append, rm, stat, cp, mv, hexdump, edit, run, ps, mem, uptime, sysinfo, mounts, lsblk, blkread, blkwrite\n";
    static const char unknown[] = "unknown command\n";
    static const char usage_blkread[] = "usage: blkread device lba\n";
    static const char usage_blkwrite[] = "usage: blkwrite device lba text\n";
    static const char usage_cat[] = "usage: cat /path\n";
    static const char usage_cp[] = "usage: cp src dst\n";
    static const char usage_edit[] = "usage: edit /path\n";
    static const char usage_hexdump[] = "usage: hexdump /path\n";
    static const char usage_ls[] = "usage: ls [/path]\n";
    static const char usage_mv[] = "usage: mv src dst\n";
    static const char usage_rm[] = "usage: rm /path\n";
    static const char usage_run[] = "usage: run /path.elf [args...]\n";
    static const char usage_stat[] = "usage: stat /path\n";
    static const char usage_touch[] = "usage: touch /path\n";
    static const char usage_write[] = "usage: write /path text\n";
    static const char usage_append[] = "usage: append /path text\n";
    static const char cd_fail[] = "cd: failed\n";
    static const char rm_ok[] = "rm: ok\n";
    static const char rm_fail[] = "rm: failed\n";
    static const char touch_ok[] = "touch: ok\n";
    static const char touch_fail[] = "touch: failed\n";
    static const char write_ok[] = "write: ok\n";
    static const char write_fail[] = "write: failed\n";
    static const char append_ok[] = "append: ok\n";
    static const char append_fail[] = "append: failed\n";
    char path[SHELL_PATH_MAX];

    line = skip_spaces(line);
    if (line[0] == '\0') {
        return;
    }
    if (strings_equal(line, "help")) {
        write_literal(help, sizeof(help) - 1);
        return;
    }
    if (strings_equal(line, "pwd")) {
        write_cstr(cwd);
        write_literal("\n", 1);
        return;
    }
    if (strings_equal(line, "clear")) {
        for (uint64_t i = 0; i < 40; i++) {
            write_literal("\n", 1);
        }
        return;
    }
    if (strings_equal(line, "ps")) {
        cmd_ps();
        return;
    }
    if (strings_equal(line, "mem")) {
        cmd_mem();
        return;
    }
    if (strings_equal(line, "uptime")) {
        cmd_uptime();
        return;
    }
    if (strings_equal(line, "sysinfo")) {
        cmd_sysinfo();
        return;
    }
    if (strings_equal(line, "lsblk")) {
        cmd_lsblk();
        return;
    }
    if (strings_equal(line, "mounts")) {
        cmd_mounts();
        return;
    }
    if (starts_with(line, "blkread ")) {
        const char *dev_start = skip_spaces(line + 8);
        const char *dev_end = find_space(dev_start);
        const char *lba_start = skip_spaces(dev_end);
        const char *lba_end = find_space(lba_start);
        char dev_token[24];
        char lba_token[24];
        uint64_t device;
        uint64_t lba;

        if (dev_start[0] == '\0' || lba_start[0] == '\0' ||
            copy_token(dev_token, sizeof(dev_token), dev_start, dev_end) == 0 ||
            copy_token(lba_token, sizeof(lba_token), lba_start, lba_end) == 0 ||
            parse_u64(dev_token, &device) != 0 || parse_u64(lba_token, &lba) != 0) {
            write_literal(usage_blkread, sizeof(usage_blkread) - 1);
            return;
        }
        cmd_blkread(device, lba, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "blkwrite ")) {
        const char *dev_start = skip_spaces(line + 9);
        const char *dev_end = find_space(dev_start);
        const char *lba_start = skip_spaces(dev_end);
        const char *lba_end = find_space(lba_start);
        const char *text = skip_spaces(lba_end);
        char dev_token[24];
        char lba_token[24];
        uint64_t device;
        uint64_t lba;

        if (dev_start[0] == '\0' || lba_start[0] == '\0' || text[0] == '\0' ||
            copy_token(dev_token, sizeof(dev_token), dev_start, dev_end) == 0 ||
            copy_token(lba_token, sizeof(lba_token), lba_start, lba_end) == 0 ||
            parse_u64(dev_token, &device) != 0 || parse_u64(lba_token, &lba) != 0) {
            write_literal(usage_blkwrite, sizeof(usage_blkwrite) - 1);
            return;
        }
        cmd_blkwrite(device, lba, text, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "echo")) {
        const char *text = skip_spaces(line + 4);
        write_cstr(text);
        write_literal("\n", 1);
        return;
    }
    if (strings_equal(line, "cd")) {
        if (copy_cstr_limited(cwd, SHELL_PATH_MAX, "/") != 0) {
            write_literal(cd_fail, sizeof(cd_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "cd ")) {
        const char *arg = skip_spaces(line + 3);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0 ||
            !path_is_dir(path) || copy_cstr_limited(cwd, SHELL_PATH_MAX, path) != 0) {
            write_literal(cd_fail, sizeof(cd_fail) - 1);
        }
        return;
    }
    if (strings_equal(line, "ls")) {
        cmd_ls(cwd);
        return;
    }
    if (starts_with(line, "ls ")) {
        const char *arg = skip_spaces(line + 3);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0) {
            write_literal(usage_ls, sizeof(usage_ls) - 1);
            return;
        }
        cmd_ls(path);
        return;
    }
    if (starts_with(line, "cat ")) {
        const char *arg = skip_spaces(line + 4);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0) {
            write_literal(usage_cat, sizeof(usage_cat) - 1);
            return;
        }
        cmd_cat(path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "hexdump ")) {
        const char *arg = skip_spaces(line + 8);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0) {
            write_literal(usage_hexdump, sizeof(usage_hexdump) - 1);
            return;
        }
        cmd_hexdump(path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "edit ")) {
        const char *arg = skip_spaces(line + 5);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0) {
            write_literal(usage_edit, sizeof(usage_edit) - 1);
            return;
        }
        cmd_edit(path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "touch ")) {
        const char *arg = skip_spaces(line + 6);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0) {
            write_literal(usage_touch, sizeof(usage_touch) - 1);
            return;
        }
        if (write_file_text(path, "", OPEN_CREATE | OPEN_APPEND) == 0) {
            write_literal(touch_ok, sizeof(touch_ok) - 1);
        } else {
            write_literal(touch_fail, sizeof(touch_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "write ")) {
        const char *path_start = skip_spaces(line + 6);
        const char *path_end = find_space(path_start);
        const char *text = skip_spaces(path_end);
        char token[SHELL_PATH_MAX];
        if (path_start[0] == '\0' || text[0] == '\0' ||
            copy_token(token, sizeof(token), path_start, path_end) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_write, sizeof(usage_write) - 1);
            return;
        }
        if (write_file_text(path, text, OPEN_CREATE | OPEN_TRUNC) == 0) {
            write_literal(write_ok, sizeof(write_ok) - 1);
        } else {
            write_literal(write_fail, sizeof(write_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "append ")) {
        const char *path_start = skip_spaces(line + 7);
        const char *path_end = find_space(path_start);
        const char *text = skip_spaces(path_end);
        char token[SHELL_PATH_MAX];
        if (path_start[0] == '\0' || text[0] == '\0' ||
            copy_token(token, sizeof(token), path_start, path_end) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_append, sizeof(usage_append) - 1);
            return;
        }
        if (write_file_text(path, text, OPEN_CREATE | OPEN_APPEND) == 0) {
            write_literal(append_ok, sizeof(append_ok) - 1);
        } else {
            write_literal(append_fail, sizeof(append_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "cp ")) {
        const char *src_start = skip_spaces(line + 3);
        const char *src_end = find_space(src_start);
        const char *dst_start = skip_spaces(src_end);
        const char *dst_end = find_space(dst_start);
        char src_token[SHELL_PATH_MAX];
        char dst_token[SHELL_PATH_MAX];
        char dst_path[SHELL_PATH_MAX];

        if (src_start[0] == '\0' || dst_start[0] == '\0' ||
            copy_token(src_token, sizeof(src_token), src_start, src_end) == 0 ||
            copy_token(dst_token, sizeof(dst_token), dst_start, dst_end) == 0 ||
            resolve_path(cwd, src_token, path, sizeof(path)) != 0 ||
            resolve_path(cwd, dst_token, dst_path, sizeof(dst_path)) != 0) {
            write_literal(usage_cp, sizeof(usage_cp) - 1);
            return;
        }
        cmd_cp(path, dst_path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "mv ")) {
        const char *src_start = skip_spaces(line + 3);
        const char *src_end = find_space(src_start);
        const char *dst_start = skip_spaces(src_end);
        const char *dst_end = find_space(dst_start);
        char src_token[SHELL_PATH_MAX];
        char dst_token[SHELL_PATH_MAX];
        char dst_path[SHELL_PATH_MAX];

        if (src_start[0] == '\0' || dst_start[0] == '\0' ||
            copy_token(src_token, sizeof(src_token), src_start, src_end) == 0 ||
            copy_token(dst_token, sizeof(dst_token), dst_start, dst_end) == 0 ||
            resolve_path(cwd, src_token, path, sizeof(path)) != 0 ||
            resolve_path(cwd, dst_token, dst_path, sizeof(dst_path)) != 0) {
            write_literal(usage_mv, sizeof(usage_mv) - 1);
            return;
        }
        cmd_mv(path, dst_path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "rm ")) {
        const char *arg = skip_spaces(line + 3);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0) {
            write_literal(usage_rm, sizeof(usage_rm) - 1);
            return;
        }
        if (sys_unlink(path) == 0) {
            write_literal(rm_ok, sizeof(rm_ok) - 1);
        } else {
            write_literal(rm_fail, sizeof(rm_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "stat ")) {
        const char *arg = skip_spaces(line + 5);
        if (arg[0] == '\0' || resolve_path(cwd, arg, path, sizeof(path)) != 0) {
            write_literal(usage_stat, sizeof(usage_stat) - 1);
            return;
        }
        cmd_stat(path);
        return;
    }
    if (starts_with(line, "run ")) {
        const char *path_start = skip_spaces(line + 4);
        const char *path_end = find_space(path_start);
        const char *args = skip_spaces(path_end);
        char token[SHELL_PATH_MAX];
        if (path_start[0] == '\0' ||
            copy_token(token, sizeof(token), path_start, path_end) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_run, sizeof(usage_run) - 1);
            return;
        }
        cmd_run(path, args);
        return;
    }

    write_literal(unknown, sizeof(unknown) - 1);
}

static void run_shell_script_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: shell script ok\n";
    static const char fail[] = "shell.elf: shell script failed\n";
    char cwd[SHELL_PATH_MAX];

    if (copy_cstr_limited(cwd, sizeof(cwd), "/") != 0) {
        sys_exit(20);
    }

    run_shell_command("pwd", cwd, buffer, buffer_len);
    run_shell_command("write script.txt alpha", cwd, buffer, buffer_len);
    run_shell_command("append script.txt beta", cwd, buffer, buffer_len);
    run_shell_command("stat script.txt", cwd, buffer, buffer_len);
    run_shell_command("cat script.txt", cwd, buffer, buffer_len);
    run_shell_command("cp script.txt copy.txt", cwd, buffer, buffer_len);

    if (!read_file_equals("/script.txt", buffer, buffer_len, "alphabeta")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    if (!read_file_equals("/copy.txt", buffer, buffer_len, "alphabeta")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }

    run_shell_command("mv copy.txt moved.txt", cwd, buffer, buffer_len);
    if (sys_open("/copy.txt") != (uint64_t)-1 ||
        !read_file_equals("/moved.txt", buffer, buffer_len, "alphabeta")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    run_shell_command("hexdump moved.txt", cwd, buffer, buffer_len);

    run_shell_command("touch empty.txt", cwd, buffer, buffer_len);
    if (!read_file_equals("/empty.txt", buffer, buffer_len, "")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }

    run_shell_command("cd dev", cwd, buffer, buffer_len);
    if (!strings_equal(cwd, "/dev")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    run_shell_command("ls", cwd, buffer, buffer_len);
    run_shell_command("cd ..", cwd, buffer, buffer_len);
    if (!strings_equal(cwd, "/")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    run_shell_command("ls /bin", cwd, buffer, buffer_len);
    run_shell_command("run /bin/hello.elf one two", cwd, buffer, buffer_len);
#ifdef TANGPINGOS_TEST_USER_PROGRAMS
    run_shell_command("run /bin/ls.elf /bin", cwd, buffer, buffer_len);
    run_shell_command("run /bin/cat.elf /hello.txt", cwd, buffer, buffer_len);
#endif
    for (uint64_t i = 0; i < 8; i++) {
        sys_yield();
    }
    run_shell_command("ps", cwd, buffer, buffer_len);
    run_shell_command("mem", cwd, buffer, buffer_len);
    run_shell_command("uptime", cwd, buffer, buffer_len);
    run_shell_command("sysinfo", cwd, buffer, buffer_len);
    run_shell_command("mounts", cwd, buffer, buffer_len);

    run_shell_command("rm script.txt", cwd, buffer, buffer_len);
    run_shell_command("rm empty.txt", cwd, buffer, buffer_len);
    run_shell_command("rm moved.txt", cwd, buffer, buffer_len);
    if (sys_open("/script.txt") != (uint64_t)-1 ||
        sys_open("/empty.txt") != (uint64_t)-1 ||
        sys_open("/moved.txt") != (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_block_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: block device read/write ok\n";
    static const char fail[] = "shell.elf: block device test failed\n";
    static const char message[] = "TangPingOS block layer works";
    struct block_device_info info;

    if (buffer_len < 512 || sys_block_info(0, &info) != 0 ||
        info.block_size != 512 || info.block_count < 4 || info.writable == 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(21);
    }

    clear_buffer(buffer, 512);
    for (uint64_t i = 0; i < sizeof(message) - 1; i++) {
        buffer[i] = message[i];
    }
    if (sys_block_write(0, 3, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(21);
    }

    clear_buffer(buffer, 512);
    if (sys_block_read(0, 3, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(21);
    }
    for (uint64_t i = 0; i < sizeof(message) - 1; i++) {
        if (buffer[i] != message[i]) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(21);
        }
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_virtio_block_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: MBR partition device read ok\n";
    static const char fail[] = "shell.elf: virtio block device test failed\n";
    static const char expected[] = "TangPingOS QEMU partition";
    struct block_device_info info;

    if (sys_block_info(1, &info) != 0) {
        return;
    }
    if (sys_block_info(2, &info) != 0 || buffer_len < 512 ||
        sys_block_read(2, 0, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(22);
    }

    for (uint64_t i = 0; i < sizeof(expected) - 1; i++) {
        if (buffer[i] != expected[i]) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(22);
        }
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_mount_info_self_test(void) {
    static const char ok[] = "shell.elf: mount table ok\n";
    static const char fail[] = "shell.elf: mount table failed\n";
    struct mount_info info;

    if (sys_mount_info(0, &info) != 0 || !strings_equal(info.name, "devfs") ||
        !strings_equal(info.path, "/dev") || info.writable != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(23);
    }
    if (sys_mount_info(1, &info) != 0 || !strings_equal(info.name, "ramfs") ||
        !strings_equal(info.path, "/") || info.writable != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(23);
    }

    write_literal(ok, sizeof(ok) - 1);
}

__attribute__((noreturn))
static void run_interactive_shell(uint64_t input_fd, char *buffer, uint64_t buffer_len) {
    static const char ready[] =
        "\fshell.elf: interactive shell ready\n"
        "TangPingOS is ready. Type help for commands.\n";
    static const char prompt_prefix[] = "TangPingOS:";
    static const char prompt_suffix[] = "> ";
    char line[SHELL_LINE_MAX];
    char cwd[SHELL_PATH_MAX];

    if (copy_cstr_limited(cwd, sizeof(cwd), "/") != 0) {
        sys_exit(18);
    }

    write_literal(ready, sizeof(ready) - 1);
    for (;;) {
        write_literal(prompt_prefix, sizeof(prompt_prefix) - 1);
        write_cstr(cwd);
        write_literal(prompt_suffix, sizeof(prompt_suffix) - 1);
        read_line(input_fd, line, sizeof(line));
        run_shell_command(line, cwd, buffer, buffer_len);
    }
}

__attribute__((noreturn))
void _start(void) {
    static const char pid_prefix[] = "shell.elf: pid=";
    static const char brk_prefix[] = "shell.elf: brk=";
    static const char heap_fail[] = "shell.elf: sbrk failed\n";
    static const char shell_prefix[] = "shell.elf: startup commands\n";
    static const char tty_ok[] = "shell.elf: /dev/tty open\n";
    static const char tty_write_ok[] = "shell.elf: write(/dev/tty) ok\n";
    static const char bad_fd_write_rejected[] = "shell.elf: bad write fd rejected\n";
    static const char dup2_tty_ok[] = "shell.elf: dup2(/dev/tty, stdio) ok\n";
    static const char dup2_tty_fail[] = "shell.elf: dup2(/dev/tty, stdio) failed\n";
    static const char tty_fallback[] = "shell.elf: /dev/tty unavailable, using stdin\n";
    static const char newline[] = "\n";

    write_literal(pid_prefix, sizeof(pid_prefix) - 1);
    write_u64_decimal(sys_getpid());
    write_literal(newline, sizeof(newline) - 1);

    write_literal(brk_prefix, sizeof(brk_prefix) - 1);
    write_u64_hex(sys_brk(0));
    write_literal(newline, sizeof(newline) - 1);

    char *heap = sys_sbrk(512);
    if (heap == (void *)0) {
        write_literal(heap_fail, sizeof(heap_fail) - 1);
        sys_exit(8);
    }

    run_startup_self_test(heap);
    write_literal(shell_prefix, sizeof(shell_prefix) - 1);
    cmd_ls("/");
    cmd_ls("/dev");
    cmd_cat("/hello.txt", heap, 256);
    run_ramfs_self_test(heap, 256);
    run_lseek_self_test(heap, 256);
    run_unlink_self_test();
    run_shell_script_self_test(heap, 256);
    run_block_self_test(heap, 512);
    run_virtio_block_self_test(heap, 512);
    run_mount_info_self_test();
    cmd_ls("/");
    cmd_cat("/ram-note.txt", heap, 256);
    run_dup2_stdin_self_test(heap, 256);

    uint64_t input_fd = sys_open("/dev/tty");
    if (input_fd == (uint64_t)-1) {
        write_literal(tty_fallback, sizeof(tty_fallback) - 1);
        input_fd = STDIN_FD;
    } else {
        write_literal(tty_ok, sizeof(tty_ok) - 1);
        if (sys_dup2(input_fd, STDIN_FD) != STDIN_FD ||
            sys_dup2(input_fd, STDOUT_FD) != STDOUT_FD) {
            write_literal(dup2_tty_fail, sizeof(dup2_tty_fail) - 1);
            sys_exit(15);
        }
        write_literal(dup2_tty_ok, sizeof(dup2_tty_ok) - 1);
        sys_write(input_fd, tty_write_ok, sizeof(tty_write_ok) - 1);
    }

    if (sys_write(99, tty_write_ok, sizeof(tty_write_ok) - 1) == (uint64_t)-1) {
        write_literal(bad_fd_write_rejected, sizeof(bad_fd_write_rejected) - 1);
    } else {
        sys_exit(13);
    }

#ifdef TANGPINGOS_TEST_USER_FAULT
    *(volatile uint64_t *)0 = 0x55;
#endif

    run_interactive_shell(input_fd, heap, 256);
}
