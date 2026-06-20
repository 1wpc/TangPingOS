#include "tpos.h"

__attribute__((noreturn))
void _start(uint64_t argc, char **argv) {
    static const char message[] = "hello.elf: hello from a spawned user program\n";

    tpos_write_literal(message, sizeof(message) - 1);
    tpos_write_literal("hello.elf: argc=", 16);
    tpos_write_u64_decimal(argc);
    tpos_write_literal("\n", 1);

    for (uint64_t i = 0; i < argc; i++) {
        tpos_write_literal("hello.elf: argv[", 16);
        tpos_write_u64_decimal(i);
        tpos_write_literal("]=", 2);
        tpos_write_cstr(argv[i]);
        tpos_write_literal("\n", 1);
    }

    tpos_exit(0);
}
