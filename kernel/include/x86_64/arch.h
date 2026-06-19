#ifndef TANGPINGOS_X86_64_ARCH_H
#define TANGPINGOS_X86_64_ARCH_H

#include <stdint.h>

void x86_64_interrupts_init(void);
void x86_64_interrupts_enable(void);
void x86_64_set_kernel_stack(uint64_t rsp0);

#endif
