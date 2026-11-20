#ifndef KERNEL_SHUTDOWN_H
#define KERNEL_SHUTDOWN_H

#include <bshit/syscall.h>

/* Asks every program to exit, then powers off or restarts (BS_SHUTDOWN_*). */
void shutdown_request(int mode);
int shutdown_pending(void); /* the requested mode, or 0 */
/* Stops the running program if the deadline has passed. Call where programs wait. */
void shutdown_check(void);
/* Once init has exited: flush, then power off or restart. Does not return. */
void shutdown_finish(void) __attribute__((noreturn));

#endif
