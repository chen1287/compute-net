#include "arp.h"
#include "base.h"
#include "types.h"
#include "ether.h"
#include "arpcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Utility function to create an ARP packet
static char* create_arp_packet(iface_info_t *iface, u32 dst_ip, struct ether_arp *req_hdr, int is_reply)
{
    char *packet = (char *)malloc(sizeof(struct ether_header) + sizeof(struct ether_arp));
    struct ether_header *header = (struct ether_header *)packet;
    struct ether_arp *arp = (struct ether_arp *)(packet + sizeof(struct ether_header));

    // Fill ether_header
    header->ether_type = htons(ETH_P_ARP);
    memcpy(header->ether_shost, iface->mac, ETH_ALEN);

    if (is_reply) {
        memcpy(header->ether_dhost, req_hdr->arp_sha, ETH_ALEN); // For ARP reply, set destination MAC to source MAC of request
    } else {
        memset(header->ether_dhost, 0xff, ETH_ALEN); // For ARP request, broadcast
    }

    // Fill arp header
    arp->arp_hrd = htons(ARPHRD_ETHER);
    arp->arp_pro = htons(0x0800);
    arp->arp_hln = 6;
    arp->arp_pln = 4;
    arp->arp_op = htons(is_reply ? ARPOP_REPLY : ARPOP_REQUEST);

    memcpy(arp->arp_sha, iface->mac, ETH_ALEN);
    memset(arp->arp_tha, 0, ETH_ALEN); // Target hardware address is zero in requests
    arp->arp_spa = htonl(iface->ip);
    arp->arp_tpa = htonl(dst_ip);

    return packet;
}

// Send an ARP request
void arp_send_request(iface_info_t *iface, u32 dst_ip)
{
    char *packet = create_arp_packet(iface, dst_ip, NULL, 0);
    iface_send_packet(iface, packet, sizeof(struct ether_header) + sizeof(struct ether_arp));
}

// Send an ARP reply
void arp_send_reply(iface_info_t *iface, struct ether_arp *req_hdr)
{
    char *packet = create_arp_packet(iface, ntohl(req_hdr->arp_spa), req_hdr, 1);
    iface_send_packet(iface, packet, sizeof(struct ether_header) + sizeof(struct ether_arp));
}

// Handle incoming ARP packets
void handle_arp_packet(iface_info_t *iface, char *packet, int len)
{
    struct ether_arp *arp = (struct ether_arp *)(packet + sizeof(struct ether_header));

    switch (ntohs(arp->arp_op)) {
        case ARPOP_REQUEST:
            if (ntohl(arp->arp_tpa) == iface->ip) {
                arp_send_reply(iface, arp);
            }
            arpcache_insert(htonl(arp->arp_spa), arp->arp_sha);
            break;
        case ARPOP_REPLY:
            arpcache_insert(htonl(arp->arp_spa), arp->arp_sha);
            break;
        default:
            free(packet);
            break;
    }
}

// Send a packet using ARP lookup for destination MAC
void iface_send_packet_by_arp(iface_info_t *iface, u32 dst_ip, char *packet, int len)
{
    struct ether_header *eh = (struct ether_header *)packet;
    memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
    eh->ether_type = htons(ETH_P_IP);

    u8 dst_mac[ETH_ALEN];
    int found = arpcache_lookup(dst_ip, dst_mac);

    if (found) {
        memcpy(eh->ether_dhost, dst_mac, ETH_ALEN);
        iface_send_packet(iface, packet, len);
    } else {
        arpcache_append_packet(iface, dst_ip, packet, len);
    }
}
