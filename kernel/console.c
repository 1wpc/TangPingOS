#include <console.h>
#include <limine.h>
#include <memory.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <x86_64/io.h>

#define COM1 0x3f8
#define FB_CHAR_WIDTH 8
#define FB_CHAR_HEIGHT 16
#define FB_WC_BASE 0xffffffffa0000000ULL
#define TEXT_MAX_COLS 512
#define TEXT_MAX_ROWS 256

static struct limine_framebuffer *fb;
static uint32_t text_x;
static uint32_t text_y;
static uint32_t text_cols;
static uint32_t text_rows;
static uint32_t fg_color = 0x00e6edf3;
static uint32_t bg_color = 0x000b1020;
static int fb_cursor_visible;
static int fb_output_enabled;
static int screen_output_suppressed;
static int fb_dirty_all;
static char text_cells[TEXT_MAX_ROWS][TEXT_MAX_COLS];
static uint8_t text_dirty[TEXT_MAX_ROWS];

static void fb_show_cursor(void);
static void fb_hide_cursor(void);

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

void console_init_serial(void) {
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

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (fb == NULL || x >= fb->width || y >= fb->height) {
        return;
    }

    uint8_t *base = (uint8_t *)fb->address;
    uint32_t *pixel = (uint32_t *)(base + y * fb->pitch + x * 4);
    *pixel = color;
}

static void fb_fill_pixels(uint8_t *dst, uint64_t pixel_count, uint32_t color) {
    uint64_t i = 0;
    uint64_t packed = ((uint64_t)color << 32) | color;
    uint64_t pairs = pixel_count / 2;
    uint64_t *wide = (uint64_t *)dst;

    for (; i < pairs; i++) {
        wide[i] = packed;
    }
    if ((pixel_count & 1) != 0) {
        ((uint32_t *)dst)[pixel_count - 1] = color;
    }
}

static void fb_clear(void) {
    if (fb == NULL) {
        return;
    }

    uint8_t *base = (uint8_t *)fb->address;
    for (uint64_t y = 0; y < fb->height; y++) {
        fb_fill_pixels(base + y * fb->pitch, fb->width, bg_color);
    }
}

static void text_mark_row_dirty(uint32_t row) {
    if (row < text_rows) {
        text_dirty[row] = 1;
    }
}

static void text_mark_all_dirty(void) {
    for (uint32_t row = 0; row < text_rows; row++) {
        text_dirty[row] = 1;
    }
    fb_dirty_all = 1;
}

static void text_clear_row(uint32_t row) {
    if (row >= text_rows) {
        return;
    }
    for (uint32_t col = 0; col < text_cols; col++) {
        text_cells[row][col] = ' ';
    }
    text_mark_row_dirty(row);
}

static void text_clear(void) {
    for (uint32_t row = 0; row < text_rows; row++) {
        text_clear_row(row);
    }
    text_x = 0;
    text_y = 0;
    text_mark_all_dirty();
}

void console_clear_framebuffer(void) {
    fb_hide_cursor();
    text_clear();
    fb_clear();
    fb_cursor_visible = 0;
    fb_show_cursor();
}

void console_set_framebuffer_enabled(int enabled) {
    fb_output_enabled = enabled != 0;
    if (fb_output_enabled) {
        fb_show_cursor();
    } else {
        fb_hide_cursor();
    }
}

void console_set_screen_output_suppressed(int suppressed) {
    screen_output_suppressed = suppressed != 0;
}

void console_enable_framebuffer_write_combining(void) {
    if (fb == NULL) {
        return;
    }

    uint64_t original = (uint64_t)fb->address;
    uint64_t offset = original & (PAGE_SIZE - 1);
    uint64_t virt_start = original & ~(PAGE_SIZE - 1);
    uint64_t length = fb->pitch * fb->height + offset;
    uint64_t page_count = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t phys_first = 0;

    if (vmm_translate_kernel_addr(virt_start, &phys_first) != 0) {
        console_write("[warn] framebuffer WC remap skipped: translate failed\n");
        return;
    }

    uint64_t wc_base = FB_WC_BASE;
    for (uint64_t i = 0; i < page_count; i++) {
        uint64_t phys = phys_first + i * PAGE_SIZE;
        uint64_t virt = wc_base + i * PAGE_SIZE;
        if (vmm_map_page(virt, phys,
                         VMM_FLAG_WRITABLE | VMM_FLAG_NO_EXEC | VMM_FLAG_WRITE_COMBINING) != 0) {
            console_write("[warn] framebuffer WC remap skipped: map failed\n");
            return;
        }
    }

    fb->address = (void *)(wc_base + offset);
    console_write("[info] framebuffer remapped write-combining\n");
}

static uint32_t fb_cols(void) {
    return text_cols;
}

static uint32_t fb_rows(void) {
    return text_rows;
}

static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (fb == NULL) {
        return;
    }

    if (x >= fb->width || y >= fb->height) {
        return;
    }
    if (x + width > fb->width) {
        width = (uint32_t)fb->width - x;
    }
    if (y + height > fb->height) {
        height = (uint32_t)fb->height - y;
    }

    uint8_t *base = (uint8_t *)fb->address;
    for (uint32_t row = 0; row < height; row++) {
        fb_fill_pixels(base + (y + row) * fb->pitch + x * 4, width, color);
    }
}

static void fb_draw_char_at(uint32_t col, uint32_t row, char c) {
    if (fb == NULL || col >= text_cols || row >= text_rows) {
        return;
    }

    const uint8_t *glyph = font8x8_basic[(uint8_t)c < 128 ? (uint8_t)c : (uint8_t)' '];
    uint32_t px = col * FB_CHAR_WIDTH;
    uint32_t py = row * FB_CHAR_HEIGHT;

    for (uint32_t glyph_row = 0; glyph_row < 8; glyph_row++) {
        for (uint32_t glyph_col = 0; glyph_col < 8; glyph_col++) {
            bool set = (glyph[glyph_row] & (1u << (7 - glyph_col))) != 0;
            uint32_t color = set ? fg_color : bg_color;
            fb_put_pixel(px + glyph_col, py + glyph_row * 2, color);
            fb_put_pixel(px + glyph_col, py + glyph_row * 2 + 1, color);
        }
    }
}

static void fb_redraw_row(uint32_t row) {
    if (fb == NULL || row >= text_rows) {
        return;
    }

    fb_fill_rect(0, row * FB_CHAR_HEIGHT,
                 text_cols * FB_CHAR_WIDTH, FB_CHAR_HEIGHT, bg_color);
    for (uint32_t col = 0; col < text_cols; col++) {
        char c = text_cells[row][col];
        if (c != ' ') {
            fb_draw_char_at(col, row, c);
        }
    }
}

static void fb_flush_dirty_rows(void) {
    if (fb == NULL || !fb_output_enabled) {
        return;
    }

    if (fb_dirty_all) {
        fb_clear();
        for (uint32_t row = 0; row < text_rows; row++) {
            for (uint32_t col = 0; col < text_cols; col++) {
                char c = text_cells[row][col];
                if (c != ' ') {
                    fb_draw_char_at(col, row, c);
                }
            }
            text_dirty[row] = 0;
        }
        fb_dirty_all = 0;
        return;
    }

    for (uint32_t row = 0; row < text_rows; row++) {
        if (text_dirty[row]) {
            fb_redraw_row(row);
            text_dirty[row] = 0;
        }
    }
}

static void fb_draw_cursor(uint32_t color) {
    if (fb == NULL || fb_cols() == 0 || fb_rows() == 0) {
        return;
    }

    uint32_t col = text_x >= fb_cols() ? fb_cols() - 1 : text_x;
    uint32_t row = text_y >= fb_rows() ? fb_rows() - 1 : text_y;
    uint32_t y = row * FB_CHAR_HEIGHT + FB_CHAR_HEIGHT - 2;
    fb_fill_rect(col * FB_CHAR_WIDTH, y, FB_CHAR_WIDTH, 2, color);
}

static void fb_show_cursor(void) {
    if (!fb_cursor_visible) {
        fb_draw_cursor(fg_color);
        fb_cursor_visible = 1;
    }
}

static void fb_hide_cursor(void) {
    if (fb_cursor_visible) {
        fb_draw_cursor(bg_color);
        fb_cursor_visible = 0;
    }
}

static void fb_newline(void) {
    text_x = 0;
    text_y++;
    if (text_y >= text_rows) {
        if (text_rows == 0) {
            text_y = 0;
            return;
        }
        for (uint32_t row = 1; row < text_rows; row++) {
            for (uint32_t col = 0; col < text_cols; col++) {
                text_cells[row - 1][col] = text_cells[row][col];
            }
        }
        for (uint32_t col = 0; col < text_cols; col++) {
            text_cells[text_rows - 1][col] = ' ';
        }
        text_y = text_rows - 1;
        text_mark_all_dirty();
    }
}

static void fb_backspace(void) {
    if (text_x > 0) {
        text_x--;
    } else if (text_y > 0) {
        text_y--;
        text_x = fb_cols() > 0 ? fb_cols() - 1 : 0;
    }

    if (text_y < text_rows && text_x < text_cols) {
        text_cells[text_y][text_x] = ' ';
        text_mark_row_dirty(text_y);
    }
}

static void fb_putc(char c) {
    if (text_cols == 0 || text_rows == 0) {
        return;
    }

    if (c == '\n') {
        fb_newline();
        return;
    }

    if (c == '\b') {
        fb_backspace();
        return;
    }

    if (text_x >= text_cols) {
        fb_newline();
    }

    text_cells[text_y][text_x] = c;
    text_mark_row_dirty(text_y);

    text_x++;
    if (text_x >= text_cols) {
        fb_newline();
    }
}

void console_write_n(const char *s, uint64_t len) {
    if (s == NULL) {
        return;
    }

    int old_cursor_visible = fb_cursor_visible;
    if (!screen_output_suppressed) {
        fb_hide_cursor();
    }

    for (uint64_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\f') {
            serial_write("\n");
            console_set_framebuffer_enabled(1);
            console_clear_framebuffer();
            old_cursor_visible = 1;
            continue;
        }

        if (c == '\n') {
            serial_putc('\r');
        }
        serial_putc(c);

        if (screen_output_suppressed || !fb_output_enabled) {
            continue;
        }

        fb_putc(c);
    }

    if (!screen_output_suppressed) {
        fb_flush_dirty_rows();
    }
    if (!screen_output_suppressed && (old_cursor_visible || fb_output_enabled)) {
        fb_show_cursor();
    }
}

void console_write(const char *s) {
    uint64_t len = 0;
    if (s == NULL) {
        return;
    }
    while (s[len] != '\0') {
        len++;
    }
    console_write_n(s, len);
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

void console_vprintf(const char *fmt, va_list args) {
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
}

void console_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    console_vprintf(fmt, args);
    va_end(args);
}

void kernel_panic(const char *message) {
    __asm__ volatile ("cli");
    console_set_framebuffer_enabled(1);
    console_write("\nPANIC: ");
    console_write(message);
    console_write("\n");
    kernel_halt();
}

void console_init_framebuffer(struct limine_framebuffer *framebuffer) {
    fb = framebuffer;
    text_cols = 0;
    text_rows = 0;
    if (fb != NULL) {
        text_cols = min_u32((uint32_t)(fb->width / FB_CHAR_WIDTH), TEXT_MAX_COLS);
        text_rows = min_u32((uint32_t)(fb->height / FB_CHAR_HEIGHT), TEXT_MAX_ROWS);
    }
    text_x = 0;
    text_y = 0;
    fb_cursor_visible = 0;
    fb_output_enabled = 1;
    screen_output_suppressed = 0;
    fb_dirty_all = 0;
    text_clear();
    fb_clear();
}
