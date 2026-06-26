#ifndef TANGPINGOS_PARTITION_H
#define TANGPINGOS_PARTITION_H

#include <stdint.h>

void partition_init(void);
void partition_scan_device(uint64_t block_index);

#endif
