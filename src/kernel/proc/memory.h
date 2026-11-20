#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <bshit/bootinfo.h>

#define PAGE_SIZE 4096u

void mem_init(const struct bootinfo *info);
/* Physical addresses, 0 when out of memory. Pages come back uninitialised. */
uint32_t page_alloc(void);
uint32_t pages_alloc(uint32_t count); /* physically contiguous, for DMA */
void page_free(uint32_t address);
void pages_free(uint32_t address, uint32_t count);
void mem_stats(uint32_t *total_kib, uint32_t *free_kib);
uint32_t mem_highest(void); /* the end of the highest usable RAM handed out */

#endif
