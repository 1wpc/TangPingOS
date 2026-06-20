#include "tpos.h"

static int cat_file(const char *path) {
    char buffer[128];
    uint64_t fd = tpos_open(path);
    if (fd == (uint64_t)-1) {
        tpos_write_cstr("cat.elf: failed to open ");
        tpos_write_cstr(path);
        tpos_write_literal("\n", 1);
        return -1;
    }

    for (;;) {
        uint64_t read_len = tpos_read(fd, buffer, sizeof(buffer));
        if (read_len == (uint64_t)-1) {
            tpos_close(fd);
            tpos_write_cstr("cat.elf: failed to read ");
            tpos_write_cstr(path);
            tpos_write_literal("\n", 1);
            return -1;
        }
        if (read_len == 0) {
            break;
        }
        tpos_write(STDOUT_FD, buffer, read_len);
    }

    tpos_close(fd);
    return 0;
}

__attribute__((noreturn))
void _start(uint64_t argc, char **argv) {
    int status = 0;

    if (argc < 2) {
        tpos_write_cstr("usage: cat.elf file...\n");
        tpos_exit(1);
    }

    for (uint64_t i = 1; i < argc; i++) {
        if (cat_file(argv[i]) != 0) {
            status = 1;
        }
    }

    tpos_exit((uint64_t)status);
}
