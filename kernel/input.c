#include <input.h>
#include <stdint.h>

#define INPUT_BUFFER_SIZE 256

static char input_buffer[INPUT_BUFFER_SIZE];
static volatile uint64_t input_read_pos;
static volatile uint64_t input_write_pos;

void input_push_key(char c) {
    uint64_t next = (input_write_pos + 1) % INPUT_BUFFER_SIZE;
    if (next == input_read_pos) {
        return;
    }

    input_buffer[input_write_pos] = c;
    input_write_pos = next;
}

uint64_t input_read(char *buffer, uint64_t len) {
    uint64_t count = 0;

    while (count < len && input_read_pos != input_write_pos) {
        buffer[count++] = input_buffer[input_read_pos];
        input_read_pos = (input_read_pos + 1) % INPUT_BUFFER_SIZE;
    }

    return count;
}
