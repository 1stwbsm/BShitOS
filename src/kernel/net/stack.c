/* Ethernet, ARP, IPv4, ICMP and UDP. */
#include "drivers/pit.h"
#include "lib/string.h"
#include "net/net.h"

#define ETH_HEADER 14u
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_IP 0x0800
#define FRAME_MAX 1514u

#define ARP_PACKET 28u
#define ARP_REQUEST 1
#define ARP_REPLY 2
#define ARP_ENTRIES 8
#define ARP_RETRIES 4
#define ARP_WAIT_MS 500

#define IP_HEADER 20u
#define IP_BROADCAST 0xFFFFFFFFu
#define IP_TTL 64
#define PROTO_ICMP 1
#define PROTO_TCP 6
#define PROTO_UDP 17

#define ICMP_HEADER 8u
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define PING_ID 0x4253
#define PING_PAYLOAD 32u

#define UDP_HEADER 8u
#define UDP_BUFFER 1500u

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static int present;
static uint8_t our_mac[6];
static uint32_t our_ip, our_mask, our_gateway, our_dns;
static uint16_t ip_id;

static struct {
    uint32_t ip;
    uint8_t mac[6];
    int used;
} arp_table[ARP_ENTRIES];
static unsigned int arp_cursor;

static uint16_t ping_sequence;
static int ping_answered;

static uint16_t udp_port;
static uint8_t udp_data[UDP_BUFFER];
static uint32_t udp_length;
static int udp_ready;

static uint8_t rx_frame[2048];

uint16_t net_checksum(const void *data, uint32_t length, uint32_t seed)
{
    const uint8_t *p = data;
    uint32_t sum = seed;

    for (; length > 1; p += 2, length -= 2)
        sum += get16(p);
    if (length)
        sum += (uint32_t)*p << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void eth_send(const uint8_t *dst, uint16_t type, const void *payload, uint32_t length)
{
    uint8_t frame[FRAME_MAX];

    if (length > FRAME_MAX - ETH_HEADER)
        return;
    memcpy(frame, dst, 6);
    memcpy(frame + 6, our_mac, 6);
    put16(frame + 12, type);
    memcpy(frame + ETH_HEADER, payload, length);
    nic_send(frame, ETH_HEADER + length);
}

static void arp_store(uint32_t ip, const uint8_t *mac)
{
    for (int i = 0; i < ARP_ENTRIES; i++) {
        if (arp_table[i].used && arp_table[i].ip == ip) {
            memcpy(arp_table[i].mac, mac, 6);
            return;
        }
    }
    arp_table[arp_cursor].ip = ip;
    arp_table[arp_cursor].used = 1;
    memcpy(arp_table[arp_cursor].mac, mac, 6);
    arp_cursor = (arp_cursor + 1) % ARP_ENTRIES;
}

static const uint8_t *arp_lookup(uint32_t ip)
{
    for (int i = 0; i < ARP_ENTRIES; i++)
        if (arp_table[i].used && arp_table[i].ip == ip)
            return arp_table[i].mac;
    return 0;
}

static void arp_send(uint16_t operation, const uint8_t *target_mac, uint32_t target_ip, const uint8_t *eth_dst)
{
    uint8_t packet[ARP_PACKET];

    put16(packet, 1);
    put16(packet + 2, ETHERTYPE_IP);
    packet[4] = 6;
    packet[5] = 4;
    put16(packet + 6, operation);
    memcpy(packet + 8, our_mac, 6);
    put32(packet + 14, our_ip);
    memcpy(packet + 18, target_mac, 6);
    put32(packet + 24, target_ip);
    eth_send(eth_dst, ETHERTYPE_ARP, packet, ARP_PACKET);
}

static const uint8_t *arp_resolve(uint32_t ip)
{
    static const uint8_t unknown[6];

    for (int attempt = 0; attempt < ARP_RETRIES; attempt++) {
        const uint8_t *found = arp_lookup(ip);

        if (found)
            return found;
        arp_send(ARP_REQUEST, unknown, ip, broadcast_mac);
        for (int waited = 0; waited < ARP_WAIT_MS && !arp_lookup(ip); waited += 2) {
            pit_sleep_ms(2);
            net_poll();
        }
    }
    return arp_lookup(ip);
}

static void arp_input(const uint8_t *packet, uint32_t length)
{
    uint32_t sender_ip;

    if (length < ARP_PACKET || get16(packet + 2) != ETHERTYPE_IP)
        return;
    sender_ip = get32(packet + 14);
    arp_store(sender_ip, packet + 8);
    if (get16(packet + 6) == ARP_REQUEST && our_ip && get32(packet + 24) == our_ip)
        arp_send(ARP_REPLY, packet + 8, sender_ip, packet + 8);
}

int net_ip_send(uint32_t dst, uint8_t protocol, const void *payload, uint32_t length)
{
    uint8_t packet[FRAME_MAX];
    const uint8_t *mac;

    if (!present)
        return -BS_ENETDOWN;
    if (length > FRAME_MAX - ETH_HEADER - IP_HEADER)
        return -BS_EINVAL;

    if (dst == IP_BROADCAST) {
        mac = broadcast_mac;
    } else {
        uint32_t next_hop = (dst & our_mask) == (our_ip & our_mask) ? dst : our_gateway;

        if (!next_hop || !(mac = arp_resolve(next_hop)))
            return -BS_EHOSTUNREACH;
    }

    memset(packet, 0, IP_HEADER);
    packet[0] = 0x45;
    put16(packet + 2, (uint16_t)(IP_HEADER + length));
    put16(packet + 4, ip_id++);
    put16(packet + 6, 0x4000); /* don't fragment */
    packet[8] = IP_TTL;
    packet[9] = protocol;
    put32(packet + 12, our_ip);
    put32(packet + 16, dst);
    put16(packet + 10, net_checksum(packet, IP_HEADER, 0));
    memcpy(packet + IP_HEADER, payload, length);
    eth_send(mac, ETHERTYPE_IP, packet, IP_HEADER + length);
    return 0;
}

static void icmp_input(uint32_t src, uint8_t *message, uint32_t length)
{
    if (length < ICMP_HEADER)
        return;
    if (message[0] == ICMP_ECHO_REQUEST) {
        message[0] = ICMP_ECHO_REPLY;
        put16(message + 2, 0);
        put16(message + 2, net_checksum(message, length, 0));
        net_ip_send(src, PROTO_ICMP, message, length);
    } else if (message[0] == ICMP_ECHO_REPLY && get16(message + 4) == PING_ID &&
               get16(message + 6) == ping_sequence) {
        ping_answered = 1;
    }
}

int net_ping(uint32_t ip, uint32_t timeout_ms)
{
    uint8_t message[ICMP_HEADER + PING_PAYLOAD];
    int rc;

    memset(message, 0, sizeof(message));
    message[0] = ICMP_ECHO_REQUEST;
    put16(message + 4, PING_ID);
    put16(message + 6, ++ping_sequence);
    put16(message + 2, net_checksum(message, sizeof(message), 0));

    ping_answered = 0;
    rc = net_ip_send(ip, PROTO_ICMP, message, sizeof(message));
    if (rc < 0)
        return rc;
    for (uint32_t waited = 0; waited < timeout_ms; waited++) {
        net_poll();
        if (ping_answered)
            return (int)waited;
        pit_sleep_ms(1);
    }
    return -BS_ETIMEDOUT;
}

void net_udp_listen(uint16_t port)
{
    udp_port = port;
    udp_ready = 0;
}

int net_udp_send(uint32_t dst, uint16_t src_port, uint16_t dst_port, const void *data, uint32_t length)
{
    uint8_t packet[UDP_BUFFER];

    if (UDP_HEADER + length > sizeof(packet))
        return -BS_EINVAL;
    put16(packet, src_port);
    put16(packet + 2, dst_port);
    put16(packet + 4, (uint16_t)(UDP_HEADER + length));
    put16(packet + 6, 0); /* the checksum is optional over IPv4 */
    memcpy(packet + UDP_HEADER, data, length);
    return net_ip_send(dst, PROTO_UDP, packet, UDP_HEADER + length);
}

int net_udp_receive(void *buf, uint32_t size, uint32_t timeout_ms)
{
    for (uint32_t waited = 0; waited < timeout_ms; waited++) {
        net_poll();
        if (udp_ready) {
            uint32_t n = udp_length < size ? udp_length : size;

            memcpy(buf, udp_data, n);
            udp_ready = 0;
            return (int)n;
        }
        pit_sleep_ms(1);
    }
    return -BS_ETIMEDOUT;
}

static void udp_input(const uint8_t *packet, uint32_t length)
{
    uint32_t payload;

    if (length < UDP_HEADER || !udp_port || get16(packet + 2) != udp_port || udp_ready)
        return;
    payload = get16(packet + 4);
    if (payload < UDP_HEADER || payload > length || payload - UDP_HEADER > UDP_BUFFER)
        return;
    udp_length = payload - UDP_HEADER;
    memcpy(udp_data, packet + UDP_HEADER, udp_length);
    udp_ready = 1;
}

static void ip_input(uint8_t *packet, uint32_t length)
{
    uint32_t header, total, dst;

    if (length < IP_HEADER || packet[0] >> 4 != 4)
        return;
    header = (uint32_t)(packet[0] & 0xF) * 4;
    total = get16(packet + 2);
    if (header < IP_HEADER || total < header || total > length)
        return;
    if (net_checksum(packet, header, 0) != 0)
        return;
    if ((get16(packet + 6) & 0x3FFF) != 0 && (get16(packet + 6) & 0x1FFF) != 0)
        return; /* fragments are not reassembled */
    dst = get32(packet + 16);
    if (our_ip && dst != our_ip && dst != IP_BROADCAST)
        return;

    switch (packet[9]) {
    case PROTO_ICMP:
        icmp_input(get32(packet + 12), packet + header, total - header);
        break;
    case PROTO_TCP:
        tcp_input(get32(packet + 12), packet + header, total - header);
        break;
    case PROTO_UDP:
        udp_input(packet + header, total - header);
        break;
    default:
        break;
    }
}

void net_poll(void)
{
    uint32_t length;

    if (!present)
        return;
    while ((length = nic_receive(rx_frame)) != 0) {
        uint16_t type;

        if (length < ETH_HEADER)
            continue;
        type = get16(rx_frame + 12);
        if (type == ETHERTYPE_ARP)
            arp_input(rx_frame + ETH_HEADER, length - ETH_HEADER);
        else if (type == ETHERTYPE_IP)
            ip_input(rx_frame + ETH_HEADER, length - ETH_HEADER);
    }
}

void net_init(void)
{
    if (nic_init() < 0)
        return;
    nic_mac(our_mac);
    present = 1;
}

int net_configured(void)
{
    return present && our_ip != 0;
}

uint32_t net_local_ip(void)
{
    return our_ip;
}

void net_set_config(uint32_t ip, uint32_t mask, uint32_t gateway, uint32_t dns)
{
    our_ip = ip;
    our_mask = mask;
    our_gateway = gateway;
    our_dns = dns;
}

void net_info(struct bs_netinfo *out)
{
    out->present = (uint32_t)present;
    out->ip = our_ip;
    out->mask = our_mask;
    out->gateway = our_gateway;
    out->dns = our_dns;
    memcpy(out->mac, our_mac, 6);
}
