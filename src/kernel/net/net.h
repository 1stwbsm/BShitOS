/*
 * The network stack. Runs polled: net_poll() drains the card, and is called
 * from blocking operations and the console's idle loop. IPv4 addresses are
 * host-order integers.
 */
#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include <bshit/syscall.h>

#define NET_MTU_PAYLOAD 1460u

/* nic.c */
int nic_init(void);
void nic_mac(uint8_t out[6]);
void nic_send(const void *frame, uint32_t length);
uint32_t nic_receive(void *frame); /* returns 0 if nothing is waiting */

/* stack.c */
void net_init(void);
void net_poll(void);
int net_configured(void);
void net_info(struct bs_netinfo *out);
int net_ip_send(uint32_t dst, uint8_t protocol, const void *payload, uint32_t length);
uint32_t net_local_ip(void);
void net_set_config(uint32_t ip, uint32_t mask, uint32_t gateway, uint32_t dns);
uint16_t net_checksum(const void *data, uint32_t length, uint32_t seed);
int net_ping(uint32_t ip, uint32_t timeout_ms);

/* UDP: one receiver at a time, which is all DHCP and DNS need. */
void net_udp_listen(uint16_t port);
int net_udp_send(uint32_t dst, uint16_t src_port, uint16_t dst_port, const void *data, uint32_t length);
int net_udp_receive(void *buf, uint32_t size, uint32_t timeout_ms);

/* services.c */
int net_dhcp(void);
int net_dns_lookup(const char *name, uint32_t *ip);

/* tcp.c */
void tcp_input(uint32_t src, const uint8_t *segment, uint32_t length);
int tcp_open(uint32_t ip, uint16_t port, const void *owner);
int tcp_send(int handle, const void *data, uint32_t length);
int tcp_receive(int handle, void *buf, uint32_t size);
int tcp_close(int handle);
void tcp_close_owned(const void *owner);

/* Big-endian field access */
static inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

#endif
