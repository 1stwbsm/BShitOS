/*
 * A TCP client. Segments are sent one at a time and retransmitted until
 * acknowledged; out-of-order data is dropped and the peer resends it.
 */
#include "drivers/pit.h"
#include "drivers/rtc.h"
#include "lib/string.h"
#include "net/net.h"

#define PROTO_TCP 6
#define HEADER 20u
#define FLAG_FIN 0x01
#define FLAG_SYN 0x02
#define FLAG_RST 0x04
#define FLAG_PSH 0x08
#define FLAG_ACK 0x10

#define MAX_CONNECTIONS 4
#define RECEIVE_BUFFER 32768u
#define WINDOW_LOW_WATER 2920u
#define MSS_OPTION_SIZE 4u

#define CONNECT_ATTEMPTS 4
#define CONNECT_WAIT_MS 1500
#define SEND_ATTEMPTS 6
#define SEND_WAIT_MS 1000
#define RECEIVE_TIMEOUT_MS 20000
#define CLOSE_WAIT_MS 300
#define FIRST_LOCAL_PORT 49152
#define LOCAL_PORT_RANGE 16000

enum state { CLOSED, SYN_SENT, ESTABLISHED };

struct connection {
    enum state state;
    const void *owner;
    uint32_t remote_ip;
    uint16_t local_port, remote_port;
    uint32_t send_next, send_acked, receive_next;
    uint8_t buffer[RECEIVE_BUFFER];
    uint32_t head, count;
    int peer_finished, reset;
};

static struct connection connections[MAX_CONNECTIONS];
static uint16_t port_counter;

static uint32_t window_of(const struct connection *c)
{
    uint32_t free_space = RECEIVE_BUFFER - c->count;

    return free_space > 0xFFFF ? 0xFFFF : free_space;
}

static int send_segment(struct connection *c, uint8_t flags, const void *data, uint32_t length, int with_mss)
{
    uint8_t segment[HEADER + MSS_OPTION_SIZE + NET_MTU_PAYLOAD];
    uint32_t header = HEADER + (with_mss ? MSS_OPTION_SIZE : 0);
    uint32_t pseudo;

    memset(segment, 0, header);
    put16(segment, c->local_port);
    put16(segment + 2, c->remote_port);
    put32(segment + 4, c->send_next);
    put32(segment + 8, flags & FLAG_ACK ? c->receive_next : 0);
    segment[12] = (uint8_t)(header / 4 << 4);
    segment[13] = flags;
    put16(segment + 14, (uint16_t)window_of(c));
    if (with_mss) {
        segment[20] = 2;
        segment[21] = 4;
        put16(segment + 22, NET_MTU_PAYLOAD);
    }
    if (length)
        memcpy(segment + header, data, length);

    pseudo = (net_local_ip() >> 16) + (net_local_ip() & 0xFFFF) +
             (c->remote_ip >> 16) + (c->remote_ip & 0xFFFF) + PROTO_TCP + header + length;
    put16(segment + 16, net_checksum(segment, header + length, pseudo));
    return net_ip_send(c->remote_ip, PROTO_TCP, segment, header + length);
}

static struct connection *find_connection(uint32_t ip, uint16_t local, uint16_t remote)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        struct connection *c = &connections[i];

        if (c->state != CLOSED && c->remote_ip == ip && c->local_port == local && c->remote_port == remote)
            return c;
    }
    return 0;
}

static void store_data(struct connection *c, const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
        c->buffer[(c->head + c->count + i) % RECEIVE_BUFFER] = data[i];
    c->count += length;
    c->receive_next += length;
}

void tcp_input(uint32_t src, const uint8_t *segment, uint32_t length)
{
    struct connection *c;
    uint32_t header, payload, sequence, acknowledgement;
    uint8_t flags;

    if (length < HEADER)
        return;
    c = find_connection(src, get16(segment + 2), get16(segment));
    if (!c)
        return;

    header = (uint32_t)(segment[12] >> 4) * 4;
    if (header < HEADER || header > length)
        return;
    payload = length - header;
    sequence = get32(segment + 4);
    acknowledgement = get32(segment + 8);
    flags = segment[13];

    if (flags & FLAG_RST) {
        c->reset = 1;
        return;
    }

    if (c->state == SYN_SENT) {
        if ((flags & (FLAG_SYN | FLAG_ACK)) == (FLAG_SYN | FLAG_ACK) && acknowledgement == c->send_next + 1) {
            c->send_next++;
            c->send_acked = c->send_next;
            c->receive_next = sequence + 1;
            c->state = ESTABLISHED;
            send_segment(c, FLAG_ACK, 0, 0, 0);
        }
        return;
    }

    if ((flags & FLAG_ACK) && (int32_t)(acknowledgement - c->send_acked) > 0)
        c->send_acked = acknowledgement;

    if (payload && sequence == c->receive_next && payload <= RECEIVE_BUFFER - c->count)
        store_data(c, segment + header, payload);

    if ((flags & FLAG_FIN) && sequence + payload == c->receive_next && !c->peer_finished) {
        c->receive_next++;
        c->peer_finished = 1;
    }

    if (payload || (flags & FLAG_FIN))
        send_segment(c, FLAG_ACK, 0, 0, 0);
}

static struct connection *get(int handle)
{
    if (handle < 0 || handle >= MAX_CONNECTIONS || connections[handle].state == CLOSED)
        return 0;
    return &connections[handle];
}

int tcp_open(uint32_t ip, uint16_t port, const void *owner)
{
    struct bs_time now;
    struct connection *c = 0;
    int handle = 0;

    if (!net_configured())
        return -BS_ENETDOWN;
    for (; handle < MAX_CONNECTIONS; handle++) {
        if (connections[handle].state == CLOSED) {
            c = &connections[handle];
            break;
        }
    }
    if (!c)
        return -BS_EMFILE;

    rtc_read(&now);
    memset(c, 0, sizeof(*c));
    c->state = SYN_SENT;
    c->owner = owner;
    c->remote_ip = ip;
    c->remote_port = port;
    c->local_port = (uint16_t)(FIRST_LOCAL_PORT + (port_counter++ + now.second * 251) % LOCAL_PORT_RANGE);
    c->send_next = (now.hour * 3600 + now.minute * 60 + now.second) * 250000u + port_counter;
    c->send_acked = c->send_next;

    for (int attempt = 0; attempt < CONNECT_ATTEMPTS; attempt++) {
        int rc = send_segment(c, FLAG_SYN, 0, 0, 1);

        if (rc < 0) {
            c->state = CLOSED;
            return rc;
        }
        for (int waited = 0; waited < CONNECT_WAIT_MS; waited++) {
            net_poll();
            if (c->reset) {
                c->state = CLOSED;
                return -BS_ECONNREFUSED;
            }
            if (c->state == ESTABLISHED)
                return handle;
            pit_sleep_ms(1);
        }
    }
    c->state = CLOSED;
    return -BS_ETIMEDOUT;
}

int tcp_send(int handle, const void *data, uint32_t length)
{
    struct connection *c = get(handle);
    const uint8_t *bytes = data;
    uint32_t sent = 0;

    if (!c)
        return -BS_EBADF;

    while (sent < length) {
        uint32_t chunk = length - sent > NET_MTU_PAYLOAD ? NET_MTU_PAYLOAD : length - sent;
        uint32_t target = c->send_next + chunk;
        int acknowledged = 0;

        for (int attempt = 0; attempt < SEND_ATTEMPTS && !acknowledged; attempt++) {
            int rc = send_segment(c, FLAG_ACK | FLAG_PSH, bytes + sent, chunk, 0);

            if (rc < 0)
                return rc;
            for (int waited = 0; waited < SEND_WAIT_MS && !acknowledged; waited++) {
                net_poll();
                if (c->reset)
                    return -BS_ECONNREFUSED;
                acknowledged = (int32_t)(c->send_acked - target) >= 0;
                if (!acknowledged)
                    pit_sleep_ms(1);
            }
        }
        if (!acknowledged)
            return -BS_ETIMEDOUT;
        c->send_next = target;
        sent += chunk;
    }
    return (int)sent;
}

int tcp_receive(int handle, void *buf, uint32_t size)
{
    struct connection *c = get(handle);
    uint8_t *out = buf;
    uint32_t taken = 0;
    uint32_t window_before;

    if (!c)
        return -BS_EBADF;

    for (uint32_t waited = 0; c->count == 0; waited++) {
        net_poll();
        if (c->count)
            break;
        if (c->reset)
            return -BS_ECONNREFUSED;
        if (c->peer_finished)
            return 0;
        if (waited >= RECEIVE_TIMEOUT_MS)
            return -BS_ETIMEDOUT;
        pit_sleep_ms(1);
    }

    window_before = window_of(c);
    while (taken < size && c->count) {
        out[taken++] = c->buffer[c->head];
        c->head = (c->head + 1) % RECEIVE_BUFFER;
        c->count--;
    }
    if (window_before < WINDOW_LOW_WATER)
        send_segment(c, FLAG_ACK, 0, 0, 0); /* reopen the window */
    return (int)taken;
}

int tcp_close(int handle)
{
    struct connection *c = get(handle);

    if (!c)
        return -BS_EBADF;
    if (!c->reset) {
        uint32_t target = c->send_next + 1;

        send_segment(c, FLAG_FIN | FLAG_ACK, 0, 0, 0);
        c->send_next = target;
        for (int waited = 0; waited < CLOSE_WAIT_MS && (int32_t)(c->send_acked - target) < 0; waited++) {
            net_poll();
            pit_sleep_ms(1);
        }
    }
    c->state = CLOSED;
    return 0;
}

void tcp_close_owned(const void *owner)
{
    for (int i = 0; i < MAX_CONNECTIONS; i++)
        if (connections[i].state != CLOSED && connections[i].owner == owner)
            tcp_close(i);
}
