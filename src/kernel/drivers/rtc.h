#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H

#include <bshit/syscall.h>

void rtc_read(struct bs_time *out);
/* Seconds since the start of 2000, for measuring intervals. */
uint32_t rtc_seconds(void);
void reboot_machine(void) __attribute__((noreturn));

#endif
