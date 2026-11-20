#ifndef KERNEL_CPUINFO_H
#define KERNEL_CPUINFO_H

#include <stdint.h>

struct cpu_info {
    uint32_t family; /* 3 = 386, 4 = 486, 5 = Pentium, 6+ = newer */
    int has_cpuid, has_pse, has_tsc;
    char vendor[13]; /* empty without CPUID */
};

extern struct cpu_info cpu;

void cpu_detect(void);

#endif
