/*
 * Identity-maps all 4 GiB with supervisor-only 4 MiB pages. Two directory
 * slots are repointed at the running process's user-accessible memory.
 */
#include <bshit/syscall.h>

#include "arch/x86/cpu.h"

#define PDE_PRESENT 0x01u
#define PDE_WRITE   0x02u
#define PDE_USER    0x04u
#define PDE_4MB     0x80u
#define CR4_PSE     0x10u
#define CR0_PG      0x80000000u

static uint32_t page_directory[1024] __attribute__((aligned(4096)));

static void reload_cr3(void)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_directory));
}

void paging_init(void)
{
    uint32_t value;

    for (uint32_t i = 0; i < 1024; i++)
        page_directory[i] = (i << 22) | PDE_PRESENT | PDE_WRITE | PDE_4MB;

    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    __asm__ volatile("mov %0, %%cr4" : : "r"(value | CR4_PSE));
    reload_cr3();
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    __asm__ volatile("mov %0, %%cr0" : : "r"(value | CR0_PG));
}

void paging_map_user(unsigned int slot, uint32_t phys)
{
    page_directory[(BS_USER_BASE >> 22) + slot] = phys | PDE_PRESENT | PDE_WRITE | PDE_USER | PDE_4MB;
    reload_cr3();
}
