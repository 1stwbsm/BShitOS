/*
 * The kernel sees physical memory at the same addresses (identity mapping):
 * with 4 MiB pages where the CPU has them (Pentium and newer), else with 4 KiB
 * page tables covering RAM, plus device memory mapped the first time the kernel
 * touches it. The user window (BS_USER_BASE..BS_USER_TOP) belongs to the
 * running process: page tables of its own, made and filled as pages are touched.
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
static uint32_t *user_tables; /* the running process's BS_USER_SLOTS table addresses */

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

/* Points the user window at a process's tables (0 for none: kernel only). */
void paging_use_tables(uint32_t *tables)
{
    user_tables = tables;
    for (uint32_t slot = 0; slot < BS_USER_SLOTS; slot++)
        page_directory[USER_SLOT + slot] =
            tables && tables[slot] ? tables[slot] | PTE_PRESENT | PTE_WRITE | PTE_USER : 0;
    reload_cr3();
}

/*
 * A page fault at address. Returns 0 if it was fixed (a user-window page or
 * device memory now mapped), -1 if it is a real fault.
 */
int paging_fault(uint32_t address)
{
    if (address >= BS_USER_BASE && address < BS_USER_TOP && user_tables) {
        uint32_t index = (address - BS_USER_BASE) >> 12, slot = index / ENTRIES;
        uint32_t *entry, page;

        if (!user_tables[slot]) { /* first touch in this 4 MiB: a table for it */
            uint32_t table = page_alloc();

            if (!table)
                return -1;
            memset((void *)table, 0, PAGE_SIZE);
            user_tables[slot] = table;
            page_directory[USER_SLOT + slot] = table | PTE_PRESENT | PTE_WRITE | PTE_USER;
            reload_cr3();
        }
        entry = &((uint32_t *)user_tables[slot])[index % ENTRIES];

        if (*entry & PTE_PRESENT)
            return -1; /* present already: a protection fault */
        page = page_alloc();
        if (!page)
            return -1;
        memset((void *)page, 0, PAGE_SIZE);
        *entry = page | PTE_PRESENT | PTE_WRITE | PTE_USER;
        return 0;
    }
    if (!cpu.has_pse && !page_directory[address >> 22] &&
        ((address >> 22) < USER_SLOT || (address >> 22) >= USER_SLOT + BS_USER_SLOTS))
        return map_identity_region(address >> 22) == 0 ? 0 : -1;
    return -1;
}

/* Frees a process's pages and its tables. */
void paging_free_user(uint32_t *tables)
{
    for (uint32_t slot = 0; slot < BS_USER_SLOTS; slot++) {
        uint32_t *table = (uint32_t *)tables[slot];

        if (!table)
            continue;
        for (uint32_t i = 0; i < ENTRIES; i++)
            if (table[i] & PTE_PRESENT)
                page_free(table[i] & ~(PAGE_SIZE - 1));
        page_free(tables[slot]);
        tables[slot] = 0;
    }
}
