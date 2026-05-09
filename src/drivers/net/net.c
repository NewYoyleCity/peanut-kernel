#include "net.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"

static net_device_t *net_devices = NULL;

void net_init(void) {
    kprint("Network stack initialized\n");
    net_devices = NULL;
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

void net_handle_packet(uint8_t *data, uint32_t len) {
    if (len < ETH_HDR_LEN) {
        return;
    }

    eth_hdr_t *eth = (eth_hdr_t *)data;

    if (eth->type == ETH_P_IP) {
        ip_hdr_t *ip = (ip_hdr_t *)(data + ETH_HDR_LEN);
        uint32_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
        
        if (len < ETH_HDR_LEN + ip_hdr_len) {
            return;
        }
        
        if (ip->protocol == 6) { // TCP
            if (len >= ETH_HDR_LEN + ip_hdr_len + TCP_HDR_LEN) {
                tcp_hdr_t *tcp = (tcp_hdr_t *)(data + ETH_HDR_LEN + ip_hdr_len);
                kprint("TCP packet: src_port=");
                kprint_hex(tcp->source);
                kprint(" dst_port=");
                kprint_hex(tcp->dest);
                kprint("\n");
            }
        } else if (ip->protocol == 17) { // UDP
            if (len >= ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN) {
                udp_hdr_t *udp = (udp_hdr_t *)(data + ETH_HDR_LEN + ip_hdr_len);
                kprint("UDP packet: src_port=");
                kprint_hex(udp->source);
                kprint(" dst_port=");
                kprint_hex(udp->dest);
                kprint("\n");
            }
        } else if (ip->protocol == 1) { // ICMP
            kprint("ICMP packet received\n");
        }
    } else if (eth->type == ETH_P_ARP) {
        kprint("ARP packet received\n");
    }
}

net_device_t *net_get_first_device(void) {
    return net_devices;
}
