# TangPingOS Development Log

- 2026-06-19: TangPingOS 已完成 x86_64 UEFI/Limine 启动、串口与 framebuffer 输出、GDT/IDT/PIT/PIC、物理与虚拟内存、可复用内核堆、抢占式调度、Ring 3 init 用户进程、syscall、brk、initrd/VFS、用户指针校验、用户异常隔离以及进程退出后的地址空间和内核栈回收。
- 2026-06-19: 新增每进程文件描述符表和 `open/read/close` syscall，用户态 init 已改为通过 fd 读取 initrd 文件。
- 2026-06-19: 新增 `getdents` 目录枚举 syscall，用户态 init 现在可以列出 initrd 根目录文件。
- 2026-06-19: 将用户态 init 整理为非交互式命令雏形，新增内置 `ls` 和 `cat` 命令函数。
- 2026-06-19: 接入 PS/2 键盘输入缓冲和 `read(0, ...)`，用户态 init 进入可轮询输入的简易命令提示符。
- 2026-06-19: 将 `read(0, ...)` 改为阻塞式输入等待，键盘中断到来后唤醒等待 stdin 的用户任务。
- 2026-06-19: 新增 TTY 设备雏形，将键盘输入和控制台输出收拢到 `/dev/tty` 抽象，用户态 shell 现在打开该设备作为输入端。
- 2026-06-19: 新增 devfs 雏形并让 VFS 聚合目录项，根目录现在能列出 `dev`，`ls /dev` 能看到 `tty` 设备。
- 2026-06-19: 新增 `write_fd(fd, buf, len)` syscall，用户态输出开始通过 stdout 或 `/dev/tty` 文件描述符写入。
- 2026-06-19: 将主 `write` syscall 收敛为 `write(fd, buf, len)`，并新增用户态坏 fd 写入拒绝自检。
- 2026-06-19: 新增 `dup2(oldfd, newfd)` syscall，标准 fd 0/1/2 现在可被已打开描述符覆盖。
- 2026-06-19: 移除默认 A/B 心跳 demo task，并让 `dup2` 后的 fd 共享同一个打开文件对象和读取 offset。
- 2026-06-19: 关闭周期性 PIT tick 串口日志，让启动后等待输入时的输出保持安静。
