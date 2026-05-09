#ifndef NET_H
#define NET_H

#include "freelib/kstdint.h"

#define ETH_ALEN 6
#define ETH_HDR_LEN 14
#define IP_HDR_LEN 20
#define TCP_HDR_LEN 20
#define UDP_HDR_LEN 8

#define ETH_P_IP 0x0800
#define ETH_P_ARP 0x0806

typedef struct {
    uint8_t dest[ETH_ALEN];
    uint8_t src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

typedef struct {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
} __attribute__((packed)) ip_hdr_t;

typedef struct {
    uint16_t source;
    uint16_t dest;
    uint32_t seq;
    uint32_t ack_seq;
    uint8_t res1:4;
    uint8_t doff:4;
    uint8_t fin:1;
    uint8_t syn:1;
    uint8_t rst:1;
    uint8_t psh:1;
    uint8_t ack:1;
    uint8_t urg:1;
    uint8_t ece:1;
    uint8_t cwr:1;
    uint16_t window;
    uint16_t check;
    uint16_t urg_ptr;
} __attribute__((packed)) tcp_hdr_t;

typedef struct {
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
} __attribute__((packed)) udp_hdr_t;

typedef struct {
    uint32_t ip;
    uint16_t port;
} __attribute__((packed)) sockaddr_t;

typedef struct net_device {
    char name[16];
    uint8_t mac[ETH_ALEN];
    uint32_t ip;
    void (*send)(struct net_device *dev, uint8_t *data, uint32_t len);
    void (*recv)(struct net_device *dev, void (*callback)(uint8_t *data, uint32_t len));
    struct net_device *next;
} net_device_t;

void net_init(void);
void net_register_device(net_device_t *dev);
void net_handle_packet(uint8_t *data, uint32_t len);
uint16_t net_checksum(uint16_t *addr, uint32_t count);
net_device_t *net_get_first_device(void);

#endif
