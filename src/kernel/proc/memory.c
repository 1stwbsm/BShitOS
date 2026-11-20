/*
 * Physical memory, in 4 KiB pages: one bit per page, set when the page is taken
 * or does not exist. Built from the boot memory map; only usable RAM above
 * 1 MiB is handed out, and only the first MEMORY_MAX of it.
 */
#include "lib/string.h"
#include "proc/memory.h"

#define MEMORY_MAX (1024u * 1024u * 1024u) /* 1 GiB is plenty for BShitOS */
#define PAGES (MEMORY_MAX / PAGE_SIZE)
#define LOW_MEMORY 0x100000u

static uint32_t taken[PAGES / 32];
static uint32_t total_pages, free_pages, highest_page, search_from;

static int is_taken(uint32_t page)
{
    return (taken[page / 32] >> (page % 32)) & 1;
}

static void set_taken(uint32_t page, int value)
{
    if (value)
        taken[page / 32] |= 1u << (page % 32);
    else
        taken[page / 32] &= ~(1u << (page % 32));
}

void mem_init(const struct bootinfo *info)
{
    const struct bootinfo_mem_range *ranges = (const struct bootinfo_mem_range *)(uintptr_t)info->mem_ranges;

    memset(taken, 0xFF, sizeof(taken));
    for (uint32_t i = 0; i < info->mem_range_count; i++) {
        uint64_t base = ranges[i].base, end = base + ranges[i].length;

        if (ranges[i].type != BOOTINFO_MEM_USABLE)
            continue;
        if (base < LOW_MEMORY)
            base = LOW_MEMORY;
        if (end > MEMORY_MAX)
            end = MEMORY_MAX;
        for (uint64_t page = (base + PAGE_SIZE - 1) / PAGE_SIZE; page < end / PAGE_SIZE; page++) {
            if (is_taken((uint32_t)page)) {
                set_taken((uint32_t)page, 0);
                total_pages++;
                if (page + 1 > highest_page)
                    highest_page = (uint32_t)page + 1;
            }
        }
    }
    /* Overlapping ranges (some firmware reports them) must not hand out the kernel. */
    for (uint32_t i = 0; i < info->mem_range_count; i++) {
        uint64_t base = ranges[i].base, end = base + ranges[i].length;

        if (ranges[i].type == BOOTINFO_MEM_USABLE)
            continue;
        for (uint64_t page = base / PAGE_SIZE; page < (end + PAGE_SIZE - 1) / PAGE_SIZE && page < PAGES; page++)
            if (!is_taken((uint32_t)page)) {
                set_taken((uint32_t)page, 1);
                total_pages--;
            }
    }
    free_pages = total_pages;
    search_from = LOW_MEMORY / PAGE_SIZE;
}

uint32_t mem_highest(void)
{
    return highest_page * PAGE_SIZE;
}

uint32_t pages_alloc(uint32_t count)
{
    uint32_t run = 0;

    for (uint32_t page = search_from; page < highest_page; page++) {
        run = is_taken(page) ? 0 : run + 1;
        if (run == count) {
            uint32_t first = page + 1 - count;

            for (uint32_t p = first; p <= page; p++)
                set_taken(p, 1);
            free_pages -= count;
            if (count == 1)
                search_from = page + 1;
            return first * PAGE_SIZE;
        }
    }
    if (search_from != LOW_MEMORY / PAGE_SIZE) { /* wrap once */
        search_from = LOW_MEMORY / PAGE_SIZE;
        return pages_alloc(count);
    }
    return 0;
}

uint32_t page_alloc(void)
{
    return pages_alloc(1);
}

void pages_free(uint32_t address, uint32_t count)
{
    uint32_t first = address / PAGE_SIZE;

    for (uint32_t p = first; p < first + count; p++) {
        if (is_taken(p)) {
            set_taken(p, 0);
            free_pages++;
        }
    }
    if (first < search_from)
        search_from = first;
}

void page_free(uint32_t address)
{
    pages_free(address, 1);
}

void mem_stats(uint32_t *total_kib, uint32_t *free_kib)
{
    *total_kib = total_pages * (PAGE_SIZE / 1024);
    *free_kib = free_pages * (PAGE_SIZE / 1024);
}
