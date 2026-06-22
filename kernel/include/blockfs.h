#ifndef TANGPINGOS_BLOCKFS_H
#define TANGPINGOS_BLOCKFS_H

#include <stdint.h>

int blockfs_mount(uint64_t block_index, const char *mount_path);

#endif
