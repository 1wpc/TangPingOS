# TangPingOS Development Log

- 2026-06-19: TangPingOS 已完成 x86_64 UEFI/Limine 启动、串口与 framebuffer 输出、GDT/IDT/PIT/PIC、物理与虚拟内存、可复用内核堆、抢占式调度、Ring 3 init 用户进程、syscall、brk、initrd/VFS、用户指针校验、用户异常隔离以及进程退出后的地址空间和内核栈回收。
- 2026-06-19: 新增每进程文件描述符表和 `open/read/close` syscall，用户态 init 已改为通过 fd 读取 initrd 文件。
- 2026-06-19: 新增 `getdents` 目录枚举 syscall，用户态 init 现在可以列出 initrd 根目录文件。
