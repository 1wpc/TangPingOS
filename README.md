# voidOS

voidOS is a tiny x86_64 operating system project booted through UEFI with
Limine. The v0 target is intentionally small: build a freestanding kernel,
boot it in QEMU, print framebuffer and memory-map information, and halt.

Current kernel features:

- UEFI boot through Limine on x86_64.
- Framebuffer and serial console output.
- GDT, IDT, CPU exception reporting, PIC, PIT timer IRQ, and keyboard IRQ.
- Limine HHDM support, bitmap physical page allocation, current page-table
  mapping, page-fault CR2 reporting, and a simple bump-style kernel heap.
- Preemptive kernel tasks with PIT-driven round-robin scheduling.
- Ring 3 user-mode demo task with TSS-backed kernel entry and `int 0x80`
  syscall dispatch.
- Per-process user address spaces: user tasks get their own PML4/CR3 while
  sharing the kernel half of the address map.
- Minimal process table fields in the scheduler: PID, state, CR3, kernel stack,
  and exit status.

Current user syscalls:

```text
rax=1 write(buf=rdi, len=rsi) -> rax=len
rax=2 exit(status=rdi)        -> does not return
```

## Requirements

Install the toolchain on macOS:

```sh
brew install llvm lld qemu nasm xorriso mtools limine
```

## Build and Run

```sh
make
make run
```

Useful targets:

```sh
make kernel
make iso
make clean
make test-exception
make test-page-fault
```

`make test-exception` rebuilds the ISO with an intentional invalid-opcode
exception after interrupt setup. Run it with `make run` afterwards to verify
the exception handler path.

`make test-page-fault` does the same for an intentional page fault and prints
the faulting CR2 address.

Build outputs:

```text
build/kernel.elf
build/voidOS.iso
```
