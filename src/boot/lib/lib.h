/* Firmware globals and the small runtime shared by all of BSBOOT. */
#ifndef BSBOOT_LIB_H
#define BSBOOT_LIB_H

#include "efi/efi.h"

extern EFI_HANDLE gImage;
extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;

/* mem.c: gcc may emit calls to these even when freestanding */
void *memset(void *dst, int c, unsigned int n);
void *memcpy(void *dst, const void *src, unsigned int n);
int memcmp(const void *a, const void *b, unsigned int n);

/* con.c */
#define CON_NORMAL   EFI_ATTR(EFI_LIGHTGRAY, EFI_BLACK)
#define CON_TITLE    EFI_ATTR(EFI_LIGHTCYAN, EFI_BLACK)
#define CON_ACCENT   EFI_ATTR(EFI_LIGHTMAGENTA, EFI_BLACK)
#define CON_DIM      EFI_ATTR(EFI_DARKGRAY, EFI_BLACK)
#define CON_SELECTED EFI_ATTR(EFI_BLACK, EFI_CYAN)
#define CON_ERROR    EFI_ATTR(EFI_LIGHTRED, EFI_BLACK)

/* Stops the firmware mirroring its console to serial ports. */
void con_detach_serial(void);
void con_clear(void);
void con_size(UINTN *cols, UINTN *rows);
void con_goto(UINTN col, UINTN row);
void con_color(UINTN attr);
void con_print(const CHAR16 *s);
void con_print_u32(UINT32 value, UINTN min_digits);
void con_repeat(CHAR16 c, UINTN count);
void con_center(UINTN row, const CHAR16 *s, UINTN attr);
UINTN con_strlen(const CHAR16 *s);
EFI_INPUT_KEY con_wait_key(void);
BOOLEAN con_poll_key(EFI_INPUT_KEY *key);
/* Full-screen error report; returns after a key press. */
void con_error(const CHAR16 *what, const CHAR16 *detail, EFI_STATUS status);

/* file.c: paths are relative to the volume BSBOOT was loaded from */
EFI_STATUS file_open(const CHAR16 *path, EFI_FILE_PROTOCOL **file);
EFI_STATUS file_size(EFI_FILE_PROTOCOL *file, UINT64 *size);
/* Allocates *data from the pool; release it with gBS->FreePool. */
EFI_STATUS file_load(const CHAR16 *path, void **data, UINTN *size);

#endif
