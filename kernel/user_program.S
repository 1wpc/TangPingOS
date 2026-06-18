.section .text
.global user_program_start
.global user_program_end

user_program_start:
    xorq %r12, %r12
1:
    movq $1, %rax
    leaq message(%rip), %rdi
    movq $(message_end - message), %rsi
    int $0x80

    movq $2000000, %rcx
2:
    pause
    loop 2b

    incq %r12
    cmpq $4, %r12
    jb 1b

    movq $2, %rax
    movq $42, %rdi
    int $0x80

3:
    jmp 3b

message:
    .ascii "user task syscall write\n"
message_end:

user_program_end:
