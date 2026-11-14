/* PS/2 keyboard, polled, scancode set 1 (the i8042's translated default). */
#include "arch/x86/cpu.h"
#include "drivers/keyboard.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define STATUS_OUTPUT_FULL 0x01
#define STATUS_FROM_MOUSE 0x20

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_RELEASE 0x80
#define SC_EXTENDED 0xE0

static const char normal[0x3A] =
    "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char shifted[0x3A] =
    "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static int shift_held;
static int skip_next;

void keyboard_init(void)
{
    while (inb(PS2_STATUS) & STATUS_OUTPUT_FULL)
        inb(PS2_DATA);
}

char keyboard_poll(void)
{
    uint8_t status = inb(PS2_STATUS), code;

    if (!(status & STATUS_OUTPUT_FULL))
        return 0;
    code = inb(PS2_DATA);
    if (status & STATUS_FROM_MOUSE)
        return 0;

    if (code == SC_EXTENDED) {
        skip_next = 1; /* arrows and friends are not used yet */
        return 0;
    }
    if (skip_next) {
        skip_next = 0;
        return 0;
    }

    if ((code & (uint8_t)~SC_RELEASE) == SC_LSHIFT || (code & (uint8_t)~SC_RELEASE) == SC_RSHIFT) {
        shift_held = !(code & SC_RELEASE);
        return 0;
    }
    if ((code & SC_RELEASE) || code >= sizeof(normal))
        return 0;
    return shift_held ? shifted[code] : normal[code];
}
