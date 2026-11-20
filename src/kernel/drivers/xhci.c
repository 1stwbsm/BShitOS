#include <bshit/syscall.h>

#include "proc/memory.h"
#include "drivers/console.h"
#include "drivers/pci.h"
#include "drivers/pit.h"
#include "drivers/ps2.h"
#include "drivers/xhci.h"
#include "lib/string.h"

#define CLASS_XHCI 0x0C0330u
#define MAX_CONTROLLERS 4
#define MAX_SLOTS 16
#define POOL_SIZE (768u * 1024u)
#define PAGE 4096u

/* Capability registers */
#define CAP_LENGTH 0x00
#define CAP_HCSPARAMS1 0x04
#define CAP_HCSPARAMS2 0x08
#define CAP_HCCPARAMS1 0x10
#define CAP_DBOFF 0x14
#define CAP_RTSOFF 0x18

/* Operational registers */
#define OP_USBCMD 0x00
#define OP_USBSTS 0x04
#define OP_CRCR 0x18
#define OP_DCBAAP 0x30
#define OP_CONFIG 0x38
#define OP_PORTSC(p) (0x400u + (p) * 0x10u)

#define CMD_RUN 1u
#define CMD_RESET 2u
#define STS_HALTED 1u
#define STS_NOT_READY (1u << 11)

#define PORT_CONNECTED (1u << 0)
#define PORT_RESET (1u << 4)
#define PORT_POWER (1u << 9)
#define PORT_RESET_CHANGE (1u << 21)
#define PORT_CHANGE_BITS 0x00FE0000u
#define PORT_SPEED(v) (((v) >> 10) & 0xF)

/* Interrupter 0 */
#define IR_ERSTSZ 0x28
#define IR_ERSTBA 0x30
#define IR_ERDP 0x38
#define ERDP_BUSY (1u << 3)

#define LEGACY_CAP_ID 1
#define LEGACY_BIOS_OWNED (1u << 16)
#define LEGACY_OS_OWNED (1u << 24)

/* TRB types */
#define TRB_NORMAL 1u
#define TRB_SETUP 2u
#define TRB_DATA 3u
#define TRB_STATUS 4u
#define TRB_LINK 6u
#define TRB_ENABLE_SLOT 9u
#define TRB_ADDRESS_DEVICE 11u
#define TRB_CONFIGURE_ENDPOINT 12u
#define TRB_EVALUATE_CONTEXT 13u
#define TRB_EVENT_TRANSFER 32u
#define TRB_EVENT_COMMAND 33u

#define TRB_CYCLE 1u
#define TRB_TOGGLE 2u
#define TRB_ISP (1u << 2)
#define TRB_IOC (1u << 5)
#define TRB_IDT (1u << 6)
#define TRB_DIR_IN (1u << 16)
#define TRB_TYPE(t) ((t) << 10)

#define CC_SUCCESS 1u
#define CC_SHORT_PACKET 13u

#define RING_SIZE 32u
#define EVENT_RING_SIZE 256u
#define WAIT_MS 2000u

#define SPEED_FULL 1
#define SPEED_LOW 2
#define SPEED_HIGH 3
#define SPEED_SUPER 4

/* Endpoint context types */
#define EP_CONTROL 4u
#define EP_BULK_OUT 2u
#define EP_BULK_IN 6u
#define EP_INTERRUPT_IN 7u

/* USB requests */
#define REQ_GET_DESCRIPTOR 6
#define REQ_SET_CONFIGURATION 9
#define REQ_SET_PROTOCOL 0x0B
#define REQ_SET_IDLE 0x0A
#define DESC_DEVICE 1
#define DESC_CONFIG 2
#define DESC_INTERFACE 4
#define DESC_ENDPOINT 5
#define CLASS_HID 3
#define CLASS_STORAGE 8
#define HID_BOOT 1
#define HID_KEYBOARD 1
#define HID_MOUSE 2
#define STORAGE_SCSI 6
#define STORAGE_BULK_ONLY 0x50

#define BOT_CBW_SIZE 31u
#define BOT_CSW_SIZE 13u
#define BOT_CBW_MAGIC 0x43425355u
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_READ_10 0x28
#define SECTOR 512u
#define READY_ATTEMPTS 10

#define REPORT_SIZE 16u

struct trb {
    uint32_t low, high, status, control;
};

struct ring {
    volatile struct trb *trbs;
    uint32_t index, cycle;
};

struct endpoint {
    struct ring ring;
    volatile int done;
    uint32_t code, residual;
};

enum hid_kind { HID_NONE, HID_KEY, HID_POINTER };

struct device {
    int active;
    uint32_t slot, speed, port;
    uint8_t *input, *output;
    struct endpoint ep0;
    struct endpoint *by_dci[32];
    /* HID */
    enum hid_kind hid;
    uint32_t hid_dci;
    struct endpoint interrupt;
    uint8_t *report;
    uint8_t last_keys[8];
    uint32_t report_size;
    /* Mass storage */
    int storage;
    uint32_t bulk_in, bulk_out;
    struct endpoint in, out;
};

struct controller {
    volatile uint8_t *caps, *op, *runtime, *doorbells;
    uint32_t context_size, ports;
    struct ring commands;
    volatile struct trb *events;
    uint32_t event_index, event_cycle;
    volatile uint64_t *dcbaa;
    volatile int command_done;
    uint32_t command_code, command_slot;
    struct device devices[MAX_SLOTS + 1];
};

static uint8_t *pool; /* allocated when a controller is found */
static uint32_t pool_used;
static struct controller controllers[MAX_CONTROLLERS];
static int controller_count;
static struct controller *storage_controller;
static struct device *storage_device;
static uint32_t bot_tag;

static void *alloc(uint32_t size, uint32_t align)
{
    uint32_t start = (pool_used + align - 1) & ~(align - 1);

    if (!pool) {
        pool = (uint8_t *)pages_alloc(POOL_SIZE / PAGE_SIZE);
        if (!pool)
            return 0;
        memset(pool, 0, POOL_SIZE);
    }
    if (start + size > POOL_SIZE)
        return 0;
    pool_used = start + size;
    return pool + start;
}

static uint32_t rd(volatile uint8_t *base, uint32_t reg)
{
    return *(volatile uint32_t *)(base + reg);
}

static void wr(volatile uint8_t *base, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(base + reg) = value;
}

static void wr64(volatile uint8_t *base, uint32_t reg, uint32_t value)
{
    wr(base, reg, value);
    wr(base, reg + 4, 0);
}

static int wait_bits(volatile uint8_t *base, uint32_t reg, uint32_t mask, uint32_t want, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        if ((rd(base, reg) & mask) == want)
            return 0;
        pit_sleep_ms(1);
    }
    return -1;
}

/* ---- Rings and events ------------------------------------------------- */

static int ring_init(struct ring *ring)
{
    ring->trbs = alloc(RING_SIZE * sizeof(struct trb), 64);
    if (!ring->trbs)
        return -1;
    ring->index = 0;
    ring->cycle = 1;
    ring->trbs[RING_SIZE - 1].low = (uint32_t)ring->trbs;
    ring->trbs[RING_SIZE - 1].control = TRB_TYPE(TRB_LINK) | TRB_TOGGLE;
    return 0;
}

static void ring_push(struct ring *ring, uint32_t low, uint32_t high, uint32_t status, uint32_t control)
{
    volatile struct trb *trb = &ring->trbs[ring->index];

    trb->low = low;
    trb->high = high;
    trb->status = status;
    __asm__ volatile("" ::: "memory");
    trb->control = control | ring->cycle;

    if (++ring->index == RING_SIZE - 1) {
        volatile struct trb *link = &ring->trbs[RING_SIZE - 1];

        link->control = TRB_TYPE(TRB_LINK) | TRB_TOGGLE | ring->cycle;
        ring->index = 0;
        ring->cycle ^= 1;
    }
}

static void ring_doorbell(struct controller *c, uint32_t slot, uint32_t target)
{
    wr(c->doorbells, slot * 4, target);
}

static void process_events(struct controller *c)
{
    int advanced = 0;

    for (;;) {
        volatile struct trb *event = &c->events[c->event_index];
        uint32_t control = event->control;
        uint32_t type = (control >> 10) & 0x3F;

        if ((control & TRB_CYCLE) != c->event_cycle)
            break;

        if (type == TRB_EVENT_COMMAND) {
            c->command_code = event->status >> 24;
            c->command_slot = control >> 24;
            c->command_done = 1;
        } else if (type == TRB_EVENT_TRANSFER) {
            uint32_t slot = control >> 24, dci = (control >> 16) & 0x1F;

            if (slot <= MAX_SLOTS && c->devices[slot].by_dci[dci]) {
                struct endpoint *ep = c->devices[slot].by_dci[dci];

                ep->code = event->status >> 24;
                ep->residual = event->status & 0xFFFFFF;
                ep->done = 1;
            }
        }

        advanced = 1;
        if (++c->event_index == EVENT_RING_SIZE) {
            c->event_index = 0;
            c->event_cycle ^= 1;
        }
    }

    if (advanced)
        wr64(c->runtime, IR_ERDP, (uint32_t)&c->events[c->event_index] | ERDP_BUSY);
}

static int run_command(struct controller *c, uint32_t low, uint32_t control)
{
    c->command_done = 0;
    ring_push(&c->commands, low, 0, 0, control);
    ring_doorbell(c, 0, 0);
    for (uint32_t i = 0; i < WAIT_MS; i++) {
        process_events(c);
        if (c->command_done)
            return c->command_code == CC_SUCCESS ? 0 : -1;
        pit_sleep_ms(1);
    }
    return -1;
}

static int wait_endpoint(struct controller *c, struct endpoint *ep)
{
    for (uint32_t i = 0; i < WAIT_MS; i++) {
        process_events(c);
        if (ep->done)
            return ep->code == CC_SUCCESS || ep->code == CC_SHORT_PACKET ? 0 : -1;
        pit_sleep_ms(1);
    }
    return -1;
}

/* ---- Control transfers ------------------------------------------------ */

static int control_transfer(struct controller *c, struct device *d, uint8_t type, uint8_t request,
                            uint16_t value, uint16_t index, void *data, uint16_t length)
{
    int in = type & 0x80;
    uint32_t transfer_type = length ? (in ? 3u : 2u) : 0u;

    d->ep0.done = 0;
    ring_push(&d->ep0.ring, type | (uint32_t)request << 8 | (uint32_t)value << 16,
              index | (uint32_t)length << 16, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | transfer_type << 16);
    if (length)
        ring_push(&d->ep0.ring, (uint32_t)data, 0, length, TRB_TYPE(TRB_DATA) | TRB_ISP | (in ? TRB_DIR_IN : 0));
    ring_push(&d->ep0.ring, 0, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | (in && length ? 0 : TRB_DIR_IN));
    ring_doorbell(c, d->slot, 1);
    return wait_endpoint(c, &d->ep0);
}

/* ---- Contexts --------------------------------------------------------- */

static volatile uint32_t *input_context(struct controller *c, struct device *d, uint32_t index)
{
    return (volatile uint32_t *)(d->input + index * c->context_size);
}

static uint32_t default_max_packet(uint32_t speed)
{
    if (speed == SPEED_SUPER)
        return 512;
    return speed == SPEED_HIGH ? 64 : 8;
}

static void write_endpoint_context(volatile uint32_t *ctx, uint32_t type, uint32_t max_packet,
                                   uint32_t interval, const struct ring *ring)
{
    ctx[0] = interval << 16;
    ctx[1] = 3u << 1 | type << 3 | max_packet << 16;
    ctx[2] = (uint32_t)ring->trbs | TRB_CYCLE;
    ctx[3] = 0;
    ctx[4] = type == EP_CONTROL ? 8u : max_packet | max_packet << 16;
}

static void clear_input(struct controller *c, struct device *d)
{
    memset(d->input, 0, c->context_size * 33);
}

static int address_device(struct controller *c, struct device *d)
{
    volatile uint32_t *control = input_context(c, d, 0);
    volatile uint32_t *slot = input_context(c, d, 1);

    clear_input(c, d);
    control[1] = 3; /* add the slot and endpoint 0 */
    slot[0] = d->speed << 20 | 1u << 27;
    slot[1] = (d->port + 1) << 16;
    write_endpoint_context(input_context(c, d, 2), EP_CONTROL, default_max_packet(d->speed), 0, &d->ep0.ring);
    return run_command(c, (uint32_t)d->input, TRB_TYPE(TRB_ADDRESS_DEVICE) | d->slot << 24);
}

static int update_max_packet(struct controller *c, struct device *d, uint32_t max_packet)
{
    volatile uint32_t *control = input_context(c, d, 0);

    clear_input(c, d);
    control[1] = 2; /* endpoint 0 only */
    write_endpoint_context(input_context(c, d, 2), EP_CONTROL, max_packet, 0, &d->ep0.ring);
    return run_command(c, (uint32_t)d->input, TRB_TYPE(TRB_EVALUATE_CONTEXT) | d->slot << 24);
}

static uint32_t endpoint_interval(uint32_t speed, uint32_t b_interval)
{
    uint32_t exponent = 3;

    if (speed == SPEED_HIGH || speed == SPEED_SUPER)
        return b_interval ? b_interval - 1 : 0;
    while ((1u << exponent) < b_interval * 8 && exponent < 10)
        exponent++;
    return exponent;
}

static int configure_endpoints(struct controller *c, struct device *d, uint32_t add_flags, uint32_t last_dci)
{
    volatile uint32_t *control = input_context(c, d, 0);
    volatile uint32_t *slot = input_context(c, d, 1);

    control[1] = add_flags | 1;
    slot[0] = d->speed << 20 | last_dci << 27;
    slot[1] = (d->port + 1) << 16;
    return run_command(c, (uint32_t)d->input, TRB_TYPE(TRB_CONFIGURE_ENDPOINT) | d->slot << 24);
}

/* ---- HID -------------------------------------------------------------- */

static uint8_t usage_to_key(uint8_t usage, int shift, int ctrl)
{
    static const char plain[] = "\n\033\b\t -=[]\\#;'`,./";
    static const char shifted[] = "\n\033\b\t _+{}|~:\"~<>?";

    if (usage >= 0x04 && usage <= 0x1D) {
        if (ctrl)
            return (uint8_t)(usage - 0x03);
        return (uint8_t)((shift ? 'A' : 'a') + usage - 0x04);
    }
    if (usage >= 0x1E && usage <= 0x27) {
        static const char digits[] = "1234567890";
        static const char symbols[] = "!@#$%^&*()";

        return (uint8_t)(shift ? symbols[usage - 0x1E] : digits[usage - 0x1E]);
    }
    if (usage >= 0x28 && usage <= 0x38)
        return (uint8_t)(shift ? shifted[usage - 0x28] : plain[usage - 0x28]);

    switch (usage) {
    case 0x4A: return BS_KEY_HOME;
    case 0x4B: return BS_KEY_PGUP;
    case 0x4C: return BS_KEY_DEL;
    case 0x4D: return BS_KEY_END;
    case 0x4E: return BS_KEY_PGDN;
    case 0x4F: return BS_KEY_RIGHT;
    case 0x50: return BS_KEY_LEFT;
    case 0x51: return BS_KEY_DOWN;
    case 0x52: return BS_KEY_UP;
    default:   return 0;
    }
}

static void handle_keyboard(struct device *d)
{
    const uint8_t *report = d->report;
    int shift = (report[0] & 0x22) != 0, ctrl = (report[0] & 0x11) != 0;

    for (int i = 2; i < 8; i++) {
        int already_down = 0;

        if (report[i] < 4)
            continue;
        for (int j = 2; j < 8; j++)
            if (d->last_keys[j] == report[i])
                already_down = 1;
        if (!already_down)
            ps2_inject_key(usage_to_key(report[i], shift, ctrl));
    }
    memcpy(d->last_keys, report, 8);
}

static void handle_mouse(struct device *d)
{
    const uint8_t *report = d->report;

    ps2_inject_mouse((int8_t)report[1], (int8_t)report[2], report[0] & 3u);
}

static void arm_report(struct controller *c, struct device *d)
{
    d->interrupt.done = 0;
    ring_push(&d->interrupt.ring, (uint32_t)d->report, 0, d->report_size,
              TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    ring_doorbell(c, d->slot, d->hid_dci);
}

void usb_poll(void)
{
    for (int i = 0; i < controller_count; i++) {
        struct controller *c = &controllers[i];

        process_events(c);
        for (uint32_t slot = 1; slot <= MAX_SLOTS; slot++) {
            struct device *d = &c->devices[slot];

            if (!d->active || d->hid == HID_NONE || !d->interrupt.done)
                continue;
            if (d->interrupt.code == CC_SUCCESS || d->interrupt.code == CC_SHORT_PACKET) {
                if (d->hid == HID_KEY)
                    handle_keyboard(d);
                else
                    handle_mouse(d);
            }
            arm_report(c, d);
        }
    }
}

/* ---- Mass storage ------------------------------------------------------ */

static int bulk(struct controller *c, struct device *d, int in, void *data, uint32_t length)
{
    struct endpoint *ep = in ? &d->in : &d->out;

    ep->done = 0;
    ring_push(&ep->ring, (uint32_t)data, 0, length, TRB_TYPE(TRB_NORMAL) | TRB_IOC | (in ? TRB_ISP : 0));
    ring_doorbell(c, d->slot, in ? d->bulk_in : d->bulk_out);
    return wait_endpoint(c, ep);
}

static int scsi_command(struct controller *c, struct device *d, const uint8_t *cdb, uint8_t cdb_length,
                        void *data, uint32_t length)
{
    static uint8_t cbw[BOT_CBW_SIZE] __attribute__((aligned(16)));
    static uint8_t csw[BOT_CSW_SIZE] __attribute__((aligned(16)));
    uint32_t magic = BOT_CBW_MAGIC, tag = ++bot_tag;

    memset(cbw, 0, sizeof(cbw));
    memcpy(cbw, &magic, 4);
    memcpy(cbw + 4, &tag, 4);
    memcpy(cbw + 8, &length, 4);
    cbw[12] = length ? 0x80 : 0; /* reads only */
    cbw[14] = cdb_length;
    memcpy(cbw + 15, cdb, cdb_length);

    if (bulk(c, d, 0, cbw, BOT_CBW_SIZE) < 0)
        return -1;
    if (length && bulk(c, d, 1, data, length) < 0)
        return -1;
    if (bulk(c, d, 1, csw, BOT_CSW_SIZE) < 0)
        return -1;
    return csw[12] == 0 ? 0 : -1;
}

int usb_storage_present(void)
{
    return storage_device != 0;
}

int usb_storage_read(uint64_t lba, uint32_t sectors, void *buf)
{
    uint8_t cdb[10] = {SCSI_READ_10};

    if (!storage_device || lba > 0xFFFFFFFFull)
        return -1;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)lba;
    cdb[7] = (uint8_t)(sectors >> 8);
    cdb[8] = (uint8_t)sectors;
    return scsi_command(storage_controller, storage_device, cdb, 10, buf, sectors * SECTOR);
}

static void wait_storage_ready(struct controller *c, struct device *d)
{
    uint8_t cdb[6] = {SCSI_TEST_UNIT_READY};

    for (int i = 0; i < READY_ATTEMPTS; i++) {
        if (scsi_command(c, d, cdb, 6, 0, 0) == 0)
            return;
        pit_sleep_ms(50);
    }
}

/* ---- Enumeration ------------------------------------------------------- */

static int add_endpoint(struct controller *c, struct device *d, struct endpoint *ep, uint32_t type,
                        const uint8_t *descriptor, uint32_t *add_flags, uint32_t *last_dci, uint32_t *dci_out)
{
    uint32_t address = descriptor[2], number = address & 0xF;
    uint32_t dci = number * 2 + (address & 0x80 ? 1 : 0);
    uint32_t max_packet = (descriptor[4] | (uint32_t)descriptor[5] << 8) & 0x7FF;

    if (ring_init(&ep->ring) < 0)
        return -1;
    d->by_dci[dci] = ep;
    write_endpoint_context(input_context(c, d, dci + 1), type, max_packet,
                           type == EP_INTERRUPT_IN ? endpoint_interval(d->speed, descriptor[6]) : 0, &ep->ring);
    *add_flags |= 1u << dci;
    if (dci > *last_dci)
        *last_dci = dci;
    *dci_out = dci;
    return 0;
}

static int claim_hid(struct controller *c, struct device *d, const uint8_t *interface, const uint8_t *endpoint,
                     uint32_t *add_flags, uint32_t *last_dci)
{
    if (add_endpoint(c, d, &d->interrupt, EP_INTERRUPT_IN, endpoint, add_flags, last_dci, &d->hid_dci) < 0)
        return -1;
    d->hid = interface[7] == HID_KEYBOARD ? HID_KEY : HID_POINTER;
    d->report = alloc(REPORT_SIZE, 64);
    d->report_size = d->hid == HID_KEY ? 8 : 4;
    return d->report ? 0 : -1;
}

static const uint8_t *next_descriptor(const uint8_t *p, const uint8_t *end, uint8_t type)
{
    for (p += p[0]; p + 2 <= end && p[0]; p += p[0])
        if (p[1] == type)
            return p;
    return 0;
}

static void configure_device(struct controller *c, struct device *d, uint8_t *config, uint32_t length)
{
    const uint8_t *end = config + length;
    uint32_t add_flags = 0, last_dci = 1;
    int want_storage = 0;
    uint8_t hid_interface = 0;

    clear_input(c, d);

    for (const uint8_t *p = config; (p = next_descriptor(p, end, DESC_INTERFACE)) != 0;) {
        const uint8_t *first = next_descriptor(p, end, DESC_ENDPOINT);

        if (!first)
            break;
        if (p[5] == CLASS_HID && p[6] == HID_BOOT && (p[7] == HID_KEYBOARD || p[7] == HID_MOUSE) &&
            d->hid == HID_NONE && (first[2] & 0x80)) {
            if (claim_hid(c, d, p, first, &add_flags, &last_dci) < 0)
                return;
            hid_interface = p[2];
        } else if (p[5] == CLASS_STORAGE && p[6] == STORAGE_SCSI && p[7] == STORAGE_BULK_ONLY && !storage_device) {
            const uint8_t *second = next_descriptor(first, end, DESC_ENDPOINT);
            const uint8_t *in = first[2] & 0x80 ? first : second;
            const uint8_t *out = first[2] & 0x80 ? second : first;

            if (!second || !in || !out || (in[2] & 0x80) == 0 || (out[2] & 0x80) != 0)
                continue;
            if (add_endpoint(c, d, &d->in, EP_BULK_IN, in, &add_flags, &last_dci, &d->bulk_in) < 0 ||
                add_endpoint(c, d, &d->out, EP_BULK_OUT, out, &add_flags, &last_dci, &d->bulk_out) < 0)
                return;
            want_storage = 1;
        }
    }

    if (!add_flags)
        return;
    if (configure_endpoints(c, d, add_flags, last_dci) < 0)
        return;
    if (control_transfer(c, d, 0x00, REQ_SET_CONFIGURATION, config[5], 0, 0, 0) < 0)
        return;

    if (d->hid != HID_NONE) {
        control_transfer(c, d, 0x21, REQ_SET_PROTOCOL, 0, hid_interface, 0, 0);
        control_transfer(c, d, 0x21, REQ_SET_IDLE, 0, hid_interface, 0, 0);
        arm_report(c, d);
        console_print(d->hid == HID_KEY ? "[\033[92m ok \033[0m] USB keyboard ready\n" : "[\033[92m ok \033[0m] USB mouse ready\n");
    }
    if (want_storage) {
        d->storage = 1;
        wait_storage_ready(c, d);
        storage_controller = c;
        storage_device = d;
        console_print("[\033[92m ok \033[0m] USB storage device ready\n");
    }
}

static void enumerate_port(struct controller *c, uint32_t port)
{
    static uint8_t buffer[512] __attribute__((aligned(64)));
    struct device *d;
    uint32_t status, slot, total;

    if (!(rd(c->op, OP_PORTSC(port)) & PORT_CONNECTED))
        return;

    wr(c->op, OP_PORTSC(port), PORT_POWER | PORT_RESET);
    if (wait_bits(c->op, OP_PORTSC(port), PORT_RESET_CHANGE, PORT_RESET_CHANGE, 500) < 0)
        return;
    pit_sleep_ms(20);
    status = rd(c->op, OP_PORTSC(port));
    wr(c->op, OP_PORTSC(port), PORT_POWER | (status & PORT_CHANGE_BITS));

    if (run_command(c, 0, TRB_TYPE(TRB_ENABLE_SLOT)) < 0)
        return;
    slot = c->command_slot;
    if (slot == 0 || slot > MAX_SLOTS)
        return;

    d = &c->devices[slot];
    memset(d, 0, sizeof(*d));
    d->slot = slot;
    d->port = port;
    d->speed = PORT_SPEED(status);
    d->input = alloc(c->context_size * 33, 64);
    d->output = alloc(c->context_size * 32, 64);
    if (!d->input || !d->output || ring_init(&d->ep0.ring) < 0)
        return;
    d->by_dci[1] = &d->ep0;
    c->dcbaa[slot] = (uint32_t)d->output;

    if (address_device(c, d) < 0)
        return;
    pit_sleep_ms(10);

    if (control_transfer(c, d, 0x80, REQ_GET_DESCRIPTOR, DESC_DEVICE << 8, 0, buffer, 8) < 0)
        return;
    if (d->speed != SPEED_SUPER && buffer[7] && buffer[7] != default_max_packet(d->speed))
        update_max_packet(c, d, buffer[7]);

    if (control_transfer(c, d, 0x80, REQ_GET_DESCRIPTOR, DESC_CONFIG << 8, 0, buffer, 9) < 0)
        return;
    total = buffer[2] | (uint32_t)buffer[3] << 8;
    if (total > sizeof(buffer))
        total = sizeof(buffer);
    if (control_transfer(c, d, 0x80, REQ_GET_DESCRIPTOR, DESC_CONFIG << 8, 0, buffer, (uint16_t)total) < 0)
        return;

    d->active = 1;
    configure_device(c, d, buffer, total);
}

static void take_ownership(struct controller *c)
{
    uint32_t offset = rd(c->caps, CAP_HCCPARAMS1) >> 16;
    volatile uint8_t *p = c->caps;

    if (!offset)
        return;
    for (p += offset * 4;; p += (rd(p, 0) >> 8 & 0xFF) * 4) {
        uint32_t value = rd(p, 0);

        if ((value & 0xFF) == LEGACY_CAP_ID) {
            wr(p, 0, value | LEGACY_OS_OWNED);
            wait_bits(p, 0, LEGACY_BIOS_OWNED, 0, 1000);
            return;
        }
        if (!(value >> 8 & 0xFF))
            return;
    }
}

static int start_controller(struct controller *c, volatile uint8_t *base)
{
    uint32_t params2, scratch_count;
    struct {
        uint32_t low, high, size, reserved;
    } volatile *segment;

    c->caps = base;
    c->op = base + (rd(base, CAP_LENGTH) & 0xFF);
    c->runtime = base + (rd(base, CAP_RTSOFF) & ~0x1Fu);
    c->doorbells = base + (rd(base, CAP_DBOFF) & ~3u);
    c->context_size = rd(base, CAP_HCCPARAMS1) & 4u ? 64 : 32;
    c->ports = rd(base, CAP_HCSPARAMS1) >> 24;

    take_ownership(c);

    wr(c->op, OP_USBCMD, rd(c->op, OP_USBCMD) & ~CMD_RUN);
    if (wait_bits(c->op, OP_USBSTS, STS_HALTED, STS_HALTED, 1000) < 0)
        return -1;
    wr(c->op, OP_USBCMD, CMD_RESET);
    if (wait_bits(c->op, OP_USBCMD, CMD_RESET, 0, 1000) < 0 ||
        wait_bits(c->op, OP_USBSTS, STS_NOT_READY, 0, 1000) < 0)
        return -1;

    wr(c->op, OP_CONFIG, MAX_SLOTS);

    c->dcbaa = alloc((MAX_SLOTS + 1) * 8, 64);
    if (!c->dcbaa)
        return -1;

    params2 = rd(base, CAP_HCSPARAMS2);
    scratch_count = (params2 >> 27 & 0x1F) | (params2 >> 21 & 0x1F) << 5;
    if (scratch_count) {
        volatile uint64_t *array = alloc(scratch_count * 8, 64);

        if (!array)
            return -1;
        for (uint32_t i = 0; i < scratch_count; i++) {
            void *page = alloc(PAGE, PAGE);

            if (!page)
                return -1;
            array[i] = (uint32_t)page;
        }
        c->dcbaa[0] = (uint32_t)array;
    }
    wr64(c->op, OP_DCBAAP, (uint32_t)c->dcbaa);

    if (ring_init(&c->commands) < 0)
        return -1;
    wr64(c->op, OP_CRCR, (uint32_t)c->commands.trbs | TRB_CYCLE);

    c->events = alloc(EVENT_RING_SIZE * sizeof(struct trb), 64);
    segment = alloc(16, 64);
    if (!c->events || !segment)
        return -1;
    segment->low = (uint32_t)c->events;
    segment->high = 0;
    segment->size = EVENT_RING_SIZE;
    c->event_index = 0;
    c->event_cycle = 1;
    wr(c->runtime, IR_ERSTSZ, 1);
    wr64(c->runtime, IR_ERDP, (uint32_t)c->events);
    wr64(c->runtime, IR_ERSTBA, (uint32_t)segment);

    wr(c->op, OP_USBCMD, CMD_RUN);
    return wait_bits(c->op, OP_USBSTS, STS_HALTED, 0, 1000);
}

void usb_init(void)
{
    struct pci_device device;

    for (unsigned int n = 0; n < MAX_CONTROLLERS && pci_find_class_nth(CLASS_XHCI, n, &device) == 0; n++) {
        struct controller *c = &controllers[controller_count];
        uint32_t bar;

        pci_enable(&device);
        bar = pci_bar(&device, 0) & ~0xFu;
        if (start_controller(c, (volatile uint8_t *)bar) < 0)
            continue;
        controller_count++;
        console_print("[\033[92m ok \033[0m] USB controller started, ");
        console_print_u32(c->ports);
        console_print(" ports\n");
        pit_sleep_ms(100); /* let ports report their connections */
        for (uint32_t port = 0; port < c->ports; port++)
            enumerate_port(c, port);
    }
}
