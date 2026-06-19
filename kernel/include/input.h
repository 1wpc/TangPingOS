#ifndef TANGPINGOS_INPUT_H
#define TANGPINGOS_INPUT_H

#include <stdint.h>

void input_push_key(char c);
uint64_t input_read(char *buffer, uint64_t len);

#endif
