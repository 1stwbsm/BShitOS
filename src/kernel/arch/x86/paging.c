/*
 * The kernel sees physical memory at the same addresses (identity mapping):
 * with 4 MiB pages where the CPU has them (Pentium and newer), else with 4 KiB
 * page tables covering RAM, plus device memory mapped the first time the kernel
 * touches it. The user window (BS_USER_BASE..BS_USER_TOP) belongs to the
 * running process: two page tables of its own, filled in as pages are touched.
 */
#include <bshit/syscall.h>

#include "arch/x86/cpu.h"
#include "arch/x86/cpuinfo.h"
#include "lib/string.h"
#include "proc/memory.h"

#define PTE_PRESENT 0x01u
#define PTE_WRITE   0x02u
#define PTE_USER    0x04u
#define PDE_4MB     0x80u
#define CR4_PSE     0x10u
#define CR0_PG      0x80000000u
#define CR0_WP      0x00010000u
#define ENTRIES     1024u
#define USER_SLOT   (BS_USER_BASE >> 22)

static uint32_t page_directory[ENTRIES] __attribute__((aligned(4096)));
static uint32_t *user_tables[2];

static void reload_cr3(void)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_directory) : "memory");
}

/* Identity-maps the 4 MiB region with directory slot `slot` using a new page table. */
static int map_identity_region(uint32_t slot)
{
    uint32_t *table = (uint32_t *)page_alloc();

    if (!table)
        return -1;
    for (uint32_t i = 0; i < ENTRIES; i++)
        table[i] = (slot << 22) + i * PAGE_SIZE + (PTE_PRESENT | PTE_WRITE);
    page_directory[slot] = (uint32_t)table | PTE_PRESENT | PTE_WRITE;
    return 0;
}

void paging_init(void)
{
    uint32_t value;

    if (cpu.has_pse) {
        for (uint32_t i = 0; i < ENTRIES; i++)
            page_directory[i] = (i << 22) | PTE_PRESENT | PTE_WRITE | PDE_4MB;
        __asm__ volatile("mov %%cr4, %0" : "=r"(value));
        __asm__ volatile("mov %0, %%cr4" : : "r"(value | CR4_PSE));
    } else {
        /* Page tables come from RAM itself, which paging is still off for. */
        for (uint32_t slot = 0; slot < (mem_highest() + (4u << 20) - 1) >> 22; slot++)
            map_identity_region(slot);
        if (!page_directory[0])
            map_identity_region(0); /* the first 4 MiB at least: VGA text, the kernel */
    }
    reload_cr3();
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    __asm__ volatile("mov %0, %%cr0" : : "r"(value | CR0_PG));
}

/* Points the user window at a process's two page tables (0 to unmap). */
void paging_use_tables(uint32_t table0, uint32_t table1)
{
    user_tables[0] = (uint32_t *)table0;
    user_tables[1] = (uint32_t *)table1;
    page_directory[USER_SLOT] = table0 ? table0 | PTE_PRESENT | PTE_WRITE | PTE_USER : 0;
    page_directory[USER_SLOT + 1] = table1 ? table1 | PTE_PRESENT | PTE_WRITE | PTE_USER : 0;
    reload_cr3();
}

/*
 * A page fault at address. Returns 0 if it was fixed (a user-window page or
 * device memory now mapped), -1 if it is a real fault.
 */
int paging_fault(uint32_t address)
{
    if (address >= BS_USER_BASE && address < BS_USER_TOP && user_tables[0]) {
        uint32_t index = (address - BS_USER_BASE) >> 12;
        uint32_t *entry = &user_tables[index / ENTRIES][index % ENTRIES];
        uint32_t page;

        if (*entry & PTE_PRESENT)
            return -1; /* present already: a protection fault */
        page = page_alloc();
        if (!page)
            return -1;
        memset((void *)page, 0, PAGE_SIZE);
        *entry = page | PTE_PRESENT | PTE_WRITE | PTE_USER;
        return 0;
    }
    if (!cpu.has_pse && !page_directory[address >> 22] && (address >> 22) != USER_SLOT &&
        (address >> 22) != USER_SLOT + 1)
        return map_identity_region(address >> 22) == 0 ? 0 : -1;
    return -1;
}

/* Frees every page a process's tables point at. The tables stay. */
void paging_free_user(uint32_t table0, uint32_t table1)
{
    uint32_t *tables[2] = {(uint32_t *)table0, (uint32_t *)table1};

    for (int t = 0; t < 2; t++) {
        if (!tables[t])
            continue;
        for (uint32_t i = 0; i < ENTRIES; i++) {
            if (tables[t][i] & PTE_PRESENT)
                page_free(tables[t][i] & ~(PAGE_SIZE - 1));
            tables[t][i] = 0;
        }
    }
}
