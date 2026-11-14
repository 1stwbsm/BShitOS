#include "arch/x86/cpu.h"
#include "lib/string.h"

#define TSS_SELECTOR 0x28

struct tss kernel_tss;

static uint64_t gdt[6] = {
    0,
    0x00CF9A000000FFFFull, /* 0x08 kernel code */
    0x00CF92000000FFFFull, /* 0x10 kernel data */
    0x00CFFA000000FFFFull, /* 0x18 user code */
    0x00CFF2000000FFFFull, /* 0x20 user data */
    0,                     /* 0x28 TSS */
};

void gdt_init(void)
{
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) descriptor = {sizeof(gdt) - 1, (uint32_t)gdt};
    uint64_t base = (uint32_t)&kernel_tss, limit = sizeof(kernel_tss) - 1;

    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.ss0 = SEL_KDATA;
    kernel_tss.iomap_base = sizeof(kernel_tss);

    gdt[5] = (limit & 0xFFFF) | (base & 0xFFFFFF) << 16 | 0x89ull << 40 |
             (limit & 0xF0000) << 32 | (base & 0xFF000000) << 32;

    __asm__ volatile("lgdt %0" : : "m"(descriptor));
    __asm__ volatile("ltr %w0" : : "r"(TSS_SELECTOR));
}
