/*
 * BShitEmu: a PC-compatible machine for running BShitOS in a web browser.
 * Built as WebAssembly for the web version, or natively for testing.
 */
#ifndef EMU_H
#define EMU_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define RAM_SIZE (192u * 1024u * 1024u)
#define FB_BASE 0xFD000000u
#define FB_WIDTH 1280
#define FB_HEIGHT 800
#define FB_BYTES (FB_WIDTH * FB_HEIGHT * 4u)

#ifdef __wasm__
#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name)))
#else
#define IMPORT(name)
#define EXPORT(name)
#endif

/* Provided by the host (JavaScript, or the native test harness). */
IMPORT("host_now") double host_now(void); /* milliseconds, monotonic */
IMPORT("host_date") void host_date(u32 *out); /* year, month, day, hour, minute, second (UTC) */
IMPORT("host_serial") void host_serial(u32 byte);
IMPORT("host_tone") void host_tone(u32 hz); /* 0 silences the speaker */

extern u8 ram[RAM_SIZE];
extern u32 framebuffer[FB_WIDTH * FB_HEIGHT];
extern int framebuffer_dirty;
extern int reboot_requested;

/* Physical memory and I/O ports (devices.c) */
u32 phys_read(u32 address, int size);
void phys_write(u32 address, u32 value, int size);
u32 io_in(u32 port, int size);
void io_out(u32 port, u32 value, int size);
void devices_reset(void);
void ps2_key(u32 code, int down); /* scancode set 1; codes above 0xFF get the E0 prefix */
void ps2_mouse(s32 dx, s32 dy, u32 buttons);

/* The processor (cpu.c) */
enum cpu_status { CPU_RUNNING, CPU_HALTED, CPU_CRASHED, CPU_REBOOT };
void cpu_reset(u32 eip, u32 esp);
enum cpu_status cpu_run(u32 instructions);
const char *cpu_crash_message(void);

#endif
