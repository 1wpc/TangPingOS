# TangPingOS Manual

This document is both the user manual and the user-space developer guide for
TangPingOS.

## Quick Start

Build and boot in QEMU:

```sh
make clean && make iso
make run
```

After boot, TangPingOS runs a startup self-test and enters the shell:

```text
TangPingOS:/>
```

The prompt format is `TangPingOS:<cwd>>`. For example, `TangPingOS:/dev>` means
the current working directory is `/dev`.

On real hardware the framebuffer display is intentionally quiet during boot:
early status is shown briefly, detailed kernel/userland startup logs continue on
the serial console, and the framebuffer is cleared when the interactive shell is
ready. This avoids very slow full-screen scrolling on high-resolution UEFI GOP
framebuffers.

## Boot Flow

Current startup is:

```text
Limine -> kernel.elf -> init.elf -> /bin/shell.elf -> interactive shell
```

`kernel.elf`, `init.elf`, and `initrd.tar` are loaded by Limine. The kernel
parses `initrd.tar` as a ustar archive and exposes it through VFS. `init.elf`
opens `/dev/tty`, binds standard fds, and spawns `/bin/shell.elf`.

## Files And Devices

Current paths:

```text
/                 root directory
/dev              device directory
/dev/tty          keyboard/console TTY device
/motd.txt         readonly initrd file
/hello.txt        readonly initrd file
/bin              readonly program directory from initrd
/bin/shell.elf    interactive shell
/bin/hello.elf    demo user program
/bin/ls.elf       standalone directory listing program
/bin/cat.elf      standalone file printing program
```

Files created by shell commands live in ramfs. They disappear after reboot
because TangPingOS does not write to disk yet.

## Shell Commands

| Command | Example | Function |
| --- | --- | --- |
| `help` | `help` | Prints the command list. |
| `pwd` | `pwd` | Prints the current working directory. |
| `cd` | `cd /dev` | Changes directory. `cd` with no argument returns to `/`; `.` and `..` are supported. |
| `ls` | `ls /bin` | Builtin directory listing. With no argument, lists the current directory. |
| `cat` | `cat /hello.txt` | Builtin file printer. |
| `echo` | `echo hello` | Prints text after the command. |
| `clear` | `clear` | Scrolls blank lines to visually clear the console. |
| `touch` | `touch note.txt` | Creates an empty ramfs file. |
| `write` | `write note.txt hello` | Creates or truncates a ramfs file and writes text. |
| `append` | `append note.txt world` | Creates a ramfs file if needed and appends text. |
| `rm` | `rm note.txt` | Removes a writable ramfs file. Readonly initrd files and device nodes are protected. |
| `stat` | `stat /bin/hello.elf` | Prints a path's type and size. |
| `cp` | `cp /hello.txt copy.txt` | Copies file contents to a new or truncated ramfs file. |
| `mv` | `mv copy.txt moved.txt` | Copies a writable source to a destination, then removes the source. |
| `hexdump` | `hexdump moved.txt` | Prints file bytes in hexadecimal. |
| `edit` | `edit note.txt` | Opens a simple line editor. Type lines, then `.save`. |
| `run` | `run /bin/hello.elf one two` | Starts an ELF program as a separate user task. |
| `ps` | `ps` | Lists tasks with PID, state, switch count, exit status, and name. |
| `mem` | `mem` | Prints physical page size, total pages, used pages, and free pages. |
| `uptime` | `uptime` | Prints scheduler ticks and whole seconds. |
| `sysinfo` | `sysinfo` | Prints CPU, framebuffer, memmap, timer, uptime, and memory summary. |

Standalone programs can be launched with `run`:

```text
run /bin/hello.elf one two
run /bin/ls.elf /bin
run /bin/cat.elf /hello.txt
```

Current `run` argument parsing is simple: arguments are split on spaces; quotes,
escaping, and environment variables are not implemented.

## Example Session

```text
help
ls /
ls /bin
cat /motd.txt
write note.txt TangPingOS
append note.txt works
cat note.txt
cp note.txt copy.txt
mv copy.txt moved.txt
hexdump moved.txt
edit draft.txt
stat note.txt
ps
mem
uptime
sysinfo
run /bin/ls.elf /bin
run /bin/cat.elf /hello.txt
rm note.txt
```

## Writing User Programs

User programs are freestanding x86_64 ELF binaries linked with
`linker/user.ld`. They run in Ring 3 and talk to the kernel through `int 0x80`
syscalls. Use the shared support library in `userspace/lib`.

Minimal program:

```c
#include "tpos.h"

__attribute__((noreturn))
void _start(uint64_t argc, char **argv) {
    tpos_write_cstr("hello from user space\n");
    tpos_exit(0);
}
```

Arguments:

- `argc` is the number of arguments.
- `argv[0]` is the program path.
- Extra words passed through `run /path.elf arg1 arg2` become `argv[1]`,
  `argv[2]`, and so on.

Add a new program today by following the existing pattern:

1. Create `userspace/name/name.c`.
2. Include `tpos.h`.
3. Implement `_start(uint64_t argc, char **argv)`.
4. Add a build variable, object list, target, and initrd copy step in
   `Makefile`.
5. Rebuild with `make clean && make iso`.
6. Run it from the shell with `run /bin/name.elf`.

This build wiring is still manual. A later cleanup should make `/bin/*.elf`
registration more automatic.

## User-Space Library

Header:

```c
#include "tpos.h"
```

Common constants:

```text
STDIN_FD  = 0
STDOUT_FD = 1
STDERR_FD = 2

OPEN_CREATE = 1
OPEN_TRUNC  = 2
OPEN_APPEND = 4

SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2
```

Important types:

```c
struct dirent;
struct task_info;
struct meminfo;
struct system_info;
```

Common helpers:

```c
uint64_t tpos_write(uint64_t fd, const char *buffer, uint64_t length);
void tpos_write_cstr(const char *s);
void tpos_write_literal(const char *message, uint64_t length);
void tpos_write_u64_decimal(uint64_t value);
void tpos_write_u64_hex(uint64_t value);
uint64_t tpos_strlen(const char *s);
uint64_t tpos_strcpy(char *dst, const char *src);
__attribute__((noreturn)) void tpos_exit(uint64_t status);
```

Filesystem and process helpers:

```c
uint64_t tpos_open(const char *path);
uint64_t tpos_open_flags(const char *path, uint64_t flags);
uint64_t tpos_read(uint64_t fd, void *buffer, uint64_t length);
uint64_t tpos_close(uint64_t fd);
uint64_t tpos_getdents(const char *path, uint64_t index, struct dirent *dirent);
uint64_t tpos_dup2(uint64_t old_fd, uint64_t new_fd);
uint64_t tpos_lseek(uint64_t fd, int64_t offset, uint64_t whence);
uint64_t tpos_unlink(const char *path);
uint64_t tpos_spawn(const char *path, const char *args);
```

System helpers:

```c
uint64_t tpos_getpid(void);
void tpos_yield(void);
void tpos_sleep_ticks(uint64_t ticks);
uint64_t tpos_brk(uint64_t new_break);
void *tpos_sbrk(uint64_t increment);
uint64_t tpos_task_info(uint64_t index, struct task_info *info);
uint64_t tpos_meminfo(struct meminfo *info);
uint64_t tpos_system_info(struct system_info *info);
uint64_t tpos_uptime(void);
```

## Syscall ABI

TangPingOS currently uses `int 0x80`.

```text
rax = syscall number
rdi = arg0
rsi = arg1
rdx = arg2
rcx = arg3
rax = return value
```

Current syscall numbers:

```text
1  write(fd, buf, len)
2  exit(status)
3  getpid()
4  yield()
5  sleep_ticks(ticks)
6  brk(new_break)
7  read_file(path, offset, buf, len)
8  open(path, flags)
9  read(fd, buf, len)
10 close(fd)
11 getdents(path, index, dirent, len)
12 write_fd(fd, buf, len)
13 dup2(oldfd, newfd)
14 write_file(path, offset, buf, len)
15 lseek(fd, offset, whence)
16 unlink(path)
17 spawn(path, args)
18 task_info(index, buf, len)
19 meminfo(buf, len)
20 sysinfo(buf, len)
21 uptime()
```

Use `tpos.h` wrappers instead of issuing raw syscalls unless you are
deliberately testing the ABI.

## Test Targets

```sh
make test-exception
make test-page-fault
make test-user-fault
make test-user-programs
```

- `test-exception` builds a kernel that triggers an invalid-opcode exception.
- `test-page-fault` builds a kernel that triggers a page fault.
- `test-user-fault` builds user programs so `shell.elf` triggers a user-mode
  null write; the kernel should kill only that process.
- `test-user-programs` makes the shell startup self-test spawn `/bin/ls.elf`
  and `/bin/cat.elf`.

After any test target, rebuild normal output with:

```sh
make clean && make iso
```

## Real Hardware Boot Notes

TangPingOS can theoretically boot on a real x86_64 UEFI PC from a USB stick,
but QEMU is still the primary supported environment.

High-level steps:

1. Build the ISO with `make clean && make iso`.
2. Write `build/TangPingOS.iso` to a USB stick as a raw disk image.
3. Boot the target PC's UEFI boot menu and choose the USB device.
4. Disable Secure Boot if the firmware refuses to boot unsigned EFI binaries.

On macOS, the dangerous raw-write flow looks like this:

```sh
diskutil list
diskutil unmountDisk /dev/diskN
sudo dd if=build/TangPingOS.iso of=/dev/rdiskN bs=4m status=progress
sync
diskutil eject /dev/diskN
```

Replace `diskN` with the USB disk, not an internal disk. Choosing the wrong disk
will destroy data.

Expected constraints on real hardware:

- The machine must be x86_64. Apple Silicon Macs cannot boot this OS directly.
- UEFI GOP framebuffer is expected; this is how the screen output is obtained.
- The framebuffer console is simple and CPU-rendered. TangPingOS reduces real
  hardware display cost in three layers: verbose boot logs are kept off the
  framebuffer until the interactive shell is ready, the framebuffer is remapped
  with x86 PAT write-combining for faster sequential pixel writes, and console
  text is stored in a shadow character buffer so scrolling updates RAM first and
  only redraws dirty rows to the framebuffer. Normal kernel info logs are kept
  on the serial console so they do not overwrite the interactive screen.
- Secure Boot is not supported.
- Disk access is not implemented, so the OS only uses the initrd packed into the
  boot image after startup.
- Keyboard input currently depends on simple PS/2-style input. Many modern
  laptops use USB/xHCI or firmware translation, so keyboard behavior on real
  hardware is uncertain until TangPingOS has USB HID support.
- Serial logs visible in QEMU will usually not be visible on a normal laptop
  unless it has a usable serial console.

For a live demo today, QEMU is reliable. For a USB demo on a real PC, test on the
exact hardware in advance and bring a fallback QEMU demo.

## Current Limitations

- No persistent disk filesystem.
- No USB stack.
- No full keyboard layout, modifiers, command history, or tab completion.
- No `wait`, pipes, redirection, signals, or environment variables.
- `edit` is a line editor, not a full-screen editor.
- `/bin` program registration in `Makefile` is still manual.
- Shell builtins and standalone programs currently coexist; builtins have not
  yet been fully moved into `/bin`.
