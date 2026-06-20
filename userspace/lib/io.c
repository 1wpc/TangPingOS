#include "tpos.h"

uint64_t tpos_strlen(const char *s) {
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

uint64_t tpos_strcpy(char *dst, const char *src) {
    uint64_t len = 0;
    while (src[len] != '\0') {
        dst[len] = src[len];
        len++;
    }
    dst[len] = '\0';
    return len;
}

uint64_t tpos_u64_to_decimal(uint64_t value, char *buffer) {
    char tmp[20];
    uint64_t count = 0;

    if (value == 0) {
        buffer[0] = '0';
        return 1;
    }

    while (value > 0) {
        tmp[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    for (uint64_t i = 0; i < count; i++) {
        buffer[i] = tmp[count - i - 1];
    }

    return count;
}

uint64_t tpos_u64_to_hex(uint64_t value, char *buffer) {
    static const char digits[] = "0123456789abcdef";
    uint64_t started = 0;
    uint64_t count = 0;

    buffer[count++] = '0';
    buffer[count++] = 'x';

    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((value >> shift) & 0xf);
        if (nibble != 0 || started || shift == 0) {
            buffer[count++] = digits[nibble];
            started = 1;
        }
    }

    return count;
}

void tpos_write_literal(const char *message, uint64_t length) {
    tpos_write(STDOUT_FD, message, length);
}

void tpos_write_cstr(const char *s) {
    tpos_write(STDOUT_FD, s, tpos_strlen(s));
}

void tpos_write_cstr_limited(const char *s, uint64_t max_len) {
    uint64_t len = 0;
    while (len < max_len && s[len] != '\0') {
        len++;
    }
    tpos_write(STDOUT_FD, s, len);
}

void tpos_write_u64_decimal(uint64_t value) {
    char buffer[20];
    uint64_t len = tpos_u64_to_decimal(value, buffer);
    tpos_write(STDOUT_FD, buffer, len);
}

void tpos_write_u64_hex(uint64_t value) {
    char buffer[18];
    uint64_t len = tpos_u64_to_hex(value, buffer);
    tpos_write(STDOUT_FD, buffer, len);
}

void tpos_write_hex_byte(uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    char out[2];
    out[0] = digits[(value >> 4) & 0xf];
    out[1] = digits[value & 0xf];
    tpos_write(STDOUT_FD, out, sizeof(out));
}
