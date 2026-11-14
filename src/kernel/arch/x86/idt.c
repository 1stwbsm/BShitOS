#include "arch/x86/cpu.h"
#include "drivers/console.h"
#include "proc/proc.h"

#define KERNEL_CS 0x08
#define VECTOR_SYSCALL 0x80
#define GATE_KERNEL 0x8E
#define GATE_USER 0xEE

extern const uint32_t isr_exceptions[32];
extern const uint32_t isr_syscall;

static uint64_t idt[256];

static uint64_t gate(uint32_t handler, uint8_t type)
{
    return (handler & 0xFFFF) | (uint64_t)KERNEL_CS << 16 | (uint64_t)type << 40 |
           (uint64_t)(handler & 0xFFFF0000) << 32;
}

void idt_init(void)
{
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) descriptor = {sizeof(idt) - 1, (uint32_t)idt};

    for (int i = 0; i < 32; i++)
        idt[i] = gate(isr_exceptions[i], GATE_KERNEL);
    idt[VECTOR_SYSCALL] = gate(isr_syscall, GATE_USER);
    __asm__ volatile("lidt %0" : : "m"(descriptor));
}

void trap_handler(struct trapframe *frame)
{
    if (frame->vector == VECTOR_SYSCALL) {
        frame->eax = (uint32_t)syscall_dispatch(frame->eax, frame->ebx, frame->ecx, frame->edx);
        return;
    }

    if (frame->cs & 3) {
        console_print("\nprocess killed: CPU exception ");
        console_print_u32(frame->vector);
        console_print("\n");
        proc_exit(139);
    }

    console_set_serial(1);
    console_print("\nKERNEL PANIC: exception ");
    console_print_u32(frame->vector);
    console_print(" at eip ");
    console_print_hex(frame->eip);
    console_print("\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}
