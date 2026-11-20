/*
 * What the processor can do. BShitOS runs on a 386: anything newer (CPUID, 4 MiB
 * pages, the cycle counter) is used only when this finds it.
 */
#include "arch/x86/cpuinfo.h"

#define EFLAGS_AC (1u << 18) /* only a 486 or newer can toggle it */
#define EFLAGS_ID (1u << 21) /* only a CPU with CPUID can toggle it */
#define FEATURE_PSE (1u << 3)
#define FEATURE_TSC (1u << 4)

struct cpu_info cpu;

static int flag_toggles(uint32_t bit)
{
    uint32_t before, after;

    __asm__ volatile("pushfl\n\t"
                     "popl %0\n\t"
                     "movl %0, %1\n\t"
                     "xorl %2, %1\n\t"
                     "pushl %1\n\t"
                     "popfl\n\t"
                     "pushfl\n\t"
                     "popl %1\n\t"
                     "pushl %0\n\t"
                     "popfl"
                     : "=&r"(before), "=&r"(after)
                     : "r"(bit));
    return ((before ^ after) & bit) != 0;
}

void cpu_detect(void)
{
    uint32_t eax, ebx, ecx, edx;

    cpu.family = flag_toggles(EFLAGS_AC) ? 4 : 3;
    cpu.has_cpuid = flag_toggles(EFLAGS_ID);
    if (!cpu.has_cpuid)
        return;
    eax = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    ((uint32_t *)cpu.vendor)[0] = ebx;
    ((uint32_t *)cpu.vendor)[1] = edx;
    ((uint32_t *)cpu.vendor)[2] = ecx;
    cpu.vendor[12] = 0;
    if (eax < 1)
        return;
    eax = 1;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    cpu.family = (eax >> 8) & 15;
    if (cpu.family == 15)
        cpu.family += (eax >> 20) & 255;
    cpu.has_pse = (edx & FEATURE_PSE) != 0;
    cpu.has_tsc = (edx & FEATURE_TSC) != 0;
}
