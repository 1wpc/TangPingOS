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

TangPingOS also has a first block-device layer. `ramblk0` is an in-memory
disk-like device with 64 sectors of 512 bytes each. In QEMU, `make run` also
creates and attaches `build/disk.img` through virtio-blk; the kernel registers
that real virtual disk as `vd0` and parses its MBR primary partitions as
`vd0p1` and `vd0p2`. The first partition is mounted through a small readonly
block-backed VFS skeleton at `/usb`. The QEMU test partition contains a minimal
exFAT-shaped boot sector and a tiny root directory sample so the kernel can
validate exFAT detection, root-directory chain parsing, subdirectory parsing,
and FAT-chain file reads. It also contains exFAT allocation-bitmap and
upcase-table metadata entries so the kernel can start validating free-space
information before any future write support. The second partition is mounted at
`/boot` and is a small FAT32 sample that stands in for the kind of UEFI boot
partition a real machine commonly uses.

`mounts` shows the current VFS mount table. Today `/dev` is a real devfs mount,
`/usb` and `/boot` are real block-device-backed mounts when QEMU provides
`vd0p1` and `vd0p2`, and writable `ramfs` plus readonly `initrd` are layered at
`/`. The current `/usb`
backend is not a full exFAT filesystem yet; it exposes test files, parsed
exFAT boot-sector metadata, and readonly root-directory files whose data can
span a FAT chain. The root directory itself can also span a FAT chain, and
simple readonly subdirectories are supported. The mount also parses the exFAT
allocation bitmap and reports scanned, used, free, and first-free cluster
counts in `/usb/info.txt`. It can also build a dry-run allocation plan for a
small future write, showing which free clusters would be reserved and how their
FAT chain would be linked. This plan is informational only; TangPingOS still
does not modify the exFAT partition. The mount also dry-runs the directory
entry side of creating `/NEW.TXT`, reporting the root-directory cluster, sector,
slot, and LBA where the three required exFAT entries would fit. It additionally
encodes those planned `0x85`, `0xc0`, and `0xc1` directory entries as hex so the
future write path can be checked before any sector is modified. Finally, it
summarizes the whole dry-run write transaction: the allocation-bitmap byte, FAT
entries, and directory-entry byte range that would be modified. The same
transaction is also applied to in-memory sector copies and checked back as a
dry-run patch, so the kernel can verify the resulting bitmap byte, FAT chain,
and `/NEW.TXT` directory metadata before normal real writes exist. A guarded
test build can additionally commit those three sector updates to the QEMU
`disk.img` and read them back for verification; normal builds keep `/usb`
readonly. The current `/boot` backend recognizes FAT32 boot-sector geometry,
lists 8.3 short-name files and ASCII long-file-name entries, walks readonly
subdirectories, and reads file cluster data. It does not yet support Unicode
long names or FAT32 writes.

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
| `sysinfo` | `sysinfo` | Prints CPU, framebuffer, memmap, timer, uptime, memory summary, and xHCI count. |
| `usb` | `usb` | Prints the first detected xHCI USB controller's PCI and MMIO information. |
| `mounts` | `mounts` | Lists VFS mount entries, sources, and writable status. |
| `lsblk` | `lsblk` | Lists registered block devices. |
| `blkread` | `blkread 2 0` | Reads one 512-byte sector and prints it in hexadecimal. |
| `blkwrite` | `blkwrite 2 2 hello` | Writes text into one 512-byte sector of a writable block device. |

Single-path commands such as `cd`, `ls`, `cat`, `stat`, `rm`, `touch`, `edit`,
and `hexdump` accept double quotes around a path, for example
`cat "/boot/very long filename.txt"`.

When QEMU provides the test disk, `/usb` contains:

| Path | Function |
| --- | --- |
| `/usb/info.txt` | Text metadata for the block-backed mount, source device, exFAT boot sector, allocation bitmap/free-space summary, and upcase table when detected. |
| `/usb/sector0.bin` | Raw bytes from LBA 0 of the mounted block device; in QEMU this starts with an exFAT boot sector. |
| `/usb/HELLO.TXT` | Parsed from the QEMU test partition's exFAT root directory and readable as a small first-cluster file. |
| `/usb/CHAIN.TXT` | Parsed from the QEMU test partition and readable through a two-cluster FAT chain. |
| `/usb/LATE.TXT` | Parsed from a later root-directory cluster, proving that the root directory chain is followed. |
| `/usb/DIR/INNER.TXT` | Parsed from a readonly exFAT subdirectory. |

The same QEMU disk also contains a FAT32 sample mounted at `/boot`:

| Path | Function |
| --- | --- |
| `/boot/info.txt` | Text metadata for the FAT32 mount, including bytes per sector, FAT size, root cluster, hidden sectors, and volume label. |
| `/boot/README.TXT` | Readonly FAT32 root-directory short-name file. |
| `/boot/KERNEL.TXT` | Second readonly FAT32 root-directory short-name file. |
| `/boot/very long filename.txt` | Readonly FAT32 long-file-name sample spanning multiple LFN entries. |
| `/boot/EFI/BOOT/BOOTX64.EFI` | Readonly FAT32 subdirectory sample matching the usual UEFI removable-media boot path. |

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
usb
lsblk
blkread 2 0
blkwrite 2 2 TangPingOS
blkread 2 2
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
struct mount_info;
struct block_device_info;
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
uint64_t tpos_mount_info(uint64_t index, struct mount_info *info);
```

`tpos_mount_info` returns mount entries by numeric index. It reports each
mounted filesystem's path, filesystem name, source, and writable flag. It
returns `-1` when the index is past the end of the table.

Block-device helpers:

```c
uint64_t tpos_block_info(uint64_t index, struct block_device_info *info);
uint64_t tpos_block_read(uint64_t index, uint64_t lba, void *buffer, uint64_t count);
uint64_t tpos_block_write(uint64_t index, uint64_t lba, const void *buffer, uint64_t count);
```

Current block devices are addressed by numeric index. `ramblk0` is index `0`;
when QEMU virtio-blk is present, `vd0` is index `1`, and the first MBR primary
partition `vd0p1` is index `2`. The current shell commands use one-sector
operations; the syscall ABI already keeps a sector count argument for later
storage work.

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
22 block_info(index, buf, len)
23 block_read(index, lba, buf, count)
24 block_write(index, lba, buf, count)
25 mount_info(index, buf, len)
```

Use `tpos.h` wrappers instead of issuing raw syscalls unless you are
deliberately testing the ABI.

## Test Targets

```sh
make test-exception
make test-page-fault
make test-user-fault
make test-user-programs
make test-exfat-commit
```

- `test-exception` builds a kernel that triggers an invalid-opcode exception.
- `test-page-fault` builds a kernel that triggers a page fault.
- `test-user-fault` builds user programs so `shell.elf` triggers a user-mode
  null write; the kernel should kill only that process.
- `test-user-programs` makes the shell startup self-test spawn `/bin/ls.elf`
  and `/bin/cat.elf`.
- `test-exfat-commit` enables the guarded exFAT transaction commit path for the
  QEMU test disk only; it writes the planned `/NEW.TXT` bitmap, FAT, and
  directory sectors, reads them back to verify the commit, and then lets the
  shell write `/usb/NEW.TXT` through the normal VFS file API and read it back.

After any test target, rebuild normal output with:

```sh
make clean && make iso
```

## QEMU Virtual Disk

`make run` builds `build/disk.img` if needed and attaches it as a legacy
virtio-blk PCI device. The image contains a simple MBR with two primary
partitions. `vd0p1` begins at LBA 2048 and contains the exFAT-shaped test
filesystem used by `/usb`. `vd0p2` begins at LBA 11264 and contains the FAT32
sample used by `/boot`. The exFAT partition has root-directory entries for
`HELLO.TXT`, `CHAIN.TXT`, `LATE.TXT`, and `DIR` for parser testing.
`CHAIN.TXT` deliberately uses a two-cluster FAT chain, `LATE.TXT` deliberately
lives in a later root-directory cluster, and `DIR/INNER.TXT` tests subdirectory
parsing. The FAT32 partition has short-name root files `README.TXT` and
`KERNEL.TXT`, an ASCII long-name file `very long filename.txt`, plus an
`EFI/BOOT/BOOTX64.EFI` subdirectory sample. The kernel scans PCI config space,
initializes the virtqueue, registers the disk as `vd0`, parses the MBR, and
registers `vd0p1` and `vd0p2`.

Useful checks in the shell:

```text
mounts
ls /usb
cat /usb/info.txt
cat /usb/HELLO.TXT
stat /usb/CHAIN.TXT
cat /usb/LATE.TXT
ls /usb/DIR
cat /usb/DIR/INNER.TXT
ls /boot
cat /boot/info.txt
cat /boot/README.TXT
cat "/boot/very long filename.txt"
ls /boot/EFI
ls /boot/EFI/BOOT
cat /boot/EFI/BOOT/BOOTX64.EFI
hexdump /usb/sector0.bin
lsblk
blkread 2 0
blkwrite 2 2 hello-from-vd0p1
blkread 2 2
```

This is real sector IO to a QEMU disk-image partition. `/usb` is now a mounted
block-device VFS that can identify an exFAT boot sector, list root-directory
entries across the root-directory FAT chain, and read files by following their
FAT cluster chain. It also supports simple readonly subdirectories and parses
the exFAT allocation bitmap well enough to count used/free clusters, find the
first free cluster, and produce a dry-run allocation plan such as
`12->13->14->EOF`. It can also dry-run the directory-entry placement for a new
root file such as `/NEW.TXT` and show the encoded directory-entry bytes. It
also summarizes the transaction as bitmap, FAT, and directory sector updates,
then applies those updates to in-memory sector copies to verify the resulting
bitmap, FAT chain, and directory metadata. The developer-only
`make test-exfat-commit` target enables a guarded QEMU test commit that writes
those three sectors to `build/disk.img` and verifies them by reading the sectors
back. In that same test build, the shell also opens `/usb/NEW.TXT` through VFS,
writes text to it, and reads the exact bytes back. Normal builds still do not
support exFAT writes.

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
- Real hardware disk-controller access is not implemented. The current
  non-memory disk path is QEMU virtio-blk only; the OS still uses the initrd
  packed into the boot image for startup content.
- The kernel can now detect xHCI USB controllers through PCI and report the
  first controller's BAR/MMIO information with `usb`, but it does not yet
  initialize xHCI rings, enumerate USB devices, or talk to USB storage.
- Keyboard input currently depends on simple PS/2-style input. Many modern
  laptops use USB/xHCI or firmware translation, so keyboard behavior on real
  hardware is uncertain until TangPingOS has USB HID support.
- Serial logs visible in QEMU will usually not be visible on a normal laptop
  unless it has a usable serial console.

For a live demo today, QEMU is reliable. For a USB demo on a real PC, test on the
exact hardware in advance and bring a fallback QEMU demo.

## Current Limitations

- No persistent disk filesystem.
- MBR primary partitions are parsed, but GPT and extended partitions are not.
- `/usb` is a readonly block-backed VFS with minimal exFAT boot-sector,
  allocation-bitmap, dry-run allocation planning, directory-entry byte
  encoding, dry-run transaction summaries, in-memory transaction patch
  validation, upcase-table, root-directory chain, subdirectory, and FAT-chain
  parsing, not a complete exFAT filesystem. The `make test-exfat-commit`
  build can commit one fixed `/NEW.TXT` transaction to the QEMU test disk only;
  this is not general-purpose exFAT write support.
- `/boot` is a readonly FAT32 sample mount with boot-sector parsing, 8.3
  short-name directory listing, ASCII long-file-name entries, subdirectory
  walking, and file reads. It does not yet support Unicode long names or writes.
- No real hardware disk-controller driver; QEMU virtio-blk is supported for
  disk-image sector IO.
- No USB stack.
- No full keyboard layout, modifiers, command history, or tab completion.
- No `wait`, pipes, redirection, signals, or environment variables.
- `edit` is a line editor, not a full-screen editor.
- `/bin` program registration in `Makefile` is still manual.
- Shell builtins and standalone programs currently coexist; builtins have not
  yet been fully moved into `/bin`.
