#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#include <stdint.h>

#define SEL_KDATA 0x10

/* Layout built by the stubs in isr.S, lowest address first. */
struct trapframe {
    uint32_t ds;
    uint32_t edi, esi, ebp, oesp, ebx, edx, ecx, eax;
    uint32_t vector, error;
    uint32_t eip, cs, eflags, user_esp, user_ss;
};

struct tss {
    uint32_t link, esp0, ss0, unused[22];
    uint16_t trap, iomap_base;
} __attribute__((packed));

extern struct tss kernel_tss;

void gdt_init(void);
void idt_init(void);
void paging_init(void);
/* Maps a 4 MiB user window: slot 0 at BS_USER_BASE, slot 1 at BS_LIB_BASE. */
void paging_map_user(unsigned int slot, uint32_t phys);

/*
 * Drops to ring 3 and returns the code later given to leave_user. Nests: each
 * process saves its own kernel stack pointer through ksp.
 */
int enter_user(uint32_t entry, uint32_t user_sp, uint32_t *ksp);
void leave_user(uint32_t ksp, int code) __attribute__((noreturn));

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %w1, %b0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %w1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

#endif
