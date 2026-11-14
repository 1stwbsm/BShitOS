/* DHCP and DNS clients. */
#include "lib/string.h"
#include "net/net.h"

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_XID 0x42534854u
#define DHCP_COOKIE 0x63825363u
#define DHCP_OPTIONS 240u
#define DHCP_PACKET 548u
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define OPT_MASK 1
#define OPT_ROUTER 3
#define OPT_DNS 6
#define OPT_REQUESTED_IP 50
#define OPT_MESSAGE_TYPE 53
#define OPT_SERVER_ID 54
#define OPT_END 255
#define DHCP_WAIT_MS 1500
#define DHCP_ATTEMPTS 3
#define BROADCAST 0xFFFFFFFFu

#define DNS_PORT 53
#define DNS_LOCAL_PORT_BASE 53000
#define DNS_HEADER 12u
#define DNS_PACKET 512u
#define DNS_FLAG_RD 0x0100
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1
#define DNS_WAIT_MS 2000
#define DNS_ATTEMPTS 3

struct dhcp_reply {
    uint32_t offered, mask, router, dns, server;
    int type;
};

static void dhcp_send(int message_type, uint32_t requested, uint32_t server)
{
    uint8_t packet[DHCP_PACKET], mac[6];
    uint32_t n = DHCP_OPTIONS;

    memset(packet, 0, sizeof(packet));
    packet[0] = 1; /* BOOTREQUEST */
    packet[1] = 1; /* Ethernet */
    packet[2] = 6;
    put32(packet + 4, DHCP_XID);
    put16(packet + 10, 0x8000); /* ask for a broadcast reply */
    nic_mac(mac);
    memcpy(packet + 28, mac, 6);
    put32(packet + 236, DHCP_COOKIE);

    packet[n++] = OPT_MESSAGE_TYPE;
    packet[n++] = 1;
    packet[n++] = (uint8_t)message_type;
    if (requested) {
        packet[n++] = OPT_REQUESTED_IP;
        packet[n++] = 4;
        put32(packet + n, requested);
        n += 4;
        packet[n++] = OPT_SERVER_ID;
        packet[n++] = 4;
        put32(packet + n, server);
        n += 4;
    }
    packet[n++] = OPT_END;
    net_udp_send(BROADCAST, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, sizeof(packet));
}

static int dhcp_wait(int wanted, struct dhcp_reply *reply)
{
    uint8_t packet[DHCP_PACKET];

    for (int tries = 0; tries < 4; tries++) {
        int length = net_udp_receive(packet, sizeof(packet), DHCP_WAIT_MS / 4);
        uint32_t n = DHCP_OPTIONS;

        if (length < (int)DHCP_OPTIONS || get32(packet + 4) != DHCP_XID ||
            get32(packet + 236) != DHCP_COOKIE)
            continue;

        memset(reply, 0, sizeof(*reply));
        reply->offered = get32(packet + 16);
        while (n + 1 < (uint32_t)length && packet[n] != OPT_END) {
            uint8_t option = packet[n], size = packet[n + 1];
            const uint8_t *value = packet + n + 2;

            if (option == 0) {
                n++;
                continue;
            }
            if (n + 2 + size > (uint32_t)length)
                break;
            if (option == OPT_MESSAGE_TYPE && size == 1)
                reply->type = value[0];
            else if (option == OPT_MASK && size == 4)
                reply->mask = get32(value);
            else if (option == OPT_ROUTER && size >= 4)
                reply->router = get32(value);
            else if (option == OPT_DNS && size >= 4)
                reply->dns = get32(value);
            else if (option == OPT_SERVER_ID && size == 4)
                reply->server = get32(value);
            n += 2 + (uint32_t)size;
        }
        if (reply->type == wanted)
            return 0;
    }
    return -BS_ETIMEDOUT;
}

int net_dhcp(void)
{
    struct dhcp_reply offer, ack;

    for (int attempt = 0; attempt < DHCP_ATTEMPTS; attempt++) {
        net_udp_listen(DHCP_CLIENT_PORT);
        dhcp_send(DHCP_DISCOVER, 0, 0);
        if (dhcp_wait(DHCP_OFFER, &offer) < 0)
            continue;

        net_udp_listen(DHCP_CLIENT_PORT);
        dhcp_send(DHCP_REQUEST, offer.offered, offer.server);
        if (dhcp_wait(DHCP_ACK, &ack) < 0)
            continue;

        net_set_config(ack.offered, ack.mask, ack.router, ack.dns);
        net_udp_listen(0);
        return 0;
    }
    net_udp_listen(0);
    return -BS_ETIMEDOUT;
}

static uint32_t encode_name(const char *name, uint8_t *out, uint32_t room)
{
    uint32_t n = 0;

    while (*name) {
        uint32_t length = 0;

        while (name[length] && name[length] != '.')
            length++;
        if (length == 0 || length > 63 || n + length + 2 > room)
            return 0;
        out[n++] = (uint8_t)length;
        memcpy(out + n, name, length);
        n += length;
        name += length;
        if (*name == '.')
            name++;
    }
    out[n++] = 0;
    return n;
}

/* Returns the offset just past a possibly compressed name, or 0 if malformed. */
static uint32_t skip_name(const uint8_t *packet, uint32_t offset, uint32_t length)
{
    while (offset < length) {
        uint8_t label = packet[offset];

        if (label == 0)
            return offset + 1;
        if ((label & 0xC0) == 0xC0)
            return offset + 2;
        offset += 1u + label;
    }
    return 0;
}

static int parse_answer(const uint8_t *packet, uint32_t length, uint16_t id, uint32_t *ip)
{
    uint32_t offset = DNS_HEADER;
    uint32_t questions, answers;

    if (length < DNS_HEADER || get16(packet) != id || !(packet[2] & 0x80))
        return -BS_EIO;
    if (packet[3] & 0x0F)
        return -BS_ENOENT;

    questions = get16(packet + 4);
    answers = get16(packet + 6);
    for (uint32_t i = 0; i < questions; i++) {
        offset = skip_name(packet, offset, length);
        if (!offset)
            return -BS_EIO;
        offset += 4;
    }
    for (uint32_t i = 0; i < answers; i++) {
        uint32_t size;

        offset = skip_name(packet, offset, length);
        if (!offset || offset + 10 > length)
            return -BS_EIO;
        size = get16(packet + offset + 8);
        if (offset + 10 + size > length)
            return -BS_EIO;
        if (get16(packet + offset) == DNS_TYPE_A && size == 4) {
            *ip = get32(packet + offset + 10);
            return 0;
        }
        offset += 10 + size;
    }
    return -BS_ENOENT;
}

int net_dns_lookup(const char *name, uint32_t *ip)
{
    static uint16_t next_id = 0x4200;
    uint8_t packet[DNS_PACKET];
    struct bs_netinfo info;
    uint16_t id = ++next_id, local_port = (uint16_t)(DNS_LOCAL_PORT_BASE + (id & 0xFF));
    uint32_t n;
    int result = -BS_ETIMEDOUT;

    net_info(&info);
    if (!info.dns)
        return -BS_ENETDOWN;

    memset(packet, 0, DNS_HEADER);
    put16(packet, id);
    put16(packet + 2, DNS_FLAG_RD);
    put16(packet + 4, 1);
    n = encode_name(name, packet + DNS_HEADER, DNS_PACKET - DNS_HEADER - 4);
    if (!n)
        return -BS_EINVAL;
    n += DNS_HEADER;
    put16(packet + n, DNS_TYPE_A);
    put16(packet + n + 2, DNS_CLASS_IN);
    n += 4;

    for (int attempt = 0; attempt < DNS_ATTEMPTS && result == -BS_ETIMEDOUT; attempt++) {
        uint8_t reply[DNS_PACKET];
        int length;

        net_udp_listen(local_port);
        result = net_udp_send(info.dns, local_port, DNS_PORT, packet, n);
        if (result < 0)
            break;
        length = net_udp_receive(reply, sizeof(reply), DNS_WAIT_MS);
        result = length < 0 ? length : parse_answer(reply, (uint32_t)length, id, ip);
    }
    net_udp_listen(0);
    return result;
}
