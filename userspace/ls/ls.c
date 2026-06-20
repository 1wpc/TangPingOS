#include "tpos.h"

static void write_dirent_name(const struct dirent *dirent) {
    tpos_write_cstr_limited(dirent->name, DIRENT_NAME_MAX);
}

__attribute__((noreturn))
void _start(uint64_t argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/";
    struct dirent dirent;

    for (uint64_t i = 0; i < 64; i++) {
        uint64_t result = tpos_getdents(path, i, &dirent);
        if (result == 0) {
            tpos_exit(0);
        }
        if (result == (uint64_t)-1) {
            tpos_write_cstr("ls.elf: failed\n");
            tpos_exit(1);
        }

        tpos_write_literal("  ", 2);
        write_dirent_name(&dirent);
        tpos_write_literal("\n", 1);
    }

    tpos_exit(0);
}
