#include <console.h>
#include <input.h>
#include <memory.h>
#include <scheduler.h>
#include <stddef.h>
#include <stdint.h>
#include <syscall.h>
#include <x86_64/interrupt_frame.h>
#include <x86_64/io.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

#define TSS_STACK_SIZE (16ULL * 1024ULL)

#define IDT_PRESENT 0x80
#define IDT_INTERRUPT_GATE 0x0e

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA    0xa1
#define PIC_EOI      0x20

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182

struct __attribute__((packed)) descriptor_ptr {
    uint16_t limit;
    uint64_t base;
};

struct __attribute__((packed)) idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

extern void x86_64_load_gdt(const struct descriptor_ptr *gdt_ptr);
extern void x86_64_load_idt(const struct descriptor_ptr *idt_ptr);
extern void x86_64_load_tss(uint16_t selector);

#define ISR_DECL(n) extern void isr##n(void)
ISR_DECL(0);  ISR_DECL(1);  ISR_DECL(2);  ISR_DECL(3);
ISR_DECL(4);  ISR_DECL(5);  ISR_DECL(6);  ISR_DECL(7);
ISR_DECL(8);  ISR_DECL(9);  ISR_DECL(10); ISR_DECL(11);
ISR_DECL(12); ISR_DECL(13); ISR_DECL(14); ISR_DECL(15);
ISR_DECL(16); ISR_DECL(17); ISR_DECL(18); ISR_DECL(19);
ISR_DECL(20); ISR_DECL(21); ISR_DECL(22); ISR_DECL(23);
ISR_DECL(24); ISR_DECL(25); ISR_DECL(26); ISR_DECL(27);
ISR_DECL(28); ISR_DECL(29); ISR_DECL(30); ISR_DECL(31);
ISR_DECL(32); ISR_DECL(33); ISR_DECL(34); ISR_DECL(35);
ISR_DECL(36); ISR_DECL(37); ISR_DECL(38); ISR_DECL(39);
ISR_DECL(40); ISR_DECL(41); ISR_DECL(42); ISR_DECL(43);
ISR_DECL(44); ISR_DECL(45); ISR_DECL(46); ISR_DECL(47);
ISR_DECL(128);

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static uint64_t gdt[7];
static struct tss64 tss;
static struct idt_entry idt[256];
static volatile uint64_t timer_ticks;

static const char *exception_names[] = {
    "divide by zero",
    "debug",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid tss",
    "segment not present",
    "stack segment fault",
    "general protection fault",
    "page fault",
    "reserved",
    "x87 floating-point exception",
    "alignment check",
    "machine check",
    "simd floating-point exception",
    "virtualization exception",
    "control protection exception",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "hypervisor injection exception",
    "vmm communication exception",
    "security exception",
    "reserved",
};

static uint64_t gdt_code_data_descriptor(uint8_t access) {
    return 0xffffULL |
           ((uint64_t)access << 40) |
           (0xfULL << 48) |
           (0xaULL << 52);
}

static void gdt_set_tss(uint64_t base, uint32_t limit) {
    gdt[5] = ((uint64_t)(limit & 0xffff)) |
             ((base & 0xffffffULL) << 16) |
             ((uint64_t)0x89 << 40) |
             (((uint64_t)(limit >> 16) & 0xf) << 48) |
             (((base >> 24) & 0xffULL) << 56);
    gdt[6] = base >> 32;
}

static void gdt_init(void) {
    uint8_t *tss_stack = kmalloc(TSS_STACK_SIZE);
    uint64_t tss_stack_top = ((uint64_t)tss_stack + TSS_STACK_SIZE) & ~0xfULL;

    tss.rsp0 = tss_stack_top;
    tss.iomap_base = sizeof(tss);

    gdt[0] = 0;
    gdt[1] = gdt_code_data_descriptor(0x9a);
    gdt[2] = gdt_code_data_descriptor(0x92);
    gdt[3] = gdt_code_data_descriptor(0xfa);
    gdt[4] = gdt_code_data_descriptor(0xf2);
    gdt_set_tss((uint64_t)&tss, sizeof(tss) - 1);

    struct descriptor_ptr ptr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)gdt,
    };
    x86_64_load_gdt(&ptr);
    x86_64_load_tss(GDT_TSS);
    console_printf("TSS loaded: rsp0=%x\n", tss.rsp0);
}

static void idt_set_gate(uint8_t vector, void (*handler)(void), uint8_t dpl) {
    uint64_t addr = (uint64_t)handler;

    idt[vector].offset_low = addr & 0xffff;
    idt[vector].selector = GDT_KERNEL_CODE;
    idt[vector].ist = 0;
    idt[vector].type_attr = IDT_PRESENT | IDT_INTERRUPT_GATE | ((dpl & 0x3) << 5);
    idt[vector].offset_mid = (addr >> 16) & 0xffff;
    idt[vector].offset_high = (addr >> 32) & 0xffffffff;
    idt[vector].zero = 0;
}

static void idt_init(void) {
    void (*handlers[48])(void) = {
        isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
        isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
        isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
        isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47,
    };

    for (uint8_t i = 0; i < 48; i++) {
        idt_set_gate(i, handlers[i], 0);
    }
    idt_set_gate(0x80, isr128, 3);

    struct descriptor_ptr ptr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt,
    };
    x86_64_load_idt(&ptr);
}

static void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

static void pic_set_mask(uint8_t irq, int masked) {
    uint16_t port;
    uint8_t bit;

    if (irq < 8) {
        port = PIC1_DATA;
        bit = irq;
    } else {
        port = PIC2_DATA;
        bit = irq - 8;
    }

    uint8_t value = inb(port);
    if (masked) {
        value |= (uint8_t)(1u << bit);
    } else {
        value &= (uint8_t)~(1u << bit);
    }
    outb(port, value);
}

static void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

static void pit_init(uint32_t hz) {
    uint32_t divisor = PIT_BASE_HZ / hz;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xff);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xff);
}

static void keyboard_handle(void) {
    uint8_t scancode = inb(0x60);
    static int shift_down;

    if (scancode == 0x2a || scancode == 0x36) {
        shift_down = 1;
        return;
    }
    if (scancode == 0xaa || scancode == 0xb6) {
        shift_down = 0;
        return;
    }

    if ((scancode & 0x80) != 0) {
        return;
    }

    static const char normal[128] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
        [0x0e] = '\b', [0x0f] = '\t', [0x10] = 'q', [0x11] = 'w',
        [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
        [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
        [0x1a] = '[', [0x1b] = ']', [0x1c] = '\n', [0x1e] = 'a',
        [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
        [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
        [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2b] = '\\',
        [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
        [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
        [0x34] = '.', [0x35] = '/', [0x39] = ' ',
    };
    static const char shifted[128] = {
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
        [0x0e] = '\b', [0x0f] = '\t', [0x10] = 'Q', [0x11] = 'W',
        [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
        [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
        [0x1a] = '{', [0x1b] = '}', [0x1c] = '\n', [0x1e] = 'A',
        [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
        [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
        [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2b] = '|',
        [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C', [0x2f] = 'V',
        [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
        [0x34] = '>', [0x35] = '?', [0x39] = ' ',
    };

    char c = shift_down ? shifted[scancode] : normal[scancode];
    if (c == '\0') {
        return;
    }

    input_push_key(c);
    scheduler_wake_input_waiters();
    if (c == '\n') {
        console_write("\n");
    } else if (c == '\b') {
        console_write("\b \b");
    } else {
        char s[2] = {c, '\0'};
        console_write(s);
    }
}

static inline uint64_t read_cr2(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static int frame_from_user(const struct interrupt_frame *frame) {
    return (frame->cs & 0x3) == 0x3;
}

struct interrupt_frame *x86_64_interrupt_handler(struct interrupt_frame *frame) {
    if (frame->vector < 32) {
        uint64_t fault_addr = frame->vector == 14 ? read_cr2() : 0;
        console_printf("\nEXCEPTION %u: %s\n", frame->vector, exception_names[frame->vector]);
        if (frame->vector == 14) {
            console_printf("cr2=%x\n", fault_addr);
        }
        console_printf("error=%x rip=%x cs=%x rflags=%x rsp=%x ss=%x\n",
                       frame->error_code, frame->rip, frame->cs, frame->rflags,
                       frame->rsp, frame->ss);

        if (frame_from_user(frame)) {
            return scheduler_fault_current(frame, frame->vector, fault_addr);
        }

        kernel_panic("unhandled CPU exception");
    }

    if (frame->vector >= 32 && frame->vector <= 47) {
        uint8_t irq = (uint8_t)(frame->vector - 32);

        switch (irq) {
            case 0:
                timer_ticks++;
                if ((timer_ticks % 100) == 0) {
                    console_printf("timer tick: %u\n", timer_ticks);
                }
                frame = scheduler_on_timer_tick(frame);
                break;
            case 1:
                keyboard_handle();
                break;
            default:
                console_printf("unhandled irq %u\n", (uint64_t)irq);
                break;
        }

        pic_send_eoi(irq);
        return frame;
    }

    if (frame->vector == 0x80) {
        frame = syscall_dispatch(frame);
        return frame;
    }

    console_printf("unexpected interrupt vector %u\n", frame->vector);
    return frame;
}

void x86_64_interrupts_init(void) {
    __asm__ volatile ("cli");

    gdt_init();
    console_write("GDT loaded\n");

    idt_init();
    console_write("IDT loaded\n");

    pic_remap();
    for (uint8_t irq = 0; irq < 16; irq++) {
        pic_set_mask(irq, 1);
    }
    pic_set_mask(0, 0);
    pic_set_mask(1, 0);
    console_write("PIC remapped: IRQ0 timer, IRQ1 keyboard unmasked\n");

    pit_init(100);
    console_write("PIT timer configured: 100 Hz\n");
}

void x86_64_interrupts_enable(void) {
    __asm__ volatile ("sti");
    console_write("interrupts enabled\n");
}

void x86_64_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
