#include "arch/x86/cpu.h"
#include "drivers/ps2.h"
#include "drivers/xhci.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64
#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL 0x02
#define STATUS_FROM_MOUSE 0x20

#define CMD_READ_CONFIG 0x20
#define CMD_WRITE_CONFIG 0x60
#define CMD_ENABLE_AUX 0xA8
#define CMD_TO_MOUSE 0xD4
#define CONFIG_IRQS 0x03
#define CONFIG_AUX_CLOCK_OFF 0x20
#define MOUSE_DEFAULTS 0xF6
#define MOUSE_ENABLE 0xF4
#define WAIT_LIMIT 100000

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL 0x1D
#define SC_RELEASE 0x80
#define SC_EXTENDED 0xE0

#define KEY_QUEUE 32u
#define PACKET_SYNC 0x08
#define PACKET_X_NEGATIVE 0x10
#define PACKET_Y_NEGATIVE 0x20
#define PACKET_OVERFLOW 0xC0

static const char normal[0x3A] =
    "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char shifted[0x3A] =
    "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static int shift_held, ctrl_held, extended;
static uint8_t keys[KEY_QUEUE];
static uint32_t key_head, key_tail;

static uint8_t packet[3];
static uint32_t packet_used;
static int32_t mouse_dx, mouse_dy;
static uint32_t mouse_buttons, mouse_latched;

static int wait_input_clear(void)
{
    for (int i = 0; i < WAIT_LIMIT; i++)
        if (!(inb(PS2_STATUS) & STATUS_INPUT_FULL))
            return 0;
    return -1;
}

static int wait_output(void)
{
    for (int i = 0; i < WAIT_LIMIT; i++)
        if (inb(PS2_STATUS) & STATUS_OUTPUT_FULL)
            return 0;
    return -1;
}

static void command(uint8_t value)
{
    if (wait_input_clear() == 0)
        outb(PS2_COMMAND, value);
}

static void data(uint8_t value)
{
    if (wait_input_clear() == 0)
        outb(PS2_DATA, value);
}

static void flush(void)
{
    while (inb(PS2_STATUS) & STATUS_OUTPUT_FULL)
        inb(PS2_DATA);
}

static void mouse_command(uint8_t value)
{
    command(CMD_TO_MOUSE);
    data(value);
    if (wait_output() == 0)
        inb(PS2_DATA); /* the ACK */
}

void ps2_init(void)
{
    uint8_t config = 0;

    flush();
    command(CMD_ENABLE_AUX);
    command(CMD_READ_CONFIG);
    if (wait_output() == 0)
        config = inb(PS2_DATA);
    /* Polled only, so both IRQs stay off. */
    config &= (uint8_t)~(CONFIG_IRQS | CONFIG_AUX_CLOCK_OFF);
    command(CMD_WRITE_CONFIG);
    data(config);

    mouse_command(MOUSE_DEFAULTS);
    mouse_command(MOUSE_ENABLE);
    flush();
}

static uint8_t extended_key(uint8_t code)
{
    switch (code) {
    case 0x48: return BS_KEY_UP;
    case 0x50: return BS_KEY_DOWN;
    case 0x4B: return BS_KEY_LEFT;
    case 0x4D: return BS_KEY_RIGHT;
    case 0x47: return BS_KEY_HOME;
    case 0x4F: return BS_KEY_END;
    case 0x53: return BS_KEY_DEL;
    case 0x49: return BS_KEY_PGUP;
    case 0x51: return BS_KEY_PGDN;
    default:   return 0;
    }
}

static void push_key(uint8_t key)
{
    uint32_t next = (key_head + 1) % KEY_QUEUE;

    if (key && next != key_tail) {
        keys[key_head] = key;
        key_head = next;
    }
}

static void handle_key_byte(uint8_t code)
{
    if (code == SC_EXTENDED) {
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        if ((code & (uint8_t)~SC_RELEASE) == SC_CTRL)
            ctrl_held = !(code & SC_RELEASE);
        else if (!(code & SC_RELEASE))
            push_key(extended_key(code));
        return;
    }
    if ((code & (uint8_t)~SC_RELEASE) == SC_LSHIFT || (code & (uint8_t)~SC_RELEASE) == SC_RSHIFT) {
        shift_held = !(code & SC_RELEASE);
        return;
    }
    if ((code & (uint8_t)~SC_RELEASE) == SC_CTRL) {
        ctrl_held = !(code & SC_RELEASE);
        return;
    }
    if (!(code & SC_RELEASE) && code < sizeof(normal)) {
        uint8_t key = (uint8_t)normal[code];

        if (ctrl_held && key >= 'a' && key <= 'z')
            push_key(key & 0x1F);
        else
            push_key((uint8_t)(shift_held ? shifted[code] : normal[code]));
    }
}

static void handle_mouse_byte(uint8_t value)
{
    /* Bit 3 of the first byte is always set; use it to resynchronise. */
    if (packet_used == 0 && !(value & PACKET_SYNC))
        return;
    packet[packet_used++] = value;
    if (packet_used < 3)
        return;
    packet_used = 0;

    mouse_buttons = packet[0] & 3u;
    mouse_latched |= mouse_buttons;
    if (packet[0] & PACKET_OVERFLOW)
        return;
    mouse_dx += (int32_t)packet[1] - (packet[0] & PACKET_X_NEGATIVE ? 256 : 0);
    mouse_dy -= (int32_t)packet[2] - (packet[0] & PACKET_Y_NEGATIVE ? 256 : 0);
}

static void pump(void)
{
    uint8_t status;

    while ((status = inb(PS2_STATUS)) & STATUS_OUTPUT_FULL) {
        uint8_t value = inb(PS2_DATA);

        if (status & STATUS_FROM_MOUSE)
            handle_mouse_byte(value);
        else
            handle_key_byte(value);
    }
}

void ps2_inject_key(uint8_t key)
{
    push_key(key);
}

void ps2_inject_mouse(int32_t dx, int32_t dy, uint32_t buttons)
{
    mouse_dx += dx;
    mouse_dy += dy;
    mouse_buttons = buttons;
    mouse_latched |= buttons;
}

uint8_t ps2_key_poll(void)
{
    uint8_t key;

    usb_poll();
    pump();
    if (key_head == key_tail)
        return 0;
    key = keys[key_tail];
    key_tail = (key_tail + 1) % KEY_QUEUE;
    return key;
}

void ps2_mouse_read(struct bs_mouse *out)
{
    usb_poll();
    pump();
    out->dx = mouse_dx;
    out->dy = mouse_dy;
    out->buttons = mouse_buttons | mouse_latched;
    mouse_latched = 0;
    mouse_dx = mouse_dy = 0;
}
