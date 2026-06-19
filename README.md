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
  mapping, page-fault CR2 reporting, and a simple reusable kernel heap.
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
- Per-process file descriptor table for user tasks. User mode can now
  `open()`, `read()`, and `close()` readonly files through fd values while VFS
  still uses initrd as the first backend.
- Root directory enumeration through a minimal `getdents()` syscall, so user
  mode can discover initrd files instead of knowing every filename upfront.
- A tiny non-interactive init command runner. `init.elf` now has builtin
  `ls`/`cat`-style command functions that exercise `getdents`, `open`, `read`,
  and `close`, giving later keyboard/TTY shell work a cleaner userland shape.
- PS/2 keyboard input plumbing: IRQ1 translates simple scancodes into
  characters, stores them in a kernel input buffer, and exposes them to user
  mode through `read(0, ...)`.
- Blocking stdin reads: if `read(0, ...)` has no available keyboard data, the
  current user task sleeps until IRQ1 wakes it after the next keypress.
- A tiny TTY device layer: keyboard input and console output are now routed
  through a kernel TTY abstraction, and `/dev/tty` can be opened by user mode as
  the shell input device.
- A minimal `devfs` backend registered with VFS. Root directory enumeration now
  includes `/dev`, and `ls /dev` exposes the `tty` device node.
- `dup2(oldfd, newfd)` for basic descriptor redirection. Standard fds 0/1/2
  default to TTY behavior but can now be overwritten by duplicated descriptors.
- Per-process open-file objects with reference counts. Duplicated descriptors
  now share the same file offset instead of copying it.
- Quiet boot: the old demo heartbeat tasks are no longer created by default, so
  serial output stays focused on kernel and init activity.
- User pointer validation and safe copy helpers for syscall buffers. Syscalls
  now copy strings and I/O buffers through page-table-checked
  `copy_from_user`/`copy_to_user` helpers instead of blindly dereferencing
  user-provided addresses.
- User address-space cleanup on process exit or user-mode fault. TangPingOS now
  switches away from the dying process before freeing its user pages and lower
  half page tables. Kernel stacks are queued for delayed reclaim so the kernel
  never frees the stack that the current interrupt handler is still executing
  on.
- Reusable kernel heap blocks. `kfree()` now marks blocks free and coalesces
  adjacent free blocks, so freed kernel allocations can be reused instead of
  permanently advancing the bump pointer.

Current user syscalls:

```text
rax=1 write(fd=rdi,
            buf=rsi,
            len=rdx)           -> rax=bytes written, or -1
rax=2 exit(status=rdi)        -> does not return
rax=3 getpid()                -> rax=pid
rax=4 yield()                 -> rax=0, scheduler may switch tasks
rax=5 sleep_ticks(ticks=rdi)  -> rax=0, task sleeps until that PIT tick
rax=6 brk(new_break=rdi)      -> rax=current/new program break
rax=7 read_file(path=rdi,
                offset=rsi,
                buf=rdx,
                len=rcx)      -> rax=bytes read, or -1 if missing
rax=8 open(path=rdi)          -> rax=fd, or -1 if missing
rax=9 read(fd=rdi,
           buf=rsi,
           len=rdx)           -> rax=bytes read, 0 if empty/EOF, or -1
rax=10 close(fd=rdi)          -> rax=0, or -1
rax=11 getdents(path=rdi,
                index=rsi,
                dirent=rdx,
                len=rcx)      -> rax=1 entry, 0 EOF, or -1
rax=12 write_fd(fd=rdi,
                buf=rsi,
                len=rdx)      -> compatibility alias for write
rax=13 dup2(oldfd=rdi,
            newfd=rsi)        -> rax=newfd, or -1
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
driver. The important step is that user mode now opens a path into a per-process
file descriptor, then reads through that fd. Right now VFS forwards those reads
to the in-memory initrd backend. Later it can forward the same API to a real
disk filesystem.
