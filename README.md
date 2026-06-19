# TangPingOS

TangPingOS is a tiny x86_64 operating system project booted through UEFI with
Limine. The early target is intentionally small: build a freestanding kernel,
boot it in QEMU, print hardware information, run a tiny user program, and grow
features one verified step at a time.

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
- User ELF loading through a Limine module: `userspace/init/init.c` is built as
  `init.elf`, copied into the ISO, preloaded by Limine, parsed by the kernel,
  and mapped into a fresh user address space as the `init` process.
- Initrd loading through a second Limine module: files in `initrd/` are packed
  into `build/initrd.tar`, parsed by the kernel as a ustar archive, and exposed
  to user mode through a minimal `read_file` syscall.
- A thin readonly VFS dispatch layer. The syscall layer talks to VFS, while
  initrd registers itself as the first filesystem backend. Future disk-backed
  filesystems can plug into the same path.
- User pointer validation and safe copy helpers for syscall buffers. Syscalls
  now copy strings and I/O buffers through page-table-checked
  `copy_from_user`/`copy_to_user` helpers instead of blindly dereferencing
  user-provided addresses.

Current user syscalls:

```text
rax=1 write(buf=rdi, len=rsi) -> rax=len
rax=2 exit(status=rdi)        -> does not return
rax=3 getpid()                -> rax=pid
rax=4 yield()                 -> rax=0, scheduler may switch tasks
rax=5 sleep_ticks(ticks=rdi)  -> rax=0, task sleeps until that PIT tick
rax=6 brk(new_break=rdi)      -> rax=current/new program break
rax=7 read_file(path=rdi,
                offset=rsi,
                buf=rdx,
                len=rcx)      -> rax=bytes read, or -1 if missing
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
make test-user-fault
```

`make test-exception` rebuilds the ISO with an intentional invalid-opcode
exception after interrupt setup. Run it with `make run` afterwards to verify
the exception handler path.

`make test-page-fault` does the same for an intentional page fault and prints
the faulting CR2 address.

`make test-user-fault` rebuilds only `init.elf` with an intentional user-mode
null write. The expected behavior is that TangPingOS reports the page fault,
kills the user process, and keeps the kernel scheduler alive.

Build outputs:

```text
build/kernel.elf
build/init.elf
build/initrd.tar
build/TangPingOS.iso
```

`init.elf` and `initrd.tar` are still loaded by Limine, not by a TangPingOS disk
driver. The important step is that user mode now asks VFS for a file by path.
Right now VFS forwards that request to the in-memory initrd backend. Later it
can forward the same API to a real disk filesystem.
