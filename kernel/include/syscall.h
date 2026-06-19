#ifndef TANGPINGOS_SYSCALL_H
#define TANGPINGOS_SYSCALL_H

#include <x86_64/interrupt_frame.h>

struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame);

#endif
