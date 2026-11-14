/*
 * The devices BShitOS talks to: memory, the framebuffer, the PS/2 controller,
 * the PIT (timing and the speaker), the CMOS clock, COM1 and a PCI bus that is
 * empty. Everything is polled, as BShitOS runs with interrupts off.
 */
#include "emu.h"

u8 ram[RAM_SIZE];
u32 framebuffer[FB_WIDTH * FB_HEIGHT];
int framebuffer_dirty;
int reboot_requested;

/* ---- Memory ------------------------------------------------------------------ */

u32 phys_read(u32 address, int size)
{
    if (address < RAM_SIZE - 4) {
        const u8 *p = ram + address;

        if (size == 4)
            return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
        if (size == 2)
            return (u32)p[0] | (u32)p[1] << 8;
        return p[0];
    }
    if (address >= FB_BASE && address - FB_BASE < FB_BYTES - 4) {
        const u8 *p = (const u8 *)framebuffer + (address - FB_BASE);
        u32 value = 0;

        for (int i = 0; i < size; i++)
            value |= (u32)p[i] << (8 * i);
        return value;
    }
    return size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFF : 0xFF;
}

void phys_write(u32 address, u32 value, int size)
{
    if (address < RAM_SIZE - 4) {
        u8 *p = ram + address;

        p[0] = (u8)value;
        if (size > 1)
            p[1] = (u8)(value >> 8);
        if (size > 2) {
            p[2] = (u8)(value >> 16);
            p[3] = (u8)(value >> 24);
        }
        return;
    }
    if (address >= FB_BASE && address - FB_BASE < FB_BYTES - 4) {
        u32 offset = address - FB_BASE;
        u8 *p = (u8 *)framebuffer + offset;

        for (int i = 0; i < size; i++)
            p[i] = (u8)(value >> (8 * i));
        /* The guest writes R, G, B, unused; the canvas wants the 4th byte opaque. */
        ((u8 *)framebuffer)[(offset & ~3u) + 3] = 0xFF;
        if (size == 4 && (offset & 3))
            ((u8 *)framebuffer)[((offset + 3) & ~3u) + 3] = 0xFF;
        framebuffer_dirty = 1;
    }
}

/* ---- PS/2 --------------------------------------------------------------------- */

#define PS2_QUEUE 512
#define PS2_ACK 0xFA
#define MOUSE_FLAG 0x100

static u16 ps2_queue[PS2_QUEUE];
static u32 ps2_head, ps2_tail;
static u8 ps2_config = 0x47;
static int ps2_next_is_config, ps2_next_to_mouse, mouse_enabled;

static void ps2_push(u16 value)
{
    u32 next = (ps2_head + 1) % PS2_QUEUE;

    if (next == ps2_tail)
        return; /* full: drop input rather than block */
    ps2_queue[ps2_head] = value;
    ps2_head = next;
}

void ps2_key(u32 code, int down)
{
    if (code > 0xFF)
        ps2_push(0xE0);
    ps2_push((u16)((code & 0x7F) | (down ? 0 : 0x80)));
}

void ps2_mouse(s32 dx, s32 dy, u32 buttons)
{
    if (!mouse_enabled)
        return;
    dy = -dy; /* PS/2 counts up as positive */
    do {
        s32 x = dx > 255 ? 255 : dx < -255 ? -255 : dx;
        s32 y = dy > 255 ? 255 : dy < -255 ? -255 : dy;
        u8 flags = (u8)(0x08 | (buttons & 3) | (x < 0 ? 0x10 : 0) | (y < 0 ? 0x20 : 0));

        ps2_push(MOUSE_FLAG | flags);
        ps2_push(MOUSE_FLAG | (u8)x);
        ps2_push(MOUSE_FLAG | (u8)y);
        dx -= x;
        dy -= y;
    } while (dx || dy);
}

static u32 ps2_status(void)
{
    u32 status = 0x04; /* system flag */

    if (ps2_head != ps2_tail) {
        status |= 0x01;
        if (ps2_queue[ps2_tail] & MOUSE_FLAG)
            status |= 0x20;
    }
    return status;
}

static u32 ps2_read(void)
{
    u16 value;

    if (ps2_head == ps2_tail)
        return 0;
    value = ps2_queue[ps2_tail];
    ps2_tail = (ps2_tail + 1) % PS2_QUEUE;
    return value & 0xFF;
}

static void ps2_command(u32 value)
{
    switch (value) {
    case 0x20: ps2_push(ps2_config); break;
    case 0x60: ps2_next_is_config = 1; break;
    case 0xD4: ps2_next_to_mouse = 1; break;
    case 0xFE: reboot_requested = 1; break;
    default: break; /* enable/disable ports: nothing to do */
    }
}

static void ps2_data(u32 value)
{
    if (ps2_next_is_config) {
        ps2_config = (u8)value;
        ps2_next_is_config = 0;
    } else if (ps2_next_to_mouse) {
        ps2_next_to_mouse = 0;
        if (value == 0xF4)
            mouse_enabled = 1;
        else if (value == 0xF5)
            mouse_enabled = 0;
        ps2_push(MOUSE_FLAG | PS2_ACK);
    } else {
        ps2_push(PS2_ACK);
    }
}

/* ---- PIT -------------------------------------------------------------------------- */

#define PIT_HZ 1193182.0

static double pit_start;
static u32 pit_count[3], pit_latched_status;
static int pit_status_ready, pit_write_high[3], pit_speaker_gate;
static u32 pit_low_byte[3];

static int pit_output(void)
{
    return (host_now() - pit_start) * (PIT_HZ / 1000.0) >= (double)pit_count[0];
}

static void update_speaker(void)
{
    if (pit_speaker_gate && pit_count[2])
        host_tone((u32)(PIT_HZ / pit_count[2]));
    else
        host_tone(0);
}

static void pit_write(u32 channel, u32 value)
{
    if (!pit_write_high[channel]) {
        pit_low_byte[channel] = value & 0xFF;
        pit_write_high[channel] = 1;
        return;
    }
    pit_write_high[channel] = 0;
    pit_count[channel] = pit_low_byte[channel] | (value & 0xFF) << 8;
    if (pit_count[channel] == 0)
        pit_count[channel] = 0x10000;
    if (channel == 0)
        pit_start = host_now();
    else if (channel == 2)
        update_speaker();
}

static void pit_command(u32 value)
{
    if ((value & 0xC0) == 0xC0) { /* read-back */
        if (!(value & 0x10) && (value & 0x02)) {
            pit_latched_status = (pit_output() ? 0x80u : 0) | 0x30;
            pit_status_ready = 1;
        }
        return;
    }
    pit_write_high[(value >> 6) & 3] = 0;
}

/* ---- CMOS ------------------------------------------------------------------------- */

static u32 cmos_index;

static u32 cmos_read(void)
{
    u32 date[6];

    host_date(date);
    switch (cmos_index & 0x7F) {
    case 0x00: return date[5];
    case 0x02: return date[4];
    case 0x04: return date[3];
    case 0x07: return date[2];
    case 0x08: return date[1];
    case 0x09: return date[0] % 100;
    case 0x0A: return 0x00; /* never mid-update */
    case 0x0B: return 0x06; /* binary values, 24-hour clock */
    default: return 0;
    }
}

/* ---- Ports -------------------------------------------------------------------------- */

#define COM1 0x3F8

u32 io_in(u32 port, int size)
{
    switch (port) {
    case 0x60: return ps2_read();
    case 0x64: return ps2_status();
    case 0x40:
        if (pit_status_ready) {
            pit_status_ready = 0;
            return pit_latched_status;
        }
        return 0;
    case 0x61: return (pit_speaker_gate ? 3u : 0u) | 0x20;
    case 0x71: return cmos_read();
    case COM1 + 5: return 0x60; /* transmitter always ready, nothing received */
    case 0xCFC: return 0xFFFFFFFFu; /* no PCI devices */
    default: return size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFF : 0xFF;
    }
}

void io_out(u32 port, u32 value, int size)
{
    (void)size;
    switch (port) {
    case 0x60: ps2_data(value); break;
    case 0x64: ps2_command(value); break;
    case 0x40: pit_write(0, value); break;
    case 0x42: pit_write(2, value); break;
    case 0x43: pit_command(value); break;
    case 0x61:
        pit_speaker_gate = (value & 3) == 3;
        update_speaker();
        break;
    case 0x70: cmos_index = value; break;
    case COM1: host_serial(value & 0xFF); break;
    case 0xCF9:
        if (value & 0x04)
            reboot_requested = 1;
        break;
    default: break;
    }
}

void devices_reset(void)
{
    ps2_head = ps2_tail = 0;
    ps2_next_is_config = ps2_next_to_mouse = mouse_enabled = 0;
    pit_count[0] = pit_count[2] = 0;
    pit_speaker_gate = 0;
    pit_status_ready = 0;
    reboot_requested = 0;
    host_tone(0);
}
