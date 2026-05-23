/* net.c -- Minimal networking stack (Ethernet / ARP / IP).
 *
 * Maintains a linked list of registered network devices, an ARP cache,
 * and a simple packet dispatch that handles ARP requests/replies and
 * prints UDP/TCP/ICMP packet headers.
 */

#include "net.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "drivers/bus/io.h"
#include "config.h"

static net_device_t *net_devices = NULL;

static uint8_t broadcast_mac[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t zero_mac[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };

static uint32_t arp_cache_ip[ARP_CACHE_SIZE];
static uint8_t arp_cache_mac[ARP_CACHE_SIZE][ETH_ALEN];
static int arp_cache_valid[ARP_CACHE_SIZE];

void net_init(void) {
    kprint("Network stack initialized\n");
    net_devices = NULL;
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache_valid[i] = 0;
        arp_cache_ip[i] = 0;
        for (uint32_t j = 0; j < ETH_ALEN; j++)
            arp_cache_mac[i][j] = 0;
    }
}

void net_register_device(net_device_t *dev) {
    dev->next = net_devices;
    net_devices = dev;
    kprint("Registered network device: ");
    kprint(dev->name);
    kprint("\n");
}

uint16_t net_checksum(uint16_t *addr, uint32_t count) {
    uint32_t sum = 0;

    while (count > 1) {
        sum += *addr++;
        count -= 2;
    }

    if (count > 0) {
        sum += *(uint8_t *)addr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}


/* net_handle_arp -- process incoming ARP packets; update cache, optionally reply.
 */static void net_handle_arp(uint8_t *data, uint32_t len, uint8_t *src_mac) {
    if (len < ETH_HDR_LEN + ARP_HDR_LEN) return;

    arp_pkt_t *arp = (arp_pkt_t *)(data + ETH_HDR_LEN);

    if (arp->htype != ARP_HTYPE_ETHER || arp->ptype != ARP_PTYPE_IP)
        return;
    if (arp->hlen != ETH_ALEN || arp->plen != 4)
        return;

    uint16_t oper = __builtin_bswap16(arp->oper);
    uint32_t spa = __builtin_bswap32(arp->spa);

    for (uint32_t i = 0; i < ETH_ALEN; i++)
        arp_cache_mac[spa % ARP_CACHE_SIZE][i] = arp->sha[i];
    arp_cache_ip[spa % ARP_CACHE_SIZE] = spa;
    arp_cache_valid[spa % ARP_CACHE_SIZE] = 1;

    if (oper == ARP_REQUEST) {
        net_device_t *dev = net_devices;
        while (dev) {
            if (arp->tpa == __builtin_bswap32(dev->ip)) {
                uint8_t reply_buf[ETH_HDR_LEN + ARP_HDR_LEN];
                eth_hdr_t *eth = (eth_hdr_t *)reply_buf;
                arp_pkt_t *arp_reply = (arp_pkt_t *)(reply_buf + ETH_HDR_LEN);

                for (uint32_t i = 0; i < ETH_ALEN; i++) {
                    eth->dest[i] = arp->sha[i];
                    eth->src[i] = dev->mac[i];
                }
                eth->type = __builtin_bswap16(ETH_P_ARP);

                arp_reply->htype = __builtin_bswap16(ARP_HTYPE_ETHER);
                arp_reply->ptype = __builtin_bswap16(ARP_PTYPE_IP);
                arp_reply->hlen = ETH_ALEN;
                arp_reply->plen = 4;
                arp_reply->oper = __builtin_bswap16(ARP_REPLY);
                for (uint32_t i = 0; i < ETH_ALEN; i++) {
                    arp_reply->sha[i] = dev->mac[i];
                    arp_reply->tha[i] = arp->sha[i];
                }
                arp_reply->spa = arp->tpa;
                arp_reply->tpa = arp->spa;

                if (dev->send)
                    dev->send(dev, reply_buf, sizeof(reply_buf));
                break;
            }
            dev = dev->next;
        }
    }
}

int net_arp_resolve(net_device_t *dev, uint32_t ip, uint8_t *mac_out) {
    if (!dev || !mac_out) return -1;

    uint32_t cache_idx = ip % ARP_CACHE_SIZE;
    if (arp_cache_valid[cache_idx] && arp_cache_ip[cache_idx] == ip) {
        for (uint32_t i = 0; i < ETH_ALEN; i++)
            mac_out[i] = arp_cache_mac[cache_idx][i];
        return 0;
    }

    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache_valid[i] && arp_cache_ip[i] == ip) {
            for (uint32_t j = 0; j < ETH_ALEN; j++)
                mac_out[j] = arp_cache_mac[i][j];
            return 0;
        }
    }

    return -1;
}

void net_arp_send_request(net_device_t *dev, uint32_t req_ip) {
    if (!dev || !dev->send) return;

    uint8_t buf[ETH_HDR_LEN + ARP_HDR_LEN];
    eth_hdr_t *eth = (eth_hdr_t *)buf;
    arp_pkt_t *arp = (arp_pkt_t *)(buf + ETH_HDR_LEN);

    for (uint32_t i = 0; i < ETH_ALEN; i++) {
        eth->dest[i] = broadcast_mac[i];
        eth->src[i] = dev->mac[i];
    }
    eth->type = __builtin_bswap16(ETH_P_ARP);

    arp->htype = __builtin_bswap16(ARP_HTYPE_ETHER);
    arp->ptype = __builtin_bswap16(ARP_PTYPE_IP);
    arp->hlen = ETH_ALEN;
    arp->plen = 4;
    arp->oper = __builtin_bswap16(ARP_REQUEST);
    for (uint32_t i = 0; i < ETH_ALEN; i++) {
        arp->sha[i] = dev->mac[i];
        arp->tha[i] = zero_mac[i];
    }
    arp->spa = __builtin_bswap32(dev->ip);
    arp->tpa = __builtin_bswap32(req_ip);

    dev->send(dev, buf, sizeof(buf));
}

void net_handle_packet(uint8_t *data, uint32_t len) {
    if (len < ETH_HDR_LEN) return;

    eth_hdr_t *eth = (eth_hdr_t *)data;
    uint8_t *src_mac = eth->src;

    if (eth->type == __builtin_bswap16(ETH_P_ARP)) {
        net_handle_arp(data, len, src_mac);
        return;
    }

    if (eth->type != __builtin_bswap16(ETH_P_IP)) return;

    ip_hdr_t *ip = (ip_hdr_t *)(data + ETH_HDR_LEN);
    uint32_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;

    if (ip_hdr_len < 20 || len < ETH_HDR_LEN + ip_hdr_len) return;

    if (ip->protocol == 6) {
        if (len >= ETH_HDR_LEN + ip_hdr_len + TCP_HDR_LEN) {
            tcp_hdr_t *tcp = (tcp_hdr_t *)(data + ETH_HDR_LEN + ip_hdr_len);
            kprint("TCP packet: src_port=");
            kprint_hex(__builtin_bswap16(tcp->source));
            kprint(" dst_port=");
            kprint_hex(__builtin_bswap16(tcp->dest));
            kprint("\n");
        }
    } else if (ip->protocol == 17) {
        if (len >= ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN) {
            udp_hdr_t *udp = (udp_hdr_t *)(data + ETH_HDR_LEN + ip_hdr_len);
            kprint("UDP packet: src_port=");
            kprint_hex(__builtin_bswap16(udp->source));
            kprint(" dst_port=");
            kprint_hex(__builtin_bswap16(udp->dest));
            kprint("\n");
        }
    } else if (ip->protocol == 1) {
        kprint("ICMP packet received\n");
    }
}

net_device_t *net_get_first_device(void) {
    return net_devices;
}
