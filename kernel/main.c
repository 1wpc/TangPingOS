#include <console.h>
#include <limine.h>
#include <memory.h>
#include <scheduler.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <user.h>
#include <x86_64/arch.h>
#include <x86_64/io.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[3] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;

#define COM1 0x3f8

static struct limine_framebuffer *fb;
static uint32_t text_x;
static uint32_t text_y;
static uint32_t fg_color = 0x00e6edf3;
static uint32_t bg_color = 0x000b1020;

static const uint8_t font8x8_basic[128][8] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!'] = {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00},
    ['#'] = {0x36,0x36,0x7f,0x36,0x7f,0x36,0x36,0x00},
    ['%'] = {0x63,0x67,0x0e,0x1c,0x38,0x73,0x63,0x00},
    [':'] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['-'] = {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00},
    ['_'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x7f,0x00},
    ['/'] = {0x03,0x07,0x0e,0x1c,0x38,0x70,0x60,0x00},
    ['0'] = {0x3e,0x63,0x67,0x6f,0x7b,0x73,0x3e,0x00},
    ['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00},
    ['2'] = {0x3e,0x63,0x03,0x0e,0x38,0x60,0x7f,0x00},
    ['3'] = {0x3e,0x63,0x03,0x1e,0x03,0x63,0x3e,0x00},
    ['4'] = {0x06,0x0e,0x1e,0x36,0x7f,0x06,0x06,0x00},
    ['5'] = {0x7f,0x60,0x7e,0x03,0x03,0x63,0x3e,0x00},
    ['6'] = {0x1e,0x30,0x60,0x7e,0x63,0x63,0x3e,0x00},
    ['7'] = {0x7f,0x63,0x06,0x0c,0x18,0x18,0x18,0x00},
    ['8'] = {0x3e,0x63,0x63,0x3e,0x63,0x63,0x3e,0x00},
    ['9'] = {0x3e,0x63,0x63,0x3f,0x03,0x06,0x3c,0x00},
    ['A'] = {0x1c,0x36,0x63,0x63,0x7f,0x63,0x63,0x00},
    ['B'] = {0x7e,0x33,0x33,0x3e,0x33,0x33,0x7e,0x00},
    ['C'] = {0x1e,0x33,0x60,0x60,0x60,0x33,0x1e,0x00},
    ['D'] = {0x7c,0x36,0x33,0x33,0x33,0x36,0x7c,0x00},
    ['E'] = {0x7f,0x60,0x60,0x7c,0x60,0x60,0x7f,0x00},
    ['F'] = {0x7f,0x60,0x60,0x7c,0x60,0x60,0x60,0x00},
    ['G'] = {0x1e,0x33,0x60,0x67,0x63,0x33,0x1f,0x00},
    ['H'] = {0x63,0x63,0x63,0x7f,0x63,0x63,0x63,0x00},
    ['I'] = {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00},
    ['J'] = {0x0f,0x06,0x06,0x06,0x66,0x66,0x3c,0x00},
    ['K'] = {0x63,0x66,0x6c,0x78,0x6c,0x66,0x63,0x00},
    ['L'] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7f,0x00},
    ['M'] = {0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0x00},
    ['N'] = {0x63,0x73,0x7b,0x6f,0x67,0x63,0x63,0x00},
    ['O'] = {0x3e,0x63,0x63,0x63,0x63,0x63,0x3e,0x00},
    ['P'] = {0x7e,0x63,0x63,0x7e,0x60,0x60,0x60,0x00},
    ['Q'] = {0x3e,0x63,0x63,0x63,0x6b,0x66,0x3d,0x00},
    ['R'] = {0x7e,0x63,0x63,0x7e,0x6c,0x66,0x63,0x00},
    ['S'] = {0x3e,0x63,0x60,0x3e,0x03,0x63,0x3e,0x00},
    ['T'] = {0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    ['U'] = {0x63,0x63,0x63,0x63,0x63,0x63,0x3e,0x00},
    ['V'] = {0x63,0x63,0x63,0x63,0x63,0x36,0x1c,0x00},
    ['W'] = {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00},
    ['X'] = {0x63,0x63,0x36,0x1c,0x36,0x63,0x63,0x00},
    ['Y'] = {0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x00},
    ['Z'] = {0x7f,0x03,0x06,0x1c,0x30,0x60,0x7f,0x00},
    ['a'] = {0x00,0x00,0x3e,0x03,0x3f,0x63,0x3f,0x00},
    ['b'] = {0x60,0x60,0x7e,0x63,0x63,0x63,0x7e,0x00},
    ['c'] = {0x00,0x00,0x3e,0x63,0x60,0x63,0x3e,0x00},
    ['d'] = {0x03,0x03,0x3f,0x63,0x63,0x63,0x3f,0x00},
    ['e'] = {0x00,0x00,0x3e,0x63,0x7f,0x60,0x3e,0x00},
    ['f'] = {0x1e,0x33,0x30,0x7c,0x30,0x30,0x30,0x00},
    ['g'] = {0x00,0x00,0x3f,0x63,0x63,0x3f,0x03,0x3e},
    ['h'] = {0x60,0x60,0x7e,0x63,0x63,0x63,0x63,0x00},
    ['i'] = {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00},
    ['j'] = {0x06,0x00,0x0e,0x06,0x06,0x66,0x66,0x3c},
    ['k'] = {0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0x00},
    ['l'] = {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00},
    ['m'] = {0x00,0x00,0x66,0x7f,0x7f,0x6b,0x63,0x00},
    ['n'] = {0x00,0x00,0x7e,0x63,0x63,0x63,0x63,0x00},
    ['o'] = {0x00,0x00,0x3e,0x63,0x63,0x63,0x3e,0x00},
    ['p'] = {0x00,0x00,0x7e,0x63,0x63,0x7e,0x60,0x60},
    ['q'] = {0x00,0x00,0x3f,0x63,0x63,0x3f,0x03,0x03},
    ['r'] = {0x00,0x00,0x6e,0x73,0x60,0x60,0x60,0x00},
    ['s'] = {0x00,0x00,0x3f,0x60,0x3e,0x03,0x7e,0x00},
    ['t'] = {0x30,0x30,0x7c,0x30,0x30,0x33,0x1e,0x00},
    ['u'] = {0x00,0x00,0x63,0x63,0x63,0x67,0x3b,0x00},
    ['v'] = {0x00,0x00,0x63,0x63,0x63,0x36,0x1c,0x00},
    ['w'] = {0x00,0x00,0x63,0x6b,0x7f,0x7f,0x36,0x00},
    ['x'] = {0x00,0x00,0x63,0x36,0x1c,0x36,0x63,0x00},
    ['y'] = {0x00,0x00,0x63,0x63,0x63,0x3f,0x03,0x3e},
    ['z'] = {0x00,0x00,0x7f,0x06,0x1c,0x30,0x7f,0x00},
};

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {
    }
    outb(COM1, (uint8_t)c);
}

static void serial_write(const char *s) {
    while (*s != '\0') {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

void kernel_halt(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (fb == NULL || x >= fb->width || y >= fb->height) {
        return;
    }

    volatile uint8_t *base = (volatile uint8_t *)fb->address;
    volatile uint32_t *pixel = (volatile uint32_t *)(base + y * fb->pitch + x * 4);
    *pixel = color;
}

static void fb_clear(void) {
    if (fb == NULL) {
        return;
    }

    for (uint64_t y = 0; y < fb->height; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            fb_put_pixel((uint32_t)x, (uint32_t)y, bg_color);
        }
    }
}

static void fb_newline(void) {
    text_x = 0;
    text_y++;
    if ((text_y + 1) * 16 >= fb->height) {
        text_y = 0;
        fb_clear();
    }
}

static void fb_putc(char c) {
    if (fb == NULL) {
        return;
    }

    if (c == '\n') {
        fb_newline();
        return;
    }

    if ((text_x + 1) * 8 >= fb->width) {
        fb_newline();
    }

    const uint8_t *glyph = font8x8_basic[(uint8_t)c < 128 ? (uint8_t)c : (uint8_t)' '];
    uint32_t px = text_x * 8;
    uint32_t py = text_y * 16;

    for (uint32_t row = 0; row < 8; row++) {
        for (uint32_t col = 0; col < 8; col++) {
            bool set = (glyph[row] & (1u << (7 - col))) != 0;
            uint32_t color = set ? fg_color : bg_color;
            fb_put_pixel(px + col, py + row * 2, color);
            fb_put_pixel(px + col, py + row * 2 + 1, color);
        }
    }

    text_x++;
}

void console_write(const char *s) {
    serial_write(s);
    while (*s != '\0') {
        fb_putc(*s++);
    }
}

void console_write_u64(uint64_t value) {
    char buf[21];
    size_t i = sizeof(buf);
    buf[--i] = '\0';

    if (value == 0) {
        buf[--i] = '0';
    } else {
        while (value > 0 && i > 0) {
            buf[--i] = (char)('0' + (value % 10));
            value /= 10;
        }
    }

    console_write(&buf[i]);
}

void console_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (size_t i = 0; i < 16; i++) {
        buf[2 + i] = digits[(value >> ((15 - i) * 4)) & 0xf];
    }
    buf[18] = '\0';
    console_write(buf);
}

static void console_write_i64(int64_t value) {
    if (value < 0) {
        console_write("-");
        console_write_u64((uint64_t)-value);
        return;
    }
    console_write_u64((uint64_t)value);
}

static void console_write_hex_compact(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char buf[17];
    size_t i = sizeof(buf);
    buf[--i] = '\0';

    if (value == 0) {
        buf[--i] = '0';
    } else {
        while (value > 0 && i > 0) {
            buf[--i] = digits[value & 0xf];
            value >>= 4;
        }
    }

    console_write("0x");
    console_write(&buf[i]);
}

void console_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt != '\0') {
        if (*fmt != '%') {
            char c[2] = {*fmt++, '\0'};
            console_write(c);
            continue;
        }

        fmt++;
        switch (*fmt++) {
            case '%':
                console_write("%");
                break;
            case 's':
                console_write(va_arg(args, const char *));
                break;
            case 'c': {
                char c[2] = {(char)va_arg(args, int), '\0'};
                console_write(c);
                break;
            }
            case 'd':
                console_write_i64((int64_t)va_arg(args, int));
                break;
            case 'u':
                console_write_u64(va_arg(args, uint64_t));
                break;
            case 'x':
                console_write_hex_compact(va_arg(args, uint64_t));
                break;
            default:
                console_write("%?");
                break;
        }
    }

    va_end(args);
}

void kernel_panic(const char *message) {
    __asm__ volatile ("cli");
    console_write("\nPANIC: ");
    console_write(message);
    console_write("\n");
    kernel_halt();
}

static void demo_task_a(void *arg) {
    (void)arg;
    uint64_t heartbeat = 0;

    for (;;) {
        console_printf("task A heartbeat %u\n", heartbeat++);
        for (uint64_t i = 0; i < 10; i++) {
            __asm__ volatile ("hlt");
        }
    }
}

static void demo_task_b(void *arg) {
    (void)arg;
    uint64_t heartbeat = 0;

    for (;;) {
        console_printf("task B heartbeat %u\n", heartbeat++);
        for (uint64_t i = 0; i < 10; i++) {
            __asm__ volatile ("hlt");
        }
    }
}

void _start(void) {
    serial_init();
    serial_write("TangPingOS serial online\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        kernel_halt();
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        kernel_panic("no framebuffer from bootloader");
    }

    fb = framebuffer_request.response->framebuffers[0];
    fb_clear();

    console_write("TangPingOS booted\n");
    console_write("UEFI framebuffer OK\n");
    console_write("framebuffer: ");
    console_write_u64(fb->width);
    console_write("x");
    console_write_u64(fb->height);
    console_write(" pitch ");
    console_write_u64(fb->pitch);
    console_write(" bpp ");
    console_write_u64(fb->bpp);
    console_write("\n");

    if (memmap_request.response == NULL) {
        kernel_panic("no memory map from bootloader");
    }

    console_write("memory map loaded: ");
    console_write_u64(memmap_request.response->entry_count);
    console_write(" entries\n");

    for (uint64_t i = 0; i < memmap_request.response->entry_count && i < 6; i++) {
        struct limine_memmap_entry *entry = memmap_request.response->entries[i];
        console_write("mem ");
        console_write_u64(i);
        console_write(": base ");
        console_write_hex(entry->base);
        console_write(" len ");
        console_write_hex(entry->length);
        console_write(" type ");
        console_write_u64(entry->type);
        console_write("\n");
    }

    if (hhdm_request.response == NULL) {
        kernel_panic("no HHDM from bootloader");
    }

    memory_init(memmap_request.response, hhdm_request.response->offset);
    memory_self_test();

    x86_64_interrupts_init();
    scheduler_init();
    if (scheduler_create_task("task-a", demo_task_a, NULL) != 0 ||
        scheduler_create_task("task-b", demo_task_b, NULL) != 0) {
        kernel_panic("failed to create demo tasks");
    }
    user_init_from_modules(module_request.response);
    scheduler_dump_tasks();
    x86_64_interrupts_enable();

#ifdef TANGPINGOS_TEST_EXCEPTION
    console_write("triggering test exception: invalid opcode\n");
    __asm__ volatile ("ud2");
#endif

#ifdef TANGPINGOS_TEST_PAGE_FAULT
    console_write("triggering test page fault\n");
    *(volatile uint64_t *)0xffff8000dead0000ULL = 0x55;
#endif

    console_write("kernel idle loop entered\n");
    kernel_halt();
}
