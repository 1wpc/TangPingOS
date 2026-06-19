#ifndef TANGPINGOS_USERCOPY_H
#define TANGPINGOS_USERCOPY_H

#include <stdint.h>

int usercopy_validate_range(const void *user_ptr, uint64_t len, int write);
int copy_from_user(void *dst, const void *user_src, uint64_t len);
int copy_to_user(void *user_dst, const void *src, uint64_t len);
int copy_string_from_user(char *dst, const char *user_src, uint64_t max_len);

#endif
